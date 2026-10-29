// Head-Up Display: boat instrument display (speed, wind, heading, depth,
// waypoint) for a HUB75 LED panel on an ESP32-S3 board, fed by a Yacht Devices
// Wi-Fi gateway.

#include <Arduino.h>
#include <WiFi.h>
#include <sys/time.h>

#include "App.h"
#include "Portal.h"

App app;

namespace {

constexpr uint32_t kInfoHoldMs = 3000;
constexpr uint32_t kResetHoldMs = 10000;
constexpr uint32_t kFrameMs = 50;
constexpr uint32_t kTestPatternMs = 60000;

uint32_t buttonDownMs = 0;
bool buttonWasDown = false;
uint32_t lastFrameMs = 0;
bool lastScheduledNight = false;

bool inWindow(int minute, int start, int end) {
  if (start == end) return false;
  if (start < end) return minute >= start && minute < end;
  return minute >= start || minute < end;
}

bool scheduledNight() {
  const Settings& s = app.settings;
  switch (s.nightMode) {
    case NightMode::Off: return false;
    case NightMode::On: return true;
    case NightMode::Schedule: {
      time_t now = time(nullptr);
      if (now < 1600000000) return false;  // no clock yet
      int64_t local = static_cast<int64_t>(now) + s.utcOffsetMin * 60;
      int minute = static_cast<int>(((local / 60) % 1440 + 1440) % 1440);
      return inWindow(minute, s.nightStartMin, s.nightEndMin);
    }
  }
  return false;
}

void updateNight() {
  bool scheduled = scheduledNight();
  if (scheduled != lastScheduledNight) {
    app.nightToggled = false;  // a manual toggle lasts until the schedule flips
    lastScheduledNight = scheduled;
  }
  app.nightActive = scheduled != app.nightToggled;
  app.display.setNight(app.nightActive);
}

void syncClockFromBus() {
  int64_t epoch;
  app.link.lock();
  bool got = app.link.decoder().takeTime(epoch);
  app.link.unlock();
  if (!got || epoch < 1600000000) return;
  time_t now = time(nullptr);
  if (llabs(static_cast<int64_t>(now) - epoch) > 2) {
    timeval tv = {static_cast<time_t>(epoch), 0};
    settimeofday(&tv, nullptr);
  }
}

void applyPendingChanges() {
  if (!app.applyAtMs || static_cast<int32_t>(millis() - app.applyAtMs) < 0) return;
  SettingsChanges c = app.pendingChanges;
  app.pendingChanges = SettingsChanges{};
  app.applyAtMs = 0;
  if (c.wifi || c.portal) app.wifi.apply(app.settings, c.wifi, c.portal);
  if (c.data || c.speed) app.link.apply(app.settings);
  if (c.display || c.speed) app.display.applySettings(app.settings);
}

// Connection problems, shown full screen with a ticker. Empty while data
// flows.
String connectionMessage(bool& isError) {
  isError = false;
  if (app.link.demo()) return "";  // simulated data needs no connection
  isError = true;
  const Settings& s = app.settings;
  if (!app.wifi.staConnected()) {
    String m = "NO WIFI " + s.staSsid;
    if (app.wifi.apActive()) m += "  SETUP: " + app.wifi.apSsid() + " " + BoatWifi::kApIp.toString();
    return m;
  }
  if (!app.link.linkUp()) {
    String target = s.host.length() ? s.host : WiFi.gatewayIP().toString();
    return "NO LINK " + target + ":" + String(s.port);
  }
  isError = false;
  if (!app.link.everReceived() || app.link.msSinceLine() > 5000) return "NO DATA";
  return "";
}

// Fills one section from the model (call with the link lock held).
void fillItem(ItemView& v, const BoatModel& model, uint32_t now) {
  DisplayUnits units = app.settings.displayUnits();
  v.unit = itemUnit(v.item, units);
  if (v.item == Item::None) return;
  BoatModel::Reading r = readItem(model, v.item, units.bearing, now);
  if (v.item == Item::Speed) v.kind = r.q;
  v.valid = r.valid;
  if (!r.valid) return;
  DisplayValue d = itemDisplay(v.item, r.value, units);
  v.value = d.value;
  v.unit = d.unit;
  v.shortDistance = d.shortDistance;
}

// Slow blink while showing values, fast blink otherwise. Blinking reads the
// same whichever way the LED is wired.
void updateStatusLed(uint32_t now, bool valuesShown) {
  if (board().statusLedPin < 0) return;
  uint32_t period = valuesShown ? 1000 : 250;
  digitalWrite(board().statusLedPin, (now / (period / 2)) % 2 ? HIGH : LOW);
}

void renderFrame() {
  uint32_t now = millis();
  if (app.otaProgress >= 0) {
    if (now - app.otaLastWriteMs < 10000) return;  // the upload handler draws progress
    app.otaProgress = -1;  // upload stalled without an abort
  }
  Renderer& d = app.display.renderer();

  if (app.testPattern && now - app.testPatternStartMs > kTestPatternMs) app.testPattern = false;
  bool holding = buttonWasDown && now - buttonDownMs > kInfoHoldMs;
  if (app.infoUntilMs && static_cast<int32_t>(app.infoUntilMs - now) <= 0) app.infoUntilMs = 0;
  bool infoActive = app.infoUntilMs != 0;
  bool valuesShown = false;

  if (app.factoryResetPending) {
    d.drawMessage("RESET", "ERASING SETTINGS", 100, now);
  } else if (app.rebootAtMs) {
    d.drawMessage("REBOOT", "", -1, now);
  } else if (holding) {
    uint32_t held = min(now - buttonDownMs, kResetHoldMs);
    char detail[24];
    snprintf(detail, sizeof(detail), "RESET IN %lus", static_cast<unsigned long>((kResetHoldMs - held) / 1000 + 1));
    d.drawMessage("RELEASE:INFO", detail, held * 100 / kResetHoldMs, now);
  } else if (app.testPattern) {
    d.drawTestPattern(now - app.testPatternStartMs);
  } else if ((!app.wifi.staConfigured() && !app.settings.demo) || infoActive) {
    InfoView v;
    v.setupMode = !app.wifi.staConfigured();
    v.apActive = app.wifi.apActive();
    v.apSsid = app.wifi.apSsid().length() ? app.wifi.apSsid() : app.settings.effectiveApSsid();
    v.apPass = app.settings.apPass;
    v.apIp = BoatWifi::kApIp.toString();
    v.staSsid = app.settings.staSsid;
    v.staIp = WiFi.isConnected() ? WiFi.localIP().toString() : String();
    v.hostname = app.settings.hostname;
    d.drawInfo(v, now / 4000);
  } else {
    const Settings& s = app.settings;
    DashView v;
    v.sections = s.sections;
    for (size_t i = 0; i < kMaxSections; i++) v.items[i].item = i < s.sections ? s.items[i] : Item::None;
    app.link.lock();
    for (size_t i = 0; i < s.sections; i++) fillItem(v.items[i], app.link.model(), now);
    app.link.unlock();
    for (size_t i = 0; i < s.sections; i++) valuesShown |= v.items[i].valid;
    v.message = connectionMessage(v.messageIsError);
    v.simulated = app.link.demo();
    // Full-screen layout: name what is missing. Multi-section layouts show
    // dashes in the sections without data instead.
    if (v.message.length() == 0 && s.sections == 1 && !v.items[0].valid) {
      const char* label = v.items[0].item == Item::Speed ? "SPEED" : itemInfo(v.items[0].item).label;
      v.message = String("NO ") + label + " DATA";
    }
    d.drawDash(v, now);
  }
  app.display.present();
  updateStatusLed(now, valuesShown);
}

void handleButton() {
  if (board().buttonPin < 0) return;
  bool down = digitalRead(board().buttonPin) == LOW;
  uint32_t now = millis();
  if (down && !buttonWasDown) {
    buttonDownMs = now;
  } else if (down && now - buttonDownMs >= kResetHoldMs && !app.factoryResetPending) {
    app.factoryResetPending = true;
    app.scheduleReboot(1500);
  } else if (!down && buttonWasDown) {
    uint32_t held = now - buttonDownMs;
    if (held < 50) {
      // bounce
    } else if (held < 1000) {
      app.nightToggled = !app.nightToggled;
    } else if (held < kResetHoldMs) {
      app.showInfo(30000);
      app.wifi.forceAp(15 * 60 * 1000);
    }
  }
  buttonWasDown = down;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  if (board().buttonPin >= 0) pinMode(board().buttonPin, INPUT_PULLUP);
  if (board().statusLedPin >= 0) pinMode(board().statusLedPin, OUTPUT);

  settingsLoad(app.settings);
  if (!app.display.begin(app.settings)) {
    log_e("panel not initialised; portal still available");
  }
  app.display.renderer().drawSplash(FW_VERSION);
  app.display.present();

  app.wifi.begin(app.settings);
  app.link.begin(app.settings);
  portal::begin();
  log_i("head-up display %s ready, portal AP '%s' on %s", FW_VERSION, app.settings.effectiveApSsid().c_str(),
        BoatWifi::kApIp.toString().c_str());
  delay(1200);
}

void loop() {
  static uint32_t lastLoopMs = millis();
  const uint32_t loopStart = millis();
  const uint32_t gap = loopStart - lastLoopMs;
  lastLoopMs = loopStart;
  if (gap > app.loopGapRecentMs) app.loopGapRecentMs = gap;
  if (gap > app.loopGapMaxMs) {
    app.loopGapMaxMs = gap;
    app.loopGapAtMs = loopStart;
  }

  app.wifi.loop();
  app.link.setNetworkUp(app.wifi.staConnected());
  portal::loop();
  handleButton();
  applyPendingChanges();
  syncClockFromBus();

  uint32_t now = millis();
  if (now - lastFrameMs >= kFrameMs) {
    lastFrameMs = now;
    updateNight();
    renderFrame();
  }

  if (app.rebootAtMs && static_cast<int32_t>(now - app.rebootAtMs) >= 0) {
    if (app.factoryResetPending) settingsErase();
    WiFi.disconnect(true);
    delay(100);
    ESP.restart();
  }
  delay(2);
}
