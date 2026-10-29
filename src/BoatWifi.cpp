#include "BoatWifi.h"

#include <ESPmDNS.h>
#include <WiFi.h>

const IPAddress BoatWifi::kApIp(192, 168, 71, 1);

namespace {

constexpr uint32_t kRetryMs = 10000;
// Each STA attempt scans all channels, which drags the AP along and drops
// phones using the portal. Retry less often while someone is connected.
constexpr uint32_t kRetryWithPortalClientsMs = 30000;
constexpr uint32_t kFallbackApDelayMs = 20000;
constexpr uint32_t kFallbackApOffDelayMs = 10000;

const char* encryptionName(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "secured";
  }
}

}  // namespace

void BoatWifi::begin(const Settings& s) {
  staSsid_ = s.staSsid;
  staPass_ = s.staPass;
  apSsidCfg_ = s.effectiveApSsid();
  apPassCfg_ = s.apPass;
  hostname_ = s.hostname;

  // Registered before Wi-Fi starts, for the same reason as the mode below:
  // the event task iterates the handler list without a lock.
  WiFi.onEvent([this](arduino_event_id_t, arduino_event_info_t info) {
    onStaDisconnected(info.wifi_sta_disconnected.reason, info.wifi_sta_disconnected.rssi);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.onEvent([this](arduino_event_id_t, arduino_event_info_t) {
    const int8_t rssi = static_cast<int8_t>(WiFi.RSSI());
    portENTER_CRITICAL(&eventLock_);
    linkUp_ = true;
    joins_++;
    portEXIT_CRITICAL(&eventLock_);
    logEvent(true, 0, rssi);
  }, ARDUINO_EVENT_WIFI_STA_GOT_IP);

  WiFi.persistent(false);
  WiFi.setHostname(hostname_.c_str());
  WiFi.setAutoReconnect(false);
  // Enable both interfaces in a single call, before Wi-Fi starts. Arduino-ESP32
  // 3.x keeps each interface's event handler in a std::vector that the
  // "arduino_events" task iterates without a lock. Going STA -> AP_STA while
  // an STA event is being dispatched grows (reallocates) that vector under the
  // task, which then calls a freed pointer: InstrFetchProhibited in
  // NetworkEvents.cpp. Registering all handlers up front sizes the vector once,
  // so turning the AP off and on later never reallocates it.
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  disconnectedSinceMs_ = millis();

  if (staConfigured()) connectSta();
  updateAp();
  if (!apActive_) WiFi.mode(WIFI_STA);  // the AP comes up later if the boat network can't be joined

  mdnsStarted_ = MDNS.begin(hostname_.c_str());
  if (mdnsStarted_) MDNS.addService("http", "tcp", 80);
}

void BoatWifi::apply(const Settings& s, bool wifiChanged, bool portalChanged) {
  if (portalChanged) {
    apSsidCfg_ = s.effectiveApSsid();
    apPassCfg_ = s.apPass;
    if (s.hostname != hostname_) {
      hostname_ = s.hostname;
      WiFi.setHostname(hostname_.c_str());
      MDNS.end();
      mdnsStarted_ = MDNS.begin(hostname_.c_str());
      if (mdnsStarted_) MDNS.addService("http", "tcp", 80);
    }
    if (apActive_) {
      stopAp();
    }
    updateAp();
  }
  if (wifiChanged) {
    staSsid_ = s.staSsid;
    staPass_ = s.staPass;
    WiFi.disconnect(false, true);
    wasConnected_ = false;
    disconnectedSinceMs_ = millis();
    if (staConfigured()) connectSta();
  }
}

bool BoatWifi::staConnected() const { return WiFi.status() == WL_CONNECTED; }

const char* BoatWifi::staState() const {
  if (!staConfigured()) return "not_configured";
  switch (WiFi.status()) {
    case WL_CONNECTED: return "connected";
    case WL_NO_SSID_AVAIL: return "network_not_found";
    case WL_CONNECT_FAILED: return "auth_failed";
    case WL_CONNECTION_LOST: return "connection_lost";
    default: return "connecting";
  }
}

void BoatWifi::connectSta() {
  lastAttemptMs_ = millis();
  log_i("joining '%s'", staSsid_.c_str());
  WiFi.begin(staSsid_.c_str(), staPass_.length() ? staPass_.c_str() : nullptr);
}

// Runs in the Wi-Fi event task. Failed join attempts also land here (every
// retry while the network is away); only the loss of a working link counts as
// a drop.
void BoatWifi::onStaDisconnected(uint8_t reason, int8_t rssi) {
  portENTER_CRITICAL(&eventLock_);
  const bool wasUp = linkUp_;
  linkUp_ = false;
  if (wasUp) drops_++;
  if (rssi == 0) rssi = lastRssi_;
  portEXIT_CRITICAL(&eventLock_);
  if (wasUp) {
    logEvent(false, reason, rssi);
    log_e("dropped from '%s': %s (%u), %d dBm", staSsid_.c_str(),
          WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(reason)), reason, rssi);
  }
}

void BoatWifi::logEvent(bool joined, uint8_t reason, int8_t rssi) {
  portENTER_CRITICAL(&eventLock_);
  events_[eventCount_ % kEventLog] = {millis(), joined, reason, rssi};
  eventCount_++;
  portEXIT_CRITICAL(&eventLock_);
}

void BoatWifi::forceAp(uint32_t ms) {
  apForcedUntil_ = millis() + ms;
  if (apForcedUntil_ == 0) apForcedUntil_ = 1;
  updateAp();
}

void BoatWifi::startAp() {
  apSsidActive_ = apSsidCfg_;
  apPassActive_ = apPassCfg_;
  apChannel_ = staConnected() ? WiFi.channel() : 1;
  WiFi.mode(WIFI_AP_STA);
  bool ok = WiFi.softAP(apSsidActive_.c_str(), apPassActive_.length() ? apPassActive_.c_str() : nullptr,
                        apChannel_ ? apChannel_ : 1, 0, 4);
  ok = ok && WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));
  apActive_ = ok;
  log_i("portal AP '%s' %s", apSsidActive_.c_str(), ok ? "up" : "FAILED");
}

