/**
 * @file    captive_portal_mcal.cpp
 * @brief   MCAL - captive portal implementation
 * @layer   MCAL
 */

#include "captive_portal_mcal.h"
#include "../HAL/nvs_hal.h"
#include "../HAL/uart_hal.h"
#include "../HAL/wifi_hal.h"
#include <ESPmDNS.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <esp_system.h>
#include <string.h>

static DNSServer s_dns;
static WebServer s_server(80);
static PortalState_t s_state = PORTAL_STATE_IDLE;
static PortalCredentials_t s_credentials;
static bool s_serverConfigured = false;
static bool s_credentialsPending = false;
static char s_apPassword[16] = "";

static const char HTML_PAGE[] PROGMEM =
"<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Stetho WiFi Setup</title><style>"
"body{font-family:Arial,sans-serif;background:#f5f7fb;margin:0;padding:28px;color:#17202a}"
".box{max-width:420px;margin:auto;background:#fff;border:1px solid #d8dee9;border-radius:8px;padding:22px}"
"h1{font-size:22px;margin:0 0 12px}label{display:block;margin-top:14px;font-weight:700}"
"input{box-sizing:border-box;width:100%;font-size:17px;padding:11px;margin-top:6px;border:1px solid #aab4c0;border-radius:6px}"
"button{width:100%;margin-top:20px;font-size:18px;padding:12px;border:0;border-radius:6px;background:#1769e0;color:white}"
"p{line-height:1.45}</style></head><body><div class='box'>"
"<h1>Smart Stethoscope WiFi</h1><p>Enter the network this device should use.</p>"
"<p>Portal: myesp.local<br>Fallback: 192.168.4.1</p>"
"<form method='POST' action='/save'><label>SSID</label><input name='ssid' maxlength='32' required autofocus>"
"<label>Password</label><input name='pass' type='password' maxlength='63'>"
"<button type='submit'>Save and Connect</button></form></div></body></html>";

static void sendPortalPage(void) {
    s_server.send_P(200, "text/html", HTML_PAGE);
}

static void redirectToPortal(void) {
    s_server.sendHeader("Location", PORTAL_SETUP_URL, true);
    s_server.send(302, "text/plain", "");
}

static void generateApPassword(void) {
    uint32_t r = esp_random();
    snprintf(s_apPassword, sizeof(s_apPassword), "STH-%08lX", (unsigned long)r);
}

static void startMdns(void) {
    MDNS.end();
    if (MDNS.begin(PORTAL_MDNS_HOST)) {
        MDNS.addService("http", "tcp", 80);
        HAL_UART_SendLine("[Portal] mDNS ready at http://myesp.local/");
    } else {
        HAL_UART_SendLine("[Portal] mDNS start failed; use fallback IP.");
    }
}

static void handleSave(void) {
    String ssid = s_server.arg("ssid");
    String pass = s_server.arg("pass");
    ssid.trim();

    if (ssid.length() == 0 || ssid.length() > 32 || pass.length() > 63) {
        s_server.send(400, "text/plain", "Invalid SSID or password length.");
        return;
    }

    strncpy(s_credentials.ssid, ssid.c_str(), sizeof(s_credentials.ssid) - 1);
    s_credentials.ssid[sizeof(s_credentials.ssid) - 1] = '\0';
    strncpy(s_credentials.pass, pass.c_str(), sizeof(s_credentials.pass) - 1);
    s_credentials.pass[sizeof(s_credentials.pass) - 1] = '\0';

    HAL_NVS_SaveWiFi(s_credentials.ssid, s_credentials.pass);
    s_credentialsPending = true;
    s_state = PORTAL_STATE_CREDENTIALS_SAVED;

    s_server.send(200, "text/html",
                  "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>Saved</title></head><body><h2>Saved</h2>"
                  "<p>The stethoscope is connecting. You can close this page.</p></body></html>");
    HAL_UART_Printf("[Portal] Credentials saved for \"%s\".\r\n", s_credentials.ssid);
}

