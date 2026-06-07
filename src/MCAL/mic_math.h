/**
 * @file    mic_math.h
 * @brief   Microphone math helpers (host-testable)
 * @layer   MCAL
 */

#ifndef MIC_MATH_H
#define MIC_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

float MCAL_Mic_RmsToDb(int64_t sumSq, uint32_t n);
uint8_t MCAL_Mic_DbToPercent(float db);
uint8_t MCAL_Mic_CapacitySeconds(uint32_t samples, uint32_t sampleRate,
                                 uint8_t minSec, uint8_t maxSec);
bool MCAL_Mic_AnalogSelfCheck(uint16_t minRaw, uint16_t maxRaw, uint32_t avgRaw);

#ifdef __cplusplus
}
#endif

#endif /* MIC_MATH_H */
