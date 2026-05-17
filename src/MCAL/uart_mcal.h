/**
 * @file    uart_mcal.h
 * @brief   MCAL — UART PC bridge
 * @layer   MCAL
 *
 * Receives command strings from a PC terminal (115200 baud) and posts
 * them to a queue. Responses are sent back as human-readable lines.
 *
 * Supported commands (send via any serial terminal):
 *   STATUS       → print current system status
 *   WIFI <ssid> <pass>  → trigger WiFi connect
 *   WIFI_DISCONNECT     → disconnect WiFi
 *   WIFI_SCAN           → scan and list networks
 *   REBOOT              → soft reboot ESP32
 *   HELP                → list commands
 */

#ifndef UART_MCAL_H
#define UART_MCAL_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "../HAL/uart_hal.h"

/* ------ Config ------ */
#define UART_CMD_MAX_LEN   128   /**< Max incoming command length */
#define UART_RX_QUEUE_LEN  8

/* ------ Types ------ */
typedef struct {
    char raw[UART_CMD_MAX_LEN];
} UARTCommand_t;

/* ------ API ------ */
QueueHandle_t MCAL_UART_Init(void);
void MCAL_UART_Poll(void);
bool MCAL_UART_GetCommand(UARTCommand_t *cmd);
void MCAL_UART_Respond(const char *fmt, ...);
void MCAL_UART_PrintBanner(void);

#endif /* UART_MCAL_H */
