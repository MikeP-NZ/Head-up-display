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
  // Longest gap between main loop passes: the portal is only answered between
  // passes, so a long one shows up as the portal losing contact.
  uint32_t loopGapMaxMs = 0;     // since start-up
  uint32_t loopGapRecentMs = 0;  // since the portal last read it
  uint32_t loopGapAtMs = 0;      // when the longest one ended
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
