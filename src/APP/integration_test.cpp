/**
 * @file    integration_test.cpp
 * @brief   Integration test helpers for incremental system bring-up
 * @layer   APP
 */

#include "integration_test.h"
#include "heart_task.h"
#include "mic_task.h"
#include "wifi_task.h"
#include "../MCAL/battery_mcal.h"
#include "../MCAL/button_mcal.h"
#include "../MCAL/heart_mcal.h"
#include "../MCAL/mic_mcal.h"
#include "../MCAL/wifi_mcal.h"
#include "../HAL/i2c_hal.h"
#include "../HAL/mic_hal.h"
#include "../HAL/uart_hal.h"
#include <esp_timer.h>

#define I2C_ADDR_OLED   0x3C
#define I2C_ADDR_MAX30102 0x57

typedef struct {
    uint32_t timestampMs;
    bool     hasHeart;
    bool     hasMic;
    uint8_t  bpm;
    uint8_t  spo2;
    bool     finger;
    float    micDb;
    bool     micClip;
} IntegrationProcMsg_t;

typedef struct {
    QueueHandle_t heartQueue;
    QueueHandle_t micLiveQueue;
    QueueHandle_t outQueue;
} IntegrationProcCtx_t;

static QueueHandle_t s_procQueue = NULL;
static TaskHandle_t  s_procTask = NULL;
static TaskHandle_t  s_outTask = NULL;
static volatile bool s_pipelineRunning = false;

static TaskHandle_t  s_stressTask0 = NULL;
static TaskHandle_t  s_stressTask1 = NULL;
static volatile bool s_stressRunning = false;

static void printBanner(const char *title) {
    HAL_UART_SendLine("\r\n====================================================");
    HAL_UART_Printf("[INTEGRATION] %s\r\n", title);
    HAL_UART_SendLine("====================================================");
}

static const char *btnEventName(ButtonEvent_t evt) {
    switch (evt) {
        case BTN_EVENT_UP_PRESSED:    return "UP_PRESSED";
        case BTN_EVENT_DOWN_PRESSED:  return "DOWN_PRESSED";
        case BTN_EVENT_SELECT_PRESSED:return "SELECT_PRESSED";
        case BTN_EVENT_BACK_PRESSED:  return "BACK_PRESSED";
        case BTN_EVENT_UP_HELD:       return "UP_HELD";
        case BTN_EVENT_DOWN_HELD:     return "DOWN_HELD";
        case BTN_EVENT_SELECT_HELD:   return "SELECT_HELD";
        case BTN_EVENT_BACK_HELD:     return "BACK_HELD";
        default:                      return "UNKNOWN";
    }
}

