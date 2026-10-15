#include "Portal.h"

#include <DNSServer.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>
#include <sys/time.h>

#include "App.h"
#include "generated/web_index.h"

namespace portal {

namespace {

WebServer server(80);
DNSServer dns;
bool dnsRunning = false;
bool otaFailed = false;
String otaError;

void sendJson(int code, JsonDocument& doc) {
  String body;
  serializeJson(doc, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}

void sendError(int code, const String& message) {
  JsonDocument doc;
  doc["ok"] = false;
  doc["error"] = message;
  sendJson(code, doc);
}

void sendOk() {
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(200, doc);
}

bool isOwnHost(const String& host) {
  if (host.length() == 0) return true;
  String h = host;
  int colon = h.indexOf(':');
  if (colon >= 0) h = h.substring(0, colon);
  if (h == BoatWifi::kApIp.toString()) return true;
  if (WiFi.isConnected() && h == WiFi.localIP().toString()) return true;
  if (h.equalsIgnoreCase(app.settings.hostname + ".local")) return true;
  if (h.equalsIgnoreCase(app.settings.hostname)) return true;
  return false;
}

void redirectToPortal() {
  server.sendHeader("Location", String("http://") + BoatWifi::kApIp.toString() + "/", true);
  server.send(302, "text/plain", "");
}

void handleIndex() {
  if (!isOwnHost(server.hostHeader())) {
    redirectToPortal();
    return;
  }
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char*>(WEB_INDEX_GZ), WEB_INDEX_GZ_LEN);
}

void boardJson(JsonObject o) {
  const BoardInfo& b = board();
  o["id"] = b.id;
  o["name"] = b.name;
  o["buttonName"] = b.buttonName;
  o["defaultClkPhase"] = b.defaultClkPhase;
  JsonArray ports = o["ports"].to<JsonArray>();
  for (size_t i = 0; i < b.portCount; i++) {
    JsonObject p = ports.add<JsonObject>();
    p["id"] = b.ports[i].id;
    p["label"] = b.ports[i].label;
  }
  JsonObject pins = o["defaultPins"].to<JsonObject>();
  const PanelPins& d = b.defaultPins;
  const int8_t values[] = {d.r1, d.g1, d.b1, d.r2, d.g2, d.b2, d.a, d.b, d.c, d.d, d.e, d.lat, d.oe, d.clk};
  const char* names[] = {"r1", "g1", "b1", "r2", "g2", "b2", "a", "b", "c", "d", "e", "lat", "oe", "clk"};
  for (size_t i = 0; i < 14; i++) pins[names[i]] = values[i];
}

void handleGetConfig() {
  JsonDocument doc;
  settingsToJson(app.settings, doc.to<JsonObject>(), false);
  doc["rebootRequired"] = app.rebootRequired;
  boardJson(doc["board"].to<JsonObject>());
  JsonArray items = doc["itemList"].to<JsonArray>();
  for (size_t i = 0; i < kItemCount; i++) {
    const ItemInfo& info = itemInfo(static_cast<Item>(i));
    JsonObject o = items.add<JsonObject>();
    o["id"] = info.id;
    o["label"] = info.label;
    o["name"] = info.name;
  }
  sendJson(200, doc);
}

void handlePostConfig() {
  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  if (err || !body.is<JsonObject>()) {
    sendError(400, "body must be a JSON object");
    return;
  }
  SettingsChanges changes;
  String error;
  if (!settingsMergeJson(app.settings, body.as<JsonObjectConst>(), changes, error)) {
    sendError(400, error);
    return;
  }
  if (changes.any() && !settingsSave(app.settings)) {
    sendError(500, "could not write settings to flash");
    return;
  }
  if (changes.panel) app.rebootRequired = true;
  app.scheduleApply(changes);

  JsonDocument doc;
  doc["ok"] = true;
  doc["rebootRequired"] = app.rebootRequired;
  JsonObject ch = doc["changed"].to<JsonObject>();
  ch["wifi"] = changes.wifi;
  ch["portal"] = changes.portal;
  ch["data"] = changes.data;
  ch["speed"] = changes.speed;
  ch["display"] = changes.display;
  ch["panel"] = changes.panel;
  sendJson(200, doc);
}

void handleExport() {
  JsonDocument doc;
  settingsToJson(app.settings, doc.to<JsonObject>(), false);
  doc["board"] = board().id;  // panel settings only make sense on the same board
  doc["portal"].remove("defaultSsid");
  doc["wifi"].remove("open");
  doc["portal"].remove("open");
  server.sendHeader("Content-Disposition", "attachment; filename=\"head-up-display-config.json\"");
  String body;
  serializeJsonPretty(doc, body);
  server.send(200, "application/json", body);
}

float roundTo(float v, int decimals) {
  float f = decimals <= 0 ? 1 : decimals == 1 ? 10 : 100;
  return roundf(v * f) / f;
}

void readingJson(JsonObject o, const BoatModel::Reading& r, SpeedUnit unit, int decimals) {
  o["valid"] = r.valid;
  if (r.ageMs || r.value) {
    o["value"] = roundTo(convertSpeed(r.value, unit), decimals);
    o["age"] = r.ageMs;
  }
}

// A value in display units, rounded for the portal.
void valueJson(JsonObject o, Item item, float value, const DisplayUnits& units) {
  DisplayValue d = itemDisplay(item, value, units);
  bool longDistance = itemInfo(item).format == ItemFormat::Distance && !d.shortDistance;
  o["value"] = roundTo(d.value, longDistance ? 2 : d.shortDistance ? 0 : 1);
  o["unit"] = d.unit;
}

void handleStatus() {
  JsonDocument doc;
  uint32_t now = millis();
  doc["fw"] = FW_VERSION;
  doc["board"] = board().name;
  doc["firmwareTag"] = firmwareTag();
  doc["deviceId"] = deviceId();
  doc["uptime"] = now / 1000;
  doc["heap"] = ESP.getFreeHeap();
  doc["minHeap"] = ESP.getMinFreeHeap();
  doc["psram"] = ESP.getPsramSize();
  doc["rebootRequired"] = app.rebootRequired;
  app.wifi.statusJson(doc.as<JsonObject>());

  const Settings& s = app.settings;
  int decimals = s.decimals >= 0 ? s.decimals : 1;

  app.link.lock();
  app.link.statusJson(doc["data"].to<JsonObject>());
  BoatModel& model = app.link.model();
  BoatModel::Reading cur = model.speed(now);
  JsonObject speed = doc["speed"].to<JsonObject>();
  speed["valid"] = cur.valid;
  speed["kind"] = quantityLabel(cur.q);
  speed["unit"] = unitLabel(s.unit);
  if (cur.valid) speed["value"] = roundTo(convertSpeed(cur.value, s.unit), decimals);
  readingJson(speed["stw"].to<JsonObject>(), model.get(Quantity::Stw, now), s.unit, decimals);
  readingJson(speed["sog"].to<JsonObject>(), model.get(Quantity::Sog, now), s.unit, decimals);

  // Every value, in display units (wind angles: negative = port).
  DisplayUnits units = s.displayUnits();
  JsonObject values = doc["values"].to<JsonObject>();
  for (size_t i = 0; i < kItemCount; i++) {
    Item item = static_cast<Item>(i);
    Quantity q;
    if (!itemQuantity(item, q)) continue;
    BoatModel::Reading r = model.bearing(q, units.bearing, now);
    JsonObject o = values[itemInfo(item).id].to<JsonObject>();
    o["valid"] = r.valid;
    o["unit"] = itemUnit(item, units);
    if (r.valid) valueJson(o, item, r.value, units);
    if (r.ageMs || r.valid) o["age"] = r.ageMs;
  }

  JsonArray sources = doc["sources"].to<JsonArray>();
  for (size_t i = 0; i < model.sourceCount(); i++) {
    const SourceInfo& src = model.source(i);
    char label[16];
    formatSourceId(src.id, label, sizeof(label));
    JsonObject o = sources.add<JsonObject>();
    o["id"] = src.id;
    o["label"] = label;
    o["kind"] = quantityLabel(src.q);
    o["via"] = src.via;
    // Bearings as they were received; everything else in display units.
    valueJson(o, itemForQuantity(src.q), src.lastValue, units);
    if (isBearing(src.q)) o["unit"] = isTrueBearing(src.q) ? "oT" : "oM";
    o["age"] = max<int32_t>(0, static_cast<int32_t>(now - src.lastMs));
    o["count"] = src.count;
  }
  app.link.unlock();

  JsonObject t = doc["time"].to<JsonObject>();
  time_t epoch = time(nullptr);
  bool timeValid = epoch > 1600000000;
  t["valid"] = timeValid;
  if (timeValid) t["epoch"] = static_cast<int64_t>(epoch);
  t["night"] = app.nightActive;

  JsonObject d = doc["display"].to<JsonObject>();
  d["panelOk"] = app.display.panelOk();
  d["width"] = app.display.renderer().width();
  d["height"] = app.display.renderer().height();
  sendJson(200, doc);
}

void handleSniff() {
  uint32_t since = server.hasArg("since") ? strtoul(server.arg("since").c_str(), nullptr, 10) : 0;
  bool speedOnly = server.arg("speed") == "1";
  JsonDocument doc;
  app.link.lock();
  app.link.sniffJson(doc.to<JsonObject>(), since, speedOnly);
  app.link.unlock();
  sendJson(200, doc);
}

void handleScanStart() {
  app.wifi.startScan();
  sendOk();
}

void handleScanGet() {
  JsonDocument doc;
  app.wifi.scanJson(doc.to<JsonObject>());
  sendJson(200, doc);
}

void handleDiscoverStart() {
  if (!WiFi.isConnected()) {
    sendError(409, "join the boat Wi-Fi first");
    return;
  }
  app.link.startDiscovery();
  sendOk();
}

void handleDiscoverGet() {
  JsonDocument doc;
  app.link.discoveryJson(doc.to<JsonObject>());
  sendJson(200, doc);
}

void handleFrame() {
  const Renderer& r = app.display.renderer();
  const uint16_t* buf = r.frame();
  size_t len = static_cast<size_t>(r.width()) * r.height() * 2;
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("X-Width", String(r.width()));
  server.sendHeader("X-Height", String(r.height()));
  server.sendHeader("X-Rotation", String(r.rotation()));
  server.setContentLength(len);
  server.send(200, "application/octet-stream", "");
  server.sendContent(reinterpret_cast<const char*>(buf), len);
}

void handleTestPattern() {
  app.testPattern = true;
  app.testPatternStartMs = millis();
  sendOk();
}

void handleInfo() {
  app.showInfo(20000);
  sendOk();
}

void handleReboot() {
  sendOk();
  app.scheduleReboot(500);
}

void handleFactoryReset() {
  sendOk();
  app.factoryResetPending = true;
  app.scheduleReboot(500);
}

void handleSetTime() {
  // Lets the browser seed the clock for the night schedule when the bus
  // has no time source.
  JsonDocument body;
  if (deserializeJson(body, server.arg("plain")) || !body["epoch"].is<int64_t>()) {
    sendError(400, "expected {\"epoch\": seconds}");
    return;
  }
  if (time(nullptr) < 1600000000) {
    timeval tv = {static_cast<time_t>(body["epoch"].as<int64_t>()), 0};
    settimeofday(&tv, nullptr);
  }
  sendOk();
}

void handleUpdateDone() {
  bool ok = !otaFailed && !Update.hasError();
  JsonDocument doc;
  doc["ok"] = ok;
  if (!ok) doc["error"] = otaError.length() ? otaError : String(Update.errorString());
  sendJson(ok ? 200 : 500, doc);
  app.otaProgress = -1;
  if (ok) app.scheduleReboot(800);
}

// Watches the uploaded image for the "HEADUP-BOARD:<id>" tag (see Board.cpp),
// across chunk boundaries. Matches with an empty id are the search string
// itself and are ignored.
class TagScanner {
 public:
  void reset() {
    tailLen_ = 0;
    matched_ = false;
    other_ = "";
  }

