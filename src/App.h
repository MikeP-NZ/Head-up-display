#pragma once

#include <Arduino.h>

#include "BoatWifi.h"
#include "DataLink.h"
#include "Display.h"
#include "Settings.h"

// Everything the main loop and the portal share. Only touched from the
// Arduino loop task, except `link`, which has its own lock.
struct App {
  Settings settings;
  BoatWifi wifi;
  DataLink link;
  Display display;

  bool rebootRequired = false;  // panel settings saved, not yet in effect
  uint32_t rebootAtMs = 0;
  bool factoryResetPending = false;

  SettingsChanges pendingChanges;
  uint32_t applyAtMs = 0;

  uint32_t testPatternStartMs = 0;
  bool testPattern = false;
  uint32_t infoUntilMs = 0;
  int otaProgress = -1;
  uint32_t otaLastWriteMs = 0;
  bool nightToggled = false;  // button flips the scheduled night state
  bool nightActive = false;

  // Defer applying so the HTTP response reaches the browser before Wi-Fi
  // settings tear the connection down.
  void scheduleApply(const SettingsChanges& c) {
    pendingChanges.wifi |= c.wifi;
    pendingChanges.portal |= c.portal;
    pendingChanges.data |= c.data;
    pendingChanges.speed |= c.speed;
    pendingChanges.display |= c.display;
    pendingChanges.panel |= c.panel;
    applyAtMs = millis() + 400;
    if (applyAtMs == 0) applyAtMs = 1;
  }

  void scheduleReboot(uint32_t delayMs) {
    rebootAtMs = millis() + delayMs;
    if (rebootAtMs == 0) rebootAtMs = 1;
  }

  void showInfo(uint32_t ms) { infoUntilMs = millis() + ms; }
};

extern App app;
