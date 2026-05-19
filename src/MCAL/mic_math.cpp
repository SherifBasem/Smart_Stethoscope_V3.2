/**
 * @file    mic_math.cpp
 * @brief   Microphone math helpers (host-testable)
 * @layer   MCAL
 */

#include "mic_math.h"
#include "mic_mcal.h"
#include <math.h>

float MCAL_Mic_RmsToDb(int64_t sumSq, uint32_t n) {
    if (n == 0) return MIC_DB_FLOOR;
    float rms = sqrtf((float)sumSq / (float)n);
    if (rms < 1.0f) return MIC_DB_FLOOR;
    float db = 20.0f * log10f(rms / 2048.0f) + 90.0f;
    if (db < MIC_DB_FLOOR) db = MIC_DB_FLOOR;
    if (db > MIC_DB_CEIL)  db = MIC_DB_CEIL;
    return db;
}

uint8_t MCAL_Mic_DbToPercent(float db) {
    float range = MIC_DB_CEIL - MIC_DB_FLOOR;
    float pct   = (db - MIC_DB_FLOOR) / range * 100.0f;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return (uint8_t)pct;
}
