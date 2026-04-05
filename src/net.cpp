#include "net.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>   // FIX: use secure client so HTTPS OTA URLs work
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

// ─────────────────────────────────────────────────────────────────────────────
//  OTA progress display
//
//  This function is defined in main.cpp and drives the OLED progress bar.
//  We declare it here with 'extern' so net.cpp can call it without creating
//  a circular dependency between the two modules.
// ─────────────────────────────────────────────────────────────────────────────
extern void displayOTAProgress(int pct);

// ─────────────────────────────────────────────────────────────────────────────
//  NVS (flash) storage namespace
//
//  The ESP32 has a small key-value store in flash called NVS (Non-Volatile
//  Storage). We use the "clock" namespace to persist user settings across
//  reboots — specifically the timezone offset set via the captive portal.
// ─────────────────────────────────────────────────────────────────────────────
static Preferences prefs;


// ─────────────────────────────────────────────────────────────────────────────
NetManager::NetManager() {}


// ─────────────────────────────────────────────────────────────────────────────
//  connect()
//
//  Flow:
//    1. Open NVS and read the last saved timezone offset (default = TZ_OFFSET_SEC).
//    2. Start WiFiManager. If credentials are saved it connects automatically.
//       If not, it opens a hotspot + captive portal so the user can enter them.
//    3. If the user changed the timezone in the portal, save the new value.
//    4. Call configTime() to tell the ESP32 which NTP servers to use and
//       what timezone offset to apply.
//
//  BUG FIXED: Previously prefs.end() was called before reading the saved TZ
//  value, causing a read from a closed NVS handle (returned garbage/default).
//  Now we capture the resolved TZ into a local variable BEFORE closing NVS.
// ─────────────────────────────────────────────────────────────────────────────
bool NetManager::connect() {
  WiFiManager wm;
  wm.setConfigPortalTimeout(PORTAL_TIMEOUT);
  wm.setConnectTimeout(20);
  wm.setConnectRetries(3);
  wm.setCleanConnect(true);

  // ── Read saved timezone from NVS ─────────────────────────────────────────
  prefs.begin("clock", /*readOnly=*/false);
  int savedTZ = prefs.getInt("tz", TZ_OFFSET_SEC);   // default = config value

  // Convert to string for the portal text field
  char tzBuf[12];
  snprintf(tzBuf, sizeof(tzBuf), "%d", savedTZ);

  // ── Add a custom "Timezone" field to the WiFiManager portal form ──────────
  // The user sees a text box labelled "Timezone offset (seconds from UTC)".
  // They can leave it as-is or type a new value (e.g. 19800 for IST).
  WiFiManagerParameter tzParam(
    "tz",                                  // key (used to retrieve value)
    "Timezone offset (seconds from UTC)",  // label shown in the portal
    tzBuf,                                 // pre-filled current value
    10                                     // max input length
  );
  wm.addParameter(&tzParam);

  // ── Attempt connection ────────────────────────────────────────────────────
  Serial.println("[NET] Starting WiFiManager...");
  bool ok = wm.autoConnect(AP_NAME, AP_PASSWORD);

  // ── Resolve and save timezone ─────────────────────────────────────────────
  // We resolve the timezone NOW, before closing NVS, so we have a valid value
  // regardless of whether the user changed it in the portal.
  //
  // Priority: value from portal field > previously saved value > compile-time default
  long resolvedTZ = savedTZ;   // start with what was saved

  if (ok) {
    _connected = true;
    Serial.printf("[NET] Connected! IP: %s\n", WiFi.localIP().toString().c_str());

    // Check if the user typed a new timezone in the portal
    String portalTZStr = String(tzParam.getValue());
    if (portalTZStr.length() > 0) {
      long portalTZ = portalTZStr.toInt();
      if (portalTZ != savedTZ) {
        prefs.putInt("tz", (int)portalTZ);
        Serial.printf("[NET] New timezone saved: %ld s\n", portalTZ);
        resolvedTZ = portalTZ;   // use the new value immediately
      }
    }
  } else {
    Serial.println("[NET] WiFiManager timed out — running offline.");
    _connected = false;
  }

  // ── Close NVS AFTER we have finished reading and writing ──────────────────
  // BUG FIX: the original code called prefs.end() before reading the TZ,
  // then called prefs.getInt() on a closed handle. We close LAST.
  prefs.end();

  // ── Configure NTP (even offline; will sync once connection is restored) ────
  // configTime() sets the global POSIX timezone.
  // Arguments: gmtOffset_sec, daylightOffset_sec, ntp_server_1, ...
  configTime(resolvedTZ, DST_OFFSET_SEC, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);
  Serial.printf("[NET] Timezone applied: %ld s from UTC\n", resolvedTZ);

  return _connected;
}


