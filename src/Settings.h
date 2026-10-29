#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "Board.h"
#include "BoatModel.h"
#include "Items.h"
#include "LineDecoder.h"
#include "PanelMap.h"

enum class Transport : uint8_t { Tcp = 0, Udp = 1 };
enum class NightMode : uint8_t { Off = 0, On = 1, Schedule = 2 };

// How the panel's rows are multiplexed: how many rows each row address
// lights, so the scan rate is 1/(height / rows lit).
//   Standard:  two rows (1/16 scan on a 32 px high panel, 1/32 on 64 px).
//   FourScan:  four rows (1/8 on 32 px, 1/10 on 40 px, 1/13 on 52 px).
//   EightScan: eight rows (1/2 on 16 px).
// Four and eight need the pixel data remapped (see PanelMap.h). The Alt
// variants fill the row blocks in the other order, which some panels use.
enum class PanelScan : uint8_t { Standard = 0, FourScan = 1, FourScanAlt = 2, EightScan = 3, EightScanAlt = 4 };

struct Settings {
  // Boat Wi-Fi (the Yacht Devices access point, or the boat router)
  String staSsid;
  String staPass;  // empty = open network

  // Configuration portal
  String apSsid;  // empty = "HeadUp-XXXX"
  String apPass = "headupdisplay";
  String hostname = "headup";

  // Yacht Devices server port
  Transport transport = Transport::Tcp;
  DataProtocol protocol = DataProtocol::Auto;
  String host;  // empty = Wi-Fi gateway (the YD device when joined to its AP)
  uint16_t port = 1456;
  // Demo mode: simulated boat data instead of the gateway (see DemoBoat.h).
  bool demo = false;

  // Speed
  SourceMode sourceMode = SourceMode::PreferStw;
  SpeedUnit unit = SpeedUnit::Knots;
  int8_t decimals = -1;  // -1 = auto
  float smoothingSec = 1.0f;
  uint16_t staleSec = 5;
  uint32_t stwFilter = 0;
  uint32_t sogFilter = 0;
  DepthUnit depthUnit = DepthUnit::Meters;
  TempUnit tempUnit = TempUnit::Celsius;
  DistanceUnit distanceUnit = DistanceUnit::NauticalMiles;
  ShortDistanceUnit shortDistanceUnit = ShortDistanceUnit::Off;
  BearingRef bearingRef = BearingRef::Magnetic;
  // True wind and VMG: received, calculated where missing, or calculated.
  TrueWindMode trueWind = TrueWindMode::FillMissing;
  HeadingSource windHeading = HeadingSource::Hdg;
  // Magnetic variation to use while none is received (east positive).
  bool defaultVariationOn = false;
  float defaultVariation = 0;

  // Display
  uint8_t brightness = 160;
  uint32_t speedColor = 0xFFFFFF;
  uint32_t accentColor = 0x00A0FF;
  bool showSource = true;
  bool showUnit = true;
  uint8_t rotation = 0;  // quarter turns
  // Layout: 1, 2 or 4 sections, each showing one item. Portrait or
  // landscape follows the rotation.
  uint8_t sections = 1;
  Item items[kMaxSections] = {Item::Speed, Item::Aws, Item::Awa, Item::Dpt};
  NightMode nightMode = NightMode::Off;
  uint8_t nightBrightness = 24;
  uint32_t nightColor = 0xFF0000;
  uint16_t nightStartMin = 20 * 60;
  uint16_t nightEndMin = 6 * 60;
  int16_t utcOffsetMin = 0;

  // Panel hardware (changes need a reboot)
  uint16_t panelWidth = 64;
  uint16_t panelHeight = 64;
  uint8_t chain = 1;
  PanelScan scan = PanelScan::Standard;
  uint8_t scanChunk = 0;  // columns per zig-zag between row blocks; 0 = whole row
  bool chipPad = true;    // pad row blocks to whole 16-output driver chips (PanelMap.h)
  String chipSkip;        // unused outputs per row block, e.g. "15,31,47"; empty = at the end
  PanelDriver driver = board().defaultDriver;
  bool clkPhase = board().defaultClkPhase;
  uint8_t i2sSpeedMHz = 8;
  uint8_t latchBlanking = 1;
  uint8_t spwmProfile = 0;  // SM16380 config register set, see Sm16380Wave.h
  uint8_t spwmGrayBits = 16;
  String colorOrder = "RGB";
  PanelPins pins = board().defaultPins;  // boards without port selection
  String hubPort = board().defaultPort ? board().defaultPort : "";  // boards with several HUB75 outputs

  panelmap::Layout panelLayout() const;
  ModelConfig modelConfig() const;
  DisplayUnits displayUnits() const;
  String effectiveApSsid() const;
};

struct SettingsChanges {
  bool wifi = false;
  bool portal = false;
  bool data = false;
  bool speed = false;
  bool display = false;
  bool panel = false;  // reboot required
  bool any() const { return wifi || portal || data || speed || display || panel; }
};

String deviceId();  // last 2 bytes of the MAC, e.g. "1A2B"

void settingsToJson(const Settings& s, JsonObject root, bool includeSecrets);

// Merges the keys present in `root` into `s`. Returns false (and leaves `s`
// untouched) if any value is invalid.
bool settingsMergeJson(Settings& s, JsonObjectConst root, SettingsChanges& changes, String& error);

void settingsLoad(Settings& s);
bool settingsSave(const Settings& s);
void settingsErase();
