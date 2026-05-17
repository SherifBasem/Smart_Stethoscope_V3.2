/**
 * @file    battery_hal.h
 * @brief   HAL — Battery ADC abstraction for ESP32-S3
 * @layer   HAL
 *
 * Hardware:
 *   Two 1 kΩ resistors form a voltage divider on GPIO4.
 *   Each battery terminal feeds one resistor; the mid-point goes to GPIO4.
 *
 *   Battery pack (2S LiPo in parallel → ~3.7 V nominal, 4.2 V max):
 *     VBAT ──┬── R1(1kΩ) ──┬── R2(1kΩ) ── GND
 *            │              └── GPIO4 (ADC)
 *   (Both batteries are wired in parallel so VBAT = single-cell voltage)
 *
 *   Divider ratio: GPIO4 = VBAT × R2/(R1+R2) = VBAT × 0.5
 *   → VBAT = ADC_voltage × 2.0
 *
 *   ESP32-S3 ADC: 12-bit (0–4095), 3.3 V reference (Vref).
 *   ADC_voltage = raw × (3.3 / 4095)
 *
 * TP4056 charging detection:
 *   The TP4056 CHRG pin (open-drain, active LOW) is NOT wired here.
 *   Charging state is inferred from rising voltage trend instead.
 */

#ifndef BATTERY_HAL_H
#define BATTERY_HAL_H

#include <Arduino.h>

/* ------ Config ------ */
#define BATTERY_ADC_PIN         4                   /**< GPIO4 — voltage divider mid-point */
#define BATTERY_ADC_BITS        12                  /**< ESP32-S3 ADC resolution           */
#define BATTERY_ADC_MAX         4095                /**< 2^12 - 1                          */
#define BATTERY_ADC_VREF        3.3f                /**< ADC reference voltage (V)         */
#define BATTERY_DIVIDER_RATIO   2.0f                /**< Vin = Vadc × ratio                */
#define BATTERY_ADC_SAMPLES     16                  /**< Oversample count for noise reject */
#define BATTERY_ADC_CHANNEL     ADC1_CHANNEL_3      /**< GPIO4 = ADC1_CH3 on S3            */

/* ------ API ------ */
/**
 * @brief  Configure GPIO4 as ADC input (attenuation 11 dB for 0–3.3 V).
 *         Call once from setup() before MCAL_Battery_Init().
 */
static inline void HAL_BatteryAdc_Init(void) {
    analogReadResolution(BATTERY_ADC_BITS);
    analogSetAttenuation(ADC_11db);   /* full 0–3.3 V range */
    pinMode(BATTERY_ADC_PIN, INPUT);
}
/**
 * @brief  Read and average multiple raw ADC samples from GPIO4.
 *
 *  Two known ESP32 ADC quirks handled here:
 *  1. The first analogRead() after channel selection often returns a
 *     stale value from the previous conversion — discard it.
 *  2. The SAR ADC needs ~10 µs between reads on the same channel to
 *     fully recharge its sampling capacitor; skipping this causes the
 *     averaged result to drift low, making the battery appear weaker
 *     than it is (and potentially blocking boot at the check stage).
 *
 * @return Averaged 12-bit ADC count (0–4095)
 */
static inline uint16_t HAL_BatteryAdc_ReadRaw(void) {
    analogRead(BATTERY_ADC_PIN);          /* warm-up / discard         */
    delayMicroseconds(10);

    uint32_t sum = 0;
    for (uint8_t i = 0; i < BATTERY_ADC_SAMPLES; i++) {
        sum += (uint32_t)analogRead(BATTERY_ADC_PIN);
        delayMicroseconds(10);            /* SAR cap recharge           */
    }
    return (uint16_t)(sum / BATTERY_ADC_SAMPLES);
}
/**
 * @brief  Convert a raw ADC count to the actual battery voltage.
 *         Applies the divider ratio and ADC scaling.
 * @param  raw  Value from HAL_BatteryAdc_ReadRaw()
 * @return Battery voltage in Volts (float)
 */
static inline float HAL_BatteryAdc_RawToVolts(uint16_t raw) {
    float adcVolts = ((float)raw / (float)BATTERY_ADC_MAX) * BATTERY_ADC_VREF;
    return adcVolts * BATTERY_DIVIDER_RATIO;
}

#endif /* BATTERY_HAL_H */