// ─────────────────────────────────────────────────────────────────────────────
//  syncNTP()
//
//  After configTime() is called the ESP32 contacts the NTP servers in the
//  background. This function polls getLocalTime() until a valid time arrives
//  or we give up after ~10 seconds.
// ─────────────────────────────────────────────────────────────────────────────
bool NetManager::syncNTP() {
  if (!_connected) return false;

  Serial.println("[NET] Waiting for NTP sync...");
  struct tm timeinfo;
  int retries = 20;  // 20 × 500ms = 10 seconds maximum wait

  while (!getLocalTime(&timeinfo, /*timeout_ms=*/1000) && retries-- > 0) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (retries <= 0) {
    Serial.println("[NET] NTP sync failed — no response from time servers.");
    return false;
  }

  _ntpSynced = true;
  Serial.printf("[NET] Time synced: %02d:%02d:%02d\n",
                timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  return true;
}


// ─────────────────────────────────────────────────────────────────────────────
//  checkOTA()
//
//  Flow:
//    1. Fetch OTA_VERSION_URL (a small JSON file, ~100 bytes).
//    2. Parse the JSON to get the remote version string and firmware URL.
//    3. Compare to FIRMWARE_VERSION (defined in config.h).
//    4. If different, download and flash the new firmware.
//
//  The version.json file looks like:
//    { "version": "1.0.1", "firmware_url": "https://example.com/firmware.bin" }
// ─────────────────────────────────────────────────────────────────────────────
bool NetManager::checkOTA() {
  if (!_connected) return false;

  Serial.println("[OTA] Checking for update at: " OTA_VERSION_URL);

  // BUG FIX: Use WiFiClientSecure so HTTPS version.json URLs work.
  // The original code used plain HTTPClient which fails on HTTPS.
  // We disable certificate verification here for simplicity; in a production
  // device you would load the server's root CA certificate instead.
  WiFiClientSecure secureClient;
  secureClient.setInsecure();   // Accept any SSL certificate (convenient but not strict)

  HTTPClient http;
  http.begin(secureClient, OTA_VERSION_URL);
  http.setTimeout(8000);   // 8 second timeout
  int httpCode = http.GET();

  if (httpCode != 200) {
    Serial.printf("[OTA] version.json fetch failed (HTTP %d). "
                  "Check OTA_VERSION_URL in config.h\n", httpCode);
    http.end();
    return false;
  }

  String body = http.getString();
  http.end();

  // ── Parse JSON ────────────────────────────────────────────────────────────
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[OTA] JSON parse error: %s\n", err.c_str());
    Serial.printf("[OTA] Raw response was: %s\n", body.c_str());
    return false;
  }

  // Use the "" fallback so we get an empty string rather than a crash if the
  // key is missing from the JSON.
  const char *remoteVersion = doc["version"]      | "";
  const char *firmwareUrl   = doc["firmware_url"] | "";

  if (strlen(remoteVersion) == 0) {
    Serial.println("[OTA] version.json missing 'version' field.");
    return false;
  }

  if (String(remoteVersion) == FIRMWARE_VERSION) {
    Serial.printf("[OTA] Already on latest firmware (v%s)\n", FIRMWARE_VERSION);
    return false;
  }

  Serial.printf("[OTA] Update available: v%s → v%s\n",
                FIRMWARE_VERSION, remoteVersion);
  Serial.printf("[OTA] Firmware URL: %s\n", firmwareUrl);

  if (strlen(firmwareUrl) == 0) {
    Serial.println("[OTA] version.json missing 'firmware_url' field.");
    return false;
  }

  return _fetchAndUpdate(String(firmwareUrl));
}


