/**
 * @file    heart_task.cpp
 * @brief   Application Layer — Heart Monitor Task implementation
 * @layer   APP
 */

#include "heart_task.h"
#include "../HAL/uart_hal.h"
#include <stdatomic.h>
#include <esp_timer.h>

/* ═══════════════════════════════════════════════════════════════════
   Shared active flag — written by UI_Task via HeartTask_SetActive(),
   read by HeartTask().  Declared volatile so the compiler does not
   cache it in a register across the loop iteration.
   ═══════════════════════════════════════════════════════════════════ */
static volatile bool s_active = false;

/* ═══════════════════════════════════════════════════════════════════
    Timing stats
    ═══════════════════════════════════════════════════════════════════ */
static TaskTimingStats_t s_timing = {0, 10000, 0, 0};
static int64_t s_lastTickUs = 0;
static int64_t s_jitterWindowStartUs = 0;

/* ═══════════════════════════════════════════════════════════════════
   HeartTask_SetActive
   Called from UI_Task (Core 1) — simple bool write is atomic on ESP32.
   ═══════════════════════════════════════════════════════════════════ */
void HeartTask_SetActive(bool active) {
    if (active == s_active) return;   /* No change — avoid redundant HAL calls */
    s_active = active;

    if (active) {
        if (!MCAL_Heart_IsReady()) {
            HAL_UART_SendLine("[Heart] Monitor requested, but MAX30102 is not available.");
            s_active = false;
            return;
        }
        MCAL_Heart_Enable();
        HAL_UART_SendLine("[Heart] Monitor screen opened — sensor ON.");
    } else {
        MCAL_Heart_Disable();
        HAL_UART_SendLine("[Heart] Monitor screen closed — sensor OFF.");
    }
}

/* ═══════════════════════════════════════════════════════════════════
   Task function
   ═══════════════════════════════════════════════════════════════════ */
void HeartTask(void *pvParams) {
    (void)pvParams;   /* queue already set up in HeartTask_Start */

    HAL_UART_SendLine("[Heart] Task running (sensor idle until screen opens).");

    for (;;) {
        if (s_active) {
            /*
             * Process all samples currently waiting in the sensor FIFO.
             * MCAL_Heart_Tick() calls s_sensor.check() internally, which
             * refills the library buffer from hardware, then processes
             * every available sample in one call.
             * A single vTaskDelay(1) hands control back to the scheduler
             * between iterations — prevents tight-loop starvation of
             * WiFi/UART tasks that share Core 0.
             */
            int64_t t0 = esp_timer_get_time();
            if (s_lastTickUs != 0) {
                uint32_t actual = (uint32_t)(t0 - s_lastTickUs);
                uint32_t jitter = (actual > s_timing.target_period_us)
                                ? (actual - s_timing.target_period_us)
                                : (s_timing.target_period_us - actual);

                if (s_jitterWindowStartUs == 0) s_jitterWindowStartUs = t0;
                if (t0 - s_jitterWindowStartUs >= 60000000LL) {
                    s_timing.max_jitter_us = 0;
                    s_jitterWindowStartUs = t0;
                }

                if (jitter > s_timing.max_jitter_us) s_timing.max_jitter_us = jitter;
                if (actual > (s_timing.target_period_us * 2)) s_timing.missed_deadlines++;
            }
            s_lastTickUs = t0;

            MCAL_Heart_Tick();
            int64_t t1 = esp_timer_get_time();
            uint32_t delta = (uint32_t)(t1 - t0);
            if (delta > s_timing.wcet_us) s_timing.wcet_us = delta;
            vTaskDelay(pdMS_TO_TICKS(10));
        } else {
            /* Screen is closed — sensor is in shutdown mode.
             * Sleep 200 ms between checks so we consume < 0.1 % CPU. */
            s_lastTickUs = 0;
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

bool HeartTask_GetTimingStats(TaskTimingStats_t *out) {
    if (!out) return false;
    *out = s_timing;
    return true;
}

/* ═══════════════════════════════════════════════════════════════════
   HeartTask_Start
   ═══════════════════════════════════════════════════════════════════ */
TaskHandle_t HeartTask_Start(HeartTask_Params_t *params) {
    /* Create the shared queue */
    params->heartQueue = xQueueCreate(1, sizeof(HeartReading_t));
    if (!params->heartQueue) {
        HAL_UART_SendLine("[Heart] FATAL: queue creation failed!");
        return NULL;
    }

    /* Initialise sensor (starts in shutdown state) */
    bool ok = MCAL_Heart_Init(params->heartQueue);
    if (!ok) {
        HAL_UART_SendLine("[Heart] WARNING: sensor absent — task runs in stub mode.");
    }

    /* Launch task */
    TaskHandle_t handle = NULL;
    xTaskCreatePinnedToCore(
        HeartTask,
        "Heart_Task",
        HEART_TASK_STACK_SIZE,
        (void *)params,
        HEART_TASK_PRIORITY,
        &handle,
        HEART_TASK_CORE
    );

    if (!handle) {
        HAL_UART_SendLine("[Heart] FATAL: task creation failed!");
    }

    return handle;
}
