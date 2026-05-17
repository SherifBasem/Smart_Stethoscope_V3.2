/**
 * @file    wifi_mcal.cpp
 * @brief   MCAL — WiFi connection manager implementation
 * @layer   MCAL
 */

#include "wifi_mcal.h"
#include "captive_portal_mcal.h"
#include "../HAL/nvs_hal.h"
#include "../HAL/uart_hal.h"   /* for debug logging */
#include <ESPmDNS.h>

/* ───────────────────────── Private State ───────────────────────── */
static QueueHandle_t  s_statusQueue  = NULL;
static WiFiHAL_State_t s_state       = WIFI_STATE_IDLE;
static char           s_ssid[33]     = {0};
static char           s_pass[64]     = {0};
static uint32_t       s_connectStart = 0;
static uint8_t        s_retries      = 0;

static void startStaMdns(void) {
    MDNS.end();
    if (MDNS.begin(PORTAL_MDNS_HOST)) {
        MDNS.addService("http", "tcp", 80);
        HAL_UART_SendLine("[WiFi] mDNS ready at http://myesp.local/");
    } else {
        HAL_UART_SendLine("[WiFi] mDNS start failed.");
    }
}

/* ── Push a status snapshot onto the queue ── */
static void pushStatus(void) {
    if (!s_statusQueue) return;
    WiFiHAL_Status_t status;
    status.state = s_state;
    status.rssi  = (s_state == WIFI_STATE_CONNECTED) ? HAL_WiFiRadio_GetRSSI() : 0;
    strncpy(status.ssid, s_ssid, sizeof(status.ssid) - 1);
    status.ssid[sizeof(status.ssid) - 1] = '\0';
    if (s_state == WIFI_STATE_CONNECTED) {
        HAL_WiFiRadio_GetIP(status.ip, sizeof(status.ip));
    } else {
        status.ip[0] = '\0';
    }
    /* Overwrite old value — only latest state matters */
    xQueueOverwrite(s_statusQueue, &status);
}

/* ───────────────────────── Init ─────────────────────────────────── */
void MCAL_WiFi_Init(QueueHandle_t statusQueue) {
    s_statusQueue = statusQueue;
    HAL_NVS_Init();
    HAL_WiFiRadio_Init();
    s_state = WIFI_STATE_IDLE;
    pushStatus();
}

