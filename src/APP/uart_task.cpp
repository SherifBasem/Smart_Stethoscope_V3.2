/**
 * @file    uart_task.cpp
 * @brief   Application Layer — UART Task implementation
 */

#include "uart_task.h"
#include "../MCAL/captive_portal_mcal.h"
#include "../MCAL/battery_mcal.h"
#include "mic_task.h"
#include "heart_task.h"
#include "ui_task.h"
#include "wifi_task.h"
#include "../HAL/i2c_hal.h"
#include <esp_system.h>
#include <esp_timer.h>

/* ═══════════════════════════════════════════════════════════════════
   Timing stats (UART Task)
   ═══════════════════════════════════════════════════════════════════ */
static TaskTimingStats_t s_timing = {0, UART_POLL_INTERVAL_MS * 1000UL, 0, 0};
static int64_t s_lastLoopUs = 0;

/* ═══════════════════════════════════════════════════════════════════
   Stress / demo helpers
   ═══════════════════════════════════════════════════════════════════ */
static TaskHandle_t s_stressTask = NULL;
static volatile uint32_t s_sharedCounter = 0;
static portMUX_TYPE s_raceMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_prioInvWaitUs = 0;

static void stressTask(void *pv) {
    (void)pv;
    volatile uint32_t x = 0;
    for (;;) {
        x = x * 1664525u + 1013904223u;
    }
}

typedef struct {
    bool useCritical;
    uint32_t iterations;
    TaskHandle_t notifyTask;
} RaceArgs_t;

static void raceWorker(void *pv) {
    RaceArgs_t *args = (RaceArgs_t *)pv;
    for (uint32_t i = 0; i < args->iterations; i++) {
        if (args->useCritical) {
            portENTER_CRITICAL(&s_raceMux);
            s_sharedCounter++;
            portEXIT_CRITICAL(&s_raceMux);
        } else {
            s_sharedCounter++;
        }
    }
    if (args->notifyTask) xTaskNotifyGive(args->notifyTask);
    vTaskDelete(NULL);
}

static void prioLowTask(void *pv) {
    TaskHandle_t notify = (TaskHandle_t)pv;
    if (g_i2cMutex) xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(500));
    if (g_i2cMutex) xSemaphoreGive(g_i2cMutex);
    if (notify) xTaskNotifyGive(notify);
    vTaskDelete(NULL);
}

static void prioHighTask(void *pv) {
    TaskHandle_t notify = (TaskHandle_t)pv;
    int64_t t0 = esp_timer_get_time();
    if (g_i2cMutex) xSemaphoreTake(g_i2cMutex, portMAX_DELAY);
    int64_t t1 = esp_timer_get_time();
    s_prioInvWaitUs = (uint32_t)(t1 - t0);
    if (g_i2cMutex) xSemaphoreGive(g_i2cMutex);
    if (notify) xTaskNotifyGive(notify);
    vTaskDelete(NULL);
}

static void prioMidTask(void *pv) {
    (void)pv;
    int64_t endUs = esp_timer_get_time() + 600000;
    volatile uint32_t x = 0;
    while (esp_timer_get_time() < endUs) {
        x = x * 22695477u + 1u;
    }
    vTaskDelete(NULL);
}

/* ═══════════════════════════════════════════════════════════════════
   Command parser
   ═══════════════════════════════════════════════════════════════════ */
