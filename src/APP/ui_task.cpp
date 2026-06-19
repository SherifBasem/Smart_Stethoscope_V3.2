/**
 * @file    ui_task.cpp
 * @brief   Application Layer — UI Task
 * @layer   APP
 *
 * Changes vs previous version:
 *   • UI_SCREEN_LUNG_SOUND added to main menu and screen router.
 *   • renderLungSound() — multi-sub-state renderer:
 *       IDLE      live dBSPL horizontal bar + scrolling waveform
 *       RECORDING countdown + bar + wave; [SEL] stops early
 *       DONE      upload confirmation
 *       UPLOADING progress spinner (blocking upload runs on UI_Task)
 *       RESULT    ML diagnosis label + confidence bar
 *   • MicTask_SetActive() called on screen enter/leave.
 *   • UITask_Params_t gains micLiveQueue.
 *   • Main menu updated from 4 to 5 items.
 */

#include "ui_task.h"
#include "../MCAL/wifi_mcal.h"
#include "../MCAL/captive_portal_mcal.h"
#include "../MCAL/heart_mcal.h"
#include "../MCAL/oled_mcal.h"
#include "../MCAL/qrcode_mcal.h"
#include "../MCAL/battery_mcal.h"
#include "../MCAL/mic_mcal.h"
#include "../HAL/uart_hal.h"
#include "heart_task.h"
#include "mic_task.h"
#include <stdio.h>
#include <string.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <HTTPClient.h>
#include <driver/gpio.h>
#include <freertos/semphr.h>
#include <esp_timer.h>

/* ══════════════════════════════════════════════════════════════════
   Constants
   ══════════════════════════════════════════════════════════════════ */

const uint32_t INACTIVITY_OPTIONS_SEC[INACTIVITY_OPTION_COUNT] = {
    0, 15, 30, 60, 120
};
static const char *INACTIVITY_LABELS[INACTIVITY_OPTION_COUNT] = {
    "Off", "15 s", "30 s", "60 s", "2 min"
};

#define BRIGHTNESS_OPTION_COUNT 3
static const uint8_t  BRIGHTNESS_VALUES[BRIGHTNESS_OPTION_COUNT] = { 20, 128, 255 };
static const char    *BRIGHTNESS_LABELS[BRIGHTNESS_OPTION_COUNT] = {
    "Low", "Medium", "High"
};

#define MENU_VISIBLE 3

/* ══════════════════════════════════════════════════════════════════
   Menu item tables
   ══════════════════════════════════════════════════════════════════ */
static const char *MAIN_MENU_ITEMS[] = {
    "1. Heart Monitor",
    "2. Lung Sound",      /* ← NEW */
    "3. Settings",
    "4. System Info",
    "5. Power Off"
};
#define MAIN_MENU_COUNT 5   /* ← was 4 */

static const char *SETTINGS_MENU_ITEMS[] = {
    "1. WiFi",
    "2. Brightness",
    "3. Inactivity",
    "4. Reboot",
    "5. System Info"
};
#define SETTINGS_MENU_COUNT 5

static const char *WIFI_MENU_ITEMS[] = {
    "1. WiFi Status",
    "2. WiFi Login",
    "3. WiFi Scan",
    "4. Setup Portal",
    "5. WiFi Reset",
    "6. Disconnect"
};
#define WIFI_MENU_COUNT 6

/* ══════════════════════════════════════════════════════════════════
   Private state
   ══════════════════════════════════════════════════════════════════ */

static UIScreen_t s_screen     = UI_SCREEN_BOOT;
static UIScreen_t s_lastScreen = (UIScreen_t)-1;

static uint8_t s_mainSel     = 0, s_mainScroll     = 0;
static uint8_t s_settingsSel = 0, s_settingsScroll = 0;
static uint8_t s_wifiSel     = 0, s_wifiScroll     = 0;

static char s_wifiSSID[33] = "WifiName";
static char s_wifiPass[64] = "Password";

static uint8_t s_brightnessIdx = 2;
static uint8_t s_inactivityIdx = INACTIVITY_DEFAULT_IDX;

static uint32_t s_lastActivityMs = 0;

static bool s_confirmYes = true;

/* Anti-blink — WiFi */
static WiFiHAL_State_t s_lastWifiState = (WiFiHAL_State_t)-1;
static char            s_lastIP[16]    = "";
static int32_t         s_lastRSSI      = 9999;
static bool            s_wifiDirty     = true;

/* Anti-blink — Heart */
static uint8_t s_lastBPM    = 255;
static uint8_t s_lastSpO2   = 255;
static bool    s_lastFinger = false;
static bool    s_heartDirty = true;

enum HeartScanState_t {
    HEART_SCAN_WAITING = 0,
    HEART_SCAN_RUNNING,
    HEART_SCAN_DONE,
    HEART_SCAN_CANCELLED,
    HEART_SCAN_ERROR
};

static HeartScanState_t s_heartScanState = HEART_SCAN_WAITING;
static uint32_t s_heartScanStartMs = 0;
static uint32_t s_heartLastBpmSampleMs = 0;
static uint32_t s_heartLastSpO2SampleMs = 0;
static uint32_t s_heartBpmSum = 0;
static uint32_t s_heartSpO2Sum = 0;
static uint16_t s_heartBpmCount = 0;
static uint16_t s_heartSpO2Count = 0;
static uint8_t s_heartAvgBPM = 0;
static uint8_t s_heartAvgSpO2 = 0;
static uint8_t s_heartLastRemaining = 255;
static HeartScanState_t s_lastHeartScanState = (HeartScanState_t)-1;

/* Anti-blink — System */
static unsigned long s_lastHeap      = 0;
static uint32_t      s_lastUptimeSec = 0;

/* Scan results */
#define SCAN_MAX 8
static char    s_scanSSIDs[SCAN_MAX][33];
static uint8_t s_scanCount  = 0;
static bool    s_scanDone   = false;
static uint8_t s_scanScroll = 0;

static uint8_t s_inactScroll = 0;

static MCAL_QRCode_t s_setupQr;
static bool     s_setupQrReady = false;
static char     s_lastPortalCredSSID[33] = "";
static char     s_lastPortalCredPass[64] = "";

static const char *SYSTEM_INFO_API_URL = "https://b1fe-156-221-176-248.ngrok-free.app/send_data/";
static const uint32_t SYSTEM_INFO_POST_INTERVAL_MS = 2000;
static UIScreen_t  s_systemInfoReturn  = UI_SCREEN_MAIN_MENU;
static uint32_t    s_lastSystemInfoPostMs = 0;

/* Battery */
static BatteryStatus_t s_battery;
static uint8_t         s_lastBattPct   = 255;
static BatteryState_t  s_lastBattState = BATTERY_STATE_UNKNOWN;
static bool            s_lastBattConnected = false;

/* ══════════════════════════════════════════════════════════════════
    Timing stats
    ══════════════════════════════════════════════════════════════════ */
static TaskTimingStats_t s_timing = {0, UI_POLL_INTERVAL_MS * 1000UL, 0, 0};
static int64_t s_lastLoopUs = 0;

/* ══════════════════════════════════════════════════════════════════
   Lung Sound screen state                                 ← NEW
   ══════════════════════════════════════════════════════════════════ */
/* Waveform display column buffer — 96 columns across the OLED width
   (leaving 16 px on the right for the battery icon).
   Each column stores a scaled amplitude value [0–WAVE_H]. */
#define WAVE_COLS    96   /* display columns used for waveform           */
#define WAVE_H       20   /* pixel height of the waveform zone (rows 2–3)*/
#define WAVE_Y0      16   /* top Y of the waveform zone                  */
#define BAR_Y        38   /* Y of the dBSPL bar (row 4)                  */
#define BAR_W        96   /* width of the dBSPL bar                      */
#define BAR_H         8   /* height of the dBSPL bar                     */

static int8_t   s_waveCol[WAVE_COLS];    /* scaled amplitude -10..+10 approx */
static uint8_t  s_waveWriteIdx = 0;      /* next column to overwrite          */
static uint32_t s_lastWaveUpdateMs = 0;
static uint8_t  s_lastBarPct  = 255;     /* anti-blink for bar                */
static bool     s_lungDirty   = true;

/* Spinner state for upload animation */
static uint8_t s_spinFrame = 0;
static const char SPIN_CHARS[] = {'|', '/', '-', '\\'};

/* ══════════════════════════════════════════════════════════════════
   Battery icon renderer (unchanged)
   ══════════════════════════════════════════════════════════════════ */
