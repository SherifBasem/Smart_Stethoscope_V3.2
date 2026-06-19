/**
 * @file    battery_mcal.cpp
 * @brief   MCAL — LiPo battery monitor implementation
 * @layer   MCAL
 */

#include "battery_mcal.h"
#include "battery_math.h"
#include "../stetho_config.h"
#include "../HAL/battery_hal.h"
#include "../HAL/uart_hal.h"

/* ═══════════════════════════════════════════════════════════════════
    Private state
    ═══════════════════════════════════════════════════════════════════ */
static BatteryStatus_t s_status     = { 0.0f, 0, 0, BATTERY_STATE_UNKNOWN, false, false, false };
static float           s_prevV      = 0.0f;
static uint32_t        s_lastPollMs = 0;
static bool            s_initialised = false;
static int32_t         s_forcedRaw  = -1;
static uint8_t         s_invalidSamples = 0;

#define BATTERY_SAMPLE_BURST       3
#define BATTERY_INVALID_LIMIT      3

/* ═══════════════════════════════════════════════════════════════════
   Helpers
   ═══════════════════════════════════════════════════════════════════ */

static void doSample(void) {
    uint16_t raw = 0;
    uint16_t minRaw = 4095;
    uint16_t maxRaw = 0;

    if (s_forcedRaw >= 0) {
        raw = (uint16_t)s_forcedRaw;
        minRaw = raw;
        maxRaw = raw;
    } else {
        uint32_t sum = 0;
        for (uint8_t i = 0; i < BATTERY_SAMPLE_BURST; i++) {
            uint16_t sample = HAL_BatteryAdc_ReadRaw();
            sum += sample;
            if (sample < minRaw) minRaw = sample;
            if (sample > maxRaw) maxRaw = sample;
            delayMicroseconds(50);
        }
        raw = (uint16_t)(sum / BATTERY_SAMPLE_BURST);
    }

    float v = HAL_BatteryAdc_RawToVolts(raw);

    bool plausible = MCAL_Battery_IsSamplePlausible(raw, v);
    bool stable = (s_forcedRaw >= 0) || MCAL_Battery_IsSampleStable(minRaw, maxRaw);
    if (!plausible || !stable) {
        if (s_invalidSamples < 255) s_invalidSamples++;
        s_status.rawAdc = raw;

        if (s_invalidSamples >= BATTERY_INVALID_LIMIT || !s_status.isConnected) {
            s_status.voltageV = 0.0f;
            s_status.percent = 0;
            s_status.state = BATTERY_STATE_UNKNOWN;
            s_status.isLow = false;
            s_status.isCritical = false;
            s_status.isConnected = false;
            s_prevV = 0.0f;
        }

#if STETHO_DEBUG_LOGS
        HAL_UART_Printf("[Battery] Invalid ADC raw=%u min=%u max=%u v=%.2f stable=%d\r\n",
                        raw, minRaw, maxRaw, v, stable ? 1 : 0);
#endif
        return;
    }

    s_invalidSamples = 0;

    /* Clamp to sane range (open-circuit / disconnected protection) */
    if (v < 2.5f) v = 2.5f;
    if (v > 4.5f) v = 4.5f;

    /* Validate ADC connection: voltage should be between 3.0V and 4.3V for a healthy LiPo */
    bool connected = (v >= 3.0f && v <= 4.3f);

    uint8_t pct = MCAL_Battery_VoltToPct(v);

    /* Charging detection: voltage rising by more than delta */
    BatteryState_t state;
    if (s_prevV < 0.1f) {
        /* First reading — no direction yet */
        state = BATTERY_STATE_UNKNOWN;
    } else if (v >= BATTERY_FULL_V) {
        state = BATTERY_STATE_FULL;
    } else if (v > s_prevV + BATTERY_CHARGE_DELTA_V) {
        state = BATTERY_STATE_CHARGING;
    } else {
        state = BATTERY_STATE_DISCHARGING;
    }

    s_prevV = v;

    s_status.voltageV   = v;
    s_status.percent    = pct;
    s_status.rawAdc     = raw;
    s_status.state      = state;
    s_status.isLow      = (pct <= BATTERY_LOW_THRESHOLD_PCT);
    s_status.isCritical = (pct <= BATTERY_CRITICAL_PCT);
    s_status.isConnected = connected;

#if STETHO_DEBUG_LOGS
    HAL_UART_Printf("[Battery] %.2fV %u%% state=%d connected=%s\r\n", 
                     v, pct, (int)state, connected ? "YES" : "NO");
#endif
}

/* ═══════════════════════════════════════════════════════════════════
   Public API
   ═══════════════════════════════════════════════════════════════════ */

void MCAL_Battery_Init(void) {
    HAL_BatteryAdc_Init();

    /*
     * Wait for the MT3608 boost converter output to stabilise before
     * sampling.  At cold boot on battery the output rail can take
     * ~50 ms to reach its regulated voltage; sampling too early gives
     * a falsely low reading.
     *
     * IMPORTANT: called from setup() — before the FreeRTOS scheduler
     * starts — so we MUST use delay(), not vTaskDelay().
     */
    delay(100);

    /* Single sample to seed s_prevV.  State left as UNKNOWN until
     * the first Tick() call compares two readings taken seconds apart,
     * which is the only reliable way to detect charging direction.    */
    doSample();          /* populates s_prevV and s_status            */

    s_lastPollMs  = (uint32_t)millis();
    s_initialised = true;
#if STETHO_DEBUG_LOGS
    HAL_UART_SendLine("[Battery] MCAL init OK.");
#endif
}

void MCAL_Battery_Tick(void) {
    if (!s_initialised) return;
    uint32_t now = (uint32_t)millis();
    if (now - s_lastPollMs < BATTERY_POLL_INTERVAL_MS) return;
    s_lastPollMs = now;
    doSample();
}

bool MCAL_Battery_GetStatus(BatteryStatus_t *out) {
    if (!out) return false;
    *out = s_status;
    return true;
}

void MCAL_Battery_ForceRefresh(void) {
    doSample();
    s_lastPollMs = (uint32_t)millis();
}

void MCAL_Battery_SetForcedRaw(int32_t raw) {
    if (raw < 0) {
        s_forcedRaw = -1;
        return;
    }
    if (raw > 4095) raw = 4095;
    s_forcedRaw = raw;
}
