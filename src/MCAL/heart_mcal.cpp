/**
 * @file    heart_mcal.cpp
 * @brief   MCAL — MAX30102 Heart-Rate & SpO2 implementation
 * @layer   MCAL
 *
 * Key design decisions vs the previous version
 * ─────────────────────────────────────────────
 * 1. Beat-by-beat BPM instead of batch BPM
 *    The old code ran maxim_heart_rate_and_oxygen_saturation() for BOTH BPM
 *    and SpO2.  That algorithm needs perfectly AC-coupled data and produces
 *    garbage unless the buffer is exactly right.  We now use checkForBeat()
 *    (SparkFun heartRate.h) on the raw IR value for BPM — it works on a
 *    single sample at a time and gives accurate real-time results.
 *    maxim_heart_rate_and_oxygen_saturation() is kept only for SpO2.
 *
 * 2. IR channel for finger detection
 *    IR is the primary channel; its mean rises much more steeply when a
 *    finger is present than the RED channel does.  The old code used RED.
 *
 * 3. Sensor enable / disable
 *    s_sensor.shutDown() / wakeUp() cut LED current to zero when the Heart
 *    Monitor screen is not active.  The Heart Task sleeps when disabled.
 *
 * 4. Buffer discipline for SpO2
 *    We fill s_irBuf / s_redBuf sample-by-sample inside Tick().  Once
 *    HEART_SAMPLE_COUNT samples have accumulated we run the SpO2 algorithm
 *    and reset the index.  BPM is updated on every beat, independently.
 */

#include "heart_mcal.h"
#include "../HAL/uart_hal.h"
#include "../HAL/i2c_hal.h"

#include <MAX30105.h>
#include <heartRate.h>       /* checkForBeat() */
#include <spo2_algorithm.h>  /* maxim_heart_rate_and_oxygen_saturation() */

/* ═══════════════════════════════════════════════════════════════════
   Private state
   ═══════════════════════════════════════════════════════════════════ */
static MAX30105       s_sensor;
static QueueHandle_t  s_queue        = NULL;
static bool           s_sensorReady  = false;
static bool           s_enabled      = false;

/* Rolling BPM average */
static float    s_bpmHistory[HEART_BPM_WINDOW];
static uint8_t  s_bpmIdx    = 0;
static uint8_t  s_bpmCount  = 0;   /* how many valid entries in history */

/* SpO2 batch buffers (32-bit as required by the algorithm) */
static uint32_t s_irBuf [HEART_SAMPLE_COUNT];
static uint32_t s_redBuf[HEART_SAMPLE_COUNT];
static int      s_sampleIdx = 0;

/* Last published reading */
static HeartReading_t s_last       = {0, 0, false, false, false, 0};
static bool           s_hasReading = false;

/* ═══════════════════════════════════════════════════════════════════
   Internal helpers
   ═══════════════════════════════════════════════════════════════════ */

static void publish(const HeartReading_t *r) {
    s_last       = *r;
    s_hasReading = true;
    if (s_queue) xQueueOverwrite(s_queue, r);
}

static void resetBpmBuffer(void) {
    for (int i = 0; i < HEART_BPM_WINDOW; i++) s_bpmHistory[i] = 0.0f;
    s_bpmIdx   = 0;
    s_bpmCount = 0;
}

static float rollingBpmAverage(float newBpm) {
    s_bpmHistory[s_bpmIdx] = newBpm;
    s_bpmIdx = (s_bpmIdx + 1) % HEART_BPM_WINDOW;
    if (s_bpmCount < HEART_BPM_WINDOW) s_bpmCount++;

    float sum = 0.0f;
    for (uint8_t i = 0; i < s_bpmCount; i++) sum += s_bpmHistory[i];
    return sum / s_bpmCount;
}

/* ═══════════════════════════════════════════════════════════════════
   MCAL_Heart_Init
   ═══════════════════════════════════════════════════════════════════ */
bool MCAL_Heart_Init(QueueHandle_t heartQueue) {
    s_queue = heartQueue;

    HAL_I2C_Lock();
    bool okBegin = s_sensor.begin(Wire, I2C_SPEED_FAST);
    HAL_I2C_Unlock();
    if (!okBegin) {
        HAL_UART_SendLine("[Heart] MAX30102 NOT found on I2C bus!");
        s_sensorReady = false;
        HeartReading_t blank = {0, 0, false, false, false, 0};
        publish(&blank);
        return false;
    }

    /*
     * Configure the sensor hardware.
     * sampleAverage = 1  — we want every raw sample for beat detection.
     *                      Averaging here would smear the AC waveform and
     *                      make checkForBeat() miss or double-count beats.
     * ledMode       = 2  — both RED and IR LEDs active (needed for SpO2)
     * sampleRate    = HEART_SAMPLE_RATE
     * pulseWidth    = 411 μs — longest pulse, best SNR
     */
    HAL_I2C_Lock();
    s_sensor.setup(
        HEART_LED_AMPLITUDE,  /* LED drive current */
        1,                    /* sampleAverage = 1 (no hardware averaging) */
        2,                    /* ledMode = RED + IR */
        HEART_SAMPLE_RATE,
        HEART_ADC_RANGE,
        411                   /* pulse width µs */
    );
    s_sensor.setPulseAmplitudeRed(HEART_LED_AMPLITUDE);
    s_sensor.setPulseAmplitudeIR(HEART_LED_AMPLITUDE);
    HAL_I2C_Unlock();

    /* Start in shutdown — enabled only when the Heart Monitor screen opens */
    s_sensor.shutDown();
    s_enabled     = false;
    s_sensorReady = true;

    HAL_UART_SendLine("[Heart] MAX30102 init OK (sensor in standby).");
    return true;
}