static bool waitForHeart(QueueHandle_t q, HeartReading_t *out, uint32_t timeoutMs) {
    if (!q || !out) return false;
    TickType_t start = xTaskGetTickCount();
    TickType_t timeoutTicks = pdMS_TO_TICKS(timeoutMs);
    while ((xTaskGetTickCount() - start) < timeoutTicks) {
        if (xQueuePeek(q, out, 0) == pdTRUE) return true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}

static bool waitForMic(QueueHandle_t q, MicLiveReading_t *out, uint32_t timeoutMs) {
    if (!q || !out) return false;
    TickType_t start = xTaskGetTickCount();
    TickType_t timeoutTicks = pdMS_TO_TICKS(timeoutMs);
    while ((xTaskGetTickCount() - start) < timeoutTicks) {
        if (xQueuePeek(q, out, 0) == pdTRUE) return true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}

static void processingTask(void *pv) {
    IntegrationProcCtx_t *ctx = (IntegrationProcCtx_t *)pv;
    while (s_pipelineRunning) {
        HeartReading_t hr;
        MicLiveReading_t mic;
        bool hasHeart = ctx->heartQueue && (xQueuePeek(ctx->heartQueue, &hr, 0) == pdTRUE);
        bool hasMic   = ctx->micLiveQueue && (xQueuePeek(ctx->micLiveQueue, &mic, 0) == pdTRUE);

        if (hasHeart || hasMic) {
            IntegrationProcMsg_t msg = {};
            msg.timestampMs = (uint32_t)millis();
            msg.hasHeart = hasHeart;
            msg.hasMic = hasMic;
            if (hasHeart) {
                msg.bpm = hr.bpm;
                msg.spo2 = hr.spo2;
                msg.finger = hr.fingerPresent;
            }
            if (hasMic) {
                msg.micDb = mic.dbSPL;
                msg.micClip = mic.clipping;
            }
            if (ctx->outQueue) xQueueOverwrite(ctx->outQueue, &msg);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}

static void outputTask(void *pv) {
    QueueHandle_t q = (QueueHandle_t)pv;
    IntegrationProcMsg_t msg;
    while (s_pipelineRunning) {
        if (q && xQueueReceive(q, &msg, pdMS_TO_TICKS(500)) == pdTRUE) {
            HAL_UART_Printf(
                "[INT-5] t=%lums heart=%s bpm=%u spo2=%u finger=%s mic=%s db=%.1f clip=%s\r\n",
                (unsigned long)msg.timestampMs,
                msg.hasHeart ? "Y" : "N",
                msg.bpm,
                msg.spo2,
                msg.finger ? "Y" : "N",
                msg.hasMic ? "Y" : "N",
                (double)msg.micDb,
                msg.micClip ? "Y" : "N"
            );
        }
    }
    vTaskDelete(NULL);
}

static void startPipeline(const IntegrationTest_Context_t *ctx, bool withOutput) {
    if (s_procQueue) {
        vQueueDelete(s_procQueue);
        s_procQueue = NULL;
    }
    s_procQueue = xQueueCreate(1, sizeof(IntegrationProcMsg_t));
    if (!s_procQueue) {
        HAL_UART_SendLine("[INT] ERROR: failed to create processing queue.");
        return;
    }

    static IntegrationProcCtx_t procCtx;
    procCtx.heartQueue = ctx ? ctx->heartQueue : NULL;
    procCtx.micLiveQueue = ctx ? ctx->micLiveQueue : NULL;
    procCtx.outQueue = s_procQueue;

    s_pipelineRunning = true;
    xTaskCreatePinnedToCore(
        processingTask,
        "INT_Proc",
        4096,
        &procCtx,
        1,
        &s_procTask,
        0
    );

    if (withOutput) {
        xTaskCreatePinnedToCore(
            outputTask,
            "INT_Out",
            4096,
            (void *)s_procQueue,
            1,
            &s_outTask,
            1
        );
    }
}

static void stopPipeline(void) {
    s_pipelineRunning = false;
    vTaskDelay(pdMS_TO_TICKS(100));

    if (s_procTask) {
        vTaskDelete(s_procTask);
        s_procTask = NULL;
    }
    if (s_outTask) {
        vTaskDelete(s_outTask);
        s_outTask = NULL;
    }
    if (s_procQueue) {
        vQueueDelete(s_procQueue);
        s_procQueue = NULL;
    }
}

static void stressTask(void *pv) {
    (void)pv;
    volatile uint32_t x = 0;
    while (s_stressRunning) {
        x = x * 1664525u + 1013904223u;
    }
    vTaskDelete(NULL);
}

static void startStress(void) {
    s_stressRunning = true;
    xTaskCreatePinnedToCore(
        stressTask,
        "INT_Stress0",
        2048,
        NULL,
        1,
        &s_stressTask0,
        0
    );
    xTaskCreatePinnedToCore(
        stressTask,
        "INT_Stress1",
        2048,
        NULL,
        1,
        &s_stressTask1,
        1
    );
}

static void stopStress(void) {
    s_stressRunning = false;
    vTaskDelay(pdMS_TO_TICKS(50));
    if (s_stressTask0) {
        vTaskDelete(s_stressTask0);
        s_stressTask0 = NULL;
    }
    if (s_stressTask1) {
        vTaskDelete(s_stressTask1);
        s_stressTask1 = NULL;
    }
}

static void step1_sensors_alone(void) {
    printBanner("INT-1: Sensor-alone checks");

    HAL_UART_Printf("[INT-1] I2C OLED probe (0x3C): %s\r\n",
                    HAL_I2C_Probe(I2C_ADDR_OLED) ? "OK" : "FAIL");
    HAL_UART_Printf("[INT-1] I2C MAX30102 probe (0x57): %s\r\n",
                    HAL_I2C_Probe(I2C_ADDR_MAX30102) ? "OK" : "FAIL");

    int16_t minAmp = 32767;
    int16_t maxAmp = -32768;
    int32_t sumAbs = 0;
    const uint16_t samples = 200;
    for (uint16_t i = 0; i < samples; i++) {
        uint16_t raw = HAL_MicAdc_ReadRaw();
        int16_t amp = HAL_MicAdc_RawToAmplitude(raw);
        if (amp < minAmp) minAmp = amp;
        if (amp > maxAmp) maxAmp = amp;
        sumAbs += (amp < 0) ? -amp : amp;
        delayMicroseconds(250);
    }
    float avgAbs = (float)sumAbs / (float)samples;
    HAL_UART_Printf("[INT-1] Mic raw: min=%d max=%d avgAbs=%.1f\r\n",
                    (int)minAmp, (int)maxAmp, (double)avgAbs);

    MCAL_Battery_ForceRefresh();
    BatteryStatus_t batt;
    if (MCAL_Battery_GetStatus(&batt)) {
        HAL_UART_Printf("[INT-1] Battery: %.2fV %u%% state=%d low=%s critical=%s\r\n",
                        (double)batt.voltageV,
                        batt.percent,
                        (int)batt.state,
                        batt.isLow ? "Y" : "N",
                        batt.isCritical ? "Y" : "N");
    } else {
        HAL_UART_SendLine("[INT-1] Battery: status unavailable");
    }

    HAL_UART_SendLine("[INT-1] Buttons: press each button within 3 seconds...");
    MCAL_Button_Reset();
    uint32_t endMs = (uint32_t)millis() + 3000;
    uint32_t eventCount = 0;
    while ((uint32_t)millis() < endMs) {
        MCAL_Button_Poll();
        ButtonEvent_t evt;
        while (MCAL_Button_GetEvent(&evt)) {
            HAL_UART_Printf("[INT-1] Button event: %s\r\n", btnEventName(evt));
            eventCount++;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    HAL_UART_Printf("[INT-1] Button events captured: %lu\r\n",
                    (unsigned long)eventCount);
}

static void step2_sensor_tasks(const IntegrationTest_Context_t *ctx) {
    printBanner("INT-2: Sensor + RTOS task checks");

    HeartTask_SetActive(true);
    MicTask_SetActive(true);
    vTaskDelay(pdMS_TO_TICKS(200));

    HeartReading_t hr;
    bool gotHeart = waitForHeart(ctx ? ctx->heartQueue : NULL, &hr, 2000);
    HAL_UART_Printf("[INT-2] Heart task active=%s queue=%s bpm=%u spo2=%u finger=%s\r\n",
                    MCAL_Heart_IsEnabled() ? "Y" : "N",
                    gotHeart ? "OK" : "NO",
                    hr.bpm,
                    hr.spo2,
                    hr.fingerPresent ? "Y" : "N");

    MicLiveReading_t mic;
    bool gotMic = waitForMic(ctx ? ctx->micLiveQueue : NULL, &mic, 2000);
    HAL_UART_Printf("[INT-2] Mic task active=%s queue=%s db=%.1f clip=%s\r\n",
                    MCAL_Mic_IsActive() ? "Y" : "N",
                    gotMic ? "OK" : "NO",
                    (double)mic.dbSPL,
                    mic.clipping ? "Y" : "N");
}

static void step3_task_queue(const IntegrationTest_Context_t *ctx) {
    printBanner("INT-3: Sensor task + queue checks");

    if (ctx && ctx->heartQueue) {
        HeartReading_t hr;
        bool ok = xQueuePeek(ctx->heartQueue, &hr, 0) == pdTRUE;
        HAL_UART_Printf("[INT-3] Heart queue: %s (waiting=%lu) bpm=%u spo2=%u\r\n",
                        ok ? "OK" : "EMPTY",
                        (unsigned long)uxQueueMessagesWaiting(ctx->heartQueue),
                        hr.bpm,
                        hr.spo2);
    } else {
        HAL_UART_SendLine("[INT-3] Heart queue: not available");
    }

    if (ctx && ctx->micLiveQueue) {
        MicLiveReading_t mic;
        bool ok = xQueuePeek(ctx->micLiveQueue, &mic, 0) == pdTRUE;
        HAL_UART_Printf("[INT-3] Mic queue: %s (waiting=%lu) db=%.1f\r\n",
                        ok ? "OK" : "EMPTY",
                        (unsigned long)uxQueueMessagesWaiting(ctx->micLiveQueue),
                        (double)mic.dbSPL);
    } else {
        HAL_UART_SendLine("[INT-3] Mic queue: not available");
    }
}

static void step4_queue_processing(const IntegrationTest_Context_t *ctx) {
    printBanner("INT-4: Queue -> processing task");

    startPipeline(ctx, false);
    vTaskDelay(pdMS_TO_TICKS(2000));

    if (s_procQueue) {
        IntegrationProcMsg_t msg;
        if (xQueuePeek(s_procQueue, &msg, 0) == pdTRUE) {
            HAL_UART_Printf("[INT-4] Proc sample: t=%lums heart=%s bpm=%u mic=%s db=%.1f\r\n",
                            (unsigned long)msg.timestampMs,
                            msg.hasHeart ? "Y" : "N",
                            msg.bpm,
                            msg.hasMic ? "Y" : "N",
                            (double)msg.micDb);
        } else {
            HAL_UART_SendLine("[INT-4] Proc sample: no data available");
        }
    }

    stopPipeline();
}

static void step5_processing_output(const IntegrationTest_Context_t *ctx) {
    printBanner("INT-5: Processing task -> output task");

    startPipeline(ctx, true);
    vTaskDelay(pdMS_TO_TICKS(5000));
    stopPipeline();
    HAL_UART_SendLine("[INT-5] Output task stopped after 5 seconds.");
}

static void step6_complete_system(const IntegrationTest_Context_t *ctx) {
    printBanner("INT-6: Complete system snapshot");

    HAL_UART_Printf("[INT-6] Free heap: %lu bytes\r\n", (unsigned long)ESP.getFreeHeap());
    HAL_UART_Printf("[INT-6] Uptime: %lu s\r\n", (unsigned long)(millis() / 1000));

    BatteryStatus_t batt;
    if (MCAL_Battery_GetStatus(&batt)) {
        HAL_UART_Printf("[INT-6] Battery: %.2fV %u%% state=%d\r\n",
                        (double)batt.voltageV,
                        batt.percent,
                        (int)batt.state);
    }

    if (ctx && ctx->wifiStatusQueue) {
        WiFiHAL_Status_t ws;
        if (xQueuePeek(ctx->wifiStatusQueue, &ws, 0) == pdTRUE) {
            const char *stStr[] = {"IDLE","CONNECTING","CONNECTED","FAILED","DISCONNECTED","SETUP_PORTAL"};
            HAL_UART_Printf("[INT-6] WiFi: %s RSSI=%d\r\n", stStr[ws.state], (int)ws.rssi);
        } else {
            HAL_UART_SendLine("[INT-6] WiFi: no status in queue");
        }
    }

    if (ctx && ctx->heartQueue) {
        HeartReading_t hr;
        if (xQueuePeek(ctx->heartQueue, &hr, 0) == pdTRUE) {
            HAL_UART_Printf("[INT-6] Heart: bpm=%u spo2=%u finger=%s\r\n",
                            hr.bpm, hr.spo2, hr.fingerPresent ? "Y" : "N");
        }
    }

    if (ctx && ctx->micLiveQueue) {
        MicLiveReading_t mic;
        if (xQueuePeek(ctx->micLiveQueue, &mic, 0) == pdTRUE) {
            HAL_UART_Printf("[INT-6] Mic: db=%.1f clip=%s\r\n",
                            (double)mic.dbSPL,
                            mic.clipping ? "Y" : "N");
        }
    }

    HAL_UART_Printf("[INT-6] Tasks: UI=%s WiFi=%s Heart=%s Mic=%s\r\n",
                    (ctx && ctx->uiTaskHandle) ? "OK" : "N/A",
                    (ctx && ctx->wifiTaskHandle) ? "OK" : "N/A",
                    (ctx && ctx->heartTaskHandle) ? "OK" : "N/A",
                    (ctx && ctx->micTaskHandle) ? "OK" : "N/A");
}

static void step7_under_load(const IntegrationTest_Context_t *ctx) {
    printBanner("INT-7: Complete system under load");

    HAL_UART_SendLine("[INT-7] Starting stress tasks for 5 seconds...");
    startStress();
    vTaskDelay(pdMS_TO_TICKS(5000));
    stopStress();

    TaskTimingStats_t stats;
    if (MicTask_GetTimingStats(&stats)) {
        HAL_UART_Printf("[INT-7] Mic  wcet=%luus jitter=%luus miss=%lu\r\n",
                        (unsigned long)stats.wcet_us,
                        (unsigned long)stats.max_jitter_us,
                        (unsigned long)stats.missed_deadlines);
    }
    if (HeartTask_GetTimingStats(&stats)) {
        HAL_UART_Printf("[INT-7] Heart wcet=%luus jitter=%luus miss=%lu\r\n",
                        (unsigned long)stats.wcet_us,
                        (unsigned long)stats.max_jitter_us,
                        (unsigned long)stats.missed_deadlines);
    }
    if (WiFiTask_GetTimingStats(&stats)) {
        HAL_UART_Printf("[INT-7] WiFi wcet=%luus jitter=%luus miss=%lu\r\n",
                        (unsigned long)stats.wcet_us,
                        (unsigned long)stats.max_jitter_us,
                        (unsigned long)stats.missed_deadlines);
    }

    if (ctx && ctx->wifiStatusQueue) {
        WiFiHAL_Status_t ws;
        if (xQueuePeek(ctx->wifiStatusQueue, &ws, 0) == pdTRUE) {
            const char *stStr[] = {"IDLE","CONNECTING","CONNECTED","FAILED","DISCONNECTED","SETUP_PORTAL"};
            HAL_UART_Printf("[INT-7] WiFi state after load: %s\r\n", stStr[ws.state]);
        }
    }
}

void IntegrationTest_RunStep(uint8_t step, const IntegrationTest_Context_t *ctx) {
    switch (step) {
        case 1: step1_sensors_alone(); break;
        case 2: step2_sensor_tasks(ctx); break;
        case 3: step3_task_queue(ctx); break;
        case 4: step4_queue_processing(ctx); break;
        case 5: step5_processing_output(ctx); break;
        case 6: step6_complete_system(ctx); break;
        case 7: step7_under_load(ctx); break;
        default:
            HAL_UART_SendLine("[INT] Usage: INT <1-7> or INT_ALL");
            break;
    }
}

void IntegrationTest_RunAll(const IntegrationTest_Context_t *ctx) {
    for (uint8_t step = 1; step <= 7; step++) {
        IntegrationTest_RunStep(step, ctx);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
