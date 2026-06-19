#ifndef OLED_MCAL_H
#define OLED_MCAL_H

#pragma once

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include "../HAL/i2c_hal.h"

/* ------ Config ------ */
#define SCREEN_WIDTH                    128
#define SCREEN_HEIGHT                   64
#define MCAL_OLED_RESET                 -1
#define SCREEN_ADDRESS                  0x3C
#define I2C_SDA                         8
#define I2C_SCL                         9
#define MCAL_OLED_DEFAULT_TEXT_SIZE     1
#define MCAL_OLED_DEFAULT_TEXT_COLOR    SSD1306_WHITE
#define MCAL_OLED_DEFAULT_BG_COLOR      SSD1306_BLACK
#define MCAL_OLED_DEFAULT_X             0
#define MCAL_OLED_DEFAULT_Y             0
#define MCAL_OLED_CHAR_WIDTH            6   // pixels per char at size 1
#define MCAL_OLED_CHAR_HEIGHT           8   // pixels per char at size 1

/* ------ Types ------ */
enum TextAlign {
  ALIGN_LEFT,
  ALIGN_CENTER,
  ALIGN_RIGHT
};

enum EasingType {
  EASE_LINEAR,
  EASE_IN,
  EASE_OUT
};

class LockedSSD1306 : public Adafruit_SSD1306 {
public:
  using Adafruit_SSD1306::Adafruit_SSD1306;

  void display(void) {
    HAL_I2C_Lock();
    Adafruit_SSD1306::display();
    HAL_I2C_Unlock();
  }

  bool begin(uint8_t vccstate, uint8_t i2caddr, bool reset=true, bool periphBegin=true) {
    HAL_I2C_Lock();
    bool ok = Adafruit_SSD1306::begin(vccstate, i2caddr, reset, periphBegin);
    HAL_I2C_Unlock();
    return ok;
  }
};

extern LockedSSD1306 display;

/* ------ API ------ */
/**
 * @brief Initialize the OLED display with I2C.
 * @param sda  SDA pin  (default: I2C_SDA)
 * @param scl  SCL pin  (default: I2C_SCL)
 * @param addr I2C addr (default: SCREEN_ADDRESS)
 * @return true on success, false on failure
 */
bool MCAL_OLED_Init(uint8_t sda = I2C_SDA, uint8_t scl = I2C_SCL, uint8_t addr = SCREEN_ADDRESS);
/**
 * @brief Return true after the SSD1306 has been detected and initialized.
 */
bool MCAL_OLED_IsReady(void);
/**
 * @brief Clear the display buffer and push it.
*/
void MCAL_OLED_Clear();
/** 
 * @brief Clear only the buffer (don't push yet)
 */
void MCAL_OLED_ClearBuffer();
/** 
 * @brief Push the buffer to the physical screen 
*/
void MCAL_OLED_Push();
/** 
 * @brief Set screen brightness (0–255). Note: SSD1306 has limited contrast range 
 * @param level brightness level (0-255)
 */
void MCAL_OLED_SetBrightness(uint8_t level);
/**
 * @brief Turn the SSD1306 panel output on/off without losing the buffer.
 */
void MCAL_OLED_SetDisplayOn(bool on);
/** 
 * @brief Flip the screen 180 degrees 
 * @param flipped true to flip, false to reset
*/
void MCAL_OLED_FlipScreen(bool flipped);
/** 
 * @brief Invert all pixels on the screen 
 * @param invert true to invert, false to reset
*/
void MCAL_OLED_InvertDisplay(bool invert);
/**
 * @brief Draw text with full control.
 * @param text       The string to display
 * @param x          X position          (default: 0)
 * @param y          Y position          (default: 0)
 * @param size       Text size 1–4       (default: 1)
 * @param color      Text color          (default: WHITE)
 * @param wrap       Auto word-wrap      (default: true)
 * @param pushNow    Push to screen now  (default: true)
 */
void MCAL_OLED_DrawText(const String &text, int x = MCAL_OLED_DEFAULT_X, int y = MCAL_OLED_DEFAULT_Y, uint8_t size = MCAL_OLED_DEFAULT_TEXT_SIZE, uint16_t color = MCAL_OLED_DEFAULT_TEXT_COLOR, bool wrap = true, bool pushNow = true);
/**
 * @brief Draw aligned text (left / center / right) in a given row.
 * @param text   Text to display
 * @param row    Screen row (0 = top). Each row is 8px * size tall.
 * @param align  ALIGN_LEFT | ALIGN_CENTER | ALIGN_RIGHT
 * @param size   Text size (default: 1)
 * @param pushNow    Push to screen now  (default: true)
 */