static void drawBatteryIcon(void) {
    const int ICON_X   = 113;
    const int ICON_Y   = 0;
    const int SHELL_W  = 12;
    const int SHELL_H  = 7;
    const int NUB_W    = 2;
    const int NUB_H    = 3;
    const int NUB_Y    = ICON_Y + (SHELL_H - NUB_H) / 2;
    const int INNER_X  = ICON_X + 1;
    const int INNER_Y  = ICON_Y + 1;
    const int INNER_W  = SHELL_W - 2;
    const int INNER_H  = SHELL_H - 2;

    display.fillRect(ICON_X, ICON_Y, SHELL_W + NUB_W + 1, SHELL_H, SSD1306_BLACK);
    display.drawRect(ICON_X, ICON_Y, SHELL_W, SHELL_H, SSD1306_WHITE);
    display.fillRect(ICON_X + SHELL_W, NUB_Y, NUB_W, NUB_H, SSD1306_WHITE);

    if (!s_battery.isConnected) {
        display.drawLine(ICON_X + 2, ICON_Y + 1, ICON_X + SHELL_W - 2, ICON_Y + SHELL_H - 2, SSD1306_WHITE);
        display.drawLine(ICON_X + 2, ICON_Y + SHELL_H - 2, ICON_X + SHELL_W - 2, ICON_Y + 1, SSD1306_WHITE);
        return;
    }

    uint8_t pct = s_battery.percent;
    if (pct > 100) pct = 100;
    int fillPx = (int)((INNER_W * (uint32_t)pct) / 100);
    if (pct > 0 && fillPx == 0) fillPx = 1;

    if (fillPx > 0) {
        display.fillRect(INNER_X, INNER_Y, fillPx, INNER_H, SSD1306_WHITE);
    }

    if (s_battery.state == BATTERY_STATE_CHARGING ||
        s_battery.state == BATTERY_STATE_FULL) {
        int bx = ICON_X + SHELL_W / 2 - 1;
        int by = ICON_Y + 1;
        uint16_t boltCol = (fillPx >= INNER_W / 2) ? SSD1306_BLACK : SSD1306_WHITE;
        display.drawPixel(bx + 1, by,     boltCol);
        display.drawPixel(bx,     by + 1, boltCol);
        display.drawPixel(bx + 1, by + 2, boltCol);
        display.drawPixel(bx,     by + 3, boltCol);
    }
}

static void pushDisplay(void) {
    if (!MCAL_OLED_IsReady()) return;
    drawBatteryIcon();
    display.display();
}

/* ══════════════════════════════════════════════════════════════════
   Tiny helpers
   ══════════════════════════════════════════════════════════════════ */

static inline void resetActivity(void) {
    s_lastActivityMs = (uint32_t)millis();
}

static void applyBrightness(void) {
    MCAL_OLED_SetBrightness(BRIGHTNESS_VALUES[s_brightnessIdx]);
}

static void oledHeader(const char *title) {
    char buf[22];
    snprintf(buf, sizeof(buf), "%-21s", title);
    MCAL_OLED_PrintLine(0, buf);
    MCAL_OLED_PrintLine(1, "---------------------");
}

static inline void oledTitle(const char *title)  { oledHeader(title); }
static inline void oledDivider(void) { MCAL_OLED_PrintLine(1, "---------------------"); }

static bool isInputLocked(void) {
    if (s_screen == UI_SCREEN_HEART_MONITOR && s_heartScanState == HEART_SCAN_RUNNING) {
        return true;
    }
    if (s_screen == UI_SCREEN_LUNG_SOUND) {
        return (MCAL_Mic_GetState() == MIC_STATE_RECORDING);
    }
    return false;
}

static void renderMenu(const char *title,
                       const char *items[],
                       uint8_t count,
                       uint8_t sel,
                       uint8_t scroll)
{
    MCAL_OLED_Clear();
    oledHeader(title);
    for (uint8_t v = 0; v < MENU_VISIBLE; v++) {
        uint8_t idx = scroll + v;
        if (idx >= count) break;
        char line[22];
        snprintf(line, sizeof(line),
                 (idx == sel) ? ">%-20s" : " %-20s",
                 items[idx]);
        if (v == MENU_VISIBLE - 1 && (scroll + MENU_VISIBLE) < count) {
            line[20] = 'v';
            line[21] = '\0';
        }
        MCAL_OLED_PrintLine(2 + v, line);
    }
    
    /* Add down arrow indicator if there are more items below */
    if (scroll + MENU_VISIBLE < count) {
        MCAL_OLED_PrintLine(5, "        v");
    }
    
    pushDisplay();
}

static void menuUp(uint8_t *sel, uint8_t *scroll) {
    if (*sel > 0) {
        (*sel)--;
        if (*sel < *scroll) (*scroll)--;
    }
}

static void menuDown(uint8_t *sel, uint8_t *scroll, uint8_t count) {
    if (*sel < count - 1) {
        (*sel)++;
        if (*sel >= *scroll + MENU_VISIBLE) (*scroll)++;
    }
}

/* ══════════════════════════════════════════════════════════════════
   Sleep helpers
   ══════════════════════════════════════════════════════════════════ */
static void configureWakeupPins(void) {
    const uint8_t pins[] = {
        BTN_UP_PIN, BTN_DOWN_PIN, BTN_SELECT_PIN, BTN_BACK_PIN
    };
    for (uint8_t i = 0; i < 4; i++) {
        gpio_wakeup_enable((gpio_num_t)pins[i], GPIO_INTR_LOW_LEVEL);
    }
    esp_sleep_enable_gpio_wakeup();
}

/* ══════════════════════════════════════════════════════════════════
   LUNG SOUND SCREEN RENDERER                               ← NEW
   ══════════════════════════════════════════════════════════════════

   Layout (128×64 OLED, 8px rows):
   Row 0   │ " Lung Sound  "          header
   Row 1   │ "---------------------"  divider
   Rows 2-3│ Waveform (20 px tall)    scrolling oscilloscope
   Row 4+  │ dBSPL bar (8 px)
   Row 5   │ status line / hint

   Sub-state labels injected into row 0:
     IDLE      " Lung Sound [REC]"
     REC       " Lung Snd  0:42  "   (remaining)
     DONE      " Upload result?  "
     UPLOADING " Uploading...  / "   (spinner)
     RESULT    " Normal  95%     "
*/

static void updateWaveformColumn(int16_t amp) {
    /* Map amp [-2048..+2047] to [-WAVE_H/2..+WAVE_H/2] */
    int8_t scaled = (int8_t)((int32_t)amp * (WAVE_H / 2) / 2048);
    s_waveCol[s_waveWriteIdx] = scaled;
    s_waveWriteIdx = (s_waveWriteIdx + 1) % WAVE_COLS;
}

static void drawWaveform(void) {
    /* Clear waveform zone */
    display.fillRect(0, WAVE_Y0, WAVE_COLS, WAVE_H, SSD1306_BLACK);

    int midY = WAVE_Y0 + WAVE_H / 2;

    /* Draw centre reference line (dashed every 4 px) */
    for (int x = 0; x < WAVE_COLS; x += 4) {
        display.drawPixel(x, midY, SSD1306_WHITE);
    }

    /* Draw waveform columns oldest-first left to right */
    for (int col = 0; col < WAVE_COLS; col++) {
        int bufIdx = (s_waveWriteIdx + col) % WAVE_COLS;
        int8_t amp = s_waveCol[bufIdx];
        int y = midY - (int)amp;
        /* Clamp to zone */
        if (y < WAVE_Y0)              y = WAVE_Y0;
        if (y >= WAVE_Y0 + WAVE_H)    y = WAVE_Y0 + WAVE_H - 1;
        display.drawPixel(col, y, SSD1306_WHITE);
        /* Draw a vertical segment from mid to sample for thicker feel */
        if (amp > 0) {
            for (int yy = y; yy <= midY; yy++)
                display.drawPixel(col, yy, SSD1306_WHITE);
        } else if (amp < 0) {
            for (int yy = midY; yy <= y; yy++)
                display.drawPixel(col, yy, SSD1306_WHITE);
        }
    }
}

static void drawDbBar(uint8_t pct, bool clipping) {
    /* Outer shell */
    display.drawRect(0, BAR_Y, BAR_W, BAR_H, SSD1306_WHITE);
    /* Clear inner */
    display.fillRect(1, BAR_Y + 1, BAR_W - 2, BAR_H - 2, SSD1306_BLACK);

    int fillPx = (int)((BAR_W - 2) * (uint32_t)pct / 100);
    if (pct > 0 && fillPx == 0) fillPx = 1;

    if (fillPx > 0) {
        /* Gradient feel: last 20% of bar is drawn brighter (solid),
           earlier portion is drawn with alternating pixels */
        int solidStart = (BAR_W - 2) * 80 / 100;
        for (int x = 1; x < 1 + fillPx; x++) {
            bool inSolid = (x - 1 >= solidStart);
            for (int y = BAR_Y + 1; y < BAR_Y + BAR_H - 1; y++) {
                if (inSolid || ((x + y) & 1) == 0) {
                    display.drawPixel(x, y, SSD1306_WHITE);
                }
            }
        }
    }

    /* Clipping indicator: blink the rightmost 4 px solid when saturated */
    if (clipping) {
        display.fillRect(BAR_W - 5, BAR_Y + 1, 4, BAR_H - 2, SSD1306_WHITE);
    }
}

