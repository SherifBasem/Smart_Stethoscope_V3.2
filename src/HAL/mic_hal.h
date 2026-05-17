/**
 * @file    mic_hal.h
 * @brief   HAL — MAX4466 Microphone ADC abstraction for ESP32-S3
 * @layer   HAL
 *
 * Hardware:
 *   MAX4466 amplified electret microphone on GPIO3 (ADC1_CH2 on ESP32-S3).
 *   The MAX4466 has a fixed gain of ~125 (42 dB) and an adjustable-gain
 *   trim pot.  The stethoscope head mechanically channels low-frequency
 *   heart sounds into the capsule.
 *
 *   Wiring:
 *     MAX4466 OUT  → GPIO3  (ADC1_CH2)
 *     MAX4466 VCC  → 3.3V
 *     MAX4466 GND  → GND
 *
 *   ESP32-S3 ADC: 12-bit (0–4095), 3.3 V reference.
 *   The microphone signal is AC-coupled around the mid-rail (~1.65 V),
 *   so raw readings swing ±~2047 counts around 2048.
 *
 * Sampling strategy:
 *   We use direct analogRead() (no DMA) at up to MIC_SAMPLE_RATE_HZ.
 *   For heart sounds the relevant band is 20–800 Hz; 4 kHz gives
 *   ample headroom with Nyquist satisfied and low CPU load.
 *
 *   A configurable circular buffer holds MIC_BUFFER_SAMPLES samples.
 *   The Mic Task fills it; the UI Task reads peak/RMS for the VU meter;
 *   the recording system drains it to a 60-second capture buffer.
 */

#ifndef MIC_HAL_H
#define MIC_HAL_H

#include <Arduino.h>
#include <driver/adc.h>

/* ------ Config ------ */
#define MIC_ADC_PIN          3          /**< GPIO3 — MAX4466 OUT              */
#define MIC_ADC_BITS         12         /**< ESP32-S3 ADC resolution           */
#define MIC_ADC_MAX          4095       /**< 2^12 - 1                          */
#define MIC_ADC_VREF         3.3f       /**< ADC reference voltage             */
#define MIC_ADC_MID          2048       /**< DC bias mid-point (AC coupling)   */
#define MIC_ADC_CHANNEL      ADC1_CHANNEL_2   /**< GPIO3 = ADC1_CH2 on S3     */
#define MIC_SAMPLE_RATE_HZ   4000       /**< 4 kHz — covers 20–800 Hz band    */
#define MIC_SAMPLE_INTERVAL_US  (1000000 / MIC_SAMPLE_RATE_HZ)  /* 250 µs     */

/* ------ API ------ */

/**
 * @brief  Configure GPIO3 as ADC input (11 dB attenuation for 0–3.3 V).
 *         Call once from setup() before MicTask starts.
 */
static inline void HAL_MicAdc_Init(void) {
    analogReadResolution(MIC_ADC_BITS);
    /* Set attenuation on the specific channel so only GPIO3 is affected */
    analogSetPinAttenuation(MIC_ADC_PIN, ADC_11db);
    pinMode(MIC_ADC_PIN, INPUT);

    /* Warm-up — first few reads after init can be unstable */
    for (int i = 0; i < 8; i++) {
        analogRead(MIC_ADC_PIN);
        delayMicroseconds(50);
    }
}

/**
 * @brief  Read one raw 12-bit ADC sample from the microphone pin.
 * @return Raw ADC count (0–4095).  DC bias ≈ 2048.
 */
static inline uint16_t HAL_MicAdc_ReadRaw(void) {
    return (uint16_t)analogRead(MIC_ADC_PIN);
}

/**
 * @brief  Convert a raw ADC count to a signed amplitude centred on zero.
 *         Subtracts the DC bias (mid-rail) so the value swings ±2047.
 * @param  raw  Value from HAL_MicAdc_ReadRaw()
 * @return Signed amplitude in range [-2048, 2047]
 */
static inline int16_t HAL_MicAdc_RawToAmplitude(uint16_t raw) {
    return (int16_t)raw - (int16_t)MIC_ADC_MID;
}

/**
 * @brief  Convert a raw ADC count to a voltage (0–3.3 V).
 * @param  raw  Value from HAL_MicAdc_ReadRaw()
 * @return Voltage in volts
 */
static inline float HAL_MicAdc_RawToVolts(uint16_t raw) {
    return ((float)raw / (float)MIC_ADC_MAX) * MIC_ADC_VREF;
}

#endif /* MIC_HAL_H */