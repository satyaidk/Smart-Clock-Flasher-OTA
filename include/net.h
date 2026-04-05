#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
//  NetManager — WiFi, NTP, and OTA update handler
//
//  Responsibilities:
//    1. Connect to WiFi (using a captive-portal for first-time setup).
//    2. Sync the system clock from internet time servers (NTP).
//    3. Periodically check a remote server for new firmware and apply it.
//    4. Auto-reconnect if the WiFi connection drops.
//
//  Usage (from main.cpp):
//    NetManager net;
//    net.connect();      // call once in setup()
//    net.syncNTP();      // call once after connect()
//    net.loop();         // call every iteration of loop()
// ─────────────────────────────────────────────────────────────────────────────
class NetManager {
public:
  NetManager();

  // ── Setup (called once) ───────────────────────────────────────────────────

  // Connects to WiFi using stored credentials, or opens a captive portal
  // for first-time setup. Returns true if connected.
  bool connect();

  // Syncs the ESP32's internal clock with an NTP server.
  // Must be called after a successful connect(). Returns true on success.
  bool syncNTP();

  // ── Runtime ───────────────────────────────────────────────────────────────

  // Call this every loop() iteration. Handles:
  //   • Auto-reconnect when WiFi drops
  //   • Periodic OTA update checks (every OTA_CHECK_INTERVAL ms)
  void loop();

  // Manually trigger an OTA update check right now.
  // Returns true if a new firmware was found AND successfully applied
  // (device will reboot automatically after a successful update).
  bool checkOTA();

  // ── State queries ─────────────────────────────────────────────────────────
  bool   isConnected() const;
  String getIP()       const;
  String getSSID()     const;

private:
  bool     _connected    = false;   // True after a successful connect()
  bool     _ntpSynced    = false;   // True after a successful syncNTP()
  uint32_t _lastOTACheck = 0;       // millis() timestamp of last OTA attempt

  // Downloads firmware from 'url' and flashes it. Reboots on success.
  bool _fetchAndUpdate(const String &url);

  // Attempts to re-join the last known WiFi network without re-opening portal.
  void _reconnect();
};
