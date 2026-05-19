/**
 * @file    uart_task.h
 * @brief   Application Layer — UART PC Bridge Task
 * @layer   APP
 *
 * Commands (115200 baud, Both NL & CR):
 *   HELP                     → list commands
 *   STATUS                   → WiFi + heart reading
 *   WIFI <ssid> <password>   → connect to WiFi
 *   WIFI_DISCONNECT          → disconnect WiFi
 *   WIFI_SCAN                → scan networks
 *   HEART                    → print latest BPM / SpO2 / IR raw
 *   REBOOT                   → software reset
 */

#ifndef UART_TASK_H
#define UART_TASK_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "../MCAL/uart_mcal.h"
#include "../MCAL/wifi_mcal.h"
#include "../MCAL/heart_mcal.h"

/* ------ Config ------ */
#define UART_TASK_STACK_SIZE  3072
#define UART_TASK_PRIORITY    1
#define UART_TASK_CORE        0
#define UART_POLL_INTERVAL_MS 50

/* ------ Types ------ */
typedef struct {
    QueueHandle_t wifiStatusQueue;
    QueueHandle_t heartQueue;
    TaskHandle_t  uiTaskHandle;
    TaskHandle_t  wifiTaskHandle;
    TaskHandle_t  heartTaskHandle;
    TaskHandle_t  micTaskHandle;
} UARTTask_Params_t;

/* ------ API ------ */
extern void UITask_SetWiFiCredentials(const char *ssid, const char *pass);
void UARTTask(void *pvParams);
TaskHandle_t UARTTask_Start(UARTTask_Params_t *params);

#ifdef UNIT_TEST
void UARTTask_Test_ProcessCommand(const char *raw, const UARTTask_Params_t *p);
#endif

#endif /* UART_TASK_H */
