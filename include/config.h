#pragma once

// ═════════════════════════════════════════════════════════════════════════════
//  config.h — Central configuration for the Smart Clock
//
//  This is the ONE file you edit when customising the clock.
//  Every setting lives here so you never have to dig into source files.
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────
//  Hardware — ESP32-C3 Super Mini + SSD1306 OLED
// ─────────────────────────────────────────────

// I²C wiring (see README wiring table)
#define OLED_SDA        20     // GPIO20 → OLED SDA pin
#define OLED_SCL        21     // GPIO21 → OLED SCL pin

// OLED screen dimensions (pixels)
#define OLED_WIDTH      128
#define OLED_HEIGHT     64

// I²C address of the SSD1306 OLED.
// Most modules use 0x3C. If the screen is blank, try 0x3D.
#define OLED_ADDR       0x3C
#define OLED_RESET      -1     // -1 = no reset pin connected

// Onboard RGB LED on the C3 Super Mini (active LOW = turns on when pin is LOW)
// Set to -1 to disable LED usage entirely.
#define LED_PIN         8
#define LED_ACTIVE_LOW  true

// Physical BOOT button (used as the UI button in firmware)
#define BOOT_PIN        9


// ─────────────────────────────────────────────
//  WiFi Setup (Captive Portal)
//
//  When no WiFi is configured the clock creates its own hotspot.
//  Connect to it with your phone to enter your home WiFi details.
// ─────────────────────────────────────────────
#define AP_NAME         "SmartClock-Setup"   // Hotspot name you see on your phone
#define AP_PASSWORD     "clocksetup"         // Hotspot password (min 8 chars)
#define PORTAL_TIMEOUT  180                  // Seconds before hotspot auto-closes


// ─────────────────────────────────────────────
//  NTP Time Servers
//
//  The clock fetches the current time from these internet servers.
//  Three servers are tried in order for reliability.
// ─────────────────────────────────────────────
#define NTP_SERVER1     "pool.ntp.org"
#define NTP_SERVER2     "time.nist.gov"
#define NTP_SERVER3     "time.google.com"

// Timezone offset from UTC in seconds.
// Examples (pick yours or enter a custom value):
//   UTC        =      0   (United Kingdom, standard time)
//   IST India  =  19800   (UTC +5:30 → 5×3600 + 30×60)
//   EST USA    = -18000   (UTC -5)
//   PST USA    = -28800   (UTC -8)
//   CET Europe =   3600   (UTC +1)
//   JST Japan  =  32400   (UTC +9)
//
//  This value is the DEFAULT. The user can change it via the WiFi setup
//  portal and it will be saved to flash memory automatically.
#define TZ_OFFSET_SEC   19800   // ← change to your region
#define DST_OFFSET_SEC  0       // Daylight saving extra offset (usually 0 or 3600)


// ─────────────────────────────────────────────
//  OTA (Over-The-Air) Firmware Updates
//
//  The clock can update itself automatically when a new version is
//  available on your server. You need to host two files:
//    • version.json  — tells the clock what the latest version number is
//    • firmware.bin  — the new firmware binary
//
//  HOW TO SET UP:
//    1. Host these files somewhere publicly accessible (GitHub Releases,
//       any web server, S3 bucket, etc.)
//    2. Replace the URL below with your actual version.json URL.
//    3. Keep version.json updated whenever you release new firmware.
//
//  version.json format:
//    {
//      "version": "1.0.1",
//      "firmware_url": "https://your-server.com/firmware.bin"
//    }
//
//  ⚠️  IMPORTANT: URL must use HTTPS for security. HTTP will also work
//      but your firmware would be transferred unencrypted.
//
//  Leave as-is to disable OTA (the fetch will fail silently every 6 hours).
// ─────────────────────────────────────────────
#define FIRMWARE_VERSION    "1.0.0"
// TODO: Replace this with your real version.json URL before deploying.
#define OTA_VERSION_URL     "https://your-domain.com/clock/version.json"
#define OTA_CHECK_INTERVAL  (6UL * 60 * 60 * 1000)   // Check every 6 hours


// ─────────────────────────────────────────────
//  Display Timing
// ─────────────────────────────────────────────

// How often the clock face redraws (milliseconds).
// 500ms = twice per second, which is enough to feel smooth.
#define CLOCK_UPDATE_MS     500

// After this many milliseconds of no button presses, the screen dims.
// Set to 0 to keep the screen always at full brightness.
#define SCREENSAVER_MS      60000   // 60 seconds

// Speed of any scrolling text animations.
#define SCROLL_SPEED_MS     80      // ms per scroll step
