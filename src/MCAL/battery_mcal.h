/**
 * @file    battery_mcal.h
 * @brief   MCAL — LiPo battery monitor
 * @layer   MCAL
 *
 * Tracks voltage, percentage, and charging state for a single-cell
 * (or 2-cell parallel) LiPo battery monitored via a 2×1 kΩ voltage
 * divider on GPIO4.
 *
 * LiPo voltage map used (parallel pack, normalized to 3.70 V full):
 *   ≥ 3.70 V → 100 %      (fully charged / nominal ceiling)
 *   3.64 V   →  85 %
 *   3.58 V   →  70 %
 *   3.52 V   →  55 %
 *   3.46 V   →  35 %
 *   ≤ 3.40 V →   0 %      (cut-off)
 *
 * Charging detection:
 *   A rolling comparison of the last two voltage readings determines
 *   direction.  If the voltage has risen by > BATTERY_CHARGE_DELTA_V
 *   between consecutive polls the charger is assumed active.
 *
 * Thread safety:
 *   MCAL_Battery_Tick() is called from UI_Task (Core 1) once per
 *   BATTERY_POLL_INTERVAL_MS.  All data is read back on the same core
 *   via MCAL_Battery_GetStatus(), so no mutex is needed.
 */

#ifndef BATTERY_MCAL_H
#define BATTERY_MCAL_H

#include <Arduino.h>

/* ------ Config ------ */
#define BATTERY_POLL_INTERVAL_MS   5000    /**< Re-read ADC every 5 s       */
#define BATTERY_CHARGE_DELTA_V     0.01f   /**< Voltage rise → charging     */
#define BATTERY_FULL_V             3.70f   /**< 100 % threshold             */
#define BATTERY_EMPTY_V            3.40f   /**< 0 %  threshold (cut-off)    */
#define BATTERY_LOW_THRESHOLD_PCT  20      /**< "Low battery" warning level */
#define BATTERY_CRITICAL_PCT       10      /**< "Critical" warning level    */

/* ------ Types ------ */
typedef enum {
    BATTERY_STATE_UNKNOWN = 0,
    BATTERY_STATE_DISCHARGING,
    BATTERY_STATE_CHARGING,
    BATTERY_STATE_FULL,
} BatteryState_t;

typedef struct {
    float          voltageV;      /**< Measured battery voltage (V)        */
    uint8_t        percent;       /**< State-of-charge 0–100 %             */
    BatteryState_t state;         /**< Charging / discharging / full       */
    bool           isLow;         /**< true when percent ≤ LOW_THRESHOLD   */
    bool           isCritical;    /**< true when percent ≤ CRITICAL        */
    bool           isConnected;   /**< true when ADC path looks valid      */
    uint16_t       rawAdc;        /**< Latest averaged raw ADC sample      */
} BatteryStatus_t;

/* ------ API ------ */
/**
 * @brief  Initialise ADC and take an initial reading.
 *         Call from setup() after HAL_I2C_Init() (order doesn't matter,
 *         but must be before any task calls MCAL_Battery_Tick()).
 */
void MCAL_Battery_Init(void);
/**
 * @brief  Poll ADC and update internal state.
 *         Call periodically from UI_Task (or any single task).
 *         Returns immediately if BATTERY_POLL_INTERVAL_MS has not elapsed.
 */
void MCAL_Battery_Tick(void);
/**
 * @brief  Return the latest battery status snapshot.
 * @param  out  Filled with the most recent reading
 * @return true always (out is always valid after MCAL_Battery_Init)
 */
bool MCAL_Battery_GetStatus(BatteryStatus_t *out);
/**
 * @brief  Force an immediate ADC re-read (ignores poll interval).
 *         Useful right after wake-from-sleep.
 */
void MCAL_Battery_ForceRefresh(void);

/**
 * @brief  Force a raw ADC value for fault injection.
 * @param  raw  Raw ADC value (0..4095), or -1 to disable forcing.
 */
void MCAL_Battery_SetForcedRaw(int32_t raw);

#endif /* BATTERY_MCAL_H */
