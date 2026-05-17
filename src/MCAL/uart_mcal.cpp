/**
 * @file    uart_mcal.cpp
 * @brief   MCAL — UART PC bridge implementation
 * @layer   MCAL
 */

#include "uart_mcal.h"
#include <stdarg.h>

/* ───────────────────────── Private State ───────────────────────── */
static QueueHandle_t s_cmdQueue = NULL;
static char          s_rxBuf[UART_CMD_MAX_LEN];
static uint8_t       s_rxIdx   = 0;

/* ───────────────────────── Init ─────────────────────────────────── */
QueueHandle_t MCAL_UART_Init(void) {
    HAL_UART_Init();
    s_cmdQueue = xQueueCreate(UART_RX_QUEUE_LEN, sizeof(UARTCommand_t));
    return s_cmdQueue;
}

/* ───────────────────────── Poll ─────────────────────────────────── */
void MCAL_UART_Poll(void) {
    while (HAL_UART_Available()) {
        int c = HAL_UART_ReadByte();
        if (c < 0) break;

        if (c == '\n' || c == '\r') {
            /* End of line — strip CR if present */
            if (s_rxIdx > 0) {
                s_rxBuf[s_rxIdx] = '\0';
                /* Strip trailing CR */
                if (s_rxIdx > 0 && s_rxBuf[s_rxIdx - 1] == '\r')
                    s_rxBuf[--s_rxIdx] = '\0';

                if (s_rxIdx > 0) {   /* Ignore blank lines */
                    UARTCommand_t cmd;
                    strncpy(cmd.raw, s_rxBuf, UART_CMD_MAX_LEN - 1);
                    cmd.raw[UART_CMD_MAX_LEN - 1] = '\0';
                    xQueueSend(s_cmdQueue, &cmd, 0);
                }
                s_rxIdx = 0;
            }
        } else {
            /* Accumulate characters */
            if (s_rxIdx < UART_CMD_MAX_LEN - 1) {
                s_rxBuf[s_rxIdx++] = (char)c;
            }
            /* Overflow protection — flush and reset */
            else {
                s_rxIdx = 0;
            }
        }
    }
}

/* ───────────────────────── GetCommand ──────────────────────────── */
bool MCAL_UART_GetCommand(UARTCommand_t *cmd) {
    if (!s_cmdQueue) return false;
    return (xQueueReceive(s_cmdQueue, cmd, 0) == pdTRUE);
}

/* ───────────────────────── Respond ─────────────────────────────── */
void MCAL_UART_Respond(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    HAL_UART_SendLine(buf);
}

/* ───────────────────────── Banner ───────────────────────────────── */
void MCAL_UART_PrintBanner(void) {
    HAL_UART_SendLine("========================================");
    HAL_UART_SendLine("  Smart Stethoscope — UART Bridge v0.1  ");
    HAL_UART_SendLine("  Commands: STATUS | WIFI <ssid> <pass>");
    HAL_UART_SendLine("           WIFI_DISCONNECT | WIFI_SCAN  ");
    HAL_UART_SendLine("           REBOOT | HELP                ");
    HAL_UART_SendLine("========================================");
}
