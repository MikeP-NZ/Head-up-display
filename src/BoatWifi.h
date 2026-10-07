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
  void connectSta();
  void updateAp();
  void startAp();
  void stopAp();

  String staSsid_, staPass_;
  String apSsidCfg_, apPassCfg_, hostname_;
  ApMode apMode_ = ApMode::Always;

  bool apActive_ = false;
  String apSsidActive_, apPassActive_;
  uint8_t apChannel_ = 0;
  uint32_t apForcedUntil_ = 0;

  uint32_t lastAttemptMs_ = 0;
  uint32_t disconnectedSinceMs_ = 0;
  uint32_t connectedSinceMs_ = 0;
  bool wasConnected_ = false;
  bool mdnsStarted_ = false;

  bool scanRequested_ = false;
  uint32_t scanStartedMs_ = 0;
  JsonDocument scanCache_;
};
