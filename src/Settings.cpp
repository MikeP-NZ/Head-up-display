#include "Settings.h"

#include <Preferences.h>
#include <esp_mac.h>

#include "Sm16380Wave.h"

namespace {

constexpr const char* kNamespace = "headupcfg";
constexpr const char* kKey = "json";

template <typename E>
struct EnumName {
  E value;
  const char* name;
};

constexpr EnumName<ApMode> kApModes[] = {{ApMode::Always, "always"}, {ApMode::Fallback, "fallback"}};
constexpr EnumName<Transport> kTransports[] = {{Transport::Tcp, "tcp"}, {Transport::Udp, "udp"}};
constexpr EnumName<DataProtocol> kProtocols[] = {
    {DataProtocol::Auto, "auto"}, {DataProtocol::Nmea0183, "nmea0183"}, {DataProtocol::Raw, "raw"}};
constexpr EnumName<SourceMode> kSourceModes[] = {{SourceMode::Stw, "stw"},
                                                 {SourceMode::Sog, "sog"},
                                                 {SourceMode::PreferStw, "prefer_stw"},
                                                 {SourceMode::PreferSog, "prefer_sog"}};
constexpr EnumName<SpeedUnit> kUnits[] = {
    {SpeedUnit::Knots, "kn"}, {SpeedUnit::Kmh, "kmh"}, {SpeedUnit::Mph, "mph"}, {SpeedUnit::Mps, "mps"}};
constexpr EnumName<DepthUnit> kDepthUnits[] = {{DepthUnit::Meters, "m"}, {DepthUnit::Feet, "ft"}};
constexpr EnumName<TempUnit> kTempUnits[] = {{TempUnit::Celsius, "c"}, {TempUnit::Fahrenheit, "f"}};
constexpr EnumName<DistanceUnit> kDistanceUnits[] = {
    {DistanceUnit::NauticalMiles, "nm"}, {DistanceUnit::Kilometers, "km"}, {DistanceUnit::Miles, "mi"}};
constexpr EnumName<ShortDistanceUnit> kShortDistanceUnits[] = {{ShortDistanceUnit::Off, "off"},
                                                               {ShortDistanceUnit::Meters, "m"},
                                                               {ShortDistanceUnit::Yards, "yd"},
                                                               {ShortDistanceUnit::Feet, "ft"}};
constexpr EnumName<BearingRef> kBearingRefs[] = {{BearingRef::Magnetic, "mag"}, {BearingRef::True, "true"}};
constexpr EnumName<NightMode> kNightModes[] = {
    {NightMode::Off, "off"}, {NightMode::On, "on"}, {NightMode::Schedule, "schedule"}};
constexpr EnumName<PanelDriver> kDrivers[] = {{PanelDriver::ShiftReg, "SHIFTREG"}, {PanelDriver::FM6124, "FM6124"},
                                              {PanelDriver::FM6126A, "FM6126A"},   {PanelDriver::ICN2038S, "ICN2038S"},
                                              {PanelDriver::MBI5124, "MBI5124"},   {PanelDriver::DP3246, "DP3246"},
                                              {PanelDriver::SM16380, "SM16380"}};

constexpr EnumName<PanelScan> kScans[] = {
    {PanelScan::Standard, "standard"}, {PanelScan::FourScan, "four"}, {PanelScan::FourScanAlt, "four-alt"}};

template <typename E, size_t N>
const char* nameOf(const EnumName<E> (&table)[N], E value) {
  for (const auto& e : table) {
    if (e.value == value) return e.name;
  }
  return table[0].name;
}

template <typename E, size_t N>
bool valueOf(const EnumName<E> (&table)[N], const char* name, E& out) {
  if (!name) return false;
  for (const auto& e : table) {
    if (strcasecmp(e.name, name) == 0) {
      out = e.value;
      return true;
    }
  }
  return false;
}

String colorToHex(uint32_t c) {
  char buf[8];
  snprintf(buf, sizeof(buf), "#%06lx", static_cast<unsigned long>(c & 0xFFFFFF));
  return String(buf);
}

bool hexToColor(const char* s, uint32_t& out) {
  if (!s) return false;
  if (*s == '#') s++;
  if (strlen(s) != 6) return false;
  char* end = nullptr;
  unsigned long v = strtoul(s, &end, 16);
  if (*end) return false;
  out = static_cast<uint32_t>(v);
  return true;
}

struct PinField {
  const char* name;
  int8_t PanelPins::*member;
};

constexpr PinField kPinFields[] = {
    {"r1", &PanelPins::r1}, {"g1", &PanelPins::g1}, {"b1", &PanelPins::b1},   {"r2", &PanelPins::r2},
    {"g2", &PanelPins::g2}, {"b2", &PanelPins::b2}, {"a", &PanelPins::a},     {"b", &PanelPins::b},
    {"c", &PanelPins::c},   {"d", &PanelPins::d},   {"e", &PanelPins::e},     {"lat", &PanelPins::lat},
    {"oe", &PanelPins::oe}, {"clk", &PanelPins::clk}};

// Small helper so each field reads as one line below.
class Merger {
 public:
  Merger(String& error) : error_(error) {}

