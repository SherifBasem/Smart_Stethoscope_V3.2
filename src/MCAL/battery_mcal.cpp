/**
 * @file    battery_mcal.cpp
 * @brief   MCAL — LiPo battery monitor implementation
 * @layer   MCAL
 */

#include "battery_mcal.h"
#include "../HAL/battery_hal.h"
#include "../HAL/uart_hal.h"

/* ═══════════════════════════════════════════════════════════════════
   LiPo voltage → percentage lookup table
   Values measured empirically for a standard 1-cell LiPo under
   ~100 mA discharge.  Linear interpolation is applied between points.
   ═══════════════════════════════════════════════════════════════════ */
typedef struct { float v; uint8_t pct; } VoltPct_t;

static const VoltPct_t LIPO_TABLE[] = {
    { 3.70f, 100 },
    { 3.64f,  85 },
    { 3.58f,  70 },
    { 3.52f,  55 },
    { 3.46f,  35 },
    { 3.40f,   0 },
};
#define LIPO_TABLE_LEN  (sizeof(LIPO_TABLE) / sizeof(LIPO_TABLE[0]))

/* ═══════════════════════════════════════════════════════════════════
   Private state
   ═══════════════════════════════════════════════════════════════════ */
static BatteryStatus_t s_status     = { 0.0f, 0, BATTERY_STATE_UNKNOWN, false, false };
static float           s_prevV      = 0.0f;
static uint32_t        s_lastPollMs = 0;
static bool            s_initialised = false;
static int32_t         s_forcedRaw  = -1;

/* ═══════════════════════════════════════════════════════════════════
   Helpers
   ═══════════════════════════════════════════════════════════════════ */

/** Linear interpolation between two points */
static uint8_t voltToPct(float v) {
    if (v >= LIPO_TABLE[0].v)                           return 100;
    if (v <= LIPO_TABLE[LIPO_TABLE_LEN - 1].v)         return 0;

    for (uint8_t i = 0; i < LIPO_TABLE_LEN - 1; i++) {
        if (v <= LIPO_TABLE[i].v && v > LIPO_TABLE[i + 1].v) {
            float vHigh = LIPO_TABLE[i].v;
            float vLow  = LIPO_TABLE[i + 1].v;
            uint8_t pHigh = LIPO_TABLE[i].pct;
            uint8_t pLow  = LIPO_TABLE[i + 1].pct;
            float t = (v - vLow) / (vHigh - vLow);
            return (uint8_t)(pLow + t * (pHigh - pLow) + 0.5f);
        }
    }
    return 0;
}

static void doSample(void) {
    uint16_t raw = (s_forcedRaw >= 0) ? (uint16_t)s_forcedRaw
                                      : HAL_BatteryAdc_ReadRaw();
    float    v   = HAL_BatteryAdc_RawToVolts(raw);

    /* Clamp to sane range (open-circuit / disconnected protection) */
    if (v < 2.5f) v = 2.5f;
    if (v > 4.5f) v = 4.5f;

    uint8_t pct = voltToPct(v);

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
    s_status.state      = state;
    s_status.isLow      = (pct <= BATTERY_LOW_THRESHOLD_PCT);
    s_status.isCritical = (pct <= BATTERY_CRITICAL_PCT);

    HAL_UART_Printf("[Battery] %.2fV  %u%%  state=%d\r\n",
                     v, pct, (int)state);
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
    HAL_UART_SendLine("[Battery] MCAL init OK.");
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