  void feed(const uint8_t* data, size_t len) {
    static constexpr char kPrefix[] = "HEADUP-BOARD:";
    constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    memcpy(buf_ + tailLen_, data, len);
    size_t n = tailLen_ + len;
    for (size_t i = 0; i + kPrefixLen <= n; i++) {
      if (buf_[i] != kPrefix[0] || memcmp(buf_ + i, kPrefix, kPrefixLen) != 0) continue;
      size_t j = i + kPrefixLen;
      String id;
      while (j < n && id.length() < 32 && (isalnum(buf_[j]) || buf_[j] == '-')) id += static_cast<char>(buf_[j++]);
      if (id.length() == 0) continue;
      if (id == board().id) matched_ = true;
      else other_ = id;
    }
    tailLen_ = min(n, kTail);
    memmove(buf_, buf_ + n - tailLen_, tailLen_);
  }

  bool matched() const { return matched_; }
  const String& other() const { return other_; }

 private:
  static constexpr size_t kTail = 48;
  uint8_t buf_[kTail + HTTP_UPLOAD_BUFLEN];
  size_t tailLen_ = 0;
  bool matched_ = false;
  String other_;
};

TagScanner tagScanner;

void failUpdate(const String& why) {
  if (!otaFailed) {
    otaFailed = true;
    otaError = why;
  }
  Update.abort();
}

void handleUpdateUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaFailed = false;
    otaError = "";
    tagScanner.reset();
    app.otaProgress = 0;
    app.otaLastWriteMs = millis();
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) failUpdate(Update.errorString());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (otaFailed) return;
    app.otaLastWriteMs = millis();
    tagScanner.feed(up.buf, up.currentSize);
    if (Update.write(up.buf, up.currentSize) != up.currentSize) failUpdate(Update.errorString());
    int total = server.clientContentLength();
    int pct = total > 0 ? static_cast<int>(static_cast<int64_t>(up.totalSize) * 100 / total) : 0;
    if (pct != app.otaProgress) {
      app.otaProgress = pct;
      app.display.renderer().drawMessage("UPDATE", "", pct, millis());
      app.display.present();
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (otaFailed) return;
    // A wrong-board image would not boot (different flash layout), so it is
    // never marked bootable.
    if (!tagScanner.matched()) {
      failUpdate(tagScanner.other().length()
                     ? "this firmware is for " + tagScanner.other() + ", but this display is a " + board().name
                     : String("not a Head-Up Display firmware image"));
    } else if (!Update.end(true)) {
      failUpdate(Update.errorString());
    } else {
      // The new image is now the boot partition; restart even if the
      // browser disconnects before the done handler runs.
      app.scheduleReboot(1500);
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    // The done handler is not called after an abort.
    failUpdate("upload aborted");
    app.otaProgress = -1;
  }
}

void handleNotFound() {
  if (!isOwnHost(server.hostHeader())) {
    redirectToPortal();
    return;
  }
  sendError(404, "not found");
}

}  // namespace

