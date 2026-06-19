#include "oled_mcal.h"
#include "../HAL/i2c_hal.h"

LockedSSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, MCAL_OLED_RESET);
static bool s_oledReady = false;

// ════════════════════════════════════════════════════════════
//  SECTION 1 — INIT
// ════════════════════════════════════════════════════════════
bool MCAL_OLED_Init(uint8_t sda, uint8_t scl, uint8_t addr) {
  Wire.begin(sda, scl);
  Wire.setClock(I2C_HAL_FREQ_HZ);
  if (!display.begin(SSD1306_SWITCHCAPVCC, addr)) {
    Serial.println("[OLED] SSD1306 init failed!");
    s_oledReady = false;
    return false;
  }
  s_oledReady = true;
  display.clearDisplay();
  display.display();
  Serial.println("[OLED] SSD1306 ready.");
  return true;
}

bool MCAL_OLED_IsReady(void) {
  return s_oledReady;
}


// ════════════════════════════════════════════════════════════
//  SECTION 2 — BASIC DISPLAY CONTROL
// ════════════════════════════════════════════════════════════
void MCAL_OLED_Clear() {
  if (!s_oledReady) return;
  display.clearDisplay();
  display.display();
}
void MCAL_OLED_ClearBuffer() {
  if (!s_oledReady) return;
  display.clearDisplay();
}
void MCAL_OLED_Push() {
  if (!s_oledReady) return;
  display.display();
}
void MCAL_OLED_SetBrightness(uint8_t level) {
  if (!s_oledReady) return;
  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(level);
}
void MCAL_OLED_SetDisplayOn(bool on) {
  if (!s_oledReady) return;
  display.ssd1306_command(on ? SSD1306_DISPLAYON : SSD1306_DISPLAYOFF);
}
void MCAL_OLED_FlipScreen(bool flipped) {
  if (!s_oledReady) return;
  display.ssd1306_command(flipped ? SSD1306_COMSCANINC : SSD1306_COMSCANDEC);
  display.ssd1306_command(flipped ? 0xA0 : 0xA1); // segment remap
}
void MCAL_OLED_InvertDisplay(bool invert) {
  if (!s_oledReady) return;
  display.invertDisplay(invert);
}

// ════════════════════════════════════════════════════════════
//  SECTION 3 — TEXT
// ════════════════════════════════════════════════════════════

/**
 * @brief Internal word-wrap text renderer.
 *        Splits text at spaces so words never get cut mid-way.
 * @param text      The string to display
 * @param x         X position          (default: 0)
 * @param y         Y position          (default: 0)
 * @param size      Text size 1–4       (default: 1)
 */