static void processCommand(const char *raw, const UARTTask_Params_t *p)
{
    char buf[UART_CMD_MAX_LEN];
    strncpy(buf, raw, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *token = strtok(buf, " ");
    if (!token) return;

    /* ── HELP ── */
    if (strcasecmp(token, "HELP") == 0) {
        MCAL_UART_Respond("Commands:");
        MCAL_UART_Respond("  STATUS           - system + sensor status");
        MCAL_UART_Respond("  WIFI <s> <p>     - connect to network");
        MCAL_UART_Respond("  WIFI_CLEAR       - clear saved credentials");
        MCAL_UART_Respond("  WIFI_SETUP       - start setup portal");
        MCAL_UART_Respond("  WIFI_DISCONNECT  - disconnect");
        MCAL_UART_Respond("  WIFI_SCAN        - list nearby networks");
        MCAL_UART_Respond("  HEART            - latest BPM / SpO2 / IR");
        MCAL_UART_Respond("  TIMING           - task WCET/period/jitter");
        MCAL_UART_Respond("  STACK            - stack high-water marks");
        MCAL_UART_Respond("  FAULT_ADC <LOW|HIGH|OFF>");
        MCAL_UART_Respond("  STRESS_ON / STRESS_OFF");
        MCAL_UART_Respond("  RTOS_STATS       - runtime stats table");
        MCAL_UART_Respond("  RACE_UNSAFE      - shared counter demo");
        MCAL_UART_Respond("  RACE_SAFE        - shared counter w/ mutex");
        MCAL_UART_Respond("  PRIO_INV         - priority inversion demo");
        MCAL_UART_Respond("  REBOOT           - software reset");
        return;
    }

    /* ── STATUS ── */
    if (strcasecmp(token, "STATUS") == 0) {
        MCAL_UART_Respond("--- System Status ---");
        MCAL_UART_Respond("Free heap : %lu bytes", (unsigned long)esp_get_free_heap_size());
        MCAL_UART_Respond("Uptime    : %lu s",     (unsigned long)(millis() / 1000));
        MCAL_UART_Respond("Core ID   : %d",        xPortGetCoreID());

    WiFiHAL_Status_t ws;
    if (p->wifiStatusQueue && xQueuePeek(p->wifiStatusQueue, &ws, 0) == pdTRUE) {
            const char *stStr[] = {"IDLE","CONNECTING","CONNECTED","FAILED","DISCONNECTED","SETUP_PORTAL"};
            MCAL_UART_Respond("WiFi      : %s", stStr[ws.state]);
            if (ws.state == WIFI_STATE_CONNECTED) {
                MCAL_UART_Respond("  IP   : %s",     ws.ip);
                MCAL_UART_Respond("  RSSI : %d dBm", (int)ws.rssi);
                MCAL_UART_Respond("  SSID : %s",     ws.ssid);
            }
        }

        if (p->heartQueue) {
            HeartReading_t hr;
            if (xQueuePeek(p->heartQueue, &hr, 0) == pdTRUE) {
                MCAL_UART_Respond("Heart     : finger=%s  BPM=%u  SpO2=%u%%  IR=%lu",
                                 hr.fingerPresent ? "YES" : "NO",
                                 hr.bpm, hr.spo2,
                                 (unsigned long)hr.irRaw);
            } else {
                MCAL_UART_Respond("Heart     : no data (screen not open?)");
            }
        }

        MCAL_UART_Respond("---------------------");
        return;
    }

    /* ── WIFI <ssid> <password> ── */
    if (strcasecmp(token, "WIFI") == 0) {
        char *ssid = strtok(NULL, " ");
        char *pass = strtok(NULL, " ");
        if (!ssid) { MCAL_UART_Respond("[ERR] Usage: WIFI <ssid> <password>"); return; }
        const char *pw = pass ? pass : "";
        UITask_SetWiFiCredentials(ssid, pw);
        MCAL_WiFi_Connect(ssid, pw);
        MCAL_UART_Respond("[OK] Connecting to \"%s\"...", ssid);
        return;
    }

    /* ── WIFI_CLEAR ── */
    if (strcasecmp(token, "WIFI_CLEAR") == 0) {
        MCAL_WiFi_ClearSavedCredentials();
        UITask_SetWiFiCredentials("", "");
        MCAL_UART_Respond("[OK] Saved WiFi credentials cleared.");
        return;
    }

    /* ── WIFI_SETUP ── */
    if (strcasecmp(token, "WIFI_SETUP") == 0) {
        if (MCAL_WiFi_StartSetupPortal()) {
            MCAL_UART_Respond("[OK] Setup portal started: %s", PORTAL_SETUP_URL);
        } else {
            MCAL_UART_Respond("[ERR] Could not start setup portal.");
        }
        return;
    }

    /* ── WIFI_DISCONNECT ── */
    if (strcasecmp(token, "WIFI_DISCONNECT") == 0) {
        MCAL_WiFi_Disconnect();
        MCAL_UART_Respond("[OK] Disconnected.");
        return;
    }

    /* ── WIFI_SCAN ── */
    if (strcasecmp(token, "WIFI_SCAN") == 0) {
        char ssids[10][33];
        uint8_t count = MCAL_WiFi_Scan(ssids, 10);
        MCAL_UART_Respond("Found %d networks:", count);
        for (uint8_t i = 0; i < count; i++)
            MCAL_UART_Respond("  [%d] %s", i + 1, ssids[i]);
        return;
    }

    /* ── HEART ── */
    if (strcasecmp(token, "HEART") == 0) {
    if (!p->heartQueue) { MCAL_UART_Respond("[Heart] Queue not available."); return; }
        HeartReading_t hr;
    if (xQueuePeek(p->heartQueue, &hr, 0) != pdTRUE) {
            MCAL_UART_Respond("[Heart] No data — open Heart Monitor screen first.");
            return;
        }
        MCAL_UART_Respond("[Heart] Finger : %s",        hr.fingerPresent ? "YES" : "NO");
        MCAL_UART_Respond("[Heart] IR raw : %lu",        (unsigned long)hr.irRaw);
        MCAL_UART_Respond("[Heart] BPM    : %u (%s)",    hr.bpm,  hr.validBPM  ? "valid" : "warming up");
        MCAL_UART_Respond("[Heart] SpO2   : %u%% (%s)",  hr.spo2, hr.validSpO2 ? "valid" : "pending");
        return;
    }

    /* ── TIMING ── */
    if (strcasecmp(token, "TIMING") == 0) {
        TaskTimingStats_t stats;
        if (MicTask_GetTimingStats(&stats)) {
            MCAL_UART_Respond("[Timing] Mic   wcet=%luus  period=%luus  jitter=%luus  miss=%lu",
                              (unsigned long)stats.wcet_us,
                              (unsigned long)stats.target_period_us,
                              (unsigned long)stats.max_jitter_us,
                              (unsigned long)stats.missed_deadlines);
        }
        if (HeartTask_GetTimingStats(&stats)) {
            MCAL_UART_Respond("[Timing] Heart wcet=%luus  period=%luus  jitter=%luus  miss=%lu",
                              (unsigned long)stats.wcet_us,
                              (unsigned long)stats.target_period_us,
                              (unsigned long)stats.max_jitter_us,
                              (unsigned long)stats.missed_deadlines);
        }
        if (WiFiTask_GetTimingStats(&stats)) {
            MCAL_UART_Respond("[Timing] WiFi  wcet=%luus  period=%luus  jitter=%luus  miss=%lu",
                              (unsigned long)stats.wcet_us,
                              (unsigned long)stats.target_period_us,
                              (unsigned long)stats.max_jitter_us,
                              (unsigned long)stats.missed_deadlines);
        }
        if (UITask_GetTimingStats(&stats)) {
            MCAL_UART_Respond("[Timing] UI    wcet=%luus  period=%luus  jitter=%luus  miss=%lu",
                              (unsigned long)stats.wcet_us,
                              (unsigned long)stats.target_period_us,
                              (unsigned long)stats.max_jitter_us,
                              (unsigned long)stats.missed_deadlines);
        }
        MCAL_UART_Respond("[Timing] UART  wcet=%luus  period=%luus  jitter=%luus  miss=%lu",
                          (unsigned long)s_timing.wcet_us,
                          (unsigned long)s_timing.target_period_us,
                          (unsigned long)s_timing.max_jitter_us,
                          (unsigned long)s_timing.missed_deadlines);
        return;
    }

    /* ── STACK ── */
    if (strcasecmp(token, "STACK") == 0) {
        MCAL_UART_Respond("[Stack] UART  : %u words", (unsigned)uxTaskGetStackHighWaterMark(NULL));
        if (p && p->uiTaskHandle)
            MCAL_UART_Respond("[Stack] UI    : %u words", (unsigned)uxTaskGetStackHighWaterMark(p->uiTaskHandle));
        if (p && p->wifiTaskHandle)
            MCAL_UART_Respond("[Stack] WiFi  : %u words", (unsigned)uxTaskGetStackHighWaterMark(p->wifiTaskHandle));
        if (p && p->heartTaskHandle)
            MCAL_UART_Respond("[Stack] Heart : %u words", (unsigned)uxTaskGetStackHighWaterMark(p->heartTaskHandle));
        if (p && p->micTaskHandle)
            MCAL_UART_Respond("[Stack] Mic   : %u words", (unsigned)uxTaskGetStackHighWaterMark(p->micTaskHandle));
        return;
    }

    /* ── FAULT_ADC ── */
    if (strcasecmp(token, "FAULT_ADC") == 0) {
        char *mode = strtok(NULL, " ");
        if (!mode) { MCAL_UART_Respond("[ERR] Usage: FAULT_ADC <LOW|HIGH|OFF>"); return; }
        if (strcasecmp(mode, "LOW") == 0) {
            MCAL_Battery_SetForcedRaw(0);
            MCAL_UART_Respond("[OK] Battery ADC forced to 0.");
        } else if (strcasecmp(mode, "HIGH") == 0) {
            MCAL_Battery_SetForcedRaw(4095);
            MCAL_UART_Respond("[OK] Battery ADC forced to 4095.");
        } else if (strcasecmp(mode, "OFF") == 0) {
            MCAL_Battery_SetForcedRaw(-1);
            MCAL_UART_Respond("[OK] Battery ADC forcing disabled.");
        } else {
            MCAL_UART_Respond("[ERR] Unknown mode: %s", mode);
        }
        return;
    }

    /* ── STRESS_ON / STRESS_OFF ── */
    if (strcasecmp(token, "STRESS_ON") == 0) {
        if (!s_stressTask) {
            xTaskCreatePinnedToCore(stressTask, "Stress_Task", 2048, NULL, 1, &s_stressTask, 0);
            MCAL_UART_Respond("[OK] Stress task started on Core 0.");
        } else {
            MCAL_UART_Respond("[OK] Stress task already running.");
        }
        return;
    }
    if (strcasecmp(token, "STRESS_OFF") == 0) {
        if (s_stressTask) {
            vTaskDelete(s_stressTask);
            s_stressTask = NULL;
            MCAL_UART_Respond("[OK] Stress task stopped.");
        } else {
            MCAL_UART_Respond("[OK] Stress task not running.");
        }
        return;
    }

    /* ── RTOS_STATS ── */
    if (strcasecmp(token, "RTOS_STATS") == 0) {
#if (configGENERATE_RUN_TIME_STATS == 1) && (configUSE_STATS_FORMATTING_FUNCTIONS == 1)
        char buf[512];
        vTaskGetRunTimeStats(buf);
        MCAL_UART_Respond("Task            Time      %%\n%s", buf);
#else
        MCAL_UART_Respond("[ERR] Runtime stats not enabled in FreeRTOSConfig.h");
#endif
        return;
    }

    /* ── RACE_UNSAFE / RACE_SAFE ── */
    if (strcasecmp(token, "RACE_UNSAFE") == 0 || strcasecmp(token, "RACE_SAFE") == 0) {
        bool safe = (strcasecmp(token, "RACE_SAFE") == 0);
        s_sharedCounter = 0;
        RaceArgs_t args[2];
        TaskHandle_t self = xTaskGetCurrentTaskHandle();
        args[0] = {safe, 10000, self};
        args[1] = {safe, 10000, self};
        xTaskCreatePinnedToCore(raceWorker, "RaceA", 2048, &args[0], 2, NULL, 0);
        xTaskCreatePinnedToCore(raceWorker, "RaceB", 2048, &args[1], 2, NULL, 0);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000));
        MCAL_UART_Respond("[Race] %s final=%lu (expected 20000)",
                          safe ? "SAFE" : "UNSAFE", (unsigned long)s_sharedCounter);
        return;
    }

    /* ── PRIO_INV ── */
    if (strcasecmp(token, "PRIO_INV") == 0) {
        s_prioInvWaitUs = 0;
        TaskHandle_t self = xTaskGetCurrentTaskHandle();
        xTaskCreatePinnedToCore(prioLowTask, "PrioLow", 2048, self, 1, NULL, 0);
        xTaskCreatePinnedToCore(prioMidTask, "PrioMid", 2048, NULL, 2, NULL, 0);
        xTaskCreatePinnedToCore(prioHighTask, "PrioHigh", 2048, self, 3, NULL, 0);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3000));
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(3000));
        MCAL_UART_Respond("[PrioInv] Heart blocked %lu us on I2C mutex.",
                          (unsigned long)s_prioInvWaitUs);
        return;
    }

    /* ── REBOOT ── */
    if (strcasecmp(token, "REBOOT") == 0) {
        MCAL_UART_Respond("[OK] Rebooting...");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
        return;
    }

    MCAL_UART_Respond("[ERR] Unknown command: %s  (try HELP)", token);
}

