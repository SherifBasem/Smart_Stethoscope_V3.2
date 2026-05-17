/**
 * @file    wifi_task.cpp
 * @brief   Application Layer — WiFi Task implementation
 */

#include "wifi_task.h"
#include "../HAL/uart_hal.h"
#include <esp_timer.h>

/* ═══════════════════════════════════════════════════════════════════
    Timing stats
    ═══════════════════════════════════════════════════════════════════ */
static TaskTimingStats_t s_timing = {0, WIFI_TICK_INTERVAL_MS * 1000UL, 0, 0};
static int64_t s_lastLoopUs = 0;

void WiFiTask(void *pvParams) {
    WiFiTask_Params_t *p = (WiFiTask_Params_t *)pvParams;

    /* Initialise WiFi HAL with the shared status queue */
    MCAL_WiFi_Init(p->wifiStatusQueue);
    HAL_UART_SendLine("[WiFi] Task running.");

    if (!MCAL_WiFi_ConnectSaved()) {
        HAL_UART_SendLine("[WiFi] No saved credentials. Starting setup portal.");
        MCAL_WiFi_StartSetupPortal();
    }

    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {
        int64_t t0 = esp_timer_get_time();
        if (s_lastLoopUs != 0) {
            uint32_t actual = (uint32_t)(t0 - s_lastLoopUs);
            uint32_t target = s_timing.target_period_us;
            uint32_t jitter = (actual > target) ? (actual - target) : (target - actual);
            if (jitter > s_timing.max_jitter_us) s_timing.max_jitter_us = jitter;
            if (actual > target * 2) s_timing.missed_deadlines++;
        }
        s_lastLoopUs = t0;

        MCAL_WiFi_Tick();
        uint32_t intervalMs = (MCAL_WiFi_GetState() == WIFI_STATE_SETUP_PORTAL)
                            ? 50
                            : WIFI_TICK_INTERVAL_MS;
        s_timing.target_period_us = intervalMs * 1000UL;

        int64_t t1 = esp_timer_get_time();
        uint32_t delta = (uint32_t)(t1 - t0);
        if (delta > s_timing.wcet_us) s_timing.wcet_us = delta;
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(intervalMs));
    }
}

bool WiFiTask_GetTimingStats(TaskTimingStats_t *out) {
    if (!out) return false;
    *out = s_timing;
    return true;
}

TaskHandle_t WiFiTask_Start(WiFiTask_Params_t *params) {
    TaskHandle_t handle = NULL;
    xTaskCreatePinnedToCore(
        WiFiTask,
        "WiFi_Task",
        WIFI_TASK_STACK_SIZE,
        (void *)params,
        WIFI_TASK_PRIORITY,
        &handle,
        WIFI_TASK_CORE
    );
    return handle;
}
