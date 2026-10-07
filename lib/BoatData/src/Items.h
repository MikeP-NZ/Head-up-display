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
  Count,
};

enum class DepthUnit : uint8_t { Meters = 0, Feet = 1 };

enum class ItemFormat : uint8_t {
  None,
  Speed,      // decimals per setting; VMC keeps its sign
  WindAngle,  // 0..180 with a port/starboard side
  Bearing,    // 000..359
  Distance,   // nautical miles
  Depth,
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

// The model quantity behind an item. False for None and Speed (Speed uses
// BoatModel::speed()).
bool itemQuantity(Item item, Quantity& q);
// The item that shows one quantity (Stw -> Item::Stw, ...).
Item itemForQuantity(Quantity q);

// Internal units -> display units (knots etc., degrees, NM, m/ft). Wind
// angles keep their sign: negative is port.
float itemDisplayValue(Item item, float value, SpeedUnit speedUnit, DepthUnit depthUnit);

// Unit text for the panel: "KN", "NM", "M", "FT", "MAG"; wind angles return
// "" (the side is shown instead).
const char* itemUnit(Item item, SpeedUnit speedUnit, DepthUnit depthUnit);

// Port/starboard tag for a wind angle in display units: 'P', 'S' or 0 when
// dead ahead/astern or not a wind angle.
char itemSide(Item item, float displayValue);

// Text for the 7-segment renderer: digits, '.', '-' and ' ' (a blank digit
// slot). Values are padded to a stable number of integer slots so the digit
// size does not jump as the value changes. Invalid values give dashes.
void formatItem(Item item, bool valid, float displayValue, int8_t decimals, char* buf, size_t len);
