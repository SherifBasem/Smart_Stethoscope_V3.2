/**
 * @file    uart_hal.h
 * @brief   HAL — UART abstraction for ESP32-S3 (USB Serial = Serial0)
 * @layer   HAL
 *
 * Exposes a thin typed API over HardwareSerial so the HAL layer
 * never depends on Arduino's Serial object directly.
 */

#ifndef UART_HAL_H
#define UART_HAL_H

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

extern SemaphoreHandle_t g_uartMutex;

/* ------ Config ------ */
#define UART_BAUD_RATE   115200
#define UART_PORT        Serial   /**< ESP32-S3 USB-UART bridge */

/* ------ API ------ */
/**
 * @brief  Initialise the UART peripheral at the configured baud rate.
 *         Call once from setup().
 */
static inline void HAL_UART_Init(void) {
    UART_PORT.begin(UART_BAUD_RATE);
}
/**
 * @brief  Check whether there are bytes waiting in the RX buffer.
 * @return Number of bytes available (0 = nothing)
 */
static inline int HAL_UART_Available(void) {
    return UART_PORT.available();
}
/**
 * @brief  Read one byte from the RX buffer (blocking if needed — prefer
 *         calling only after HAL_UART_Available() > 0).
 * @return Received byte as int (-1 on timeout)
 */
static inline int HAL_UART_ReadByte(void) {
    return UART_PORT.read();
}
/**
 * @brief  Read a full line (until '\\n') into a caller-supplied buffer.
 * @param  buf     Destination buffer
 * @param  maxLen  Maximum number of characters (including null terminator)
 * @return Number of characters read (0 = nothing available yet)
 */
static inline size_t HAL_UART_ReadLine(char *buf, size_t maxLen) {
    if (!UART_PORT.available()) return 0;
    size_t n = UART_PORT.readBytesUntil('\n', buf, maxLen - 1);
    buf[n] = '\0';
    /* strip trailing CR if present */
    if (n > 0 && buf[n - 1] == '\r') buf[--n] = '\0';
    return n;
}
/**
 * @brief  Transmit a null-terminated string followed by CRLF.
 * @param  str  String to send
 */
static inline void HAL_UART_SendLine(const char *str) {
    if (g_uartMutex) xSemaphoreTake(g_uartMutex, portMAX_DELAY);
    UART_PORT.println(str);
    if (g_uartMutex) xSemaphoreGive(g_uartMutex);
}
/**
 * @brief  Transmit a formatted string (printf-style).
 *         Uses Serial.printf() available on ESP32 Arduino core.
 */
static inline void HAL_UART_Printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char tmp[256];
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (g_uartMutex) xSemaphoreTake(g_uartMutex, portMAX_DELAY);
    UART_PORT.print(tmp);
    if (g_uartMutex) xSemaphoreGive(g_uartMutex);
}

#endif /* UART_HAL_H */
