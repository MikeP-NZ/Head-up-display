#pragma once

#include <stddef.h>
#include <stdint.h>

#include "BoatModel.h"
#include "Quantity.h"

// What a display section can show.
enum class Item : uint8_t {
  None = 0,
  Speed,  // STW or SOG, picked by the source mode (the classic speed display)
  Stw,
  Sog,
  Awa,
  Aws,
  Twa,
  Tws,
  Twd,
  Hdg,
  Dtw,
  Vmg,
  Vmc,
  Dpt,
  Btw,
  Cog,
  Mtw,
  Count,
};

enum class DepthUnit : uint8_t { Meters = 0, Feet = 1 };
enum class TempUnit : uint8_t { Celsius = 0, Fahrenheit = 1 };
enum class DistanceUnit : uint8_t { NauticalMiles = 0, Kilometers = 1, Miles = 2 };
// Distances that come to less than 1000 of this unit are shown in it instead
// of the distance unit (Off: always the distance unit).
enum class ShortDistanceUnit : uint8_t { Off = 0, Meters = 1, Yards = 2, Feet = 3 };

struct DisplayUnits {
  SpeedUnit speed = SpeedUnit::Knots;
  DepthUnit depth = DepthUnit::Meters;
  TempUnit temperature = TempUnit::Celsius;
  DistanceUnit distance = DistanceUnit::NauticalMiles;
  ShortDistanceUnit shortDistance = ShortDistanceUnit::Off;
  BearingRef bearing = BearingRef::Magnetic;
};

enum class ItemFormat : uint8_t {
  None,
  Speed,      // decimals per setting; VMC keeps its sign
  WindAngle,  // 0..180 with a port/starboard side
  Bearing,    // 000..359
  Distance,   // NM/km/mi, or whole m/yd/ft when short
  Depth,
  Temperature,  // one decimal
};

struct ItemInfo {
  Item item;
  const char* id;     // settings / API id, e.g. "awa"
  const char* label;  // panel label, e.g. "AWA"
  const char* name;   // portal name, e.g. "Apparent wind angle"
  ItemFormat format;
};

constexpr size_t kItemCount = static_cast<size_t>(Item::Count);
constexpr size_t kMaxSections = 4;

const ItemInfo& itemInfo(Item item);
bool itemFromId(const char* id, Item& out);

// The model quantity behind an item (the magnetic one for bearings). False
// for None and Speed (Speed uses BoatModel::speed()).
bool itemQuantity(Item item, Quantity& q);
// The item that shows one quantity (Stw -> Item::Stw, HdgTrue -> Item::Hdg, ...).
Item itemForQuantity(Quantity q);

// The current reading behind an item: Speed per the source mode, bearings
// referenced as `ref` asks, true wind and VMG received or calculated per the
// true wind mode (BoatModel::shown).
BoatModel::Reading readItem(const BoatModel& model, Item item, BearingRef ref, uint32_t nowMs);

struct DisplayValue {
  float value = 0;             // display units
  const char* unit = "";       // as itemUnit, or the short distance unit
  bool shortDistance = false;  // a distance in m/yd/ft (whole numbers)
};

// Internal units -> display units (knots etc., degrees, NM/km/mi or
// m/yd/ft, m/ft, C/F). Wind angles keep their sign: negative is port.
DisplayValue itemDisplay(Item item, float value, const DisplayUnits& units);

// Unit text for the panel, in capitals except the lower-case "o" that stands
// for a degree sign: "KN", "NM", "M", "FT", "oM", "oT", "oC", "oF"; wind angles
// return "" (the side is shown instead). Distances give the long unit; a
// short distance's unit comes from itemDisplay().
const char* itemUnit(Item item, const DisplayUnits& units);

// Port/starboard tag for a wind angle in display units: 'P', 'S' or 0 when
// dead ahead/astern or not a wind angle.
char itemSide(Item item, float displayValue);

// Text for the 7-segment renderer: digits, '.', '-' and ' ' (a blank digit
// slot). Values are padded to a stable number of integer slots so the digit
// size does not jump as the value changes. Invalid values give dashes.
// Short distances (see DisplayValue) are whole numbers.
void formatItem(Item item, bool valid, float displayValue, int8_t decimals, char* buf, size_t len,
                bool shortDistance = false);
