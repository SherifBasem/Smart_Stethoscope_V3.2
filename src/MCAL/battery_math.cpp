/**
 * @file    battery_math.cpp
 * @brief   Battery math helpers (host-testable)
 * @layer   MCAL
 */

#include "battery_math.h"

#define BATTERY_RAW_RAIL_MARGIN       32
#define BATTERY_RAW_MAX               4095
#define BATTERY_MIN_CONNECTED_V       3.00f
#define BATTERY_MAX_CONNECTED_V       4.35f
#define BATTERY_MAX_SAMPLE_SPREAD     96

/* LiPo voltage → percentage lookup table (empirical). */
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

uint8_t MCAL_Battery_VoltToPct(float v) {
    if (v >= LIPO_TABLE[0].v)                   return 100;
    if (v <= LIPO_TABLE[LIPO_TABLE_LEN - 1].v)  return 0;

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

bool MCAL_Battery_IsSamplePlausible(uint16_t raw, float v) {
    if (raw <= BATTERY_RAW_RAIL_MARGIN) return false;
    if (raw >= (BATTERY_RAW_MAX - BATTERY_RAW_RAIL_MARGIN)) return false;
    if (v < BATTERY_MIN_CONNECTED_V) return false;
    if (v > BATTERY_MAX_CONNECTED_V) return false;
    return true;
}

bool MCAL_Battery_IsSampleStable(uint16_t minRaw, uint16_t maxRaw) {
    if (maxRaw < minRaw) return false;
    return ((uint16_t)(maxRaw - minRaw) <= BATTERY_MAX_SAMPLE_SPREAD);
}
