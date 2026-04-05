#include <Arduino.h>
#include <time.h>
#include <WiFi.h>
#include "config.h"
#include "display.h"
#include "net.h"

// ═════════════════════════════════════════════════════════════════════════════
//  main.cpp — Application entry point and main loop
//
//  This is the "glue" file. It owns the two main objects (display + net),
//  coordinates startup, handles the physical button, and drives the clock
//  face redraws on a timer.
//
//  Arduino framework always looks for exactly two functions:
//    setup()  — runs once when the device powers on or resets
//    loop()   — runs repeatedly, forever, after setup() finishes
// ═════════════════════════════════════════════════════════════════════════════

// ── Module instances ──────────────────────────────────────────────────────────
// These are global so both setup() and loop() can use them.
DisplayManager display;
NetManager     net;

// ── Runtime state ─────────────────────────────────────────────────────────────
uint32_t lastClockUpdate = 0;   // millis() of last screen redraw
uint32_t lastActivity    = 0;   // millis() of last button press (for screensaver)
bool     ntpOK           = false; // true once we have a valid NTP time

// ── Button state (for edge detection) ─────────────────────────────────────────
// We track the previous reading so we can detect the moment the button is
// pressed (HIGH→LOW) and released (LOW→HIGH) rather than just "is it pressed".
static bool     btnPrev     = HIGH;
static uint32_t btnPressAt  = 0;      // millis() when button went LOW
static bool     longHandled = false;  // prevents long-press firing multiple times


// ─────────────────────────────────────────────────────────────────────────────
//  displayOTAProgress()
//
//  Called by net.cpp during a firmware download. The 'extern' declaration in
//  net.cpp connects the call in that file to this function here.
//  This way net.cpp can update the screen without #including display.h.
// ─────────────────────────────────────────────────────────────────────────────
void displayOTAProgress(int pct) {
  display.showOTAProgress(pct);
}


// ─────────────────────────────────────────────────────────────────────────────
//  handleButton()
//
//  Reads GPIO BOOT_PIN and implements two behaviours:
//    • Short press (< 2 seconds): cycle to the next clock face
//    • Long press  (≥ 2 seconds): erase saved WiFi credentials and reboot
//                                 (useful if you move to a new network)
//
//  Called every loop() iteration, so it reacts within ~10ms.
// ─────────────────────────────────────────────────────────────────────────────
void handleButton() {
  bool cur = digitalRead(BOOT_PIN);

  // ── Falling edge: button just pressed ─────────────────────────────────────
  if (cur == LOW && btnPrev == HIGH) {
    btnPressAt   = millis();
    longHandled  = false;
    lastActivity = millis();  // reset screensaver timer on any button touch
  }

  // ── Still held: check for long-press threshold ─────────────────────────────
  if (cur == LOW && !longHandled && (millis() - btnPressAt > 2000)) {
    longHandled = true;
    display.showMessage("WiFi reset!", "Rebooting...");
    delay(1500);
    WiFi.disconnect(/*wifioff=*/true, /*eraseCredentials=*/true);
    delay(200);
    ESP.restart();
  }

  // ── Rising edge: button just released ─────────────────────────────────────
  if (cur == HIGH && btnPrev == LOW) {
    uint32_t heldMs = millis() - btnPressAt;
    if (!longHandled && heldMs < 2000) {
      // Short press: advance to the next clock face
      display.nextFace();
    }
  }

  btnPrev = cur;
}


