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

/**
 * @brief Return true when a raw ADC/voltage pair is plausible for a
 *        connected single-cell LiPo through the configured divider.
 */
bool MCAL_Battery_IsSamplePlausible(uint16_t raw, float v);

/**
 * @brief Return true when repeated ADC samples are close enough to treat
 *        the divider as stable instead of floating/disconnected.
 */
bool MCAL_Battery_IsSampleStable(uint16_t minRaw, uint16_t maxRaw);

#ifdef __cplusplus
}
#endif

#endif /* BATTERY_MATH_H */
