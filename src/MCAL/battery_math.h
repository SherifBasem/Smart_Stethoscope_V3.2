/**
 * @file    battery_math.h
 * @brief   Battery math helpers (host-testable)
 * @layer   MCAL
 */

#ifndef BATTERY_MATH_H
#define BATTERY_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Convert LiPo voltage to percentage using the lookup table.
 * @param v  Battery voltage in volts.
 * @return Percentage (0-100).
 */
uint8_t MCAL_Battery_VoltToPct(float v);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_MATH_H */