void MCAL_OLED_DrawAlignedText(const String &text, int row = 0, TextAlign align = ALIGN_LEFT, uint8_t size = MCAL_OLED_DEFAULT_TEXT_SIZE, bool pushNow = true);
/**
 * @brief Clear a single text row and optionally redraw it.
 * @param row   Row index (0-based)
 * @param size  Text size used on that row
 */
void MCAL_OLED_ClearRow(int row, uint8_t size = MCAL_OLED_DEFAULT_TEXT_SIZE, bool pushNow = true);
/**
 * @brief Show a centered title + subtitle layout (common pattern).
 * @param title     Title text
 * @param subtitle  Subtitle text
 * @param titleSize Title text size
 * @param subSize   Subtitle text size
 */
void MCAL_OLED_DrawTitleScreen(const String &title, const String &subtitle = "", uint8_t titleSize = 2, uint8_t subSize = 1);
/**
 * @brief Draw a rectangle
 * @param x         X position
 * @param y         Y position
 * @param w         Width
 * @param h         Height
 * @param filled    Whether to fill the rectangle
 * @param pushNow   Whether to push to screen now
 */
void MCAL_OLED_DrawRect(int x, int y, int w, int h, bool filled = false, bool pushNow = true);
/**
 * @brief Draw a circle
 * @param cx        Center X position
 * @param cy        Center Y position
 * @param r         Radius
 * @param filled    Whether to fill the circle
 * @param pushNow   Whether to push to screen now
 */
void MCAL_OLED_DrawCircle(int cx, int cy, int r, bool filled = false, bool pushNow = true);
/**
 * @brief Draw a line
 * @param x0        X position of the start of the line
 * @param y0        Y position of the start of the line
 * @param x1        X position of the end of the line
 * @param y1        Y position of the end of the line
 * @param pushNow   Whether to push to screen now
 */
void MCAL_OLED_DrawLine(int x0, int y0, int x1, int y1, bool pushNow = true);
/**
 * @brief Draw a pixel
 * @param x         X position
 * @param y         Y position
 * @param pushNow   Whether to push to screen now
 */
void MCAL_OLED_DrawPixel(int x, int y, bool pushNow = false);
/**
 * @brief Draw a horizontal divider line
 * @param y         Y position
 * @param pushNow   Whether to push to screen now
 */
void MCAL_OLED_DrawDivider(int y, bool pushNow = true);
/**
 * @brief Draw a progress bar
 * @param x         X position
 * @param y         Y position
 * @param w         Width
 * @param h         Height
 * @param percent   Progress percentage (0.0 to 1.0)
 * @param pushNow   Whether to push to screen now
 */
void MCAL_OLED_DrawProgressBar(int x, int y, int w, int h, float percent, bool pushNow = true);
/**
 * @brief Draw a bitmap image
 * @param x         X position
 * @param y         Y position
 * @param w         Width
 * @param h         Height
 * @param bitmap    Pointer to the bitmap data
 * @param pushNow   Whether to push to screen now
 */
void MCAL_OLED_DrawBitmap(int x, int y, int w, int h, const uint8_t *bitmap, bool pushNow = true);
/**
 * @brief Scroll text horizontally across the screen (marquee/ticker).
 * @param text      Text to scroll
 * @param y         Vertical position
 * @param size      Text size
 * @param delayMs   Delay between each step (lower = faster)
 * @param loops     How many times to loop (0 = infinite)
 * @param stepPx    How many pixels to advance per frame (default 1)
 */
void MCAL_OLED_ScrollText(const String &text, int y = 0, uint8_t size = MCAL_OLED_DEFAULT_TEXT_SIZE, int delayMs = 30, int loops = 1, int stepPx = 1);
/**
 * @brief Fade the screen in by ramping contrast from 0 to full.
 * @param steps     Number of brightness steps
 * @param delayMs   Delay between steps
 */
void MCAL_OLED_FadeIn(int steps = 16, int delayMs = 30);
/**
 * @brief Fade the screen out by ramping contrast from full to 0.
 */
void MCAL_OLED_FadeOut(int steps = 16, int delayMs = 30);
/**
 * @brief Slide a text string in from a direction.
 * @param text      Text to slide in
 * @param targetX   Final X position
 * @param targetY   Final Y position
 * @param fromLeft  true = slide from left, false = slide from right
 * @param size      Text size
 * @param steps     Number of animation frames
 * @param delayMs   Delay between frames
 */
void MCAL_OLED_SlideInText(const String &text, int targetX = 0, int targetY = 0, bool fromLeft = false, uint8_t size = MCAL_OLED_DEFAULT_TEXT_SIZE, int steps = 16, int delayMs = 20, EasingType easing = EASE_OUT);
/**
 * @brief Animate a bitmap sprite moving from one point to another.
 * @param bitmap    Pointer to bitmap array
 * @param bmpW      Bitmap width  in pixels
 * @param bmpH      Bitmap height in pixels
 * @param x0,y0     Start position
 * @param x1,y1     End position
 * @param steps     Number of frames
 * @param delayMs   Frame delay in ms
 * @param easing    Easing curve to use
 */