/* ───────────────────────── Connect ─────────────────────────────── */
void MCAL_WiFi_Connect(const char *ssid, const char *password) {
    if (!ssid || ssid[0] == '\0') return;
    const char *pw = password ? password : "";
    MCAL_Portal_Stop();
    HAL_WiFiRadio_SetStationMode();
    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    strncpy(s_pass, pw,   sizeof(s_pass) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
    s_pass[sizeof(s_pass) - 1] = '\0';
    HAL_NVS_SaveWiFi(s_ssid, s_pass);
    s_retries     = 0;
    s_state       = WIFI_STATE_CONNECTING;
    s_connectStart = (uint32_t)millis();
    HAL_WiFiRadio_Begin(s_ssid, s_pass);
    HAL_UART_Printf("[WiFi] Connecting to \"%s\"...\r\n", s_ssid);
    pushStatus();
}

bool MCAL_WiFi_ConnectSaved(void) {
    char ssid[33];
    char pass[64];
    if (!HAL_NVS_LoadWiFi(ssid, sizeof(ssid), pass, sizeof(pass))) {
        return false;
    }
    HAL_UART_Printf("[WiFi] Loaded saved credentials for \"%s\".\r\n", ssid);
    MCAL_WiFi_Connect(ssid, pass);
    return true;
}

bool MCAL_WiFi_StartSetupPortal(void) {
    if (s_state == WIFI_STATE_SETUP_PORTAL &&
        MCAL_Portal_GetState() == PORTAL_STATE_RUNNING) {
        return true;
    }

    HAL_WiFiRadio_Disconnect();
    bool ok = MCAL_Portal_Start();
    if (ok) {
        s_state = WIFI_STATE_SETUP_PORTAL;
        strncpy(s_ssid, PORTAL_AP_SSID, sizeof(s_ssid) - 1);
        s_ssid[sizeof(s_ssid) - 1] = '\0';
        s_pass[0] = '\0';
        pushStatus();
    } else {
        s_state = WIFI_STATE_FAILED;
        pushStatus();
    }
    return ok;
}

/* ───────────────────────── Disconnect ───────────────────────────── */
void MCAL_WiFi_Disconnect(void) {
    MCAL_Portal_Stop();
    HAL_WiFiRadio_Disconnect();
    s_state = WIFI_STATE_DISCONNECTED;
    HAL_UART_SendLine("[WiFi] Disconnected.");
    pushStatus();
}

void MCAL_WiFi_ClearSavedCredentials(void) {
    HAL_NVS_ClearWiFi();
    MCAL_WiFi_Disconnect();
    s_ssid[0] = '\0';
    s_pass[0] = '\0';
    pushStatus();
    HAL_UART_SendLine("[WiFi] Saved credentials cleared.");
}

/* ───────────────────────── Tick ─────────────────────────────────── */
void MCAL_WiFi_Tick(void) {
    if (s_state == WIFI_STATE_SETUP_PORTAL) {
        MCAL_Portal_Tick();
        if (MCAL_Portal_GetState() == PORTAL_STATE_CREDENTIALS_SAVED) {
            PortalCredentials_t creds;
            if (MCAL_Portal_ConsumeCredentials(&creds)) {
                MCAL_Portal_Stop();
                HAL_UART_Printf("[WiFi] Portal submitted \"%s\". Connecting...\r\n",
                                 creds.ssid);
                MCAL_WiFi_Connect(creds.ssid, creds.pass);
            }
        }
        return;
    }

    if (s_state != WIFI_STATE_CONNECTING) return;

    WiFi_Status_t hw = HAL_WiFiRadio_Status();

    if (hw == WIFI_STATUS_CONNECTED) {
        s_state = WIFI_STATE_CONNECTED;
        char ip[16];
        HAL_WiFiRadio_GetIP(ip, sizeof(ip));
        HAL_UART_Printf("[WiFi] Connected! IP: %s  RSSI: %d dBm\r\n",
                         ip, HAL_WiFiRadio_GetRSSI());
        startStaMdns();
        pushStatus();
        return;
    }

    uint32_t elapsed = (uint32_t)millis() - s_connectStart;
    if (elapsed >= WIFI_CONNECT_TIMEOUT_MS) {
        if (s_retries < WIFI_MAX_RETRIES) {
            s_retries++;
            HAL_UART_Printf("[WiFi] Timeout — retry %d/%d\r\n",
                             s_retries, WIFI_MAX_RETRIES);
            s_connectStart = (uint32_t)millis();
            HAL_WiFiRadio_Begin(s_ssid, s_pass);
        } else {
            s_state = WIFI_STATE_FAILED;
            HAL_UART_SendLine("[WiFi] Failed after max retries.");
            pushStatus();
            MCAL_WiFi_StartSetupPortal();
        }
    }
}

/* ───────────────────────── Get State ───────────────────────────── */
WiFiHAL_State_t MCAL_WiFi_GetState(void) {
    return s_state;
}

/* ───────────────────────── Scan ─────────────────────────────────── */
uint8_t MCAL_WiFi_Scan(char ssidList[][33], uint8_t maxCount) {
    HAL_UART_SendLine("[WiFi] Scanning...");
    int16_t found = HAL_WiFiRadio_Scan();
    if (found <= 0) return 0;
    uint8_t count = (found < maxCount) ? (uint8_t)found : maxCount;
    for (uint8_t i = 0; i < count; i++) {
        HAL_WiFiRadio_GetScannedSSID(i, ssidList[i], 33);
    }
    HAL_UART_Printf("[WiFi] Found %d networks.\r\n", count);
    return count;
}