  // Network names and passwords may legitimately start or end with spaces,
  // so only trim where `trim` is set.
  void str(JsonObjectConst o, const char* key, String& out, size_t maxLen, bool trim = true) {
    JsonVariantConst v = o[key];
    if (v.isNull()) return;
    if (!v.is<const char*>()) return fail(key, "must be a string");
    String s = v.as<const char*>();
    if (trim) s.trim();
    if (s.length() > maxLen) return fail(key, "is too long");
    out = s;
  }

  template <typename T>
  void num(JsonObjectConst o, const char* key, T& out, long lo, long hi) {
    JsonVariantConst v = o[key];
    if (v.isNull()) return;
    if (!v.is<long>() && !v.is<float>()) return fail(key, "must be a number");
    long n = v.as<long>();
    if (n < lo || n > hi) return fail(key, "is out of range");
    out = static_cast<T>(n);
  }

  void flt(JsonObjectConst o, const char* key, float& out, float lo, float hi) {
    JsonVariantConst v = o[key];
    if (v.isNull()) return;
    if (!v.is<float>()) return fail(key, "must be a number");
    float f = v.as<float>();
    if (!(f >= lo && f <= hi)) return fail(key, "is out of range");
    out = f;
  }

  void boolean(JsonObjectConst o, const char* key, bool& out) {
    JsonVariantConst v = o[key];
    if (v.isNull()) return;
    if (!v.is<bool>()) return fail(key, "must be true or false");
    out = v.as<bool>();
  }

  void color(JsonObjectConst o, const char* key, uint32_t& out) {
    JsonVariantConst v = o[key];
    if (v.isNull()) return;
    if (!hexToColor(v.as<const char*>(), out)) fail(key, "must be a #rrggbb colour");
  }

  template <typename E, size_t N>
  void choice(JsonObjectConst o, const char* key, const EnumName<E> (&table)[N], E& out) {
    JsonVariantConst v = o[key];
    if (v.isNull()) return;
    if (!valueOf(table, v.as<const char*>(), out)) fail(key, "has an unknown value");
  }

  void fail(const char* key, const char* why) {
    if (error_.length() == 0) error_ = String(key) + " " + why;
  }

  bool ok() const { return error_.length() == 0; }

 private:
  String& error_;
};

}  // namespace

ModelConfig Settings::modelConfig() const {
  ModelConfig c;
  c.mode = sourceMode;
  c.smoothingSec = smoothingSec;
  c.staleMs = static_cast<uint32_t>(staleSec) * 1000;
  c.stwFilter = stwFilter;
  c.sogFilter = sogFilter;
  return c;
}

DisplayUnits Settings::displayUnits() const {
  DisplayUnits u;
  u.speed = unit;
  u.depth = depthUnit;
  u.temperature = tempUnit;
  u.distance = distanceUnit;
  u.shortDistance = shortDistanceUnit;
  u.bearing = bearingRef;
  return u;
}

String deviceId() {
  // Read from eFuse: WiFi.macAddress() returns nothing before Wi-Fi starts.
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char buf[5];
  snprintf(buf, sizeof(buf), "%02X%02X", mac[4], mac[5]);
  return String(buf);
}

String Settings::effectiveApSsid() const {
  return apSsid.length() ? apSsid : String("HeadUp-") + deviceId();
  //return apSsid.length() ? apSsid : String("Speed");
}

