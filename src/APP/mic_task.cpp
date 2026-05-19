/**
 * @file    mic_task.cpp
 * @brief   Application Layer — Microphone Task implementation
 * @layer   APP
 */

#include "mic_task.h"
#include "../HAL/uart_hal.h"
#include <esp_timer.h>

/* ═══════════════════════════════════════════════════════════════════
   Active flag — written by UI_Task via MicTask_SetActive(),
   read by MicTask().  Volatile prevents compiler register caching.
   ═══════════════════════════════════════════════════════════════════ */
static volatile bool s_active = false;

/* ═══════════════════════════════════════════════════════════════════
   Timing stats
   ═══════════════════════════════════════════════════════════════════ */
static TaskTimingStats_t s_timing = {
    0,
    MIC_TASK_INTERVAL_US,
    0,
    0
};
static int64_t s_lastTickUs = 0;
static TaskHandle_t s_uploadTaskHandle = NULL;
static TaskHandle_t s_micTaskHandle = NULL;

static void MicUploadTask(void *pvParams) {
    (void)pvParams;
    MCAL_Mic_UploadRecording();
    s_uploadTaskHandle = NULL;
    vTaskDelete(NULL);
}

/* ═══════════════════════════════════════════════════════════════════
   MicTask_SetActive
   ═══════════════════════════════════════════════════════════════════ */
void MicTask_SetActive(bool active) {
    if (active && !MCAL_Mic_IsReady()) {
        HAL_UART_SendLine("[Mic] Screen opened, but MAX4466/ADC path is unavailable.");
        s_active = false;
        return;
    }
    if (active == s_active) return;
    s_active = active;
    MCAL_Mic_SetActive(active);
    HAL_UART_Printf("[Mic] Screen %s — ADC %s.\r\n",
                     active ? "opened" : "closed",
                     active ? "ON"     : "OFF");
}

/* ═══════════════════════════════════════════════════════════════════
   Task function
   ═══════════════════════════════════════════════════════════════════ */
void MicTask(void *pvParams) {
    (void)pvParams;

    HAL_UART_SendLine("[Mic] Task running (ADC idle until screen opens).");

    for (;;) {
        if (MCAL_Mic_GetState() == MIC_STATE_UPLOADING) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (s_active) {
            /*
             * Precision timing using esp_timer_get_time() (µs resolution).
             * We call MCAL_Mic_Tick() then busy-wait for the remainder of
             * the 250 µs sample interval.  This gives ±10 µs jitter —
             * adequate for 4 kHz audio without a hardware timer ISR.
             *
             * vTaskDelay(1) every 40 samples (= every 10 ms) hands control
             * back to the FreeRTOS scheduler so WiFi / Heart tasks can run.
             */
            static uint8_t s_yieldCounter = 0;
            static int64_t s_nextSampleUs = 0;

            if (s_nextSampleUs == 0) {
                s_nextSampleUs = esp_timer_get_time();
            }

            int64_t t0 = esp_timer_get_time();
            if (s_lastTickUs != 0) {
                uint32_t actual = (uint32_t)(t0 - s_lastTickUs);
                uint32_t jitter = (actual > MIC_TASK_INTERVAL_US)
                                ? (actual - MIC_TASK_INTERVAL_US)
                                : (MIC_TASK_INTERVAL_US - actual);
                if (jitter > s_timing.max_jitter_us) s_timing.max_jitter_us = jitter;
                if (actual > MIC_TASK_INTERVAL_US * 2) s_timing.missed_deadlines++;
            }
            s_lastTickUs = t0;

            MCAL_Mic_Tick();
            int64_t t1 = esp_timer_get_time();
            uint32_t delta = (uint32_t)(t1 - t0);
            if (delta > s_timing.wcet_us) s_timing.wcet_us = delta;
            s_nextSampleUs += MIC_TASK_INTERVAL_US;

            /* Busy-wait for the next sample slot */
            int64_t now = esp_timer_get_time();
            int64_t wait = s_nextSampleUs - now;
            if (wait > 0 && wait < (int64_t)MIC_TASK_INTERVAL_US * 2) {
                /* Tight wait — acceptable at P3 priority */
                while (esp_timer_get_time() < s_nextSampleUs) { /* spin */ }
            } else {
                /* Catch-up: reset reference if we've drifted badly */
                s_nextSampleUs = esp_timer_get_time() + MIC_TASK_INTERVAL_US;
            }

            /* Yield every 40 samples (10 ms) */
            if (++s_yieldCounter >= 40) {
                s_yieldCounter = 0;
                s_nextSampleUs = 0;   /* reset reference after yield */
                vTaskDelay(1);        /* 1 tick = 1 ms — let others run */
            }

        } else {
            /* Screen closed — sleep 200 ms between checks */
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

bool MicTask_GetTimingStats(TaskTimingStats_t *out) {
    if (!out) return false;
    *out = s_timing;
    return true;
}

/* ═══════════════════════════════════════════════════════════════════
   MicTask_Start
   ═══════════════════════════════════════════════════════════════════ */
TaskHandle_t MicTask_Start(MicTask_Params_t *params) {
    /* Create the shared live queue */
    params->micLiveQueue = xQueueCreate(1, sizeof(MicLiveReading_t));
    if (!params->micLiveQueue) {
        HAL_UART_SendLine("[Mic] FATAL: live queue creation failed!");
        return NULL;
    }

    /* Initialise MCAL (ADC + PCM buffer) */
    bool ok = MCAL_Mic_Init(params->micLiveQueue);
    if (!ok) {
        HAL_UART_SendLine("[Mic] WARNING: MCAL init failed — task runs in stub mode.");
    }

    /* Launch task */
    TaskHandle_t handle = NULL;
    xTaskCreatePinnedToCore(
        MicTask,
        "Mic_Task",
        MIC_TASK_STACK_SIZE,
        (void *)params,
        MIC_TASK_PRIORITY,
        &handle,
        MIC_TASK_CORE
    );

    if (!handle) {
        HAL_UART_SendLine("[Mic] FATAL: task creation failed!");
    } else {
        s_micTaskHandle = handle;
    }

    return handle;
}

bool MicTask_StartUpload(void) {
    if (s_uploadTaskHandle) return false;
    if (!MCAL_Mic_IsReady()) return false;

    BaseType_t ok = xTaskCreatePinnedToCore(
        MicUploadTask,
        "Mic_Upload",
        MIC_UPLOAD_TASK_STACK_SIZE,
        NULL,
        1,
        &s_uploadTaskHandle,
        1
    );

    if (ok != pdPASS) {
        s_uploadTaskHandle = NULL;
        HAL_UART_SendLine("[Mic] Upload task creation failed.");
        return false;
    }

    return true;
}