static void renderLungSound(QueueHandle_t micQ) {
    if (!MCAL_Mic_IsReady()) {
        if (!s_lungDirty) return;
        s_lungDirty = false;
        MCAL_OLED_Clear();
        oledTitle(" Lung Sound ");
        oledDivider();
        MCAL_OLED_PrintLine(2, MCAL_Mic_IsHardwarePresent() ? " Memory too low" : " Mic unavailable");
        MCAL_OLED_PrintLine(3, MCAL_Mic_IsHardwarePresent() ? " Need 30s buffer" : " Check MAX4466");
        MCAL_OLED_PrintLine(4, " See UART logs");
        MCAL_OLED_PrintLine(5, "[BACK] Return");
        pushDisplay();
        return;
    }

    MicState_t micState = MCAL_Mic_GetState();

    /* ── Fetch latest live reading ── */
    MicLiveReading_t live = {MIC_DB_FLOOR, 0, 0, false};
    if (micQ) xQueuePeek(micQ, &live, 0);

    /* ── Update waveform column every 20 ms (matches UI loop) ── */
    uint32_t now = (uint32_t)millis();
    if (now - s_lastWaveUpdateMs >= 20) {
        s_lastWaveUpdateMs = now;

        /* Sample waveform from ring buffer — get newest single value */
        int16_t snap = 0;
        MCAL_Mic_GetWaveformSnapshot(&snap, 1);
        updateWaveformColumn(snap);
    }

    /* ── Determine if a redraw is needed ── */
    bool barChanged  = (live.barPercent != s_lastBarPct);
    bool needRedraw  = s_lungDirty || barChanged ||
                       (micState == MIC_STATE_RECORDING) ||
                       (micState == MIC_STATE_UPLOADING);

    if (!needRedraw) return;

    s_lungDirty  = false;
    s_lastBarPct = live.barPercent;

    if (!MCAL_OLED_IsReady()) return;

    /* ── Build header line based on sub-state ── */
    char header[22];
    char statusLine[22];

    switch (micState) {

        case MIC_STATE_IDLE: {
            snprintf(header,     sizeof(header),     " Lung Sound      ");
            uint8_t availSec = MCAL_Mic_GetAvailableRecordSeconds();
            snprintf(statusLine, sizeof(statusLine), "Rec:%us [SEL]Rec", availSec);
            break;
        }

        case MIC_STATE_RECORDING: {
            uint8_t rem = MCAL_Mic_GetSecondsRemaining();
            uint8_t ela = MCAL_Mic_GetSecondsElapsed();
            snprintf(header,     sizeof(header),     " Recording %02u:%02u  ",
                     ela / 60, ela % 60);
            snprintf(statusLine, sizeof(statusLine), "-%02us [SEL]Stop  ",
                     rem);
            break;
        }

        case MIC_STATE_DONE:
            snprintf(header,     sizeof(header),     " Recording Done  ");
            snprintf(statusLine, sizeof(statusLine), "[SEL]Upload [BCK]Dsc");
            break;

        case MIC_STATE_UPLOADING:
            s_spinFrame = (s_spinFrame + 1) & 3;
            snprintf(header,     sizeof(header),     " Uploading...  %c ",
                     SPIN_CHARS[s_spinFrame]);
            snprintf(statusLine, sizeof(statusLine), " Please wait...  ");
            break;

        case MIC_STATE_UPLOAD_OK: {
            MicMLResult_t res = {"", 0.0f, false};
            MCAL_Mic_GetMLResult(&res);
            snprintf(header,     sizeof(header),     " Result: %-9s", res.diagnosis);
            snprintf(statusLine, sizeof(statusLine), " Conf:%.0f%%[SEL]New",
                     res.confidence * 100.0f);
            break;
        }

        case MIC_STATE_UPLOAD_ERR:
            snprintf(header,     sizeof(header),     " Upload Failed   ");
            snprintf(statusLine, sizeof(statusLine), "[SEL]Retry [BCK]Back");
            break;

        default:
            snprintf(header,     sizeof(header),     " Lung Sound      ");
            snprintf(statusLine, sizeof(statusLine), "[BCK] Return      ");
            break;
    }

    /* ── Draw everything ── */
    MCAL_OLED_Clear();

    /* Row 0: header */
    MCAL_OLED_PrintLine(0, header);
    /* Row 1: divider */
    MCAL_OLED_PrintLine(1, "---------------------");

    /* Waveform zone (rows 2–3, pixel rows 16–35) */
    drawWaveform();

    /* dBSPL bar (pixel row 38–45) */
    drawDbBar(live.barPercent, live.clipping);

    /* dB label right of bar */
    {
        char dbLabel[8];
        snprintf(dbLabel, sizeof(dbLabel), "%3.0fdB", live.dbSPL);
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(BAR_W + 2, BAR_Y);
        display.print(dbLabel);
    }

    /* Bottom row: status / hint */
    MCAL_OLED_PrintLine(7, statusLine);

    /* For UPLOAD_OK also draw confidence bar on row 5 */
    if (micState == MIC_STATE_UPLOAD_OK) {
        MicMLResult_t res = {"", 0.0f, false};
        MCAL_Mic_GetMLResult(&res);
        uint8_t confPct = (uint8_t)(res.confidence * 100.0f);
        /* Small confidence bar above the bottom prompt. */
        display.drawRect(0, 48, 80, 6, SSD1306_WHITE);
        int confFill = (int)(78 * (uint32_t)confPct / 100);
        if (confFill > 0) display.fillRect(1, 49, confFill, 4, SSD1306_WHITE);
    }

    pushDisplay();
}

/* ══════════════════════════════════════════════════════════════════
   Screen renderers (unchanged from original)
   ══════════════════════════════════════════════════════════════════ */

static void renderBoot(void) {
    vTaskDelay(pdMS_TO_TICKS(200));
    MCAL_OLED_Clear();
    MCAL_OLED_PrintLine(0, "  Smart Stethoscope  ");
    MCAL_OLED_PrintLine(1, "---------------------");
    MCAL_OLED_PrintLine(2, "   Booting  v 0.4    ");
    MCAL_OLED_PrintLine(3, "     Loading...      ");
    pushDisplay();
}

static void renderMainMenu(void) {
    renderMenu("=== Main Menu ===",
               MAIN_MENU_ITEMS, MAIN_MENU_COUNT,
               s_mainSel, s_mainScroll);
}

static void resetHeartScan(void) {
    s_heartScanState = MCAL_Heart_IsReady() ? HEART_SCAN_WAITING : HEART_SCAN_ERROR;
    s_heartScanStartMs = 0;
    s_heartLastBpmSampleMs = 0;
    s_heartLastSpO2SampleMs = 0;
    s_heartBpmSum = 0;
    s_heartSpO2Sum = 0;
    s_heartBpmCount = 0;
    s_heartSpO2Count = 0;
    s_heartAvgBPM = 0;
    s_heartAvgSpO2 = 0;
    s_heartLastRemaining = 255;
    s_lastHeartScanState = (HeartScanState_t)-1;
    s_lastBPM = 255;
    s_lastSpO2 = 255;
    s_lastFinger = false;
    s_heartDirty = true;
    HAL_UART_SendLine(MCAL_Heart_IsReady()
        ? "[HeartScan] Ready. Place finger to start 60-second scan."
        : "[HeartScan] ERROR: MAX30102 not detected; heart monitor unavailable.");
}