void settingsToJson(const Settings& s, JsonObject root, bool includeSecrets) {
  JsonObject wifi = root["wifi"].to<JsonObject>();
  wifi["ssid"] = s.staSsid;
  wifi["open"] = s.staPass.length() == 0;
  if (includeSecrets) wifi["password"] = s.staPass;

  JsonObject portal = root["portal"].to<JsonObject>();
  portal["apMode"] = nameOf(kApModes, s.apMode);
  portal["ssid"] = s.apSsid;
  portal["defaultSsid"] = String("HeadUp-") + deviceId();
  portal["open"] = s.apPass.length() == 0;
  if (includeSecrets) portal["password"] = s.apPass;
  portal["hostname"] = s.hostname;

  JsonObject data = root["data"].to<JsonObject>();
  data["transport"] = nameOf(kTransports, s.transport);
  data["protocol"] = nameOf(kProtocols, s.protocol);
  data["host"] = s.host;
  data["port"] = s.port;

  JsonObject speed = root["speed"].to<JsonObject>();
  speed["source"] = nameOf(kSourceModes, s.sourceMode);
  speed["unit"] = nameOf(kUnits, s.unit);
  speed["decimals"] = s.decimals;
  speed["smoothing"] = s.smoothingSec;
  speed["staleSec"] = s.staleSec;
  speed["stwFilter"] = s.stwFilter;
  speed["sogFilter"] = s.sogFilter;
  speed["depthUnit"] = nameOf(kDepthUnits, s.depthUnit);
  speed["tempUnit"] = nameOf(kTempUnits, s.tempUnit);
  speed["distanceUnit"] = nameOf(kDistanceUnits, s.distanceUnit);
  speed["shortDistanceUnit"] = nameOf(kShortDistanceUnits, s.shortDistanceUnit);
  speed["bearingRef"] = nameOf(kBearingRefs, s.bearingRef);

  JsonObject display = root["display"].to<JsonObject>();
  display["brightness"] = s.brightness;
  display["speedColor"] = colorToHex(s.speedColor);
  display["accentColor"] = colorToHex(s.accentColor);
  display["showSource"] = s.showSource;
  display["showUnit"] = s.showUnit;
  display["showStatus"] = s.showStatus;
  display["rotation"] = s.rotation;
  display["sections"] = s.sections;
  JsonArray items = display["items"].to<JsonArray>();
  for (Item it : s.items) items.add(itemInfo(it).id);
  JsonObject night = display["night"].to<JsonObject>();
  night["mode"] = nameOf(kNightModes, s.nightMode);
  night["brightness"] = s.nightBrightness;
  night["color"] = colorToHex(s.nightColor);
  night["start"] = s.nightStartMin;
  night["end"] = s.nightEndMin;
  night["utcOffset"] = s.utcOffsetMin;

  JsonObject panel = root["panel"].to<JsonObject>();
  panel["width"] = s.panelWidth;
  panel["height"] = s.panelHeight;
  panel["chain"] = s.chain;
  panel["scan"] = nameOf(kScans, s.scan);
  panel["driver"] = nameOf(kDrivers, s.driver);
  panel["clkPhase"] = s.clkPhase;
  panel["i2sSpeed"] = s.i2sSpeedMHz;
  panel["latchBlanking"] = s.latchBlanking;
  panel["spwmProfile"] = s.spwmProfile;
  panel["spwmGrayBits"] = s.spwmGrayBits;
  panel["colorOrder"] = s.colorOrder;
  if (board().portCount) panel["port"] = s.hubPort;
  JsonObject pins = panel["pins"].to<JsonObject>();
  for (const auto& f : kPinFields) pins[f.name] = s.pins.*(f.member);
}