static void configureServer(void) {
    if (s_serverConfigured) return;

    s_server.on("/", HTTP_GET, sendPortalPage);
    s_server.on("/save", HTTP_POST, handleSave);
    s_server.on("/generate_204", HTTP_GET, redirectToPortal);
    s_server.on("/gen_204", HTTP_GET, redirectToPortal);
    s_server.on("/hotspot-detect.html", HTTP_GET, redirectToPortal);
    s_server.on("/connecttest.txt", HTTP_GET, redirectToPortal);
    s_server.on("/ncsi.txt", HTTP_GET, redirectToPortal);
    s_server.onNotFound(sendPortalPage);
    s_serverConfigured = true;
}

bool MCAL_Portal_Start(void) {
    if (s_state == PORTAL_STATE_RUNNING || s_state == PORTAL_STATE_CREDENTIALS_SAVED) {
        return true;
    }

    memset(&s_credentials, 0, sizeof(s_credentials));
    s_credentialsPending = false;
    generateApPassword();

    IPAddress apIp(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);

    HAL_WiFiRadio_SetAPStationMode();
    if (!HAL_WiFiRadio_SoftAPConfig(apIp, gateway, subnet)) {
        s_state = PORTAL_STATE_ERROR;
        HAL_UART_SendLine("[Portal] softAPConfig failed.");
        return false;
    }

    bool apOk = HAL_WiFiRadio_StartSoftAP(PORTAL_AP_SSID, s_apPassword,
                                      PORTAL_AP_CHANNEL, false, 4);
    if (!apOk) {
        s_state = PORTAL_STATE_ERROR;
        HAL_UART_SendLine("[Portal] softAP failed.");
        return false;
    }

    s_dns.start(53, "*", apIp);
    configureServer();
    s_server.begin();
    startMdns();
    s_state = PORTAL_STATE_RUNNING;

    HAL_UART_Printf("[Portal] AP \"%s\" started at %s. Password: %s\r\n",
                     PORTAL_AP_SSID, PORTAL_AP_IP, s_apPassword);
    return true;
}

void MCAL_Portal_Stop(void) {
    s_server.stop();
    s_dns.stop();
    MDNS.end();
    HAL_WiFiRadio_StopSoftAP();
    if (s_state != PORTAL_STATE_IDLE) s_state = PORTAL_STATE_STOPPED;
    HAL_UART_SendLine("[Portal] Stopped.");
}

PortalState_t MCAL_Portal_GetState(void) {
    return s_state;
}

bool MCAL_Portal_GetCredentials(PortalCredentials_t *out) {
    if (!out || s_credentials.ssid[0] == '\0') return false;
    *out = s_credentials;
    return true;
}

bool MCAL_Portal_ConsumeCredentials(PortalCredentials_t *out) {
    if (!out || !s_credentialsPending) return false;
    *out = s_credentials;
    s_credentialsPending = false;
    return true;
}

uint8_t MCAL_Portal_GetStationCount(void) {
    if (s_state != PORTAL_STATE_RUNNING && s_state != PORTAL_STATE_CREDENTIALS_SAVED) {
        return 0;
    }
    return HAL_WiFiRadio_GetAPStationCount();
}

const char *MCAL_Portal_GetApSSID(void) {
    return PORTAL_AP_SSID;
}

const char *MCAL_Portal_GetApPassword(void) {
    return s_apPassword;
}

const char *MCAL_Portal_GetSetupURL(void) {
    return PORTAL_SETUP_URL;
}

const char *MCAL_Portal_GetFallbackURL(void) {
    return PORTAL_FALLBACK_URL;
}

void MCAL_Portal_GetWiFiQrText(char *out, size_t outLen) {
    if (!out || outLen == 0) return;
    snprintf(out, outLen, "WIFI:T:WPA;S:%s;P:%s;;", PORTAL_AP_SSID, s_apPassword);
    out[outLen - 1] = '\0';
}

void MCAL_Portal_Tick(void) {
    if (s_state != PORTAL_STATE_RUNNING && s_state != PORTAL_STATE_CREDENTIALS_SAVED) return;
    s_dns.processNextRequest();
    s_server.handleClient();
}