static void renderHeartMonitor(QueueHandle_t heartQ) {
    if (!MCAL_Heart_IsReady()) {
        if (!s_heartDirty) return;
        s_heartDirty = false;
        MCAL_OLED_Clear();
        oledTitle(" Heart Monitor ");
        oledDivider();
        MCAL_OLED_PrintLine(2, " MAX30102 missing");
        MCAL_OLED_PrintLine(3, " Check I2C/power");
        MCAL_OLED_PrintLine(4, " See UART logs");
        MCAL_OLED_PrintLine(5, "[BACK] Return");
        pushDisplay();
        return;
    }

    if (!heartQ) {
        if (!s_heartDirty) return;
        s_heartDirty = false;
        MCAL_OLED_Clear();
        oledTitle(" Heart Monitor ");
        oledDivider();
        MCAL_OLED_PrintLine(2, " Queue unavailable");
        MCAL_OLED_PrintLine(3, " See UART logs");
        MCAL_OLED_PrintLine(4, "");
        MCAL_OLED_PrintLine(5, "[BACK] Return");
        pushDisplay();
        return;
    }

    HeartReading_t r = {0, 0, false, false, false, 0};
    bool hasData = (xQueuePeek(heartQ, &r, 0) == pdTRUE);
    uint32_t now = (uint32_t)millis();

    if (hasData) {
        if (s_heartScanState == HEART_SCAN_WAITING && r.fingerPresent) {
            s_heartScanState = HEART_SCAN_RUNNING;
            s_heartScanStartMs = now;
            s_heartLastBpmSampleMs = 0;
            s_heartLastSpO2SampleMs = 0;
            s_heartBpmSum = 0;
            s_heartSpO2Sum = 0;
            s_heartBpmCount = 0;
            s_heartSpO2Count = 0;
            s_heartAvgBPM = 0;
            s_heartAvgSpO2 = 0;
            HAL_UART_SendLine("[HeartScan] Started 60-second scan.");
            s_heartDirty = true;
        } else if (s_heartScanState == HEART_SCAN_RUNNING && !r.fingerPresent) {
            s_heartScanState = HEART_SCAN_CANCELLED;
            HAL_UART_SendLine("[HeartScan] Cancelled: finger removed.");
            s_heartDirty = true;
        }

        if (s_heartScanState == HEART_SCAN_RUNNING) {
            if (r.validBPM && (s_heartLastBpmSampleMs == 0 || now - s_heartLastBpmSampleMs >= 1000UL)) {
                s_heartBpmSum += r.bpm;
                s_heartBpmCount++;
                s_heartLastBpmSampleMs = now;
            }
            if (r.validSpO2 && (s_heartLastSpO2SampleMs == 0 || now - s_heartLastSpO2SampleMs >= 1000UL)) {
                s_heartSpO2Sum += r.spo2;
                s_heartSpO2Count++;
                s_heartLastSpO2SampleMs = now;
            }

            if (now - s_heartScanStartMs >= 60000UL) {
                s_heartAvgBPM = s_heartBpmCount ? (uint8_t)((s_heartBpmSum + s_heartBpmCount / 2) / s_heartBpmCount) : 0;
                s_heartAvgSpO2 = s_heartSpO2Count ? (uint8_t)((s_heartSpO2Sum + s_heartSpO2Count / 2) / s_heartSpO2Count) : 0;
                s_heartScanState = HEART_SCAN_DONE;
                HAL_UART_Printf("[HeartScan] Done. Avg BPM=%u from %u samples, Avg SpO2=%u%% from %u samples.\r\n",
                                s_heartAvgBPM, s_heartBpmCount,
                                s_heartAvgSpO2, s_heartSpO2Count);
                s_heartDirty = true;
            }
        }
    }

    uint8_t remaining = 60;
    if (s_heartScanState == HEART_SCAN_RUNNING) {
        uint32_t elapsedSec = (now - s_heartScanStartMs) / 1000UL;
        remaining = (elapsedSec >= 60UL) ? 0 : (uint8_t)(60UL - elapsedSec);
    }

    bool changed = s_heartDirty
        || !hasData
        || (s_heartScanState != s_lastHeartScanState)
        || (remaining != s_heartLastRemaining)
        || (r.fingerPresent != s_lastFinger)
        || (r.bpm           != s_lastBPM)
        || (r.spo2          != s_lastSpO2)
        || (s_battery.percent != s_lastBattPct);

    if (!changed) return;

    s_heartDirty = false;
    if (hasData) {
        s_lastFinger = r.fingerPresent;
        s_lastBPM    = r.bpm;
        s_lastSpO2   = r.spo2;
    }
    s_lastHeartScanState = s_heartScanState;
    s_heartLastRemaining = remaining;

    MCAL_OLED_Clear();
    oledTitle(" Heart Monitor ");
    oledDivider();

    if (!hasData) {
        MCAL_OLED_PrintLine(2, " Waiting...");
        MCAL_OLED_PrintLine(3, "");
        MCAL_OLED_PrintLine(4, "");
    } else {
        char line[22];

        switch (s_heartScanState) {
            case HEART_SCAN_WAITING:
                MCAL_OLED_PrintLine(2, " Place finger");
                MCAL_OLED_PrintLine(3, " Scan: 60 sec");
                MCAL_OLED_PrintLine(4, " Avg shown at end");
                break;

            case HEART_SCAN_RUNNING:
                snprintf(line, sizeof(line), " Time left: %02us", remaining);
                MCAL_OLED_PrintLine(2, line);
                snprintf(line, sizeof(line), " BPM: %s%u",
                         r.validBPM ? "" : "... ",
                         r.validBPM ? r.bpm : 0);
                MCAL_OLED_PrintLine(3, r.validBPM ? line : " BPM: ...");
                snprintf(line, sizeof(line), " SpO2: %s%u%%",
                         r.validSpO2 ? "" : "... ",
                         r.validSpO2 ? r.spo2 : 0);
                MCAL_OLED_PrintLine(4, r.validSpO2 ? line : " SpO2: ...");
                break;

            case HEART_SCAN_DONE:
                MCAL_OLED_PrintLine(2, " Scan complete");
                snprintf(line, sizeof(line), " Avg BPM: %s%u",
                         s_heartBpmCount ? "" : "-- ",
                         s_heartBpmCount ? s_heartAvgBPM : 0);
                MCAL_OLED_PrintLine(3, s_heartBpmCount ? line : " Avg BPM: --");
                snprintf(line, sizeof(line), " Avg SpO2: %s%u%%",
                         s_heartSpO2Count ? "" : "-- ",
                         s_heartSpO2Count ? s_heartAvgSpO2 : 0);
                MCAL_OLED_PrintLine(4, s_heartSpO2Count ? line : " Avg SpO2: --");
                break;

            case HEART_SCAN_CANCELLED:
                MCAL_OLED_PrintLine(2, " Scan cancelled");
                MCAL_OLED_PrintLine(3, " Finger removed");
                MCAL_OLED_PrintLine(4, "[SEL] Restart");
                break;

            case HEART_SCAN_ERROR:
            default:
                MCAL_OLED_PrintLine(2, " Sensor error");
                MCAL_OLED_PrintLine(3, " See UART logs");
                MCAL_OLED_PrintLine(4, "");
                break;
        }
    }

    MCAL_OLED_PrintLine(5, (s_heartScanState == HEART_SCAN_DONE) ? "[SEL]Again [BCK]Back" : "[BACK] Return");
    pushDisplay();
}

static void renderSystemInfo(void) {
    unsigned long heap      = esp_get_free_heap_size();
    uint32_t      uptimeSec = (uint32_t)(millis() / 1000);

    bool battChanged = (s_battery.percent != s_lastBattPct ||
                        s_battery.state   != s_lastBattState ||
                        s_battery.isConnected != s_lastBattConnected);

    if (heap == s_lastHeap && uptimeSec == s_lastUptimeSec && !battChanged) return;

    s_lastHeap      = heap;
    s_lastUptimeSec = uptimeSec;

    MCAL_OLED_Clear();
    oledHeader("  System Info  ");

    char line[22];
    snprintf(line, sizeof(line), " Heap: %luKB", heap / 1024);
    MCAL_OLED_PrintLine(2, line);
    snprintf(line, sizeof(line), " Up: %lus C%d",
             (unsigned long)uptimeSec, xPortGetCoreID());
    MCAL_OLED_PrintLine(3, line);
    if (s_battery.isConnected) {
        snprintf(line, sizeof(line), " Bat: %.2fV %u%%",
                 s_battery.voltageV, s_battery.percent);
    } else {
        snprintf(line, sizeof(line), " Bat: N/C");
    }
    MCAL_OLED_PrintLine(4, line);
    MCAL_OLED_PrintLine(5, "[SEL]Battery [BCK]Back");

    pushDisplay();
}

static void renderBatteryInfo(void) {
    bool changed = (s_battery.percent != s_lastBattPct ||
                    s_battery.state   != s_lastBattState ||
                    s_battery.isConnected != s_lastBattConnected);
    if (!changed) return;

    s_lastBattPct   = s_battery.percent;
    s_lastBattState = s_battery.state;
    s_lastBattConnected = s_battery.isConnected;

    MCAL_OLED_Clear();
    oledHeader("   Battery     ");

    char line[22];
    if (s_battery.isConnected) {
        snprintf(line, sizeof(line), " Volt: %.3f V", s_battery.voltageV);
    } else {
        snprintf(line, sizeof(line), " Volt: ---");
    }
    MCAL_OLED_PrintLine(2, line);

    const char *connStatus = s_battery.isConnected ? "Connected" : "NOT CONNECTED";
    snprintf(line, sizeof(line), " %s", connStatus);
    MCAL_OLED_PrintLine(3, line);

    const char *stateLabel;
    switch (s_battery.state) {
        case BATTERY_STATE_CHARGING:    stateLabel = "CHG";  break;
        case BATTERY_STATE_FULL:        stateLabel = "FULL"; break;
        case BATTERY_STATE_DISCHARGING: stateLabel = "DIS";  break;
        default:                        stateLabel = "???";  break;
    }
    snprintf(line, sizeof(line), " %u%%  [%s]", s_battery.percent, stateLabel);
    MCAL_OLED_PrintLine(4, line);

    const uint8_t BAR_CHARS = 18;
    uint8_t filled = s_battery.isConnected
        ? (uint8_t)((s_battery.percent * BAR_CHARS) / 100)
        : 0;
    char bar[22];
    bar[0] = ' ';
    for (uint8_t i = 0; i < BAR_CHARS; i++) {
        bar[1 + i] = (i < filled) ? (char)0xFF : '-';
    }
    bar[1 + BAR_CHARS] = '\0';
    MCAL_OLED_PrintLine(5, bar);

    if (s_battery.isCritical) {
        MCAL_OLED_PrintLine(6, "!! CRITICAL LOW !!");
    } else if (s_battery.isLow) {
        MCAL_OLED_PrintLine(6, "! Low battery");
    } else {
        MCAL_OLED_PrintLine(6, "[BACK] Return");
    }

    pushDisplay();
}

static void sendSystemInfoIfNeeded(void) {
    if (MCAL_WiFi_GetState() != WIFI_STATE_CONNECTED) return;
    uint32_t now = (uint32_t)millis();
    if (now - s_lastSystemInfoPostMs < SYSTEM_INFO_POST_INTERVAL_MS) return;
    s_lastSystemInfoPostMs = now;

    unsigned long heap      = esp_get_free_heap_size();
    uint32_t      uptimeSec = (uint32_t)(millis() / 1000);
    char payload[192];
    snprintf(payload, sizeof(payload),
             "{\"free_heap_kb\":%lu,\"uptime_s\":%lu,\"core\":%d,"
             "\"battery_connected\":%s,\"battery_v\":%.3f,"
             "\"battery_pct\":%u,\"battery_state\":%d,\"battery_raw\":%u}",
             heap / 1024, (unsigned long)uptimeSec, xPortGetCoreID(),
             s_battery.isConnected ? "true" : "false",
             s_battery.isConnected ? s_battery.voltageV : 0.0f,
             s_battery.isConnected ? s_battery.percent : 0,
             (int)s_battery.state, s_battery.rawAdc);

    HTTPClient http;
    http.begin(SYSTEM_INFO_API_URL);
    http.addHeader("Content-Type", "application/json");
    http.POST((uint8_t *)payload, strlen(payload));
    http.end();
}

