// Renders one panel screen with the firmware's Renderer and writes the raw
// RGB565 (little-endian) canvas to stdout. Used by render_png.py and
// mock_device.py, so what you see off-device is the firmware's drawing code.
//
//   preview speed value=6.4 kind=stw unit=kn
//   preview dash width=64 height=32 sections=4 items=stw,awa,aws,dpt values=6.4,-42,14.2,8.3
//       (values in display units; wind angles negative = port; valid=1,1,0,1)
//   preview info setup=1 apssid=Speed-1A2B appass=boatspeed
//   preview test t=7000
//   preview message title=UPDATE progress=40

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "Renderer.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: preview speed|info|test|message|splash [key=value...]\n");
    return 2;
  }
  std::string screen = argv[1];
  std::map<std::string, std::string> kv;
  for (int i = 2; i < argc; i++) {
    std::string a = argv[i];
    size_t eq = a.find('=');
    if (eq != std::string::npos) kv[a.substr(0, eq)] = a.substr(eq + 1);
  }
  auto str = [&](const char* k, const char* def) { return kv.count(k) ? kv[k] : std::string(def); };
  auto num = [&](const char* k, double def) { return kv.count(k) ? atof(kv[k].c_str()) : def; };

  int width = static_cast<int>(num("width", 64));
  int height = static_cast<int>(num("height", 64));
  Renderer r(width, height);

  RenderStyle style;
  style.speedRgb = strtoul(str("color", "FFFFFF").c_str(), nullptr, 16);
  style.accentRgb = strtoul(str("accent", "00A0FF").c_str(), nullptr, 16);
  style.nightRgb = strtoul(str("nightcolor", "FF0000").c_str(), nullptr, 16);
  style.showSource = num("showsource", 1) != 0;
  style.showUnit = num("showunit", 1) != 0;
  style.showStatus = num("showstatus", 1) != 0;
  std::string unit = str("unit", "kn");
  style.depthUnit = str("depthunit", "m") == "ft" ? DepthUnit::Feet : DepthUnit::Meters;
  style.unit = unit == "kmh" ? SpeedUnit::Kmh : unit == "mph" ? SpeedUnit::Mph : unit == "mps" ? SpeedUnit::Mps : SpeedUnit::Knots;
  style.decimals = static_cast<int8_t>(num("decimals", -1));
  style.rotation = static_cast<uint8_t>(num("rotation", 0));
  r.setStyle(style);
  r.setNight(num("night", 0) != 0);
  uint32_t now = static_cast<uint32_t>(num("now", 0));

  auto list = [&](const char* k) {
    std::vector<std::string> out;
    std::string v = str(k, "");
    size_t p = 0;
    while (p <= v.size() && !v.empty()) {
      size_t c = v.find(',', p);
      out.push_back(v.substr(p, c == std::string::npos ? std::string::npos : c - p));
      if (c == std::string::npos) break;
      p = c + 1;
    }
    return out;
  };

  if (screen == "speed" || screen == "dash") {
    DashView v;
    v.wifiUp = num("wifi", 1) != 0;
    v.linkUp = num("link", 1) != 0;
    v.message = str("msg", "").c_str();
    v.messageIsError = num("err", 0) != 0;
    if (screen == "speed") {
      // The classic single speed screen.
      v.sections = 1;
      v.items[0].item = Item::Speed;
      v.items[0].valid = num("valid", 1) != 0;
      v.items[0].value = static_cast<float>(num("value", 0));
      v.items[0].kind = str("kind", "stw") == "sog" ? Quantity::Sog : Quantity::Stw;
    } else {
      v.sections = static_cast<uint8_t>(num("sections", 4));
      std::vector<std::string> items = list("items"), values = list("values"), valid = list("valid");
      for (size_t i = 0; i < kMaxSections; i++) {
        ItemView& iv = v.items[i];
        if (i < items.size()) itemFromId(items[i].c_str(), iv.item);
        iv.value = i < values.size() ? static_cast<float>(atof(values[i].c_str())) : 0.0f;
        iv.valid = i < valid.size() ? valid[i] != "0" : i < values.size();
        iv.kind = str("kind", "stw") == "sog" ? Quantity::Sog : Quantity::Stw;
      }
    }
    r.drawDash(v, now);
  } else if (screen == "info") {
    InfoView v;
    v.setupMode = num("setup", 0) != 0;
    v.apActive = num("ap", 1) != 0;
    v.apSsid = str("apssid", "Speed-1A2B").c_str();
    v.apPass = str("appass", "boatspeed").c_str();
    v.apIp = "192.168.71.1";
    v.staSsid = str("ssid", "YDWG").c_str();
    v.staIp = str("ip", "192.168.4.2").c_str();
    v.hostname = str("hostname", "speed").c_str();
    r.drawInfo(v, static_cast<uint32_t>(num("page", 0)));
  } else if (screen == "test") {
    r.drawTestPattern(static_cast<uint32_t>(num("t", 0)));
  } else if (screen == "message") {
    r.drawMessage(str("title", "UPDATE").c_str(), str("detail", "").c_str(), static_cast<int>(num("progress", -1)), now);
  } else if (screen == "splash") {
    r.drawSplash(str("version", "1.0.0").c_str());
  } else {
    fprintf(stderr, "unknown screen '%s'\n", screen.c_str());
    return 2;
  }

  fwrite(r.frame(), sizeof(uint16_t), static_cast<size_t>(width) * height, stdout);
  return 0;
}
