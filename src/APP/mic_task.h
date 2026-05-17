/**
 * @file    mic_task.h
 * @brief   Application Layer — Microphone (Lung Sound) Task
 * @layer   APP
 *
 * Drives MCAL_Mic_Tick() at MIC_SAMPLE_RATE_HZ (4 kHz) using a
 * precision-timed loop and manages sensor power:
 *
 *   • ADC is idle at boot and whenever the Lung Sound screen is closed.
 *   • UI_Task calls MicTask_SetActive(true/false) on screen transitions.
 *   • When inactive the task sleeps on a 200 ms vTaskDelay, consuming < 0.1% CPU.
 *
 * Sampling loop design:
 *   vTaskDelayUntil() at 250 µs (4 kHz) keeps the loop jitter below ±50 µs,
 *   which is adequate for audio digitisation in the 20–800 Hz band.
 *   The task is pinned to Core 0 alongside WiFi/UART to keep Core 1 free
 *   for the UI.
 *
 * Task placement:
 *   Core 0, Priority 3 (highest among data tasks) — must run at 4 kHz
 *   without being preempted by WiFi (P1) or UART (P1).
 *   Heart_Task (P2) runs on the same core; at 50 Hz it causes at most
 *   one 20 ms interruption per second, negligible for audio quality.
 */

#ifndef MIC_TASK_H
#define MIC_TASK_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "../MCAL/mic_mcal.h"
#include "task_timing.h"

/* ------ Config ------ */
#define MIC_TASK_STACK_SIZE  4096
#define MIC_TASK_PRIORITY    3         /**< Highest data-task priority       */
#define MIC_TASK_CORE        0         /**< Core 0 alongside WiFi/UART/Heart */

/* Interval in FreeRTOS ticks for the 4 kHz sampling loop.
   portTICK_PERIOD_MS is typically 1 ms on ESP32, so this gives 1-tick
   granularity.  For sub-millisecond accuracy MCAL_Mic_Tick() uses
   esp_timer or a tight busy-wait internally if configured.             */
#define MIC_TASK_INTERVAL_US  250      /**< 250 µs = 4 000 Hz               */

/* ------ Types ------ */
typedef struct {
    QueueHandle_t micLiveQueue;  /**< Length-1 queue of MicLiveReading_t    */
} MicTask_Params_t;

/* ------ API ------ */

/** FreeRTOS task body — do NOT call directly. */
void MicTask(void *pvParams);

/**
 * @brief  Create the live queue, init MCAL, and start the task.
 *         params->micLiveQueue is valid when this returns.
 * @return Task handle (NULL on failure)
 */
TaskHandle_t MicTask_Start(MicTask_Params_t *params);

/**
 * @brief  Enable or disable ADC sampling.
 *         Call MicTask_SetActive(true)  when entering UI_SCREEN_LUNG_SOUND.
 *         Call MicTask_SetActive(false) when leaving  UI_SCREEN_LUNG_SOUND.
 *         Thread-safe (uses volatile bool — single-word write on ESP32).
 */
void MicTask_SetActive(bool active);

/**
 * @brief  Retrieve the latest timing statistics for Mic_Task.
 * @param  out  Filled on success
 * @return true if stats are available
 */
bool MicTask_GetTimingStats(TaskTimingStats_t *out);

#endif /* MIC_TASK_H */