// ─────────────────────────────────────────────────────────────────────────────
//  setup()
//
//  Runs once at power-on. Order matters:
//    1. Serial    — for debug output (open Serial Monitor to see logs)
//    2. Display   — must be ready before we show any status screens
//    3. Network   — blocks until WiFi connected or portal times out
//    4. NTP       — only useful after network is up
//    5. OTA check — first check on boot; then repeats every 6 hours
// ─────────────────────────────────────────────────────────────────────────────
void setup() {
  // Serial at 115200 baud — view with: pio device monitor
  Serial.begin(115200);
  delay(500);  // small pause so the Serial monitor has time to open
  Serial.println("\n\n=== Smart Clock v" FIRMWARE_VERSION " ===");

  // Configure the BOOT button pin as input with internal pull-up resistor.
  // With INPUT_PULLUP the pin reads HIGH when not pressed, LOW when pressed.
  pinMode(BOOT_PIN, INPUT_PULLUP);

  // ── Initialise OLED ───────────────────────────────────────────────────────
  if (!display.begin()) {
    // If the display doesn't respond, we have no screen to show errors on.
    // Blink the LED rapidly as a visual distress signal instead.
    Serial.println("[ERROR] OLED not found. Check SDA/SCL wiring and I2C address.");
    pinMode(LED_PIN, OUTPUT);
    while (true) {
      digitalWrite(LED_PIN, LOW);  delay(150);
      digitalWrite(LED_PIN, HIGH); delay(150);
    }
  }

  display.showBootScreen();
  delay(1500);

  // ── Connect to WiFi ───────────────────────────────────────────────────────
  // If this is the first boot (no saved credentials), or the saved network is
  // unavailable, net.connect() will open a hotspot named "SmartClock-Setup".
  // Connect to it from your phone and follow the captive portal to enter WiFi.
  bool connected = net.connect();

  if (connected) {
    display.showConnected(net.getIP().c_str());
    delay(1000);

    // Sync time from NTP servers
    ntpOK = net.syncNTP();
    if (!ntpOK) {
      display.showError("NTP sync failed");
      delay(1500);
      // Not fatal — the clock will show boot-elapsed time as a fallback
    }

    // Check for a firmware update on first boot
    net.checkOTA();

  } else {
    // No WiFi — the clock still works but shows elapsed time instead of real time
    display.showMessage("No WiFi", "Offline mode");
    delay(2000);

    // Set the internal clock to "zero" so elapsed-time fallback works correctly
    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
  }

  lastActivity = millis();
  Serial.println("[MAIN] Setup complete. Entering main loop.");
}


// ─────────────────────────────────────────────────────────────────────────────
//  loop()
//
//  Runs continuously. Structured as a simple event-driven system:
//    • Button events  — checked every iteration (~10ms)
//    • Screensaver    — checked every iteration (compare millis)
//    • Clock redraw   — every CLOCK_UPDATE_MS (default 500ms)
//    • Network tasks  — delegated to net.loop() (reconnect + OTA timer)
// ─────────────────────────────────────────────────────────────────────────────
void loop() {
  handleButton();   // ~1ms: read GPIO, act on press/release edges
  net.loop();       // ~1ms normally; long during reconnect or OTA download

  uint32_t now = millis();

  // ── Screensaver ────────────────────────────────────────────────────────────
  if (SCREENSAVER_MS > 0 && (now - lastActivity) > SCREENSAVER_MS) {
    display.setDimmed(true);
  } else {
    display.setDimmed(false);
  }

  // ── Clock face redraw ──────────────────────────────────────────────────────
  if (now - lastClockUpdate >= CLOCK_UPDATE_MS) {
    lastClockUpdate = now;

    struct tm timeinfo;
    bool haveTime = getLocalTime(&timeinfo, /*timeout_ms=*/0);

    if (haveTime) {
      // Normal case: we have a valid, NTP-synced time
      ntpOK = true;
      display.update(&timeinfo);

    } else if (net.isConnected()) {
      // Connected but NTP hasn't replied yet — show a waiting message
      display.showMessage("Syncing time...", NTP_SERVER1);

    } else {
      // Offline fallback: show elapsed seconds since boot as HH:MM:SS
      // This lets the display be useful even without a network.
      uint32_t sec = now / 1000;
      struct tm fake = {};
      fake.tm_hour = (sec / 3600) % 24;
      fake.tm_min  = (sec /   60) % 60;
      fake.tm_sec  =  sec         % 60;
      fake.tm_mday = 1;    // placeholder date
      fake.tm_wday = 0;    // Sunday
      display.update(&fake);
    }
  }

  // Small yield so the ESP32's background WiFi/Bluetooth stack gets CPU time.
  // Without this delay() the watchdog timer may trigger a reboot.
  delay(10);
}