// ─────────────────────────────────────────────────────────────────────────────
//  _fetchAndUpdate()
//
//  Downloads the firmware binary from 'url' and flashes it using the ESP32
//  Arduino HTTPUpdate library. On success the device reboots automatically.
//
//  BUG FIX: Original code used a plain WiFiClient, which cannot handle HTTPS
//  firmware URLs. Replaced with WiFiClientSecure (same fix as checkOTA).
// ─────────────────────────────────────────────────────────────────────────────
bool NetManager::_fetchAndUpdate(const String &url) {
  // Use a secure client for the firmware download (handles both HTTP + HTTPS)
  WiFiClientSecure secureClient;
  secureClient.setInsecure();  // See note in checkOTA() about certificate checking

  // Flash the LED during the update so the user has visual feedback
  httpUpdate.setLedPin(LED_PIN, LOW);

  // Reboot automatically on success (this is the default, but explicit is clearer)
  httpUpdate.rebootOnUpdate(true);

  // Wire up the OLED progress bar
  httpUpdate.onProgress([](int current, int total) {
    int pct = (total > 0) ? (current * 100 / total) : 0;
    displayOTAProgress(pct);
    Serial.printf("[OTA] Progress: %d%%\n", pct);
  });

  Serial.println("[OTA] Starting firmware download...");
  t_httpUpdate_return result = httpUpdate.update(secureClient, url);

  switch (result) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("[OTA] Update FAILED: (%d) %s\n",
                    httpUpdate.getLastError(),
                    httpUpdate.getLastErrorString().c_str());
      return false;

    case HTTP_UPDATE_NO_UPDATES:
      // Server said "not modified" — shouldn't happen since we checked the version,
      // but handle it gracefully.
      Serial.println("[OTA] Server reported no update available.");
      return false;

    case HTTP_UPDATE_OK:
      // Device will reboot before this line is reached when rebootOnUpdate=true
      Serial.println("[OTA] Update successful. Rebooting...");
      return true;
  }

  return false;
}


// ─────────────────────────────────────────────────────────────────────────────
//  loop()
//
//  Called every iteration of Arduino loop(). Does two things:
//    1. If WiFi dropped, tries to reconnect silently.
//    2. Every OTA_CHECK_INTERVAL milliseconds, checks for a new firmware.
// ─────────────────────────────────────────────────────────────────────────────
void NetManager::loop() {
  // ── Auto-reconnect ────────────────────────────────────────────────────────
  if (_connected && !WiFi.isConnected()) {
    Serial.println("[NET] WiFi connection lost. Reconnecting...");
    _reconnect();
  }

  // ── Periodic OTA check ────────────────────────────────────────────────────
  if (_connected) {
    uint32_t now = millis();
    bool firstCheck   = (_lastOTACheck == 0);
    bool intervalDue  = (now - _lastOTACheck) >= OTA_CHECK_INTERVAL;

    if (firstCheck || intervalDue) {
      _lastOTACheck = now;
      checkOTA();
    }
  }
}


// ─────────────────────────────────────────────────────────────────────────────
//  _reconnect()
//
//  Attempts to re-join the last known WiFi network without opening a portal.
//  Gives up after 15 seconds and sets _connected = false.
// ─────────────────────────────────────────────────────────────────────────────
void NetManager::_reconnect() {
  WiFi.reconnect();
  uint32_t startMs = millis();

  while (!WiFi.isConnected() && (millis() - startMs) < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  _connected = WiFi.isConnected();
  if (_connected) {
    Serial.printf("[NET] Reconnected. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[NET] Reconnect failed — continuing offline.");
  }
}


// ── Simple getters ────────────────────────────────────────────────────────────
bool   NetManager::isConnected() const { return WiFi.isConnected(); }
String NetManager::getIP()       const { return WiFi.localIP().toString(); }
String NetManager::getSSID()     const { return WiFi.SSID(); }