bool settingsMergeJson(Settings& s, JsonObjectConst root, SettingsChanges& changes, String& error) {
  error = "";
  Settings n = s;
  Merger m(error);

  if (JsonObjectConst o = root["wifi"]) {
    m.str(o, "ssid", n.staSsid, 32, false);
    m.str(o, "password", n.staPass, 63, false);
    if (o["open"] == true) n.staPass = "";
    if (n.staPass.length() > 0 && n.staPass.length() < 8) m.fail("wifi password", "must be 8-63 characters");
  }
  if (JsonObjectConst o = root["portal"]) {
    m.choice(o, "apMode", kApModes, n.apMode);
    m.str(o, "ssid", n.apSsid, 32, false);
    m.str(o, "password", n.apPass, 63, false);
    if (o["open"] == true) n.apPass = "";
    if (n.apPass.length() > 0 && n.apPass.length() < 8) m.fail("portal password", "must be 8-63 characters");
    m.str(o, "hostname", n.hostname, 32);
    if (n.hostname.length() == 0) n.hostname = "headup";
    for (char c : n.hostname) {
      if (!isalnum(static_cast<unsigned char>(c)) && c != '-') {
        m.fail("hostname", "may only contain letters, digits and '-'");
        break;
      }
    }
  }
  if (JsonObjectConst o = root["data"]) {
    m.choice(o, "transport", kTransports, n.transport);
    m.choice(o, "protocol", kProtocols, n.protocol);
    m.str(o, "host", n.host, 63);
    m.num(o, "port", n.port, 1, 65535);
  }
  if (JsonObjectConst o = root["speed"]) {
    m.choice(o, "source", kSourceModes, n.sourceMode);
    m.choice(o, "unit", kUnits, n.unit);
    m.num(o, "decimals", n.decimals, -1, 2);
    m.flt(o, "smoothing", n.smoothingSec, 0.0f, 30.0f);
    m.num(o, "staleSec", n.staleSec, 1, 120);
    m.num(o, "stwFilter", n.stwFilter, 0, 0x2FFFF);
    m.num(o, "sogFilter", n.sogFilter, 0, 0x2FFFF);
    m.choice(o, "depthUnit", kDepthUnits, n.depthUnit);
    m.choice(o, "tempUnit", kTempUnits, n.tempUnit);
    m.choice(o, "distanceUnit", kDistanceUnits, n.distanceUnit);
    m.choice(o, "shortDistanceUnit", kShortDistanceUnits, n.shortDistanceUnit);
    m.choice(o, "bearingRef", kBearingRefs, n.bearingRef);
  }
  if (JsonObjectConst o = root["display"]) {
    m.num(o, "brightness", n.brightness, 0, 255);
    m.color(o, "speedColor", n.speedColor);
    m.color(o, "accentColor", n.accentColor);
    m.boolean(o, "showSource", n.showSource);
    m.boolean(o, "showUnit", n.showUnit);
    m.boolean(o, "showStatus", n.showStatus);
    m.num(o, "rotation", n.rotation, 0, 3);
    m.num(o, "sections", n.sections, 1, 4);
    if (n.sections == 3) m.fail("sections", "must be 1, 2 or 4");
    JsonVariantConst items = o["items"];
    if (!items.isNull()) {
      if (!items.is<JsonArrayConst>() || items.size() > kMaxSections) {
        m.fail("items", "must be a list of up to 4 items");
      } else {
        size_t i = 0;
        for (JsonVariantConst id : items.as<JsonArrayConst>()) {
          if (!itemFromId(id.as<const char*>(), n.items[i++])) {
            m.fail("items", "has an unknown item");
            break;
          }
        }
      }
    }
    if (n.items[0] == Item::None) m.fail("items", "the first section must show something");
    if (JsonObjectConst night = o["night"]) {
      m.choice(night, "mode", kNightModes, n.nightMode);
      m.num(night, "brightness", n.nightBrightness, 0, 255);
      m.color(night, "color", n.nightColor);
      m.num(night, "start", n.nightStartMin, 0, 1439);
      m.num(night, "end", n.nightEndMin, 0, 1439);
      m.num(night, "utcOffset", n.utcOffsetMin, -14 * 60, 14 * 60);
    }
  }
  if (JsonObjectConst o = root["panel"]) {
    m.num(o, "width", n.panelWidth, 16, 128);
    m.num(o, "height", n.panelHeight, 16, 128);
    m.num(o, "chain", n.chain, 1, 4);
    m.choice(o, "scan", kScans, n.scan);
    m.choice(o, "driver", kDrivers, n.driver);
    m.boolean(o, "clkPhase", n.clkPhase);
    m.num(o, "i2sSpeed", n.i2sSpeedMHz, 2, 20);
    m.num(o, "latchBlanking", n.latchBlanking, 1, 4);
    m.num(o, "spwmProfile", n.spwmProfile, 0, sm16380::kProfileCount - 1);
    m.num(o, "spwmGrayBits", n.spwmGrayBits, 14, 16);
    m.str(o, "colorOrder", n.colorOrder, 3);
    n.colorOrder.toUpperCase();
    if (n.colorOrder.length() != 3 || n.colorOrder.indexOf('R') < 0 || n.colorOrder.indexOf('G') < 0 ||
        n.colorOrder.indexOf('B') < 0) {
      m.fail("colorOrder", "must be a permutation of RGB");
    }
    const bool spwm = n.driver == PanelDriver::SM16380;
    if (n.scan != PanelScan::Standard && n.panelHeight != 32 && n.panelHeight != 16 &&
        !(spwm && n.panelHeight == 64)) {
      m.fail("scan", "four-row scan needs a 32 px high panel (or 16 px; 64 px with SM16380)");
    }
    const bool slowClock = spwm && (n.i2sSpeedMHz == 2 || n.i2sSpeedMHz == 5);
    if (n.i2sSpeedMHz != 8 && n.i2sSpeedMHz != 16 && n.i2sSpeedMHz != 20 && !slowClock) {
      m.fail("i2sSpeed", "must be 8, 16 or 20 (or 2 or 5 for SM16380)");
    }
    if (n.spwmGrayBits != 14 && n.spwmGrayBits != 16) m.fail("spwmGrayBits", "must be 14 or 16");
    if (spwm) {
      if (n.panelWidth % 16 != 0) m.fail("width", "must be a multiple of 16 for SM16380 panels");
      if (n.panelHeight % 2 != 0 || n.panelHeight > 64) m.fail("height", "must be even and at most 64 for SM16380 panels");
    }
    if (JsonObjectConst pins = o["pins"]) {
      for (const auto& f : kPinFields) {
        int8_t& pin = n.pins.*(f.member);
        m.num(pins, f.name, pin, -1, 48);
      }
    }
    if (o["pins"] == "default") n.pins = board().defaultPins;
    for (const auto& f : kPinFields) {
      int8_t pin = n.pins.*(f.member);
      bool optional = strcmp(f.name, "e") == 0 && pin == -1;
      if (!optional && !isUsableOutputPin(pin)) {
        m.fail(f.name, "is not a usable GPIO on this board");
        break;
      }
    }
    if (board().portCount) {
      m.str(o, "port", n.hubPort, 8);
      n.hubPort.toUpperCase();
      if (!findPort(n.hubPort.c_str())) m.fail("port", "is not an output on this board");
    }
  }

  if (!m.ok()) return false;

  JsonDocument before, after;
  settingsToJson(s, before.to<JsonObject>(), true);
  settingsToJson(n, after.to<JsonObject>(), true);
  auto differs = [&](const char* group) { return before[group] != after[group]; };
  changes.wifi = differs("wifi");
  changes.portal = differs("portal");
  changes.data = differs("data");
  changes.speed = differs("speed");
  changes.display = differs("display");
  changes.panel = differs("panel");
  s = n;
  return true;
}

