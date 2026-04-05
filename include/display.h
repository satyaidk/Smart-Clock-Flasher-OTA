#pragma once
#include <Adafruit_SSD1306.h>
#include "config.h"

// ─────────────────────────────────────────────────────────────────────────────
//  ClockFace — selects which clock style is rendered on the OLED
//
//  The user cycles through these by pressing the BOOT button.
// ─────────────────────────────────────────────────────────────────────────────
enum ClockFace {
  FACE_DIGITAL = 0,   // Large HH:MM, seconds bar, date row
  FACE_ANALOG  = 1,   // Classic round clock with hands and tick marks
  FACE_MINIMAL = 2,   // Giant hour + medium minutes + dot-row seconds
  FACE_COUNT   = 3    // Always keep this last — used for wrap-around cycling
};

// ─────────────────────────────────────────────────────────────────────────────
//  DisplayManager — all OLED output goes through this class
//
//  It owns the Adafruit_SSD1306 driver and exposes two kinds of methods:
//    • Clock faces   — called every 500ms from the main loop
//    • Status screens — called during setup, OTA, and error states
//
//  Usage (from main.cpp):
//    DisplayManager display;
//    display.begin();             // call once in setup()
//    display.showBootScreen();    // splash screen
//    display.update(&timeinfo);   // call in loop() to draw the clock
// ─────────────────────────────────────────────────────────────────────────────
class DisplayManager {
public:
  DisplayManager();

  // Initialises the OLED hardware. Returns false if the display is not found
  // (check wiring and I²C address in config.h).
  bool begin();

  // Redraws the active clock face. Call every CLOCK_UPDATE_MS milliseconds.
  // 't' is a standard C time struct populated by getLocalTime().
  void update(struct tm *t);

  // ── Status / info screens (full-screen, blocking) ─────────────────────────
  void showBootScreen();                              // Splash on power-on
  void showWiFiSetup(const char *apName,
                     const char *apPass);            // Hotspot setup instructions
  void showConnecting(const char *ssid);             // "Connecting to …"
  void showConnected(const char *ip);                // IP address confirmation
  void showOTAProgress(int pct);                     // Firmware update progress bar
  void showError(const char *msg);                   // Error message
  void showMessage(const char *line1,
                   const char *line2 = nullptr);     // Generic two-line message

  // ── Controls ──────────────────────────────────────────────────────────────

  // Advances to the next clock face (wraps around after the last one).
  void nextFace();

  // Sets OLED contrast (0 = off, 255 = maximum brightness).
  void setBrightness(uint8_t level);

  // Dims the screen (screensaver). When true, reduces contrast significantly.
  void setDimmed(bool dim);

  // Returns which face is currently active.
  ClockFace getFace() const { return _face; }

private:
  Adafruit_SSD1306 _oled;               // The hardware driver
  ClockFace        _face      = FACE_DIGITAL;
  bool             _dimmed    = false;
  uint8_t          _brightness = 200;   // Current target brightness (0-255)

  // ── Clock face renderers (called by update()) ─────────────────────────────
  void _drawDigital(struct tm *t);
  void _drawAnalog(struct tm *t);
  void _drawMinimal(struct tm *t);

  // ── Shared drawing helpers ─────────────────────────────────────────────────
  // Draws a thick analog clock hand from centre (cx,cy) at 'angle' radians.
  void _drawAnalogHand(int cx, int cy, float angle,
                       int len, uint8_t thick, uint16_t col);

  // Prints text centred horizontally at vertical position y.
  void _centerStr(const char *s, int y, uint8_t size = 1);
};
