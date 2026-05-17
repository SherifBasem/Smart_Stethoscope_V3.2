/**
 * @file    wifi_task.h / wifi_task.cpp (combined single-header for simplicity)
 * @brief   Application Layer — WiFi Task
 * @layer   APP
 *
 * Drives the MCAL_WiFi_Tick() state machine and keeps the shared
 * wifiStatusQueue up to date. Runs on Core 0 (Protocol core).
 */

#ifndef WIFI_TASK_H
#define WIFI_TASK_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "../MCAL/wifi_mcal.h"
#include "task_timing.h"

/* ------ Config ------ */
#define WIFI_TASK_STACK_SIZE  4096
#define WIFI_TASK_PRIORITY    1
#define WIFI_TASK_CORE        0       /**< Core 0 — WiFi/BT protocol stack runs here */
#define WIFI_TICK_INTERVAL_MS 500

/* ------ Types ------ */
typedef struct {
    QueueHandle_t wifiStatusQueue;   /**< Queue of WiFiHAL_Status_t (length 1, overwrite) */
} WiFiTask_Params_t;

/* ------ API ------ */
void WiFiTask(void *pvParams);
/**
 * @brief  Create and start the WiFi task.
 * @param  params  Pointer to heap-allocated WiFiTask_Params_t
 * @return Task handle
 */
TaskHandle_t WiFiTask_Start(WiFiTask_Params_t *params);

/**
 * @brief  Retrieve the latest timing statistics for WiFi_Task.
 * @param  out  Filled on success
 * @return true if stats are available
 */
bool WiFiTask_GetTimingStats(TaskTimingStats_t *out);

#endif /* WIFI_TASK_H */
