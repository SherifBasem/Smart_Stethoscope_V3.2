/**
 * @file    integration_test.h
 * @brief   Integration test helpers for incremental system bring-up
 * @layer   APP
 */

#ifndef INTEGRATION_TEST_H
#define INTEGRATION_TEST_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

typedef struct {
    QueueHandle_t heartQueue;
    QueueHandle_t micLiveQueue;
    QueueHandle_t wifiStatusQueue;
    TaskHandle_t  uiTaskHandle;
    TaskHandle_t  wifiTaskHandle;
    TaskHandle_t  heartTaskHandle;
    TaskHandle_t  micTaskHandle;
} IntegrationTest_Context_t;

void IntegrationTest_RunStep(uint8_t step, const IntegrationTest_Context_t *ctx);
void IntegrationTest_RunAll(const IntegrationTest_Context_t *ctx);

#endif /* INTEGRATION_TEST_H */