void settingsLoad(Settings& s) {
  s = Settings{};

  Preferences prefs;
  // Open read-write: a read-only open fails with NOT_FOUND until the
  // namespace exists, which is always the case on first boot.
  if (!prefs.begin(kNamespace, false)) {
    log_w("settings storage unavailable, using defaults");
    return;
  }
  bool stored = prefs.isKey(kKey);  // avoids getString() logging NOT_FOUND
  String json = stored ? prefs.getString(kKey, "") : String();
  prefs.end();

  if (json.length() == 0) {
    log_w("no stored settings, saving defaults");
    if (!settingsSave(s)) log_e("could not save default settings");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    log_e("stored settings are corrupt, using defaults");
    return;
  }
  SettingsChanges ignored;
  String error;
  if (!settingsMergeJson(s, doc.as<JsonObjectConst>(), ignored, error)) {
    log_e("stored settings rejected (%s), using defaults", error.c_str());
    s = Settings{};
  }
}

bool settingsSave(const Settings& s) {
  JsonDocument doc;
  settingsToJson(s, doc.to<JsonObject>(), true);
  String json;
  serializeJson(doc, json);
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  bool ok = prefs.putString(kKey, json) == json.length();
  prefs.end();
  return ok;
}

void settingsErase() {
  Preferences prefs;
  if (prefs.begin(kNamespace, false)) {
    prefs.clear();
    prefs.end();
  }
}
