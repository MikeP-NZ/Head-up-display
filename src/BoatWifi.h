#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <IPAddress.h>

#include "Settings.h"

// Station link to the boat network plus the configuration access point.
//
// The portal AP deliberately lives on 192.168.71.0/24: Yacht Devices gateways
// hand out 192.168.4.0/24, and two interfaces on one subnet breaks routing.
class BoatWifi {
 public:
  static const IPAddress kApIp;

  void begin(const Settings& s);
  void apply(const Settings& s, bool wifiChanged, bool portalChanged);
  void loop();

  bool staConfigured() const { return staSsid_.length() > 0; }
  bool staConnected() const;
  const String& staSsid() const { return staSsid_; }
  const char* staState() const;

  bool apActive() const { return apActive_; }
  const String& apSsid() const { return apSsidActive_; }
  const String& apPass() const { return apPassActive_; }

  // Keep the AP up for `ms` regardless of mode (button / info screen).
  void forceAp(uint32_t ms);

  void startScan();
  void scanJson(JsonObject out);

  void statusJson(JsonObject out) const;

 private:
  // Recent joins and drops, for the portal's System tab: a drop's reason is
  // the one the Wi-Fi stack gives (beacon timeout, handshake timeout, ...).
  struct LinkEvent {
    uint32_t atMs;
    bool joined;
    uint8_t reason;  // drops only (wifi_err_reason_t)
    int8_t rssi;     // signal at the time, dBm
  };
  static constexpr size_t kEventLog = 12;
  void onStaDisconnected(uint8_t reason, int8_t rssi);
  void logEvent(bool joined, uint8_t reason, int8_t rssi);

  void connectSta();
  uint8_t apClients();  // setup network stations, read at most every 250 ms
  void updateAp();
  void startAp();
  void stopAp();

  String staSsid_, staPass_;
  String apSsidCfg_, apPassCfg_, hostname_;

  bool apActive_ = false;
  String apSsidActive_, apPassActive_;
  uint8_t apChannel_ = 0;
  uint8_t apClients_ = 0;
  uint32_t apClientsReadMs_ = 0;
  uint32_t apForcedUntil_ = 0;

  uint32_t lastAttemptMs_ = 0;
  uint32_t disconnectedSinceMs_ = 0;
  uint32_t connectedSinceMs_ = 0;
  bool wasConnected_ = false;
  bool mdnsStarted_ = false;

  // Written by the Wi-Fi event task, read by the portal: guarded by eventLock_.
  mutable portMUX_TYPE eventLock_ = portMUX_INITIALIZER_UNLOCKED;
  LinkEvent events_[kEventLog] = {};
  size_t eventCount_ = 0;  // total ever logged; the newest is at (eventCount_ - 1) % kEventLog
  uint32_t drops_ = 0;     // connected -> disconnected
  uint32_t joins_ = 0;
  bool linkUp_ = false;    // as the event task sees it
  int8_t lastRssi_ = 0;    // sampled while connected

  bool scanRequested_ = false;
  uint32_t scanStartedMs_ = 0;
  JsonDocument scanCache_;
};
