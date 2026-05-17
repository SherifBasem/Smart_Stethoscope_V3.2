/**
 * @file    wifi_mcal.h
 * @brief   MCAL — WiFi connection manager
 * @layer   MCAL
 *
 * Provides a non-blocking state machine for connecting to WiFi.
 * The UI Task triggers connection; this HAL manages the retry loop and
 * posts its state to a shared queue for any task to read.
 */

#ifndef WIFI_MCAL_H
#define WIFI_MCAL_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "../HAL/wifi_hal.h"

/* ------ Config ------ */
#define WIFI_CONNECT_TIMEOUT_MS  15000
#define WIFI_MAX_RETRIES         3

/* ------ Types ------ */
typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED,
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_SETUP_PORTAL
} WiFiHAL_State_t;

typedef struct {
    WiFiHAL_State_t state;
    char            ip[16];
    int32_t         rssi;
    char            ssid[33];
} WiFiHAL_Status_t;

/* ------ API ------ */
/**
 * @brief  Get the current WiFi state (snapshot, no blocking).
 */
WiFiHAL_State_t MCAL_WiFi_GetState(void);
/**
 * @brief  Initialise WiFi radio (Station mode, radio off).
 *         Call once from setup().
 * @param  statusQueue  Queue to push WiFiHAL_Status_t updates onto
 */
void MCAL_WiFi_Init(QueueHandle_t statusQueue);
/**
 * @brief  Trigger a connection attempt to the given SSID.
 *         Non-blocking — the WiFi Task drives progress via MCAL_WiFi_Tick().
 */
void MCAL_WiFi_Connect(const char *ssid, const char *password);
/**
 * @brief  Load saved credentials from NVS and connect if present.
 * @return true if saved credentials existed and a connection was started.
 */
bool MCAL_WiFi_ConnectSaved(void);
/**
 * @brief  Start AP captive portal setup mode.
 */
bool MCAL_WiFi_StartSetupPortal(void);
/**
 * @brief  Disconnect from the current network.
 */
void MCAL_WiFi_Disconnect(void);
/**
 * @brief  Clear saved credentials and disconnect.
 */
void MCAL_WiFi_ClearSavedCredentials(void);
/**
 * @brief  State-machine tick — call from the WiFi Task loop (every 500 ms).
 *         Internally updates state and pushes to the status queue.
 */
void MCAL_WiFi_Tick(void);
/**
 * @brief  Scan for networks and fill an array of SSID strings.
 * @param  ssidList   2-D array: ssidList[i] is char[33]
 * @param  maxCount   Size of the array
 * @return Number of networks actually found
 */
uint8_t MCAL_WiFi_Scan(char ssidList[][33], uint8_t maxCount);

#endif /* WIFI_MCAL_H */