void BoatWifi::stopAp() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive_ = false;
  log_i("portal AP down");
}

uint8_t BoatWifi::apClients() {
  // Reading the station list locks the Wi-Fi driver; once per loop pass
  // (hundreds of times a second) measurably slowed the boat network.
  const uint32_t now = millis();
  if (!apActive_) {
    apClients_ = 0;
  } else if (now - apClientsReadMs_ >= 250) {
    apClientsReadMs_ = now;
    apClients_ = WiFi.softAPgetStationNum();
  }
  return apClients_;
}

void BoatWifi::updateAp() {
  uint32_t now = millis();
  bool connected = staConnected();
  bool forced = apForcedUntil_ && static_cast<int32_t>(apForcedUntil_ - now) > 0;
  if (!forced) apForcedUntil_ = 0;
  bool hasClients = apClients() > 0;

  // The setup network only runs while the display isn't on the boat
  // network: running both on one radio delayed and lost the boat network's
  // packets. It also stays up while someone is connected to it, and when
  // forced on (button hold, connection info).
  bool want;
  if (!staConfigured() || forced || hasClients) {
    want = true;
  } else if (connected) {
    want = apActive_ && now - connectedSinceMs_ < kFallbackApOffDelayMs;
  } else {
    want = apActive_ || now - disconnectedSinceMs_ > kFallbackApDelayMs;
  }

  if (want && !apActive_) startAp();
  if (!want && apActive_) stopAp();
}