void MCAL_OLED_AnimateBitmap(const uint8_t *bitmap, int bmpW, int bmpH, int x0, int y0, int x1, int y1, int steps = 20, int delayMs = 20, EasingType easing = EASE_LINEAR);
/**
 * @brief Blink the entire display N times.
 * @param times     How many blinks
 * @param onMs      On duration  in ms
 * @param offMs     Off duration in ms
 */
void MCAL_OLED_Blink(int times = 3, int onMs = 300, int offMs = 300);
/**
 * @brief Animate a progress bar filling up over time.
 * @param x,y,w,h   Bar geometry
 * @param fromPct   Start percentage 0.0–1.0
 * @param toPct     End percentage   0.0–1.0
 * @param durationMs Total animation time in ms
 */
void MCAL_OLED_AnimateProgressBar(int x, int y, int w, int h, float fromPct, float toPct, int durationMs = 1000);
/**
 * @brief Draw a simple loading spinner at a given position.
 * @param cx,cy     Center of spinner
 * @param r         Radius
 * @param frames    How many frames to animate
 * @param delayMs   Frame delay
 */
void MCAL_OLED_DrawSpinner(int cx, int cy, int r = 8, int frames = 24, int delayMs = 60);
/**
 * @brief Typewriter effect — prints characters one by one.
 * @param text      Text to type
 * @param x,y       Start position
 * @param size      Text size
 * @param delayMs   Delay between characters
 */
void MCAL_OLED_TypewriterText(const String &text, int x = 0, int y = 0, uint8_t size = MCAL_OLED_DEFAULT_TEXT_SIZE, int delayMs = 60);
/**
 * @brief Draw a simple status bar at the top of the screen.
 *        Left label + right value, separated by a divider line.
 * @param label     Left-aligned label (e.g. "Temp")
 * @param value     Right-aligned value (e.g. "36.5C")
 */
void MCAL_OLED_DrawStatusBar(const String &label, const String &value);
/**
 * @brief Draw a simple menu list. Highlight the selected item.
 * @param items     Array of menu item strings
 * @param count     Number of items
 * @param selected  Index of selected item
 */
void MCAL_OLED_DrawMenu(const String items[], int count, int selected = 0);
/**
 * @brief Show a big number centered on screen (great for sensor readings).
 * @param value   The number string to display (e.g. "42")
 * @param unit    Small unit label below (e.g. "°C"), optional
 */
void MCAL_OLED_DrawBigNumber(const String &value, const String &unit = "", uint8_t numSize = 3);
/**
 * @brief Show a Yes/No confirmation dialog.
 * @param question  Question text
 * @param yesSelected  true highlights YES, false highlights NO
 */
void MCAL_OLED_DrawConfirmDialog(const String &question, bool yesSelected = true);
/**
 * @brief Get how many lines a text string will occupy with word wrap.
 * @param text  Input text
 * @param size  Text size
 * @return Number of lines needed
 */
int MCAL_OLED_CountLines(const String &text, uint8_t size = 1);
/**
 * @brief Return true if a string fits on one row at the given size.
 * @param text  Input text
 * @param size  Text size
 * @return true if the text fits on one row, false otherwise
 */
bool MCAL_OLED_FitsOneLine(const String &text, uint8_t size = 1);
/**
 * @brief Run a full walkthrough demo of every OLED function.
 *        Each demo slide shows the function name + a live example.
 *        Call this once from setup() after MCAL_OLED_Init() to test your display.
 *
 * @param holdMs   How long to hold each demo slide (default: 2000ms)
 */
void MCAL_OLED_RunFullDemo(int holdMs = 2000);
/**
 * @brief Print display diagnostics to Serial.
 */
void MCAL_OLED_PrintDiag();

/* ------ Compatibility Wrappers ------ */
/**
 * @brief Compatibility wrapper for MCAL_OLED_Push
 */
inline void MCAL_OLED_Display() {
    MCAL_OLED_Push();
}
/**
 * @brief Compatibility wrapper to clear a line and print text
 */
inline void MCAL_OLED_PrintLine(uint8_t row, const String &str) {
    if (!MCAL_OLED_IsReady()) return;
    MCAL_OLED_ClearRow(row, MCAL_OLED_DEFAULT_TEXT_SIZE, false);
    MCAL_OLED_DrawText(str, 0, row * MCAL_OLED_CHAR_HEIGHT * MCAL_OLED_DEFAULT_TEXT_SIZE, MCAL_OLED_DEFAULT_TEXT_SIZE, MCAL_OLED_DEFAULT_TEXT_COLOR, false, false);
}

#endif /* OLED_MCAL_H */