void _OLED_DrawWrapped(const String &text, int x, int y, uint8_t size) {
  int charW       = MCAL_OLED_CHAR_WIDTH  * size;
  int charH       = MCAL_OLED_CHAR_HEIGHT * size;
  int maxCols     = (SCREEN_WIDTH  - x) / charW;
  int maxRows     = (SCREEN_HEIGHT - y) / charH;
  int curRow      = 0;

  String remaining = text;
  while (remaining.length() > 0 && curRow < maxRows) {
    // Take up to maxCols characters
    String line = "";
    if ((int)remaining.length() <= maxCols) {
      line = remaining;
      remaining = "";
    } else {
      // Find last space within maxCols
      int breakAt = maxCols;
      for (int i = maxCols; i >= 0; i--) {
        if (remaining[i] == ' ') { breakAt = i; break; }
      }
      line      = remaining.substring(0, breakAt);
      remaining = remaining.substring(breakAt + 1); // skip the space
    }
    display.setCursor(x, y + curRow * charH);
    display.print(line);
    curRow++;
  }
}
void MCAL_OLED_DrawText(const String &text,
                  int x,
                  int y,
                  uint8_t size,
                  uint16_t color,
                  bool wrap,
                  bool pushNow)
{
  if (!s_oledReady) return;
  display.setTextSize(size);
  display.setTextColor(color, MCAL_OLED_DEFAULT_BG_COLOR);
  display.setTextWrap(false); // we handle wrapping manually

  if (wrap) {
    _OLED_DrawWrapped(text, x, y, size);
  } else {
    display.setCursor(x, y);
    display.print(text);
  }

  if (pushNow) display.display();
}
void MCAL_OLED_DrawAlignedText(const String &text,
                         int row,
                         TextAlign align,
                         uint8_t size,
                         bool pushNow)
{
  if (!s_oledReady) return;
  int charW  = MCAL_OLED_CHAR_WIDTH * size;
  int charH  = MCAL_OLED_CHAR_HEIGHT * size;
  int textW  = text.length() * charW;
  int y      = row * charH;
  int x      = 0;

  if      (align == ALIGN_CENTER) x = (SCREEN_WIDTH - textW) / 2;
  else if (align == ALIGN_RIGHT)  x = SCREEN_WIDTH - textW;

  MCAL_OLED_DrawText(text, x, y, size, MCAL_OLED_DEFAULT_TEXT_COLOR, false, pushNow);
}
void MCAL_OLED_ClearRow(int row, uint8_t size, bool pushNow) {
  if (!s_oledReady) return;
  int y = row * MCAL_OLED_CHAR_HEIGHT * size;
  int h = MCAL_OLED_CHAR_HEIGHT * size;
  display.fillRect(0, y, SCREEN_WIDTH, h, SSD1306_BLACK);
  if (pushNow) display.display();
}
void MCAL_OLED_DrawTitleScreen(const String &title,
                         const String &subtitle,
                         uint8_t titleSize,
                         uint8_t subSize)
{
  if (!s_oledReady) return;
  display.clearDisplay();
  int titleH = MCAL_OLED_CHAR_HEIGHT * titleSize;
  int subH   = MCAL_OLED_CHAR_HEIGHT * subSize;
  int totalH = titleH + (subtitle.length() > 0 ? subH + 2 : 0);
  int startY = (SCREEN_HEIGHT - totalH) / 2;

  // Title centered
  int titleW = title.length() * MCAL_OLED_CHAR_WIDTH * titleSize;
  display.setTextSize(titleSize);
  display.setTextColor(MCAL_OLED_DEFAULT_TEXT_COLOR);
  display.setCursor((SCREEN_WIDTH - titleW) / 2, startY);
  display.print(title);

  // Subtitle centered
  if (subtitle.length() > 0) {
    int subW = subtitle.length() * MCAL_OLED_CHAR_WIDTH * subSize;
    display.setTextSize(subSize);
    display.setCursor((SCREEN_WIDTH - subW) / 2, startY + titleH + 2);
    display.print(subtitle);
  }

  display.display();
}

// ════════════════════════════════════════════════════════════
//  SECTION 4 — SHAPES & DRAWING
// ════════════════════════════════════════════════════════════

void MCAL_OLED_DrawRect(int x, int y, int w, int h, bool filled, bool pushNow) {
  if (!s_oledReady) return;
  if (filled) display.fillRect(x, y, w, h, SSD1306_WHITE);
  else        display.drawRect(x, y, w, h, SSD1306_WHITE);
  if (pushNow) display.display();
}
void MCAL_OLED_DrawCircle(int cx, int cy, int r, bool filled, bool pushNow) {
  if (!s_oledReady) return;
  if (filled) display.fillCircle(cx, cy, r, SSD1306_WHITE);
  else        display.drawCircle(cx, cy, r, SSD1306_WHITE);
  if (pushNow) display.display();
}
void MCAL_OLED_DrawLine(int x0, int y0, int x1, int y1, bool pushNow) {
  if (!s_oledReady) return;
  display.drawLine(x0, y0, x1, y1, SSD1306_WHITE);
  if (pushNow) display.display();
}
void MCAL_OLED_DrawPixel(int x, int y, bool pushNow) {
  if (!s_oledReady) return;
  display.drawPixel(x, y, SSD1306_WHITE);
  if (pushNow) display.display();
}
void MCAL_OLED_DrawDivider(int y, bool pushNow) {
  if (!s_oledReady) return;
  display.drawLine(0, y, SCREEN_WIDTH - 1, y, SSD1306_WHITE);
  if (pushNow) display.display();
}
void MCAL_OLED_DrawProgressBar(int x, int y, int w, int h,
                          float percent,   // 0.0 to 1.0
                          bool pushNow)
{
  if (!s_oledReady) return;
  percent = constrain(percent, 0.0f, 1.0f);
  display.drawRect(x, y, w, h, SSD1306_WHITE);
  int filled = (int)((w - 2) * percent);
  if (filled > 0)
    display.fillRect(x + 1, y + 1, filled, h - 2, SSD1306_WHITE);
  if (pushNow) display.display();
}
void MCAL_OLED_DrawBitmap(int x, int y, int w, int h,
                    const uint8_t *bitmap, bool pushNow)
{
  if (!s_oledReady) return;
  display.drawBitmap(x, y, bitmap, w, h, SSD1306_WHITE);
  if (pushNow) display.display();
}