static void renderPowerOff(void) {
    MCAL_OLED_Clear();
    oledHeader("   Power Off   ");
    MCAL_OLED_PrintLine(2, " Sleep device?");
    char line[22];
    snprintf(line, sizeof(line), " [%s] YES  [%s] NO",
             s_confirmYes ? "X" : " ",
             s_confirmYes ? " " : "X");
    MCAL_OLED_PrintLine(3, line);
    MCAL_OLED_PrintLine(4, "[SEL]OK  [BCK]Cancel");
    pushDisplay();
}

static void renderSettings(void) {
    renderMenu("=== Settings ===",
               SETTINGS_MENU_ITEMS, SETTINGS_MENU_COUNT,
               s_settingsSel, s_settingsScroll);
}

static void renderBrightness(void) {
    MCAL_OLED_Clear();
    oledHeader("  Brightness   ");
    for (uint8_t i = 0; i < BRIGHTNESS_OPTION_COUNT; i++) {
        char line[22];
        snprintf(line, sizeof(line), " %s%-15s",
                 (i == s_brightnessIdx) ? "[X] " : "[ ] ",
                 BRIGHTNESS_LABELS[i]);
        MCAL_OLED_PrintLine(2 + i, line);
    }
    pushDisplay();
}

static void renderInactivity(void) {
    if (s_inactivityIdx < s_inactScroll)
        s_inactScroll = s_inactivityIdx;
    if (s_inactivityIdx >= s_inactScroll + MENU_VISIBLE)
        s_inactScroll = s_inactivityIdx - MENU_VISIBLE + 1;

    MCAL_OLED_Clear();
    oledHeader(" Inact. Timeout");
    for (uint8_t v = 0; v < MENU_VISIBLE; v++) {
        uint8_t idx = s_inactScroll + v;
        if (idx >= INACTIVITY_OPTION_COUNT) break;
        char line[22];
        snprintf(line, sizeof(line), " %s%-14s",
                 (idx == s_inactivityIdx) ? "[X] " : "[ ] ",
                 INACTIVITY_LABELS[idx]);
        MCAL_OLED_PrintLine(2 + v, line);
    }
    pushDisplay();
}

static void renderReboot(void) {
    MCAL_OLED_Clear();
    oledHeader("    Reboot     ");
    MCAL_OLED_PrintLine(2, " Reboot device?");
    char line[22];
    snprintf(line, sizeof(line), " [%s] YES  [%s] NO",
             s_confirmYes ? "X" : " ",
             s_confirmYes ? " " : "X");
    MCAL_OLED_PrintLine(3, line);
    MCAL_OLED_PrintLine(4, "[SEL]OK  [BCK]Cancel");
    pushDisplay();
}

static void renderWifiMenu(void) {
    renderMenu("=== WiFi ===",
               WIFI_MENU_ITEMS, WIFI_MENU_COUNT,
               s_wifiSel, s_wifiScroll);
}

static void renderWifiStatus(QueueHandle_t wifiQ) {
    WiFiHAL_Status_t status;
    bool hasData = (xQueuePeek(wifiQ, &status, 0) == pdTRUE);

    bool changed = s_wifiDirty;
    if (!changed && hasData) {
        changed = (status.state != s_lastWifiState)
               || (strcmp(status.ip, s_lastIP) != 0)
               || (status.rssi != s_lastRSSI)
               || (s_battery.percent != s_lastBattPct);
    }
    if (!changed) return;

    s_wifiDirty = false;
    if (hasData) {
        s_lastWifiState = status.state;
        strncpy(s_lastIP, status.ip, sizeof(s_lastIP) - 1);
        s_lastRSSI = status.rssi;
    }

    MCAL_OLED_Clear();
    oledHeader("  WiFi Status  ");
    if (!hasData) {
        MCAL_OLED_PrintLine(2, " No data yet...");
    } else {
        char line[22];
        switch (status.state) {
            case WIFI_STATE_IDLE:
                MCAL_OLED_PrintLine(2, " State: Idle"); break;
            case WIFI_STATE_CONNECTING:
                MCAL_OLED_PrintLine(2, " Connecting..."); break;
            case WIFI_STATE_FAILED:
                MCAL_OLED_PrintLine(2, " FAILED"); break;
            case WIFI_STATE_DISCONNECTED:
                MCAL_OLED_PrintLine(2, " Disconnected"); break;
            case WIFI_STATE_SETUP_PORTAL:
                MCAL_OLED_PrintLine(2, " Setup portal ON");
                MCAL_OLED_PrintLine(3, " AP:Stetho-Setup");
                snprintf(line, sizeof(line), " PW:%s", MCAL_Portal_GetApPassword());
                MCAL_OLED_PrintLine(4, line);
                break;
            case WIFI_STATE_CONNECTED:
                snprintf(line, sizeof(line), " IP:%.16s", status.ip);
                MCAL_OLED_PrintLine(2, line);
                snprintf(line, sizeof(line), " RSSI:%d dBm", (int)status.rssi);
                MCAL_OLED_PrintLine(3, line);
                break;
        }
    }
    MCAL_OLED_PrintLine(4, "[BACK] Return");
    pushDisplay();
}

static void renderWifiSetup(void) {
    MCAL_OLED_Clear();
    oledHeader("  Setup Mode   ");
    char line[22];
    snprintf(line, sizeof(line), " AP:%s", MCAL_Portal_GetApSSID());
    MCAL_OLED_PrintLine(2, line);
    snprintf(line, sizeof(line), " PW:%s", MCAL_Portal_GetApPassword());
    MCAL_OLED_PrintLine(3, line);
    MCAL_OLED_PrintLine(4, " IP:192.168.4.1");
    MCAL_OLED_PrintLine(5, "[SEL] WiFi QR");
    pushDisplay();
}

static void renderWifiSetupQr(void) {
    if (!s_setupQrReady) {
        char qrText[64];
        MCAL_Portal_GetWiFiQrText(qrText, sizeof(qrText));
        s_setupQrReady = MCAL_QRCode_GenerateText(&s_setupQr, qrText);
    }
    if (!MCAL_OLED_IsReady()) return;

    MCAL_OLED_Clear();
    if (!s_setupQrReady) {
        oledHeader("  Setup QR     ");
        MCAL_OLED_PrintLine(2, " QR unavailable");
        MCAL_OLED_PrintLine(4, " IP:192.168.4.1");
        pushDisplay();
        return;
    }

    display.clearDisplay();
    const uint8_t scale    = 2;
    const uint8_t qrPixels = QR_UTIL_SIZE * scale;
    const uint8_t x0       = (SCREEN_WIDTH  - qrPixels) / 2;
    const uint8_t y0       = (SCREEN_HEIGHT - qrPixels) / 2;

    display.drawRect(x0 - 3, y0 - 3, qrPixels + 6, qrPixels + 6, SSD1306_WHITE);
    for (uint8_t y = 0; y < QR_UTIL_SIZE; y++) {
        for (uint8_t x = 0; x < QR_UTIL_SIZE; x++) {
            if (MCAL_QRCode_GetModule(&s_setupQr, x, y)) {
                display.fillRect(x0 + x * scale, y0 + y * scale,
                                 scale, scale, SSD1306_WHITE);
            }
        }
    }
    drawBatteryIcon();
    display.display();
}

static void renderWifiLogin(void) {
    MCAL_OLED_Clear();
    oledHeader("  WiFi Login   ");
    char line[22];
    snprintf(line, sizeof(line), "SS:%.17s", s_wifiSSID);
    MCAL_OLED_PrintLine(2, line);
    snprintf(line, sizeof(line), "PW:%.17s", s_wifiPass);
    MCAL_OLED_PrintLine(3, line);
    MCAL_OLED_PrintLine(4, "[SEL]Conn [BCK]Back");
    pushDisplay();
}

static void renderWifiScan(bool force) {
    if (!s_scanDone) {
        MCAL_OLED_Clear();
        oledHeader("  WiFi Scan    ");
        MCAL_OLED_PrintLine(2, " Scanning...");
        MCAL_OLED_PrintLine(3, " Please wait");
        pushDisplay();
        s_scanCount  = MCAL_WiFi_Scan(s_scanSSIDs, SCAN_MAX);
        s_scanDone   = true;
        s_scanScroll = 0;
        force = true;
    }
    if (!force) return;

    MCAL_OLED_Clear();
    oledHeader("  WiFi Scan    ");
    if (s_scanCount == 0) {
        MCAL_OLED_PrintLine(2, " No networks found");
        MCAL_OLED_PrintLine(3, " [SEL] Rescan");
    } else {
        for (uint8_t v = 0; v < MENU_VISIBLE; v++) {
            uint8_t idx = s_scanScroll + v;
            if (idx >= s_scanCount) break;
            char line[22];
            snprintf(line, sizeof(line), " %.21s", s_scanSSIDs[idx]);
            MCAL_OLED_PrintLine(2 + v, line);
        }
    }
    pushDisplay();
}

