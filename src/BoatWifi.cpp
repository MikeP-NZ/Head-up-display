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
  apMode_ = s.apMode;
  hostname_ = s.hostname;

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
  if (!apActive_) WiFi.mode(WIFI_STA);  // fallback mode: AP comes up later if needed

  mdnsStarted_ = MDNS.begin(hostname_.c_str());
  if (mdnsStarted_) MDNS.addService("http", "tcp", 80);
}

void BoatWifi::apply(const Settings& s, bool wifiChanged, bool portalChanged) {
  if (portalChanged) {
    apSsidCfg_ = s.effectiveApSsid();
    apPassCfg_ = s.apPass;
    apMode_ = s.apMode;
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

void BoatWifi::updateAp() {
  uint32_t now = millis();
  bool connected = staConnected();
  bool forced = apForcedUntil_ && static_cast<int32_t>(apForcedUntil_ - now) > 0;
  if (!forced) apForcedUntil_ = 0;
  bool hasClients = apActive_ && WiFi.softAPgetStationNum() > 0;

  bool want;
  if (apMode_ == ApMode::Always || !staConfigured() || forced || hasClients) {
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

  if (!connected && staConfigured() && !scanRequested_) {
    uint32_t interval =
        apActive_ && WiFi.softAPgetStationNum() > 0 ? kRetryWithPortalClientsMs : kRetryMs;
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

  JsonObject ap = out["ap"].to<JsonObject>();
  ap["active"] = apActive_;
  ap["ssid"] = apActive_ ? apSsidActive_ : apSsidCfg_;
  ap["ip"] = kApIp.toString();
  ap["clients"] = apActive_ ? WiFi.softAPgetStationNum() : 0;
}