#ifdef UNIT_TEST
void UARTTask_Test_ProcessCommand(const char *raw, const UARTTask_Params_t *p) {
    processCommand(raw, p);
}
#endif

/* ═══════════════════════════════════════════════════════════════════
   Task
   ═══════════════════════════════════════════════════════════════════ */
void UARTTask(void *pvParams) {
    UARTTask_Params_t *p = (UARTTask_Params_t *)pvParams;

    MCAL_UART_Init();
    vTaskDelay(pdMS_TO_TICKS(500));
    MCAL_UART_PrintBanner();
    MCAL_UART_Respond("[UART] Task running. Type HELP for commands.");

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

        MCAL_UART_Poll();
        UARTCommand_t cmd;
        while (MCAL_UART_GetCommand(&cmd)) {
            MCAL_UART_Respond(">> %s", cmd.raw);
            processCommand(cmd.raw, p);
        }
        int64_t t1 = esp_timer_get_time();
        uint32_t delta = (uint32_t)(t1 - t0);
        if (delta > s_timing.wcet_us) s_timing.wcet_us = delta;
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(UART_POLL_INTERVAL_MS));
    }
}

TaskHandle_t UARTTask_Start(UARTTask_Params_t *params) {
    TaskHandle_t handle = NULL;
    xTaskCreatePinnedToCore(
        UARTTask, "UART_Task",
        UART_TASK_STACK_SIZE, (void *)params,
        UART_TASK_PRIORITY, &handle, UART_TASK_CORE
    );
    return handle;
}