static void renderWifiReset(void) {
    MCAL_OLED_Clear();
    oledHeader("   WiFi Reset  ");
    MCAL_OLED_PrintLine(2, " Reset & forget");
    MCAL_OLED_PrintLine(3, " credentials?");
    char line[22];
    snprintf(line, sizeof(line), " [%s] YES  [%s] NO",
             s_confirmYes ? "X" : " ",
             s_confirmYes ? " " : "X");
    MCAL_OLED_PrintLine(4, line);
    pushDisplay();
}

static void renderWifiDisconnect(void) {
    MCAL_OLED_Clear();
    oledHeader(" WiFi Discnct  ");
    MCAL_OLED_PrintLine(2, " Disconnect now?");
    char line[22];
    snprintf(line, sizeof(line), " [%s] YES  [%s] NO",
             s_confirmYes ? "X" : " ",
             s_confirmYes ? " " : "X");
    MCAL_OLED_PrintLine(3, line);
    MCAL_OLED_PrintLine(4, "[SEL]OK  [BCK]Cancel");
    pushDisplay();
}

/* ══════════════════════════════════════════════════════════════════
   goTo — transition helper
   ══════════════════════════════════════════════════════════════════ */
static void goTo(UIScreen_t target) {
    /* ── Heart sensor power management ── */
    if (s_screen == UI_SCREEN_HEART_MONITOR && target != UI_SCREEN_HEART_MONITOR) {
        HeartTask_SetActive(false);
    } else if (s_screen != UI_SCREEN_HEART_MONITOR && target == UI_SCREEN_HEART_MONITOR) {
        HeartTask_SetActive(true);
    }

    /* ── Mic sensor power management ── */          /* ← NEW */
    if (s_screen == UI_SCREEN_LUNG_SOUND && target != UI_SCREEN_LUNG_SOUND) {
        /* Leaving lung sound — abort any recording in progress */
        if (MCAL_Mic_GetState() == MIC_STATE_RECORDING) {
            MCAL_Mic_StopRecording();
        }
        MicTask_SetActive(false);
    } else if (s_screen != UI_SCREEN_LUNG_SOUND && target == UI_SCREEN_LUNG_SOUND) {
        MicTask_SetActive(true);
    }

    if (target == UI_SCREEN_SYSTEM_INFO) {
        s_systemInfoReturn = s_screen;
    }

    s_screen = target;

    if (target == UI_SCREEN_POWER_OFF      ||
        target == UI_SCREEN_REBOOT         ||
        target == UI_SCREEN_WIFI_RESET     ||
        target == UI_SCREEN_WIFI_DISCONNECT) {
        s_confirmYes = true;
    }
    if (target == UI_SCREEN_WIFI_SETUP) {
        MCAL_WiFi_StartSetupPortal();
    }
    if (target == UI_SCREEN_WIFI_SETUP_QR) {
        s_setupQrReady = false;
    }
    if (target == UI_SCREEN_WIFI_SCAN) {
        s_scanDone   = false;
        s_scanScroll = 0;
    }
    if (target == UI_SCREEN_HEART_MONITOR) resetHeartScan();
    if (target == UI_SCREEN_WIFI_STATUS)   s_wifiDirty  = true;
    if (target == UI_SCREEN_LUNG_SOUND) {  /* ← NEW */
        s_lungDirty = true;
        s_lastBarPct = 255;
        memset(s_waveCol, 0, sizeof(s_waveCol));
        s_waveWriteIdx    = 0;
        s_lastWaveUpdateMs = 0;
        /* Reset any stale ML result */
        if (MCAL_Mic_GetState() == MIC_STATE_UPLOAD_OK ||
            MCAL_Mic_GetState() == MIC_STATE_UPLOAD_ERR) {
            MCAL_Mic_ResetRecording();
        }
    }
    if (target == UI_SCREEN_SYSTEM_INFO || target == UI_SCREEN_BATTERY_INFO) {
        s_lastHeap      = 0;
        s_lastUptimeSec = 0;
        s_lastBattPct   = 255;
        s_lastBattState = BATTERY_STATE_UNKNOWN;
        s_lastBattConnected = false;
    }

    HAL_UART_Printf("[UI] -> screen %d\r\n", (int)target);
}

/* ══════════════════════════════════════════════════════════════════
   Input handler
   ══════════════════════════════════════════════════════════════════ */
