/**
 * @file    heart_task.h
 * @brief   Application Layer — Heart Monitor Task
 * @layer   APP
 *
 * Drives MCAL_Heart_Tick() sample-by-sample and manages sensor power:
 *   • Sensor is DISABLED at boot and whenever the Heart Monitor screen
 *     is not active, saving power and preventing spurious readings.
 *   • UI_Task calls HeartTask_SetActive(true/false) when the user
 *     enters or leaves UI_SCREEN_HEART_MONITOR.
 *   • When inactive the task sleeps on a 200 ms vTaskDelay loop,
 *     consuming negligible CPU.
 *
 * Task placement:
 *   Core 0, Priority 2 — same core as WiFi/UART.
 *   MCAL_Heart_Tick() yields naturally inside s_sensor.check() while
 *   waiting for FIFO data, so it does not starve other Core 0 tasks.
 */

#ifndef HEART_TASK_H
#define HEART_TASK_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "../MCAL/heart_mcal.h"
#include "task_timing.h"

/* ------ Config ------ */
#define HEART_TASK_STACK_SIZE   4096
#define HEART_TASK_PRIORITY     2
#define HEART_TASK_CORE         0

/* ------ Types ------ */
typedef struct {
    QueueHandle_t heartQueue;
} HeartTask_Params_t;

/* ------ API ------ */
/** FreeRTOS task body — do NOT call directly. */
void HeartTask(void *pvParams);
/**
 * @brief  Create the queue, init the MCAL, and start the task.
 *         params->heartQueue is valid when this returns.
 */
TaskHandle_t HeartTask_Start(HeartTask_Params_t *params);
/**
 * @brief  Enable or disable sensor sampling.
 *         Thread-safe (uses atomic bool).
 *         Call HeartTask_SetActive(true)  when entering Heart Monitor screen.
 *         Call HeartTask_SetActive(false) when leaving  Heart Monitor screen.
 */
void HeartTask_SetActive(bool active);

/**
 * @brief  Retrieve the latest timing statistics for Heart_Task.
 * @param  out  Filled on success
 * @return true if stats are available
 */
bool HeartTask_GetTimingStats(TaskTimingStats_t *out);

#endif /* HEART_TASK_H */
