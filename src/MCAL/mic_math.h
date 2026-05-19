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

#ifdef __cplusplus
}
#endif

#endif /* MIC_MATH_H */