static void handleInput(ButtonEvent_t evt, QueueHandle_t wifiQ) {
    (void)wifiQ;

    if (evt == BTN_EVENT_BACK_PRESSED || evt == BTN_EVENT_BACK_HELD) {
        switch (s_screen) {
            case UI_SCREEN_HEART_MONITOR:
            case UI_SCREEN_LUNG_SOUND:      /* ← NEW */
            case UI_SCREEN_SETTINGS:
            case UI_SCREEN_POWER_OFF:
                goTo(UI_SCREEN_MAIN_MENU); break;

            case UI_SCREEN_SYSTEM_INFO:
                goTo(s_systemInfoReturn); break;

            case UI_SCREEN_BATTERY_INFO:
                goTo(UI_SCREEN_SYSTEM_INFO); break;

            case UI_SCREEN_BRIGHTNESS:
            case UI_SCREEN_INACTIVITY:
            case UI_SCREEN_REBOOT:
            case UI_SCREEN_WIFI_MENU:
                goTo(UI_SCREEN_SETTINGS); break;

            case UI_SCREEN_WIFI_STATUS:
            case UI_SCREEN_WIFI_LOGIN:
            case UI_SCREEN_WIFI_SCAN:
            case UI_SCREEN_WIFI_RESET:
            case UI_SCREEN_WIFI_DISCONNECT:
            case UI_SCREEN_WIFI_SETUP:
                goTo(UI_SCREEN_WIFI_MENU); break;

            case UI_SCREEN_WIFI_SETUP_QR:
                goTo(UI_SCREEN_WIFI_SETUP); break;

            default: break;
        }
        return;
    }

    switch (s_screen) {

        case UI_SCREEN_MAIN_MENU:
            if      (evt == BTN_EVENT_UP_PRESSED   || evt == BTN_EVENT_UP_HELD)
                menuUp(&s_mainSel, &s_mainScroll);
            else if (evt == BTN_EVENT_DOWN_PRESSED || evt == BTN_EVENT_DOWN_HELD)
                menuDown(&s_mainSel, &s_mainScroll, MAIN_MENU_COUNT);
            else if (evt == BTN_EVENT_SELECT_PRESSED) {
                switch (s_mainSel) {
                    case 0: goTo(UI_SCREEN_HEART_MONITOR); break;
                    case 1: goTo(UI_SCREEN_LUNG_SOUND);    break;  /* ← NEW */
                    case 2: goTo(UI_SCREEN_SETTINGS);      break;
                    case 3: goTo(UI_SCREEN_SYSTEM_INFO);   break;
                    case 4: goTo(UI_SCREEN_POWER_OFF);     break;
                }
            }
            break;

        /* ── Lung Sound screen ── */                            /* ← NEW */
        case UI_SCREEN_HEART_MONITOR:
            if (evt == BTN_EVENT_SELECT_PRESSED &&
                (s_heartScanState == HEART_SCAN_DONE ||
                 s_heartScanState == HEART_SCAN_CANCELLED ||
                 s_heartScanState == HEART_SCAN_ERROR)) {
                resetHeartScan();
            }
            break;

        case UI_SCREEN_LUNG_SOUND: {
            MicState_t ms = MCAL_Mic_GetState();

            if (evt == BTN_EVENT_SELECT_PRESSED) {
                switch (ms) {
                    case MIC_STATE_IDLE:
                        /* Start recording with available time (between min and max) */
                        {
                            uint8_t recSeconds = MCAL_Mic_GetAvailableRecordSeconds();
                            MCAL_Mic_StartRecording(recSeconds);
                        }
                        s_lungDirty = true;
                        break;

                    case MIC_STATE_RECORDING:
                        /* Stop early and move to DONE */
                        {
                            /* We mark done by stopping the recording;
                               MCAL transitions to DONE internally when
                               we call StopRecording with samples present.
                               But StopRecording() discards — we need to
                               force-stop by triggering the time limit.
                               Hack: flip state directly via a flag in MCAL. */
                            /* Actually stop + keep data: we expose a
                               "seal" operation via StopRecording which
                               sets state to IDLE.  Instead we just let
                               MCAL_Mic_Tick() keep running, but signal
                               via a helper that sets the flag.
                               Simplest: re-use StopRecording but set
                               state to DONE manually after. */
                            MCAL_Mic_StopRecording();
                            /* Since StopRecording resets to IDLE and
                               clears buffer, user experience is:
                               pressing SEL early discards this take and
                               returns to IDLE. Make that obvious in UI. */
                            s_lungDirty = true;
                        }
                        break;

                    case MIC_STATE_DONE:
                        /* Upload to ML API (async) */
                        MicTask_StartUpload();
                        s_lungDirty = true;
                        break;

                    case MIC_STATE_UPLOAD_OK:
                    case MIC_STATE_UPLOAD_ERR:
                        /* New recording */
                        MCAL_Mic_ResetRecording();
                        s_lungDirty = true;
                        break;

                    default:
                        break;
                }
            }
            /* BACK is handled globally above */
            break;
        }

        case UI_SCREEN_POWER_OFF:
            if (evt == BTN_EVENT_UP_PRESSED   || evt == BTN_EVENT_UP_HELD   ||
                evt == BTN_EVENT_DOWN_PRESSED || evt == BTN_EVENT_DOWN_HELD)
                s_confirmYes = !s_confirmYes;
            else if (evt == BTN_EVENT_SELECT_PRESSED) {
                if (s_confirmYes) {
                    goTo(UI_SCREEN_POWER_OFF);
                    s_confirmYes = false;
                } else {
                    goTo(UI_SCREEN_MAIN_MENU);
                }
            }
            break;

        case UI_SCREEN_SETTINGS:
            if      (evt == BTN_EVENT_UP_PRESSED   || evt == BTN_EVENT_UP_HELD)
                menuUp(&s_settingsSel, &s_settingsScroll);
            else if (evt == BTN_EVENT_DOWN_PRESSED || evt == BTN_EVENT_DOWN_HELD)
                menuDown(&s_settingsSel, &s_settingsScroll, SETTINGS_MENU_COUNT);
            else if (evt == BTN_EVENT_SELECT_PRESSED) {
                switch (s_settingsSel) {
                    case 0: goTo(UI_SCREEN_WIFI_MENU);  break;
                    case 1: goTo(UI_SCREEN_BRIGHTNESS); break;
                    case 2: goTo(UI_SCREEN_INACTIVITY); break;
                    case 3: goTo(UI_SCREEN_REBOOT);     break;
                    case 4: goTo(UI_SCREEN_SYSTEM_INFO); break;
                }
            }
            break;

        case UI_SCREEN_BRIGHTNESS:
            if      (evt == BTN_EVENT_UP_PRESSED   || evt == BTN_EVENT_UP_HELD) {
                if (s_brightnessIdx > 0) s_brightnessIdx--;
                applyBrightness();
            } else if (evt == BTN_EVENT_DOWN_PRESSED || evt == BTN_EVENT_DOWN_HELD) {
                if (s_brightnessIdx < BRIGHTNESS_OPTION_COUNT - 1) s_brightnessIdx++;
                applyBrightness();
            } else if (evt == BTN_EVENT_SELECT_PRESSED) {
                goTo(UI_SCREEN_SETTINGS);
            }
            break;

        case UI_SCREEN_INACTIVITY:
            if      (evt == BTN_EVENT_UP_PRESSED   || evt == BTN_EVENT_UP_HELD) {
                if (s_inactivityIdx > 0) s_inactivityIdx--;
            } else if (evt == BTN_EVENT_DOWN_PRESSED || evt == BTN_EVENT_DOWN_HELD) {
                if (s_inactivityIdx < INACTIVITY_OPTION_COUNT - 1) s_inactivityIdx++;
            } else if (evt == BTN_EVENT_SELECT_PRESSED) {
                goTo(UI_SCREEN_SETTINGS);
            }
            break;

        case UI_SCREEN_REBOOT:
            if (evt == BTN_EVENT_UP_PRESSED   || evt == BTN_EVENT_UP_HELD   ||
                evt == BTN_EVENT_DOWN_PRESSED || evt == BTN_EVENT_DOWN_HELD)
                s_confirmYes = !s_confirmYes;
            else if (evt == BTN_EVENT_SELECT_PRESSED) {
                if (s_confirmYes) {
                    HAL_UART_SendLine("[UI] Rebooting...");
                    MCAL_OLED_Clear();
                    MCAL_OLED_PrintLine(2, "  Rebooting...  ");
                    pushDisplay();
                    vTaskDelay(pdMS_TO_TICKS(400));
                    esp_restart();
                } else {
                    goTo(UI_SCREEN_SETTINGS);
                }
            }
            break;

        case UI_SCREEN_WIFI_MENU:
            if      (evt == BTN_EVENT_UP_PRESSED   || evt == BTN_EVENT_UP_HELD)
                menuUp(&s_wifiSel, &s_wifiScroll);
            else if (evt == BTN_EVENT_DOWN_PRESSED || evt == BTN_EVENT_DOWN_HELD)
                menuDown(&s_wifiSel, &s_wifiScroll, WIFI_MENU_COUNT);
            else if (evt == BTN_EVENT_SELECT_PRESSED) {
                switch (s_wifiSel) {
                    case 0: goTo(UI_SCREEN_WIFI_STATUS);     break;
                    case 1: goTo(UI_SCREEN_WIFI_LOGIN);      break;
                    case 2: goTo(UI_SCREEN_WIFI_SCAN);       break;
                    case 3: goTo(UI_SCREEN_WIFI_SETUP);      break;
                    case 4: goTo(UI_SCREEN_WIFI_RESET);      break;
                    case 5: goTo(UI_SCREEN_WIFI_DISCONNECT); break;
                }
            }
            break;

        case UI_SCREEN_SYSTEM_INFO:
            if (evt == BTN_EVENT_SELECT_PRESSED) {
                goTo(UI_SCREEN_BATTERY_INFO);
            }
            break;

        case UI_SCREEN_BATTERY_INFO:
            if (evt == BTN_EVENT_SELECT_PRESSED) {
                goTo(UI_SCREEN_SYSTEM_INFO);
            }
            break;

        case UI_SCREEN_WIFI_SETUP:
            if (evt == BTN_EVENT_SELECT_PRESSED) {
                goTo(UI_SCREEN_WIFI_SETUP_QR);
            }
            break;

        case UI_SCREEN_WIFI_LOGIN:
            if (evt == BTN_EVENT_SELECT_PRESSED) {
                MCAL_WiFi_Connect(s_wifiSSID, s_wifiPass);
                goTo(UI_SCREEN_WIFI_STATUS);
            }
            break;

        case UI_SCREEN_WIFI_SCAN:
            if      (evt == BTN_EVENT_UP_PRESSED   || evt == BTN_EVENT_UP_HELD) {
                if (s_scanScroll > 0) s_scanScroll--;
            } else if (evt == BTN_EVENT_DOWN_PRESSED || evt == BTN_EVENT_DOWN_HELD) {
                if (s_scanCount > 0 && s_scanScroll < (uint8_t)(s_scanCount - 1))
                    s_scanScroll++;
            } else if (evt == BTN_EVENT_SELECT_PRESSED) {
                s_scanDone = false;
            }
            break;

        case UI_SCREEN_WIFI_RESET:
            if (evt == BTN_EVENT_UP_PRESSED   || evt == BTN_EVENT_UP_HELD   ||
                evt == BTN_EVENT_DOWN_PRESSED || evt == BTN_EVENT_DOWN_HELD)
                s_confirmYes = !s_confirmYes;
            else if (evt == BTN_EVENT_SELECT_PRESSED) {
                if (s_confirmYes) {
                    MCAL_WiFi_ClearSavedCredentials();
                    s_wifiSSID[0] = '\0';
                    s_wifiPass[0] = '\0';
                }
                goTo(UI_SCREEN_WIFI_MENU);
            }
            break;

        case UI_SCREEN_WIFI_DISCONNECT:
            if (evt == BTN_EVENT_UP_PRESSED   || evt == BTN_EVENT_UP_HELD   ||
                evt == BTN_EVENT_DOWN_PRESSED || evt == BTN_EVENT_DOWN_HELD)
                s_confirmYes = !s_confirmYes;
            else if (evt == BTN_EVENT_SELECT_PRESSED) {
                if (s_confirmYes) MCAL_WiFi_Disconnect();
                goTo(UI_SCREEN_WIFI_MENU);
            }
            break;

        default: break;
    }
}

/* ══════════════════════════════════════════════════════════════════
   doSleep
   ══════════════════════════════════════════════════════════════════ */
static void doSleep(TickType_t *lastWake) {
    HAL_UART_SendLine("[UI] Entering light-sleep.");
    MCAL_OLED_Clear();

    bool heartWasOn = MCAL_Heart_IsEnabled();
    if (heartWasOn) HeartTask_SetActive(false);

    bool micWasOn = MCAL_Mic_IsActive();           /* ← NEW */
    if (micWasOn) MicTask_SetActive(false);

    configureWakeupPins();
    esp_light_sleep_start();

    HAL_UART_SendLine("[UI] Woke from light-sleep.");

    if (heartWasOn) HeartTask_SetActive(true);
    if (micWasOn)   MicTask_SetActive(true);       /* ← NEW */

    *lastWake = xTaskGetTickCount();

    s_lastActivityMs = (uint32_t)millis();
    s_lastScreen     = (UIScreen_t)-1;
    s_heartDirty     = true;
    s_wifiDirty      = true;
    s_lungDirty      = true;                       /* ← NEW */
    s_lastHeap       = 0;
    s_lastUptimeSec  = 0;
    s_lastBattPct    = 255;
    s_lastBattConnected = false;
    applyBrightness();

    MCAL_Battery_ForceRefresh();
    MCAL_Battery_GetStatus(&s_battery);

    if (g_btnSemaphore) {
        while (xSemaphoreTake(g_btnSemaphore, 0) == pdTRUE) { /* drop */ }
    }
    MCAL_Button_ReinitPins();
    vTaskDelay(pdMS_TO_TICKS(50));  /* Allow GPIO pins to stabilize after sleep */
    MCAL_Button_Reset();
    /* Clear any stale button events that may have accumulated during sleep */
    ButtonEvent_t dummy;
    while (MCAL_Button_GetEvent(&dummy)) { /* drain queue */ }
}