void BoatWifi::loop() {
  uint32_t now = millis();
  bool connected = staConnected();
  if (connected && !wasConnected_) {
    connectedSinceMs_ = now;
    log_i("joined '%s' ip=%s gw=%s ch=%d", staSsid_.c_str(), WiFi.localIP().toString().c_str(),
          WiFi.gatewayIP().toString().c_str(), WiFi.channel());
  } else if (!connected && wasConnected_) {
    disconnectedSinceMs_ = now;
    lastAttemptMs_ = now;  // give the stack a moment before retrying
  }
  wasConnected_ = connected;
  static uint32_t lastRssiMs = 0;
  if (connected && now - lastRssiMs > 2000) {
    lastRssiMs = now;
    const int8_t rssi = static_cast<int8_t>(WiFi.RSSI());
    portENTER_CRITICAL(&eventLock_);
    lastRssi_ = rssi;
    portEXIT_CRITICAL(&eventLock_);
  }

  if (!connected && staConfigured() && !scanRequested_) {
    uint32_t interval = apClients() > 0 ? kRetryWithPortalClientsMs : kRetryMs;
    if (now - lastAttemptMs_ > interval) {
      WiFi.disconnect(false, false);
      connectSta();
    }
  }

  if (scanRequested_) {
    int16_t n = WiFi.scanComplete();
    if (n >= 0 || n == WIFI_SCAN_FAILED || now - scanStartedMs_ > 20000) {
      scanCache_.clear();
      JsonArray list = scanCache_["networks"].to<JsonArray>();
      for (int16_t i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        bool dup = false;
        for (JsonObject existing : list) {
          if (existing["ssid"] == ssid) {
            dup = true;
            break;
          }
        }
        if (dup) continue;  // strongest first; skip repeaters on other channels
        JsonObject o = list.add<JsonObject>();
        o["ssid"] = ssid;
        o["rssi"] = WiFi.RSSI(i);
        o["channel"] = WiFi.channel(i);
        o["security"] = encryptionName(WiFi.encryptionType(i));
        o["yachtDevices"] = ssid.startsWith("YD");
      }
      scanCache_["error"] = n < 0;
      scanCache_["at"] = now;
      WiFi.scanDelete();
      scanRequested_ = false;
    }
  }

  updateAp();
}

void BoatWifi::startScan() {
  if (scanRequested_) return;
  // A connection attempt in progress makes the scan fail; pause it.
  if (!staConnected()) WiFi.disconnect(false, false);
  int16_t r = WiFi.scanNetworks(true, false);
  scanRequested_ = r == WIFI_SCAN_RUNNING || r >= 0;
  scanStartedMs_ = millis();
  if (!scanRequested_) {
    scanCache_.clear();
    scanCache_["error"] = true;
  }
}

void BoatWifi::scanJson(JsonObject out) {
  out["scanning"] = scanRequested_;
  if (!scanCache_.isNull()) {
    out["networks"] = scanCache_["networks"];
    out["error"] = scanCache_["error"];
  }
}

void BoatWifi::statusJson(JsonObject out) const {
  JsonObject sta = out["wifi"].to<JsonObject>();
  sta["configured"] = staConfigured();
  sta["ssid"] = staSsid_;
  sta["state"] = staState();
  bool connected = staConnected();
  sta["connected"] = connected;
  if (connected) {
    sta["ip"] = WiFi.localIP().toString();
    sta["gateway"] = WiFi.gatewayIP().toString();
    sta["rssi"] = WiFi.RSSI();
    sta["channel"] = WiFi.channel();
  }
  sta["hostname"] = hostname_;
  sta["mac"] = WiFi.macAddress();

  // Joins and drops, newest first; "ago" in seconds.
  LinkEvent log[kEventLog];
  size_t count;
  uint32_t drops, joins;
  portENTER_CRITICAL(&eventLock_);
  memcpy(log, events_, sizeof(log));
  count = eventCount_;
  drops = drops_;
  joins = joins_;
  portEXIT_CRITICAL(&eventLock_);
  sta["joins"] = joins;
  sta["drops"] = drops;
  JsonArray events = sta["events"].to<JsonArray>();
  const uint32_t now = millis();
  for (size_t i = 0; i < count && i < kEventLog; i++) {
    const LinkEvent& e = log[(count - 1 - i) % kEventLog];
    JsonObject o = events.add<JsonObject>();
    o["ago"] = (now - e.atMs) / 1000;
    o["joined"] = e.joined;
    o["rssi"] = e.rssi;
    if (!e.joined) {
      o["reason"] = e.reason;
      o["reasonName"] = WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(e.reason));
    }
  }

  JsonObject ap = out["ap"].to<JsonObject>();
  ap["active"] = apActive_;
  ap["ssid"] = apActive_ ? apSsidActive_ : apSsidCfg_;
  ap["ip"] = kApIp.toString();
  ap["clients"] = apActive_ ? WiFi.softAPgetStationNum() : 0;
  ap["mac"] = WiFi.softAPmacAddress();
}
