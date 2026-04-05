#include "display.h"
#include <Wire.h>
#include <math.h>

// ── Custom large font digits (3×5 pixel bitmap, scaled up) ──────────────────
// We use the built-in Adafruit GFX large font via setTextSize(4) for digital,
// and a hand-drawn analog face. No external font files needed.

DisplayManager::DisplayManager()
  : _oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET)
{}

bool DisplayManager::begin() {
  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);   // 400 kHz fast mode

  if (!_oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    return false;
  }
  _oled.setRotation(0);
  _oled.ssd1306_command(SSD1306_SETCONTRAST);
  _oled.ssd1306_command(_brightness);
  _oled.clearDisplay();
  _oled.display();
  return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Public: update clock
// ─────────────────────────────────────────────────────────────────────────────
void DisplayManager::update(struct tm *t) {
  _oled.clearDisplay();
  switch (_face) {
    case FACE_DIGITAL: _drawDigital(t); break;
    case FACE_ANALOG:  _drawAnalog(t);  break;
    case FACE_MINIMAL: _drawMinimal(t); break;
    default: break;
  }
  _oled.display();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Face: DIGITAL
//  Layout: HH:MM large (top), seconds bar, date + day (bottom)
// ─────────────────────────────────────────────────────────────────────────────
void DisplayManager::_drawDigital(struct tm *t) {
  char buf[16];

  // ── Big time ───────────────────────────────
  snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
  _oled.setTextSize(3);
  _oled.setTextColor(SSD1306_WHITE);
  // Centre: size-3 char ≈ 18px wide, 24px tall. "HH:MM" = 5 chars × 18 = 90px
  int16_t tw = 5 * 18;
  int16_t tx = (OLED_WIDTH - tw) / 2;
  _oled.setCursor(tx, 4);
  _oled.print(buf);

  // ── Seconds progress bar ───────────────────
  int barY  = 34;
  int barW  = OLED_WIDTH - 4;
  int fillW = map(t->tm_sec, 0, 59, 0, barW);
  _oled.drawRect(2, barY, barW, 5, SSD1306_WHITE);
  if (fillW > 0) _oled.fillRect(2, barY, fillW, 5, SSD1306_WHITE);

  // Blinking colon overlay (flash every second)
  // Already part of the string, but we can blink the colon:
  if (t->tm_sec % 2 == 0) {
    // Erase colon pixels (overwrite with black rect)
    // colon starts at tx + 2 chars × 18 = tx+36, width ≈ 18px, height 24
    _oled.fillRect(tx + 36, 4, 18, 24, SSD1306_BLACK);
    // Draw two dots
    _oled.fillCircle(tx + 44, 13, 2, SSD1306_WHITE);
    _oled.fillCircle(tx + 44, 22, 2, SSD1306_WHITE);
  }

  // ── Date row ───────────────────────────────
  const char *days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  const char *mons[] = {"Jan","Feb","Mar","Apr","May","Jun",
                         "Jul","Aug","Sep","Oct","Nov","Dec"};
  snprintf(buf, sizeof(buf), "%s %02d %s",
           days[t->tm_wday], t->tm_mday, mons[t->tm_mon]);
  _oled.setTextSize(1);
  _centerStr(buf, 44);

  // ── Seconds readout ────────────────────────
  snprintf(buf, sizeof(buf), ":%02d", t->tm_sec);
  _oled.setTextSize(1);
  _oled.setCursor(OLED_WIDTH - 18, 44);
  _oled.print(buf);

  // ── Bottom separator ───────────────────────
  _oled.drawFastHLine(0, 42, OLED_WIDTH, SSD1306_WHITE);

  // ── WiFi indicator (top-right corner, tiny dot) ─
  _oled.fillCircle(OLED_WIDTH - 4, 4, 2, SSD1306_WHITE);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Face: ANALOG
//  Draws a clock face with tick marks, hour/min/sec hands, and date at bottom
// ─────────────────────────────────────────────────────────────────────────────
void DisplayManager::_drawAnalog(struct tm *t) {
  const int cx = 36, cy = 32, r = 30;   // clock face, left-aligned

  // Face circle
  _oled.drawCircle(cx, cy, r, SSD1306_WHITE);
  _oled.drawCircle(cx, cy, r - 1, SSD1306_WHITE);  // double ring

  // Tick marks
  for (int i = 0; i < 60; i++) {
    float ang  = i * 6.0f * DEG_TO_RAD;
    float sinA = sin(ang), cosA = cos(ang);
    int   len  = (i % 5 == 0) ? 4 : 2;
    int   x1   = cx + (r - 1)   * sinA;
    int   y1   = cy - (r - 1)   * cosA;
    int   x2   = cx + (r - len) * sinA;
    int   y2   = cy - (r - len) * cosA;
    _oled.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
  }

  // Hour numerals (12, 3, 6, 9)
  _oled.setTextSize(1);
  const struct { int h; int dx; int dy; } nums[] = {
    {12, -3, -r+6}, {3, r-10, -3}, {6, -3, r-10}, {9, -r+5, -3}
  };
  for (auto &n : nums) {
    _oled.setCursor(cx + n.dx, cy + n.dy);
    _oled.print(n.h);
  }

  // Hour hand
  float hAng = ((t->tm_hour % 12) * 60 + t->tm_min) / 2.0f * DEG_TO_RAD;
  _drawAnalogHand(cx, cy, hAng, r - 12, 3, SSD1306_WHITE);

  // Minute hand
  float mAng = (t->tm_min * 60 + t->tm_sec) / 10.0f * DEG_TO_RAD;
  _drawAnalogHand(cx, cy, mAng, r - 6, 2, SSD1306_WHITE);

  // Second hand (thin)
  float sAng = t->tm_sec * 6.0f * DEG_TO_RAD;
  int   sx2  = cx + (r - 4) * sin(sAng);
  int   sy2  = cy - (r - 4) * cos(sAng);
  int   sx1  = cx - 8 * sin(sAng);  // counterbalance tail
  int   sy1  = cy + 8 * cos(sAng);
  _oled.drawLine(sx1, sy1, sx2, sy2, SSD1306_WHITE);

  // Centre dot
  _oled.fillCircle(cx, cy, 2, SSD1306_WHITE);

  // ── Right panel: time + date ───────────────
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
  _oled.setTextSize(2);
  _oled.setCursor(78, 8);
  _oled.print(buf);

  snprintf(buf, sizeof(buf), ":%02d", t->tm_sec);
  _oled.setTextSize(1);
  _oled.setCursor(78, 30);
  _oled.print(buf);

  const char *days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  const char *mons[] = {"Jan","Feb","Mar","Apr","May","Jun",
                         "Jul","Aug","Sep","Oct","Nov","Dec"};
  snprintf(buf, sizeof(buf), "%s", days[t->tm_wday]);
  _oled.setCursor(78, 42);
  _oled.print(buf);

  snprintf(buf, sizeof(buf), "%d %s", t->tm_mday, mons[t->tm_mon]);
  _oled.setCursor(78, 53);
  _oled.print(buf);
}

void DisplayManager::_drawAnalogHand(int cx, int cy, float angle,
                                      int len, uint8_t thick, uint16_t col) {
  float s = sin(angle), c = cos(angle);
  int x2 = cx + len * s;
  int y2 = cy - len * c;
  _oled.drawLine(cx, cy, x2, y2, col);
  if (thick >= 2) {
    // Draw adjacent pixels for thickness
    _oled.drawLine(cx + 1, cy, x2 + 1, y2, col);
    _oled.drawLine(cx, cy + 1, x2, y2 + 1, col);
  }
  if (thick >= 3) {
    _oled.drawLine(cx - 1, cy, x2 - 1, y2, col);
    _oled.drawLine(cx, cy - 1, x2, y2 - 1, col);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Face: MINIMAL
//  Huge hour + tiny minutes, full date below, seconds dot row
// ─────────────────────────────────────────────────────────────────────────────
void DisplayManager::_drawMinimal(struct tm *t) {
  char buf[16];

  // Giant hour
  snprintf(buf, sizeof(buf), "%02d", t->tm_hour);
  _oled.setTextSize(4);
  _oled.setTextColor(SSD1306_WHITE);
  _oled.setCursor(4, 2);
  _oled.print(buf);

  // Minute (top-right, medium)
  snprintf(buf, sizeof(buf), "%02d", t->tm_min);
  _oled.setTextSize(2);
  _oled.setCursor(82, 2);
  _oled.print(buf);

  // Colon between them
  _oled.fillCircle(77, 14, 2, SSD1306_WHITE);
  _oled.fillCircle(77, 24, 2, SSD1306_WHITE);

  // Seconds (tiny)
  snprintf(buf, sizeof(buf), ":%02d", t->tm_sec);
  _oled.setTextSize(1);
  _oled.setCursor(82, 22);
  _oled.print(buf);

  // Divider
  _oled.drawFastHLine(0, 38, OLED_WIDTH, SSD1306_WHITE);

  // Date full
  const char *days[] = {"Sunday","Monday","Tuesday","Wednesday",
                          "Thursday","Friday","Saturday"};
  const char *mons[] = {"Jan","Feb","Mar","Apr","May","Jun",
                         "Jul","Aug","Sep","Oct","Nov","Dec"};
  snprintf(buf, sizeof(buf), "%d %s", t->tm_mday, mons[t->tm_mon]);
  _oled.setTextSize(1);
  _centerStr(buf, 42);

  // Day name (scroll if too long)
  _centerStr(days[t->tm_wday], 53);

  // Second progress dots across bottom
  int dotTotal = 12;
  int dotStep  = 60 / dotTotal;
  int dotOn    = t->tm_sec / dotStep;
  for (int i = 0; i < dotTotal; i++) {
    int dx = 8 + i * 10;
    if (i <= dotOn)
      _oled.fillCircle(dx, 61, 2, SSD1306_WHITE);
    else
      _oled.drawCircle(dx, 61, 2, SSD1306_WHITE);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Status / info screens
// ─────────────────────────────────────────────────────────────────────────────
void DisplayManager::showBootScreen() {
  _oled.clearDisplay();
  _oled.setTextColor(SSD1306_WHITE);
  _oled.setTextSize(1);
  _centerStr("Smart Clock", 10);
  _oled.setTextSize(2);
  _centerStr("v" FIRMWARE_VERSION, 24);
  _oled.setTextSize(1);
  _centerStr("ESP32-C3 Super Mini", 46);
  _centerStr("Initialising...", 56);
  _oled.display();
}

void DisplayManager::showWiFiSetup(const char *apName, const char *apPass) {
  _oled.clearDisplay();
  _oled.setTextColor(SSD1306_WHITE);
  _oled.setTextSize(1);
  _centerStr("WiFi Setup", 0);
  _oled.drawFastHLine(0, 10, OLED_WIDTH, SSD1306_WHITE);
  _centerStr("Connect to:", 14);
  _oled.setTextSize(1);
  _centerStr(apName, 26);
  _centerStr("Password:", 38);
  _centerStr(apPass, 49);
  // QR-code hint
  _centerStr("192.168.4.1", 58);
  _oled.display();
}

void DisplayManager::showConnecting(const char *ssid) {
  _oled.clearDisplay();
  _oled.setTextColor(SSD1306_WHITE);
  _oled.setTextSize(1);
  _centerStr("Connecting...", 16);
  _centerStr(ssid, 30);
  _oled.display();
}

void DisplayManager::showConnected(const char *ip) {
  _oled.clearDisplay();
  _oled.setTextColor(SSD1306_WHITE);
  _oled.setTextSize(1);
  _centerStr("Connected!", 16);
  _centerStr(ip, 30);
  _centerStr("Syncing time...", 44);
  _oled.display();
}

void DisplayManager::showOTAProgress(int pct) {
  _oled.clearDisplay();
  _oled.setTextColor(SSD1306_WHITE);
  _oled.setTextSize(1);
  _centerStr("Updating firmware", 10);
  // Progress bar
  _oled.drawRect(10, 28, OLED_WIDTH - 20, 12, SSD1306_WHITE);
  int fill = map(pct, 0, 100, 0, OLED_WIDTH - 24);
  if (fill > 0) _oled.fillRect(12, 30, fill, 8, SSD1306_WHITE);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  _centerStr(buf, 46);
  _oled.display();
}

void DisplayManager::showError(const char *msg) {
  _oled.clearDisplay();
  _oled.setTextColor(SSD1306_WHITE);
  _oled.setTextSize(1);
  _centerStr("! ERROR !", 16);
  _centerStr(msg, 36);
  _oled.display();
}

void DisplayManager::showMessage(const char *line1, const char *line2) {
  _oled.clearDisplay();
  _oled.setTextColor(SSD1306_WHITE);
  _oled.setTextSize(1);
  _centerStr(line1, line2 ? 22 : 28);
  if (line2) _centerStr(line2, 38);
  _oled.display();
}

// ─────────────────────────────────────────────────────────────────────────────
//  Controls
// ─────────────────────────────────────────────────────────────────────────────
void DisplayManager::nextFace() {
  _face = (ClockFace)(((int)_face + 1) % FACE_COUNT);
}

void DisplayManager::setBrightness(uint8_t level) {
  _brightness = level;
  _oled.ssd1306_command(SSD1306_SETCONTRAST);
  _oled.ssd1306_command(_dimmed ? max(0, (int)level - 180) : level);
}

void DisplayManager::setDimmed(bool dim) {
  _dimmed = dim;
  setBrightness(_brightness);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────
void DisplayManager::_centerStr(const char *s, int y, uint8_t size) {
  _oled.setTextSize(size);
  int16_t x1, y1; uint16_t w, h;
  _oled.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  _oled.setCursor((OLED_WIDTH - w) / 2, y);
  _oled.print(s);
}