bool MCAL_Heart_IsReady(void) {
    return s_sensorReady;
}

/* ═══════════════════════════════════════════════════════════════════
   MCAL_Heart_Enable / Disable
   ═══════════════════════════════════════════════════════════════════ */
void MCAL_Heart_Enable(void) {
    if (!s_sensorReady || s_enabled) return;

    s_sensor.wakeUp();

    /* Clear FIFO of any stale samples accumulated while sleeping */
    s_sensor.clearFIFO();

    /* Reset algorithm state */
    resetBpmBuffer();
    s_sampleIdx = 0;

    /* Publish a "waiting for finger" reading immediately so the UI
     * does not show stale data from the previous session. */
    HeartReading_t r = {0, 0, false, false, false, 0};
    publish(&r);

    s_enabled = true;
    HAL_UART_SendLine("[Heart] Sensor enabled.");
}

void MCAL_Heart_Disable(void) {
    if (!s_sensorReady || !s_enabled) return;

    s_sensor.shutDown();
    s_enabled = false;
    resetBpmBuffer();
    s_sampleIdx = 0;

    HAL_UART_SendLine("[Heart] Sensor disabled (standby).");
}

bool MCAL_Heart_IsEnabled(void) {
    return s_enabled;
}

/* ═══════════════════════════════════════════════════════════════════
   MCAL_Heart_Tick  —  called once per FIFO sample from the Heart Task
   ═══════════════════════════════════════════════════════════════════ */
void MCAL_Heart_Tick(void) {
    if (!s_sensorReady || !s_enabled) return;

    /* Pump the sensor driver — fills the internal library buffer */
    HAL_I2C_Lock();
    s_sensor.check();

    /* Process all samples currently in the library buffer */
    while (s_sensor.available()) {

        uint32_t ir  = s_sensor.getIR();
        uint32_t red = s_sensor.getRed();
        s_sensor.nextSample();

        HeartReading_t r = s_last;   /* start from last known state */
        r.irRaw = ir;

        /* ── Finger detection (IR channel) ── */
        bool fingerNow = (ir > HEART_IR_FINGER_THRESHOLD);

        if (!fingerNow) {
            /* No finger — reset everything and publish immediately */
            if (r.fingerPresent) {
                /* Transition: finger just removed */
                resetBpmBuffer();
                s_sampleIdx = 0;
                r = {0, 0, false, false, false, ir};
                publish(&r);
            }
            /* If already reporting "no finger" nothing changes */
            continue;
        }

        /* Finger is present */
        r.fingerPresent = true;

        /* ── Beat-by-beat BPM (IR channel, checkForBeat) ── */
        if (checkForBeat(ir)) {
            /* millis() gives time of this beat; delta to last beat = period */
            static uint32_t s_lastBeatMs = 0;
            uint32_t now   = (uint32_t)millis();
            uint32_t delta = now - s_lastBeatMs;
            s_lastBeatMs   = now;

            /* delta == 0 on the very first beat — skip to avoid division */
            if (delta > 0 && delta < 3000) {   /* 3 s → 20 BPM lower bound */
                float instantBpm = 60000.0f / (float)delta;

                /* Reject physiologically impossible values */
                if (instantBpm >= 30.0f && instantBpm <= 220.0f) {
                    float avgBpm = rollingBpmAverage(instantBpm);
                    r.bpm      = (uint8_t)(avgBpm + 0.5f);
                    r.validBPM = (s_bpmCount >= HEART_BPM_WINDOW);

                    HAL_UART_Printf(
                        "[Heart] Beat! instant=%.1f avg=%.1f BPM  IR=%lu\r\n",
                        instantBpm, avgBpm, (unsigned long)ir);
                }
            }
        }

        /* ── SpO2 batch accumulation ── */
        s_irBuf [s_sampleIdx] = ir;
        s_redBuf[s_sampleIdx] = red;
        s_sampleIdx++;

        if (s_sampleIdx >= HEART_SAMPLE_COUNT) {
            s_sampleIdx = 0;

            int32_t spo2 = 0; int8_t validSpo2 = 0;
            int32_t hr   = 0; int8_t validHr   = 0;

            maxim_heart_rate_and_oxygen_saturation(
                s_irBuf,  HEART_SAMPLE_COUNT,
                s_redBuf,
                &spo2, &validSpo2,
                &hr,   &validHr
            );

            if (validSpo2 && spo2 >= 70 && spo2 <= 100) {
                r.spo2      = (uint8_t)spo2;
                r.validSpO2 = true;
            } else {
                r.spo2      = 0;
                r.validSpO2 = false;
            }

            HAL_UART_Printf(
                "[Heart] SpO2 batch: spo2=%d (v=%d)  batchBPM=%d\r\n",
                (int)spo2, (int)validSpo2, (int)hr);
        }

        publish(&r);
    }
    HAL_I2C_Unlock();
}

/* ═══════════════════════════════════════════════════════════════════
   MCAL_Heart_GetLast
   ═══════════════════════════════════════════════════════════════════ */
bool MCAL_Heart_GetLast(HeartReading_t *out) {
    if (!s_hasReading || !out) return false;
    *out = s_last;
    return true;
}
