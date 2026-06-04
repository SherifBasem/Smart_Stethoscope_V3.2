/**
 * @file    captive_portal_mcal.h
 * @brief   MCAL - WiFi captive portal setup mode
 * @layer   MCAL
 */

#ifndef CAPTIVE_PORTAL_MCAL_H
#define CAPTIVE_PORTAL_MCAL_H

#include <Arduino.h>

/* ------ Config ------ */
#define PORTAL_AP_SSID      "Stetho-Setup"
#define PORTAL_AP_IP        "192.168.4.1"
#define PORTAL_AP_CHANNEL   6
#define PORTAL_MDNS_HOST    "myesp"
#define PORTAL_SETUP_URL    "http://myesp.local/"
#define PORTAL_FALLBACK_URL "http://192.168.4.1/"

/* ------ Types ------ */
typedef enum {
    PORTAL_STATE_IDLE = 0,
    PORTAL_STATE_RUNNING,
    PORTAL_STATE_CREDENTIALS_SAVED,
    PORTAL_STATE_STOPPED,
    PORTAL_STATE_ERROR
} PortalState_t;

typedef struct {
    char ssid[33];
    char pass[64];
} PortalCredentials_t;

/* ------ API ------ */
bool MCAL_Portal_Start(void);
void MCAL_Portal_Stop(void);
PortalState_t MCAL_Portal_GetState(void);
bool MCAL_Portal_GetCredentials(PortalCredentials_t *out);
bool MCAL_Portal_ConsumeCredentials(PortalCredentials_t *out);
uint8_t MCAL_Portal_GetStationCount(void);
const char *MCAL_Portal_GetApSSID(void);
const char *MCAL_Portal_GetApPassword(void);
const char *MCAL_Portal_GetSetupURL(void);
const char *MCAL_Portal_GetFallbackURL(void);
void MCAL_Portal_GetWiFiQrText(char *out, size_t outLen);
void MCAL_Portal_Tick(void);

#endif /* CAPTIVE_PORTAL_MCAL_H */
