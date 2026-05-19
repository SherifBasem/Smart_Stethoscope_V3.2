/**
 * @file    ui_task.h
 * @brief   Application Layer — UI / Navigation Task
 * @layer   APP
 *
 * Full navigation tree:
 *
 *   BOOT_SCREEN
 *       │
 *       └─► MAIN_MENU  (5 items)
 *               ├─► SCREEN_HEART_MONITOR
 *               ├─► SCREEN_LUNG_SOUND        ← NEW (MAX4466 + ML)
 *               ├─► SCREEN_SETTINGS  (5 items)
 *               │       ├─► SETTINGS_WIFI  (6 items)
 *               │       │      ├─► SCREEN_WIFI_STATUS
 *               │       │      ├─► SCREEN_WIFI_LOGIN
 *               │       │      ├─► SCREEN_WIFI_SCAN
 *               │       │      ├─► SCREEN_WIFI_SETUP → SCREEN_WIFI_SETUP_QR
 *               │       │      ├─► SCREEN_WIFI_RESET
 *               │       │      └─► SCREEN_WIFI_DISCONNECT
 *               │       ├─► SCREEN_BRIGHTNESS   (Low / Med / High)
 *               │       ├─► SCREEN_INACTIVITY   (Off/15s/30s/60s/2min)
 *               │       ├─► SCREEN_REBOOT
 *               │       └─► SCREEN_SYSTEM_INFO
 *               ├─► SCREEN_SYSTEM_INFO
 *               │       └─► SCREEN_BATTERY_INFO  (SEL → battery details)
 *               └─► SCREEN_POWER_OFF  (confirm → light-sleep)
 *
 * Lung Sound screen sub-states (handled internally, not separate screens):
 *   IDLE     → showing live dBSPL bar + waveform, [SEL] starts recording
 *   REC      → countdown timer + live bar + waveform, [SEL] stops early
 *   DONE     → "Upload? [SEL] / [BCK] Discard"
 *   UPLOADING → spinner
 *   RESULT   → diagnosis label + confidence, [SEL/BCK] resets
 *
 * Battery icon:
 *   A 13×7 px battery widget is drawn in the top-right corner of
 *   every screen.  It shows fill level proportional to percentage
 *   and a lightning-bolt overlay when the charger is active.
 *
 * Button mapping:
 *   UP     (GPIO 12) → scroll up   in any menu
 *   DOWN   (GPIO 13) → scroll down in any menu
 *   SELECT (GPIO 10) → confirm / enter highlighted item
 *   BACK   (GPIO 11) → return to parent screen from anywhere
 */

#ifndef UI_TASK_H
#define UI_TASK_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "../MCAL/button_mcal.h"
#include "../MCAL/wifi_mcal.h"
#include "task_timing.h"

/* ------ Config ------ */
#define UI_TASK_STACK_SIZE      4096   /* Reduced to free heap for WiFi */
#define UI_TASK_PRIORITY        2
#define UI_TASK_CORE            1
#define UI_POLL_INTERVAL_MS     20
#define BOOT_SPLASH_MS          2000

#define INACTIVITY_OPTION_COUNT 5
extern const uint32_t INACTIVITY_OPTIONS_SEC[INACTIVITY_OPTION_COUNT];
#define INACTIVITY_DEFAULT_IDX  2      /**< 30 s */

/* ------ Types ------ */
typedef enum {
    /* Boot */
    UI_SCREEN_BOOT = 0,

    /* Top-level */
    UI_SCREEN_MAIN_MENU,
    UI_SCREEN_HEART_MONITOR,
    UI_SCREEN_LUNG_SOUND,           /**< NEW — MAX4466 mic + ML upload     */
    UI_SCREEN_SYSTEM_INFO,
    UI_SCREEN_BATTERY_INFO,
    UI_SCREEN_POWER_OFF,

    /* Settings sub-menu */
    UI_SCREEN_SETTINGS,
    UI_SCREEN_BRIGHTNESS,
    UI_SCREEN_INACTIVITY,
    UI_SCREEN_REBOOT,

    /* WiFi sub-sub-menu */
    UI_SCREEN_WIFI_MENU,
    UI_SCREEN_WIFI_STATUS,
    UI_SCREEN_WIFI_LOGIN,
    UI_SCREEN_WIFI_SCAN,
    UI_SCREEN_WIFI_RESET,
    UI_SCREEN_WIFI_DISCONNECT,
    UI_SCREEN_WIFI_SETUP,
    UI_SCREEN_WIFI_SETUP_QR,
} UIScreen_t;

typedef struct {
    QueueHandle_t btnQueue;
    QueueHandle_t wifiStatusQueue;
    QueueHandle_t heartQueue;
    QueueHandle_t micLiveQueue;     /**< NEW — MicLiveReading_t queue      */
} UITask_Params_t;

/* ------ API ------ */
void         UITask(void *pvParams);
TaskHandle_t UITask_Start(UITask_Params_t *params);
void         UITask_SetWiFiCredentials(const char *ssid, const char *pass);

/**
 * @brief  Retrieve the latest timing statistics for UI_Task.
 * @param  out  Filled on success
 * @return true if stats are available
 */
bool UITask_GetTimingStats(TaskTimingStats_t *out);

#endif /* UI_TASK_H */