// ════════════════════════════════════════════════════════════
//  SECTION 5 — ANIMATION HELPERS
// ════════════════════════════════════════════════════════════

void MCAL_OLED_ScrollText(const String &text,
                    int y,
                    uint8_t size,
                    int delayMs,
                    int loops,
                    int stepPx)
{
  if (!s_oledReady) return;
  int charW   = MCAL_OLED_CHAR_WIDTH  * size;
  int charH   = MCAL_OLED_CHAR_HEIGHT * size;
  int textPxW = (int)text.length() * charW;
  int numChars= (int)text.length();

  display.setTextSize(size);
  display.setTextColor(MCAL_OLED_DEFAULT_TEXT_COLOR, SSD1306_BLACK);
  display.setTextWrap(false);

  int count = 0;
  // xPos = left edge of the full text string in screen coordinates.
  // Starts fully off the right, ends fully off the left.
  int xPos = SCREEN_WIDTH;

  while (loops == 0 || count < loops) {
    // Clear only the scroll row to avoid full-screen flicker
    display.fillRect(0, y, SCREEN_WIDTH, charH, SSD1306_BLACK);

    // Draw only the characters that are (at least partially) on screen.
    // This is the core clipping fix — we never let print() run off-bounds.
    for (int ci = 0; ci < numChars; ci++) {
      int cx = xPos + ci * charW;           // pixel X of this character
      if (cx + charW <= 0) continue;        // fully off left  — skip
      if (cx >= SCREEN_WIDTH)  continue;    // fully off right — skip
      display.setCursor(cx, y);
      display.print(text[ci]);              // draw one char at a time
    }

    display.display();
    delay(delayMs);

    xPos -= stepPx;

    // One full loop = text has scrolled completely off the left edge
    if (xPos <= -textPxW) {
      xPos = SCREEN_WIDTH;  // reset to right edge
      count++;
    }
  }
}
void MCAL_OLED_FadeIn(int steps, int delayMs) {
  if (!s_oledReady) return;
  for (int i = 0; i <= steps; i++) {
    uint8_t brightness = (uint8_t)(255 * i / steps);
    MCAL_OLED_SetBrightness(brightness);
    delay(delayMs);
  }
}
void MCAL_OLED_FadeOut(int steps, int delayMs) {
  if (!s_oledReady) return;
  for (int i = steps; i >= 0; i--) {
    uint8_t brightness = (uint8_t)(255 * i / steps);
    MCAL_OLED_SetBrightness(brightness);
    delay(delayMs);
  }
}
void MCAL_OLED_SlideInText(const String &text,
                     int targetX,
                     int targetY,
                     bool fromLeft,
                     uint8_t size,
                     int steps,
                     int delayMs,
                     EasingType easing)
{
  if (!s_oledReady) return;
  int charW  = MCAL_OLED_CHAR_WIDTH * size;
  int charH  = MCAL_OLED_CHAR_HEIGHT * size;
  int textW  = text.length() * charW;
  int startX = fromLeft ? -textW : SCREEN_WIDTH;

  display.setTextSize(size);
  display.setTextColor(MCAL_OLED_DEFAULT_TEXT_COLOR);

  for (int i = 0; i <= steps; i++) {
    float t = (float)i / steps;

    // Apply easing
    float eased;
    if      (easing == EASE_IN)  eased = t * t;
    else if (easing == EASE_OUT) eased = 1.0f - (1.0f - t) * (1.0f - t);
    else                          eased = t;

    int curX = startX + (int)((targetX - startX) * eased);

    display.fillRect(0, targetY, SCREEN_WIDTH, charH, SSD1306_BLACK);
    display.setCursor(curX, targetY);
    display.print(text);
    display.display();
    delay(delayMs);
  }
}
void MCAL_OLED_AnimateBitmap(const uint8_t *bitmap,
                       int bmpW, int bmpH,
                       int x0, int y0,
                       int x1, int y1,
                       int steps,
                       int delayMs,
                       EasingType easing)
{
  if (!s_oledReady) return;
  for (int i = 0; i <= steps; i++) {
    float t = (float)i / steps;
    float eased;
    if      (easing == EASE_IN)  eased = t * t;
    else if (easing == EASE_OUT) eased = 1.0f - (1.0f - t) * (1.0f - t);
    else                          eased = t;

    int cx = x0 + (int)((x1 - x0) * eased);
    int cy = y0 + (int)((y1 - y0) * eased);

    display.clearDisplay();
    display.drawBitmap(cx, cy, bitmap, bmpW, bmpH, SSD1306_WHITE);
    display.display();
    delay(delayMs);
  }
}
void MCAL_OLED_Blink(int times, int onMs, int offMs) {
  if (!s_oledReady) return;
  for (int i = 0; i < times; i++) {
    display.invertDisplay(true);
    delay(onMs);
    display.invertDisplay(false);
    delay(offMs);
  }
}
void MCAL_OLED_AnimateProgressBar(int x, int y, int w, int h,
                             float fromPct, float toPct,
                             int durationMs)
{
  if (!s_oledReady) return;
  const int steps = 40;
  int stepDelay   = durationMs / steps;
  for (int i = 0; i <= steps; i++) {
    float pct = fromPct + (toPct - fromPct) * (float)i / steps;
    display.fillRect(x, y, w, h + 2, SSD1306_BLACK); // clear area
    MCAL_OLED_DrawProgressBar(x, y, w, h, pct, false);
    display.display();
    delay(stepDelay);
  }
}
void MCAL_OLED_DrawSpinner(int cx, int cy, int r,
                     int frames, int delayMs)
{
  if (!s_oledReady) return;
  for (int f = 0; f < frames; f++) {
    display.clearDisplay();
    // Draw 8 dots around a circle, highlight one
    for (int d = 0; d < 8; d++) {
      float angle = (d * 45.0f + f * 15.0f) * PI / 180.0f;
      int px = cx + (int)(r * cos(angle));
      int py = cy + (int)(r * sin(angle));
      display.drawPixel(px, py, SSD1306_WHITE);
      // Make active dot bigger
      if (d == 0) {
        display.drawPixel(px + 1, py, SSD1306_WHITE);
        display.drawPixel(px, py + 1, SSD1306_WHITE);
      }
    }
    display.display();
    delay(delayMs);
  }
}
void MCAL_OLED_TypewriterText(const String &text,
                        int x, int y,
                        uint8_t size,
                        int delayMs)
{
  if (!s_oledReady) return;
  display.setTextSize(size);
  display.setTextColor(MCAL_OLED_DEFAULT_TEXT_COLOR);

  String drawn = "";
  int charW    = MCAL_OLED_CHAR_WIDTH * size;
  int charH    = MCAL_OLED_CHAR_HEIGHT * size;

  for (int i = 0; i < (int)text.length(); i++) {
    drawn += text[i];
    display.fillRect(x, y, SCREEN_WIDTH - x, charH, SSD1306_BLACK);
    display.setCursor(x, y);
    display.print(drawn);
    display.display();
    delay(delayMs);
  }
}