void begin() {
  server.on("/", HTTP_GET, handleIndex);
  server.on("/index.html", HTTP_GET, handleIndex);
  server.on("/api/config", HTTP_GET, handleGetConfig);
  server.on("/api/config", HTTP_POST, handlePostConfig);
  server.on("/api/export", HTTP_GET, handleExport);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/sniff", HTTP_GET, handleSniff);
  server.on("/api/scan", HTTP_POST, handleScanStart);
  server.on("/api/scan", HTTP_GET, handleScanGet);
  server.on("/api/discover", HTTP_POST, handleDiscoverStart);
  server.on("/api/discover", HTTP_GET, handleDiscoverGet);
  server.on("/api/frame", HTTP_GET, handleFrame);
  server.on("/api/test-pattern", HTTP_POST, handleTestPattern);
  server.on("/api/info", HTTP_POST, handleInfo);
  server.on("/api/time", HTTP_POST, handleSetTime);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/factory-reset", HTTP_POST, handleFactoryReset);
  server.on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  // OS captive-portal probes: anything but the expected answer makes the
  // phone open the portal page.
  for (const char* probe : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                            "/connecttest.txt", "/ncsi.txt", "/canonical.html", "/success.txt"}) {
    server.on(probe, HTTP_GET, redirectToPortal);
  }
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  bool wantDns = app.wifi.apActive();
  if (wantDns && !dnsRunning) {
    dns.setErrorReplyCode(DNSReplyCode::NoError);
    dnsRunning = dns.start(53, "*", BoatWifi::kApIp);
  } else if (!wantDns && dnsRunning) {
    dns.stop();
    dnsRunning = false;
  }
  if (dnsRunning) dns.processNextRequest();
  server.handleClient();
}

}  // namespace portal
