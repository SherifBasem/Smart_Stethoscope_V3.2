/**
 * @file    heart_mcal.h
 * @brief   MCAL — MAX30102 Heart-Rate & SpO2 abstraction
 * @layer   MCAL
 *
 * Algorithm:
 *   BPM  — beat-by-beat using SparkFun checkForBeat() on the IR channel.
 *          A rolling average of the last HEART_BPM_WINDOW beats is kept.
 *   SpO2 — batch algorithm (maxim_heart_rate_and_oxygen_saturation) run
 *          on every HEART_SAMPLE_COUNT IR+RED samples once a finger is
 *          confirmed present.
 *
 * Finger detection:
 *   Uses the IR channel mean.  IR rises sharply when a vascularised
 *   fingertip covers the sensor window.
 *   Threshold: HEART_IR_FINGER_THRESHOLD (≈ 50 000 counts).
 *   IR is used (not RED) because it is the primary channel, less
 *   sensitive to skin-tone variation, and what checkForBeat() operates on.
 *
 * Power management:
 *   MCAL_Heart_Enable()  — wake sensor, flush FIFO, start LEDs.
 *   MCAL_Heart_Disable() — shut down LEDs, reset rolling BPM buffer.
 *   The Heart Task sleeps (vTaskDelay) when IsEnabled() returns false,
 *   consuming no CPU and keeping the sensor completely off.
 *
 * Dependencies (Arduino Library Manager):
 *   "SparkFun MAX3010x Pulse and Proximity Sensor Library" by SparkFun
 *   (library files: MAX30105.h / heartRate.h / spo2_algorithm.h)
 */

#ifndef HEART_MCAL_H
#define HEART_MCAL_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

/* ------ Config ------ */
/**
 * IR ADC counts below which no finger is assumed present.
 *   No finger  : ~5 000 – 30 000 counts
 *   Finger on  : ~50 000 – 262 000 counts
 */
#define HEART_IR_FINGER_THRESHOLD   50000UL
/** Samples per SpO2 batch (100 @ 100 SPS = ~1 s per batch) */
#define HEART_SAMPLE_COUNT          100
/**
 * LED pulse amplitude for both RED and IR LEDs.
 *   0x1F  ≈  6.4 mA  — good starting point, increase to 0x3F if
 *                       you get weak signal on dark skin or thick fingers.
 */
#define HEART_LED_AMPLITUDE         0x1F
/** Sensor hardware sample rate (samples / second) */
#define HEART_SAMPLE_RATE           100
/** ADC full-scale range (2048 / 4096 / 8192 / 16384) */
#define HEART_ADC_RANGE             4096
/**
 * Rolling-average window for BPM.
 * Larger = smoother but slower to respond to rate changes.
 */
#define HEART_BPM_WINDOW            4
/** I2C address of the MAX30102 */
#define MAX30102_I2C_ADDR           0x57

/* ------ Types ------ */

/**
 * @brief  Snapshot published to the heart queue on every update cycle.
 *
 *  bpm           — rolling-average BPM  (0 = not enough beats yet)
 *  spo2          — oxygen saturation %  (0 = not computed / invalid)
 *  fingerPresent — true when IR mean > HEART_IR_FINGER_THRESHOLD
 *  validBPM      — true when HEART_BPM_WINDOW consecutive beats averaged
 *  validSpO2     — true when SpO2 algorithm returned a plausible result
 *  irRaw         — latest raw IR ADC sample (useful for debug output)
 */
typedef struct {
    uint8_t  bpm;
    uint8_t  spo2;
    bool     fingerPresent;
    bool     validBPM;
    bool     validSpO2;
    uint32_t irRaw;
} HeartReading_t;

/* ------ API ------ */
/**
 * @brief  Initialise the MAX30102 over I2C.
 *         Wire must already be running (HAL_I2C_Init called in setup).
 *         Sensor starts DISABLED — call MCAL_Heart_Enable() to begin.
 * @param  heartQueue  Length-1 queue; receives HeartReading_t on every update
 * @return true on success, false if sensor not detected on I2C bus
 */
bool MCAL_Heart_Init(QueueHandle_t heartQueue);
/**
 * @brief Return true only when the MAX30102 was detected and configured.
 */
bool MCAL_Heart_IsReady(void);
/**
 * @brief  Wake the sensor and start LED sampling.
 *         Also flushes the FIFO and resets the rolling BPM buffer so
 *         stale samples do not corrupt the first reading.
 *         Call when the user navigates to UI_SCREEN_HEART_MONITOR.
 */
void MCAL_Heart_Enable(void);
/**
 * @brief  Power down sensor LEDs (shutdown mode).
 *         Resets BPM rolling buffer so values are clean next time.
 *         Call when the user leaves UI_SCREEN_HEART_MONITOR.
 */
void MCAL_Heart_Disable(void);
/**
 * @brief  Query current power state.
 * @return true after MCAL_Heart_Enable(), false after MCAL_Heart_Disable()
 */
bool MCAL_Heart_IsEnabled(void);
/**
 * @brief  Process one FIFO sample: run beat detection, update rolling BPM,
 *         accumulate SpO2 batch buffer, and publish a HeartReading_t.
 *         Call in a tight loop from the Heart Task (one call per sample).
 *         Returns immediately (no-op) when the sensor is disabled.
 */
void MCAL_Heart_Tick(void);
/**
 * @brief  Return the last published reading without touching the queue.
 * @param  out  Filled on success
 * @return true if at least one reading has been published since enable
 */
bool MCAL_Heart_GetLast(HeartReading_t *out);

#endif /* HEART_MCAL_H */
