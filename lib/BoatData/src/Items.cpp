#include "Items.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace {

const ItemInfo kItems[] = {
    {Item::None, "none", "", "Nothing", ItemFormat::None},
    {Item::Speed, "speed", "SPD", "Boat speed (per source mode)", ItemFormat::Speed},
    {Item::Stw, "stw", "STW", "Speed through water", ItemFormat::Speed},
    {Item::Sog, "sog", "SOG", "Speed over ground", ItemFormat::Speed},
    {Item::Awa, "awa", "AWA", "Apparent wind angle", ItemFormat::WindAngle},
    {Item::Aws, "aws", "AWS", "Apparent wind speed", ItemFormat::Speed},
    {Item::Twa, "twa", "TWA", "True wind angle", ItemFormat::WindAngle},
    {Item::Tws, "tws", "TWS", "True wind speed", ItemFormat::Speed},
    {Item::Twd, "twd", "TWD", "True wind direction (magnetic)", ItemFormat::Bearing},
    {Item::Hdg, "hdg", "HDG", "Heading (magnetic)", ItemFormat::Bearing},
    {Item::Dtw, "dtw", "DTW", "Distance to waypoint", ItemFormat::Distance},
    {Item::Vmg, "vmg", "VMG", "Velocity made good to wind", ItemFormat::Speed},
    {Item::Vmc, "vmc", "VMC", "Velocity made good to waypoint", ItemFormat::Speed},
    {Item::Dpt, "dpt", "DPT", "Depth", ItemFormat::Depth},
};
static_assert(sizeof(kItems) / sizeof(kItems[0]) == kItemCount, "item table out of sync");

struct QuantityItem {
  Quantity q;
  Item item;
};

const QuantityItem kQuantityItems[] = {
    {Quantity::Stw, Item::Stw}, {Quantity::Sog, Item::Sog}, {Quantity::Awa, Item::Awa}, {Quantity::Aws, Item::Aws},
    {Quantity::Twa, Item::Twa}, {Quantity::Tws, Item::Tws}, {Quantity::Twd, Item::Twd}, {Quantity::Hdg, Item::Hdg},
    {Quantity::Dtw, Item::Dtw}, {Quantity::Vmg, Item::Vmg}, {Quantity::Vmc, Item::Vmc}, {Quantity::Dpt, Item::Dpt},
};

// Right-aligns `text` so it has at least `slots` digits before the '.'.
void padInteger(char* text, size_t len, int slots) {
  const char* dot = strchr(text, '.');
  int intDigits = dot ? static_cast<int>(dot - text) : static_cast<int>(strlen(text));
  int pad = slots - intDigits;
  size_t n = strlen(text);
  if (pad <= 0 || n + pad + 1 > len) return;
  memmove(text + pad, text, n + 1);
  memset(text, ' ', pad);
}

void dashes(char* buf, size_t len, int intSlots, int decimals) {
  char t[12];
  size_t n = 0;
  for (int i = 0; i < intSlots && n < sizeof(t) - 1; i++) t[n++] = '-';
  if (decimals > 0 && n < sizeof(t) - 1) t[n++] = '.';
  for (int i = 0; i < decimals && n < sizeof(t) - 1; i++) t[n++] = '-';
  t[n] = 0;
  snprintf(buf, len, "%s", t);
}

}  // namespace

const ItemInfo& itemInfo(Item item) {
  size_t i = static_cast<size_t>(item);
  return kItems[i < kItemCount ? i : 0];
}

bool itemFromId(const char* id, Item& out) {
  if (!id) return false;
  for (const ItemInfo& info : kItems) {
    if (strcasecmp(info.id, id) == 0) {
      out = info.item;
      return true;
    }
  }
  return false;
}

bool itemQuantity(Item item, Quantity& q) {
  for (const QuantityItem& m : kQuantityItems) {
    if (m.item == item) {
      q = m.q;
      return true;
    }
  }
  return false;
}

Item itemForQuantity(Quantity q) {
  for (const QuantityItem& m : kQuantityItems) {
    if (m.q == q) return m.item;
  }
  return Item::None;
}

float itemDisplayValue(Item item, float value, SpeedUnit speedUnit, DepthUnit depthUnit) {
  switch (itemInfo(item).format) {
    case ItemFormat::Speed: return convertSpeed(value, speedUnit);
    case ItemFormat::Distance: return value / 1852.0f;
    case ItemFormat::Depth: return depthUnit == DepthUnit::Feet ? value / 0.3048f : value;
    default: return value;
  }
}

const char* itemUnit(Item item, SpeedUnit speedUnit, DepthUnit depthUnit) {
  switch (itemInfo(item).format) {
    case ItemFormat::Speed: return unitLabel(speedUnit);
    case ItemFormat::Bearing: return "MAG";
    case ItemFormat::Distance: return "NM";
    case ItemFormat::Depth: return depthUnit == DepthUnit::Feet ? "FT" : "M";
    default: return "";
  }
}

char itemSide(Item item, float v) {
  if (itemInfo(item).format != ItemFormat::WindAngle) return 0;
  float a = fabsf(v);
  if (a < 0.5f || a >= 179.5f) return 0;
  return v < 0 ? 'P' : 'S';
}

void formatItem(Item item, bool valid, float v, int8_t decimals, char* buf, size_t len) {
  if (len == 0) return;
  buf[0] = 0;
  switch (itemInfo(item).format) {
    case ItemFormat::None:
      return;
    case ItemFormat::Speed: {
      if (item == Item::Vmg) v = fabsf(v);  // upwind and downwind both count
      int d = decimals >= 0 ? decimals : (fabsf(v) < 99.95f ? 1 : 0);
      if (!valid) return dashes(buf, len, 2, decimals >= 0 ? decimals : 1);
      if (fabsf(v) * powf(10.0f, static_cast<float>(d)) < 0.5f) v = 0;  // no "-0.0"
      snprintf(buf, len, "%.*f", d, v);
      padInteger(buf, len, 2);
      return;
    }
    case ItemFormat::WindAngle: {
      if (!valid) return dashes(buf, len, 3, 0);
      int a = static_cast<int>(lroundf(fabsf(v)));
      if (a > 180) a = 180;
      snprintf(buf, len, "%d", a);
      padInteger(buf, len, 3);
      return;
    }
    case ItemFormat::Bearing: {
      if (!valid) return dashes(buf, len, 3, 0);
      int b = static_cast<int>(lroundf(wrapBearing(v))) % 360;
      snprintf(buf, len, "%03d", b);
      return;
    }
    case ItemFormat::Distance: {
      if (!valid) return dashes(buf, len, 2, 1);
      int d = v < 9.995f ? 2 : v < 99.95f ? 1 : 0;
      if (v > 9999) v = 9999;
      snprintf(buf, len, "%.*f", d, v);
      return;
    }
    case ItemFormat::Depth: {
      if (!valid) return dashes(buf, len, 2, 1);
      int d = v < 99.95f ? 1 : 0;
      if (v > 9999) v = 9999;
      snprintf(buf, len, "%.*f", d, v);
      padInteger(buf, len, 2);
      return;
    }
  }
}