// ════════════════════════════════════════════════════════════
//  SECTION 6 — LAYOUTS / UI COMPONENTS
// ════════════════════════════════════════════════════════════

void MCAL_OLED_DrawStatusBar(const String &label, const String &value) {
  if (!s_oledReady) return;
  display.fillRect(0, 0, SCREEN_WIDTH, 9, SSD1306_BLACK);
  display.setTextSize(1);
  display.setTextColor(MCAL_OLED_DEFAULT_TEXT_COLOR);
  display.setCursor(0, 0);
  display.print(label);

  int valX = SCREEN_WIDTH - (value.length() * MCAL_OLED_CHAR_WIDTH);
  display.setCursor(valX, 0);
  display.print(value);

  display.drawLine(0, 9, SCREEN_WIDTH, 9, SSD1306_WHITE);
  display.display();
}
void MCAL_OLED_DrawMenu(const String items[], int count, int selected) {
  if (!s_oledReady) return;
  display.clearDisplay();
  for (int i = 0; i < count && i < 5; i++) {
    int y = i * 12;
    if (i == selected) {
      display.fillRect(0, y, SCREEN_WIDTH, 11, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setTextSize(1);
    display.setCursor(2, y + 2);
    display.print(items[i]);
  }
  display.display();
}
void MCAL_OLED_DrawBigNumber(const String &value,
                       const String &unit,
                       uint8_t numSize)
{
  if (!s_oledReady) return;
  display.clearDisplay();
  int numW = value.length() * MCAL_OLED_CHAR_WIDTH * numSize;
  int numH = MCAL_OLED_CHAR_HEIGHT * numSize;
  int numX = (SCREEN_WIDTH  - numW) / 2;
  int numY = unit.length() > 0
               ? (SCREEN_HEIGHT - numH - MCAL_OLED_CHAR_HEIGHT - 2) / 2
               : (SCREEN_HEIGHT - numH) / 2;

  display.setTextSize(numSize);
  display.setTextColor(MCAL_OLED_DEFAULT_TEXT_COLOR);
  display.setCursor(numX, numY);
  display.print(value);

  if (unit.length() > 0) {
    int unitW = unit.length() * MCAL_OLED_CHAR_WIDTH;
    display.setTextSize(1);
    display.setCursor((SCREEN_WIDTH - unitW) / 2, numY + numH + 2);
    display.print(unit);
  }
  display.display();
}
void MCAL_OLED_DrawConfirmDialog(const String &question, bool yesSelected) {
  if (!s_oledReady) return;
  display.clearDisplay();
  // Question text with wrapping
  display.setTextSize(1);
  display.setTextColor(MCAL_OLED_DEFAULT_TEXT_COLOR);
  _OLED_DrawWrapped(question, 0, 0, 1);

  // YES button
  if (yesSelected) {
    display.fillRect(10, 50, 40, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.drawRect(10, 50, 40, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_WHITE);
  }
  display.setCursor(18, 53);
  display.print("YES");

  // NO button
  if (!yesSelected) {
    display.fillRect(78, 50, 40, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.drawRect(78, 50, 40, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_WHITE);
  }
  display.setCursor(88, 53);
  display.print("NO");

  display.display();
}

// ════════════════════════════════════════════════════════════
//  SECTION 7 — UTILITY
// ════════════════════════════════════════════════════════════

int MCAL_OLED_CountLines(const String &text, uint8_t size) {
  int charW   = MCAL_OLED_CHAR_WIDTH * size;
  int maxCols = SCREEN_WIDTH / charW;
  int lines   = 1;
  int col     = 0;

  for (int i = 0; i < (int)text.length(); i++) {
    if (text[i] == '\n') { lines++; col = 0; continue; }
    if (col >= maxCols) {
      // find last space
      lines++; col = 0;
    }
    col++;
  }
  return lines;
}
bool MCAL_OLED_FitsOneLine(const String &text, uint8_t size) {
  return (int)text.length() <= (SCREEN_WIDTH / (MCAL_OLED_CHAR_WIDTH * size));
}

// ════════════════════════════════════════════════════════════
//  SECTION 8 — FULL DEMO
// ════════════════════════════════════════════════════════════

void MCAL_OLED_RunFullDemo(int holdMs) {

  // ── Helper lambda-style macro: show label, run demo, hold, clear ──
  // We show the function name at the top (small, row 0) then demo below.

  auto demoLabel = [&](const String &label) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    // Draw label in inverse bar so it's clearly a header
    display.fillRect(0, 0, SCREEN_WIDTH, 10, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    int lx = (SCREEN_WIDTH - (int)label.length() * MCAL_OLED_CHAR_WIDTH) / 2;
    display.setCursor(max(0, lx), 1);
    display.print(label);
    display.setTextColor(SSD1306_WHITE);
  };

  // ───────────────────────────────────────────
  // 1. MCAL_OLED_DrawText()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_DrawText");
  display.setTextSize(1);
  display.setCursor(0, 14);
  display.println("Size 1 text");
  display.setTextSize(2);
  display.setCursor(0, 26);
  display.println("Size 2");
  display.display();
  delay(holdMs);

  // ───────────────────────────────────────────
  // 2. MCAL_OLED_DrawText() — word wrap
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_DrawText wrap");
  MCAL_OLED_DrawText("This long sentence wraps automatically across lines.",
               0, 14, 1, SSD1306_WHITE, true, true);
  delay(holdMs);

  // ───────────────────────────────────────────
  // 3. MCAL_OLED_DrawAlignedText()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_DrawAlignedText");
  display.display(); // push header first
  MCAL_OLED_DrawAlignedText("LEFT",   1, ALIGN_LEFT,   1, false);
  MCAL_OLED_DrawAlignedText("CENTER", 3, ALIGN_CENTER, 1, false);
  MCAL_OLED_DrawAlignedText("RIGHT",  5, ALIGN_RIGHT,  1, true);
  delay(holdMs);

  // ───────────────────────────────────────────
  // 4. MCAL_OLED_DrawTitleScreen()
  // ───────────────────────────────────────────
  MCAL_OLED_DrawTitleScreen("MY APP", "MCAL_OLED_DrawTitleScreen");
  delay(holdMs);

  // ───────────────────────────────────────────
  // 5. MCAL_OLED_DrawBigNumber()
  // ───────────────────────────────────────────
  MCAL_OLED_DrawBigNumber("42", "MCAL_OLED_DrawBigNumber");
  delay(holdMs);

  // ───────────────────────────────────────────
  // 6. MCAL_OLED_DrawStatusBar()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_DrawStatusBar");
  display.display();
  delay(300);
  MCAL_OLED_DrawStatusBar("Temp", "36.5C");
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println("Label on left,");
  display.println("value on right.");
  display.display();
  delay(holdMs);

  // ───────────────────────────────────────────
  // 7. MCAL_OLED_DrawMenu()
  // ───────────────────────────────────────────
  {
    String menuItems[] = { "Settings", "WiFi", "Bluetooth", "About" };
    MCAL_OLED_DrawMenu(menuItems, 4, 0);
    delay(holdMs / 2);
    MCAL_OLED_DrawMenu(menuItems, 4, 2);
    delay(holdMs / 2);
  }

  // ───────────────────────────────────────────
  // 8. MCAL_OLED_DrawConfirmDialog()
  // ───────────────────────────────────────────
  MCAL_OLED_DrawConfirmDialog("MCAL_OLED_DrawConfirmDialog - Delete file?", true);
  delay(holdMs / 2);
  MCAL_OLED_DrawConfirmDialog("MCAL_OLED_DrawConfirmDialog - Delete file?", false);
  delay(holdMs / 2);

  // ───────────────────────────────────────────
  // 9. MCAL_OLED_DrawRect()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_DrawRect");
  display.display();
  MCAL_OLED_DrawRect(2,  14, 50, 20, false, false); // outline
  MCAL_OLED_DrawRect(70, 14, 50, 20, true,  true);  // filled
  delay(holdMs);

  // ───────────────────────────────────────────
  // 10. MCAL_OLED_DrawCircle()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_DrawCircle");
  display.display();
  MCAL_OLED_DrawCircle(32, 40, 18, false, false); // outline
  MCAL_OLED_DrawCircle(96, 40, 18, true,  true);  // filled
  delay(holdMs);

  // ───────────────────────────────────────────
  // 11. MCAL_OLED_DrawLine() + MCAL_OLED_DrawDivider()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_DrawLine/Divider");
  display.display();
  MCAL_OLED_DrawLine(0, 63, 127, 14, false);
  MCAL_OLED_DrawLine(0, 14, 127, 63, false);
  MCAL_OLED_DrawDivider(38, true);
  delay(holdMs);

  // ───────────────────────────────────────────
  // 12. MCAL_OLED_DrawProgressBar()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_DrawProgressBar");
  display.display();
  MCAL_OLED_DrawProgressBar(4, 18, 120, 10, 0.30f, false);
  MCAL_OLED_DrawProgressBar(4, 34, 120, 10, 0.65f, false);
  MCAL_OLED_DrawProgressBar(4, 50, 120, 10, 1.00f, true);
  delay(holdMs);

  // ───────────────────────────────────────────
  // 13. MCAL_OLED_AnimateProgressBar()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_AnimateProgressBar");
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 14);
  display.print("Filling bar...");
  display.display();
  MCAL_OLED_AnimateProgressBar(4, 40, 120, 12, 0.0f, 1.0f, holdMs);

  // ───────────────────────────────────────────
  // 14. MCAL_OLED_ScrollText()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_ScrollText");
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 14);
  display.print("Scrolling below:");
  display.display();
  MCAL_OLED_ScrollText(">> MCAL_OLED_ScrollText - pixel-clipped marquee! <<",
                 40, 1, 25, 1, 2);
  delay(300);

  // ───────────────────────────────────────────
  // 15. MCAL_OLED_SlideInText()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_SlideInText");
  display.display();
  MCAL_OLED_SlideInText("From Right", 10, 28, false, 1, 20, 15, EASE_OUT);
  delay(holdMs / 2);
  demoLabel("MCAL_OLED_SlideInText");
  display.display();
  MCAL_OLED_SlideInText("From Left!", 10, 28, true, 1, 20, 15, EASE_OUT);
  delay(holdMs / 2);

  // ───────────────────────────────────────────
  // 16. MCAL_OLED_TypewriterText()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_TypewriterText");
  display.display();
  MCAL_OLED_TypewriterText("Typing effect...", 0, 28, 1, 70);
  delay(holdMs / 2);

  // ───────────────────────────────────────────
  // 17. MCAL_OLED_DrawSpinner()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_DrawSpinner");
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(30, 50);
  display.print("Loading...");
  display.display();
  MCAL_OLED_DrawSpinner(64, 32, 12, 30, 50);

  // ───────────────────────────────────────────
  // 18. MCAL_OLED_FadeOut() + MCAL_OLED_FadeIn()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_FadeOut/FadeIn");
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 28);
  display.print("Watch brightness");
  display.display();
  delay(600);
  MCAL_OLED_FadeOut(20, 25);
  delay(200);
  MCAL_OLED_FadeIn(20, 25);
  delay(holdMs / 2);

  // ───────────────────────────────────────────
  // 19. MCAL_OLED_InvertDisplay()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_InvertDisplay");
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 28);
  display.print("Inverted display!");
  display.display();
  delay(500);
  MCAL_OLED_InvertDisplay(true);
  delay(holdMs / 2);
  MCAL_OLED_InvertDisplay(false);
  delay(300);

  // ───────────────────────────────────────────
  // 20. MCAL_OLED_Blink()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_Blink");
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 30);
  display.print("Blinking 3x...");
  display.display();
  delay(600);
  MCAL_OLED_Blink(3, 200, 200);

  // ───────────────────────────────────────────
  // 21. MCAL_OLED_FlipScreen()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_FlipScreen");
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 28);
  display.print("Flipping 180deg");
  display.display();
  delay(holdMs / 2);
  MCAL_OLED_FlipScreen(true);
  delay(holdMs / 2);
  MCAL_OLED_FlipScreen(false);
  delay(300);

  // ───────────────────────────────────────────
  // 22. MCAL_OLED_ClearRow()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_ClearRow");
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 14); display.println("Row 1: will stay");
  display.setCursor(0, 24); display.println("Row 2: will clear");
  display.setCursor(0, 34); display.println("Row 3: will stay");
  display.display();
  delay(holdMs / 2);
  display.fillRect(0, 24, SCREEN_WIDTH, 8, SSD1306_BLACK);
  display.display();
  delay(holdMs / 2);

  // ───────────────────────────────────────────
  // 23. MCAL_OLED_SetBrightness()
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_SetBrightness");
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 14);
  display.println("Dim -> Mid -> Full");
  display.display();
  MCAL_OLED_SetBrightness(20);  delay(600);
  MCAL_OLED_SetBrightness(128); delay(600);
  MCAL_OLED_SetBrightness(255); delay(600);

  // ───────────────────────────────────────────
  // 24. MCAL_OLED_CountLines() + MCAL_OLED_FitsOneLine() — Serial output demo
  // ───────────────────────────────────────────
  demoLabel("MCAL_OLED_CountLines/Fits");
  String testStr = "This string is quite long and will wrap";
  int    lines   = MCAL_OLED_CountLines(testStr, 1);
  bool   fits    = MCAL_OLED_FitsOneLine(testStr, 1);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 14);
  display.print("Text needs ");
  display.print(lines);
  display.println(" lines");
  display.print("Fits 1 line: ");
  display.println(fits ? "YES" : "NO");
  display.display();
  delay(holdMs);

  // ───────────────────────────────────────────
  // Done!
  // ───────────────────────────────────────────
  MCAL_OLED_DrawTitleScreen("DEMO DONE", "All functions shown");
  delay(holdMs);
  MCAL_OLED_Clear();
}
void MCAL_OLED_PrintDiag() {
  Serial.println("=== SSD1306 OLED DIAG ===");
  Serial.printf("Screen:  %d x %d px\n", SCREEN_WIDTH, SCREEN_HEIGHT);
  Serial.printf("I2C SDA: GPIO%d  SCL: GPIO%d\n", I2C_SDA, I2C_SCL);
  Serial.printf("Address: 0x%02X\n", SCREEN_ADDRESS);
  Serial.println("========================");
}