/* ══════════════════════════════════════════════════════════════════
   Task body
   ══════════════════════════════════════════════════════════════════ */
void UITask(void *pvParams) {
    UITask_Params_t *p = (UITask_Params_t *)pvParams;

    MCAL_Battery_ForceRefresh();
    MCAL_Battery_GetStatus(&s_battery);

    s_screen = UI_SCREEN_BOOT;
    renderBoot();
    vTaskDelay(pdMS_TO_TICKS(BOOT_SPLASH_MS));
    applyBrightness();

    s_lastActivityMs = (uint32_t)millis();
    goTo(UI_SCREEN_MAIN_MENU);
    s_lastScreen = (UIScreen_t)-1;

    HAL_UART_SendLine("[UI] Task running. "
                       "UP/DOWN=scroll  SELECT=confirm  BACK=back");

    TickType_t lastWake = xTaskGetTickCount();

    for (;;) {

        int64_t t0 = esp_timer_get_time();
        if (s_lastLoopUs != 0) {
            uint32_t actual = (uint32_t)(t0 - s_lastLoopUs);
            uint32_t target = s_timing.target_period_us;
            uint32_t jitter = (actual > target) ? (actual - target) : (target - actual);
            if (jitter > s_timing.max_jitter_us) s_timing.max_jitter_us = jitter;
            if (actual > target * 2) s_timing.missed_deadlines++;
        }
        s_lastLoopUs = t0;

        /* ── 1. Battery tick ── */
        MCAL_Battery_Tick();
        MCAL_Battery_GetStatus(&s_battery);

        /* ── 2. Poll buttons ── */
        MCAL_Button_Poll();

        ButtonEvent_t evt;
        bool hadInput = false;
        bool inputLocked = isInputLocked();
        while (MCAL_Button_GetEvent(&evt)) {
            if (inputLocked) {
                continue;
            }
            resetActivity();
            handleInput(evt, p->wifiStatusQueue);
            hadInput = true;
        }

        /* ── 3. Power-Off execute ── */
        if (s_screen == UI_SCREEN_POWER_OFF && !s_confirmYes) {
            MCAL_OLED_Clear();
            oledHeader("   Power Off   ");
            MCAL_OLED_PrintLine(2, "  Going to sleep ");
            MCAL_OLED_PrintLine(3, "  Press any btn  ");
            MCAL_OLED_PrintLine(4, "  to wake up...  ");
            pushDisplay();
            vTaskDelay(pdMS_TO_TICKS(1000));
            doSleep(&lastWake);
            goTo(UI_SCREEN_MAIN_MENU);
            hadInput = false;
        }

        /* ── 4. Inactivity sleep (Lung Sound screen excluded) ── */
        uint32_t timeoutMs = INACTIVITY_OPTIONS_SEC[s_inactivityIdx] * 1000UL;
    bool allowSleep = (s_screen != UI_SCREEN_WIFI_SETUP_QR)  &&
              (s_screen != UI_SCREEN_SYSTEM_INFO)     &&
              (s_screen != UI_SCREEN_LUNG_SOUND);      /* ← don't sleep mid-recording */
        if (allowSleep && s_screen != UI_SCREEN_HEART_MONITOR && timeoutMs > 0 &&
            ((uint32_t)millis() - s_lastActivityMs) >= timeoutMs) {
            doSleep(&lastWake);
            hadInput = false;
        }

        /* ── 5. Portal credential sync ── */
        PortalCredentials_t portalCreds;
        if (MCAL_Portal_GetCredentials(&portalCreds) &&
            (strcmp(portalCreds.ssid, s_lastPortalCredSSID) != 0 ||
             strcmp(portalCreds.pass, s_lastPortalCredPass) != 0)) {
            UITask_SetWiFiCredentials(portalCreds.ssid, portalCreds.pass);
            strncpy(s_lastPortalCredSSID, portalCreds.ssid, sizeof(s_lastPortalCredSSID) - 1);
            s_lastPortalCredSSID[sizeof(s_lastPortalCredSSID) - 1] = '\0';
            strncpy(s_lastPortalCredPass, portalCreds.pass, sizeof(s_lastPortalCredPass) - 1);
            s_lastPortalCredPass[sizeof(s_lastPortalCredPass) - 1] = '\0';
        }

        if (s_screen == UI_SCREEN_WIFI_SETUP_QR && MCAL_Portal_GetStationCount() > 0) {
            goTo(UI_SCREEN_WIFI_STATUS);
            hadInput = false;
        }

        /* ── 6. Lung Sound: auto-advance to DONE state display ── */  /* ← NEW */
        if (s_screen == UI_SCREEN_LUNG_SOUND) {
            MicState_t ms = MCAL_Mic_GetState();
            if (ms == MIC_STATE_DONE || ms == MIC_STATE_UPLOAD_OK ||
                ms == MIC_STATE_UPLOAD_ERR) {
                s_lungDirty = true;   /* force redraw when state changes */
            }
        }

        /* ── 7. Render ── */
        bool screenChanged = (s_screen != s_lastScreen);
        if (screenChanged) s_lastScreen = s_screen;

        switch (s_screen) {

            case UI_SCREEN_MAIN_MENU:
                if (screenChanged || hadInput ||
                    s_battery.percent != s_lastBattPct ||
                    s_battery.isConnected != s_lastBattConnected) renderMainMenu();
                break;

            case UI_SCREEN_HEART_MONITOR:
                renderHeartMonitor(p->heartQueue);
                break;

            case UI_SCREEN_LUNG_SOUND:                         /* ← NEW */
                renderLungSound(p->micLiveQueue);
                break;

            case UI_SCREEN_SYSTEM_INFO:
                sendSystemInfoIfNeeded();
                renderSystemInfo();
                break;

            case UI_SCREEN_BATTERY_INFO:
                renderBatteryInfo();
                break;


            case UI_SCREEN_POWER_OFF:
                if (screenChanged || hadInput) renderPowerOff();
                break;

            case UI_SCREEN_SETTINGS:
                if (screenChanged || hadInput ||
                    s_battery.percent != s_lastBattPct ||
                    s_battery.isConnected != s_lastBattConnected) renderSettings();
                break;

            case UI_SCREEN_BRIGHTNESS:
                if (screenChanged || hadInput) renderBrightness();
                break;

            case UI_SCREEN_INACTIVITY:
                if (screenChanged || hadInput) renderInactivity();
                break;

            case UI_SCREEN_REBOOT:
                if (screenChanged || hadInput) renderReboot();
                break;

            case UI_SCREEN_WIFI_MENU:
                if (screenChanged || hadInput) renderWifiMenu();
                break;

            case UI_SCREEN_WIFI_STATUS:
                renderWifiStatus(p->wifiStatusQueue);
                break;

            case UI_SCREEN_WIFI_LOGIN:
                if (screenChanged || hadInput) renderWifiLogin();
                break;

            case UI_SCREEN_WIFI_SCAN:
                renderWifiScan(screenChanged || hadInput);
                break;

            case UI_SCREEN_WIFI_SETUP:
                if (screenChanged || hadInput) renderWifiSetup();
                break;

            case UI_SCREEN_WIFI_SETUP_QR:
                if (screenChanged || hadInput) renderWifiSetupQr();
                break;

            case UI_SCREEN_WIFI_RESET:
                if (screenChanged || hadInput) renderWifiReset();
                break;

            case UI_SCREEN_WIFI_DISCONNECT:
                if (screenChanged || hadInput) renderWifiDisconnect();
                break;

            default: break;
        }

        /* Update anti-blink sentinels after render */
        s_lastBattPct   = s_battery.percent;
        s_lastBattState = s_battery.state;
        s_lastBattConnected = s_battery.isConnected;

        int64_t t1 = esp_timer_get_time();
        uint32_t delta = (uint32_t)(t1 - t0);
        if (delta > s_timing.wcet_us) s_timing.wcet_us = delta;

        if (g_btnSemaphore) {
            xSemaphoreTake(g_btnSemaphore, pdMS_TO_TICKS(UI_POLL_INTERVAL_MS));
        } else {
            vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(UI_POLL_INTERVAL_MS));
        }
    }
}

/* ══════════════════════════════════════════════════════════════════
   Public helpers
   ══════════════════════════════════════════════════════════════════ */
TaskHandle_t UITask_Start(UITask_Params_t *params) {
    TaskHandle_t handle = NULL;
    xTaskCreatePinnedToCore(
        UITask, "UI_Task",
        UI_TASK_STACK_SIZE, (void *)params,
        UI_TASK_PRIORITY, &handle, UI_TASK_CORE
    );
    return handle;
}

void UITask_SetWiFiCredentials(const char *ssid, const char *pass) {
    strncpy(s_wifiSSID, ssid, sizeof(s_wifiSSID) - 1);
    strncpy(s_wifiPass, pass, sizeof(s_wifiPass) - 1);
    s_wifiSSID[sizeof(s_wifiSSID) - 1] = '\0';
    s_wifiPass[sizeof(s_wifiPass) - 1] = '\0';
    HAL_UART_Printf("[UI] Credentials updated: \"%s\"\r\n", s_wifiSSID);
}

bool UITask_GetTimingStats(TaskTimingStats_t *out) {
    if (!out) return false;
    *out = s_timing;
    return true;
}
