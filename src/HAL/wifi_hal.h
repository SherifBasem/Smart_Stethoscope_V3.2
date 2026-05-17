/**
 * @file    wifi_hal.h
 * @brief   HAL — WiFi peripheral abstraction for ESP32-S3
 * @layer   HAL
 *
 * Wraps the Arduino WiFi.h API behind typed primitives.
 * All blocking waits and retry logic live in the HAL layer above.
 */

#ifndef WIFI_HAL_H
#define WIFI_HAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

/* ------ Types ------ */
typedef enum {
    WIFI_STATUS_CONNECTED    = WL_CONNECTED,
    WIFI_STATUS_DISCONNECTED = WL_DISCONNECTED,
    WIFI_STATUS_FAILED       = WL_CONNECT_FAILED,
    WIFI_STATUS_IDLE         = WL_IDLE_STATUS,
    WIFI_STATUS_NO_SSID      = WL_NO_SSID_AVAIL,
} WiFi_Status_t;

/* ------ API ------ */
/**
 * @brief  Set the WiFi operating mode to Station (client).
 *         Must be called before any connect attempt.
 */
static inline void HAL_WiFiRadio_Init(void) {
    WiFi.mode(WIFI_STA);
    // Set max TX power to 40 (~20 dBm) to fix connectivity issues
    esp_wifi_set_max_tx_power(40);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    WiFi.disconnect(true);   /* flush any stale connection */
    delay(100);
}
/**
 * @brief  Force station-only mode before a client connection attempt.
 */
static inline void HAL_WiFiRadio_SetStationMode(void) {
    WiFi.mode(WIFI_STA);
    delay(100);
}
/**
 * @brief  Force AP+STA mode for setup portal.
 */
static inline void HAL_WiFiRadio_SetAPStationMode(void) {
    WiFi.mode(WIFI_AP_STA);
    delay(100);
}
/**
 * @brief  Begin association to an access point (non-blocking start).
 * @param  ssid      Network name
 * @param  password  Network password (NULL or "" for open networks)
 */
static inline void HAL_WiFiRadio_Begin(const char *ssid, const char *password) {
    WiFi.begin(ssid, password);
}
/**
 * @brief  Query the current association state.
 * @return One of the WiFi_Status_t enum values
 */
static inline WiFi_Status_t HAL_WiFiRadio_Status(void) {
    return (WiFi_Status_t)WiFi.status();
}
/**
 * @brief  Disconnect and power down the WiFi radio.
 */
static inline void HAL_WiFiRadio_Disconnect(void) {
    WiFi.disconnect(true);
}
/**
 * @brief  Return the assigned IP address as a string.
 * @param  buf     Output buffer
 * @param  bufLen  Buffer size (16 bytes is enough for IPv4)
 */
static inline void HAL_WiFiRadio_GetIP(char *buf, size_t bufLen) {
    strncpy(buf, WiFi.localIP().toString().c_str(), bufLen - 1);
    buf[bufLen - 1] = '\0';
}
/**
 * @brief  Return the RSSI (signal strength) in dBm.
 */
static inline int32_t HAL_WiFiRadio_GetRSSI(void) {
    return WiFi.RSSI();
}
/**
 * @brief  Scan for nearby networks (synchronous — can take ~2 s).
 * @return Number of networks found (negative on error)
 */
static inline int16_t HAL_WiFiRadio_Scan(void) {
    return (int16_t)WiFi.scanNetworks();
}
/**
 * @brief  Get the SSID of the Nth network from the last scan.
 * @param  index  Network index (0-based)
 * @param  buf    Output buffer
 * @param  bufLen Buffer size
 */
static inline void HAL_WiFiRadio_GetScannedSSID(uint8_t index, char *buf, size_t bufLen) {
    strncpy(buf, WiFi.SSID(index).c_str(), bufLen - 1);
    buf[bufLen - 1] = '\0';
}
/**
 * @brief Configure the SoftAP network address.
 */
static inline bool HAL_WiFiRadio_SoftAPConfig(IPAddress apIp,
                                           IPAddress gateway,
                                           IPAddress subnet) {
    return WiFi.softAPConfig(apIp, gateway, subnet);
}
/**
 * @brief Start the SoftAP radio with the provided credentials.
 */
static inline bool HAL_WiFiRadio_StartSoftAP(const char *ssid,
                                          const char *password,
                                          uint8_t channel,
                                          bool hidden,
                                          uint8_t maxConnection) {
    return WiFi.softAP(ssid, password, channel, hidden, maxConnection);
}
/**
 * @brief Stop the SoftAP radio and disconnect associated stations.
 */
static inline void HAL_WiFiRadio_StopSoftAP(void) {
    WiFi.softAPdisconnect(true);
}
/**
 * @brief Return the number of stations connected to the SoftAP.
 */
static inline uint8_t HAL_WiFiRadio_GetAPStationCount(void) {
    return (uint8_t)WiFi.softAPgetStationNum();
}

#endif
