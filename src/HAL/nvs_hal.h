/**
 * @file    nvs_hal.h
 * @brief   HAL - NVS credential storage abstraction
 * @layer   HAL
 */

#ifndef NVS_HAL_H
#define NVS_HAL_H

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

/* ------ Types ------ */
static Preferences s_prefs;

/* ------ API ------ */
static inline void HAL_NVS_Init(void) {
    /* Preferences opens lazily per namespace. */
}
static inline bool HAL_NVS_SaveWiFi(const char *ssid, const char *password) {
    if (!ssid || ssid[0] == '\0' || !password) return false;

    s_prefs.begin("wifi", false);
    size_t ssidWritten = s_prefs.putString("ssid", ssid);
    size_t passWritten = s_prefs.putString("pass", password);
    s_prefs.end();

    return ssidWritten > 0 && passWritten == strlen(password);
}
static inline bool HAL_NVS_LoadWiFi(char *ssidBuf, size_t ssidBufLen,
                       char *passBuf, size_t passBufLen) {
    if (!ssidBuf || !passBuf || ssidBufLen == 0 || passBufLen == 0) return false;

    s_prefs.begin("wifi", true);
    String ssid = s_prefs.getString("ssid", "");
    String pass = s_prefs.getString("pass", "");
    s_prefs.end();

    if (ssid.length() == 0) return false;

    strncpy(ssidBuf, ssid.c_str(), ssidBufLen - 1);
    ssidBuf[ssidBufLen - 1] = '\0';
    strncpy(passBuf, pass.c_str(), passBufLen - 1);
    passBuf[passBufLen - 1] = '\0';
    return true;
}
static inline bool HAL_NVS_ClearWiFi(void) {
    s_prefs.begin("wifi", false);
    s_prefs.remove("ssid");
    s_prefs.remove("pass");
    s_prefs.end();
    return true;
}
static inline bool HAL_NVS_HasWiFi(void) {
    s_prefs.begin("wifi", true);
    String ssid = s_prefs.getString("ssid", "");
    s_prefs.end();
    return ssid.length() > 0;
}

#endif /* NVS_HAL_H */
