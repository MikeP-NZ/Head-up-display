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
    {Item::Twd, "twd", "TWD", "True wind direction", ItemFormat::Bearing},
    {Item::Hdg, "hdg", "HDG", "Heading", ItemFormat::Bearing},
    {Item::Dtw, "dtw", "DTW", "Distance to waypoint", ItemFormat::Distance},
    {Item::Vmg, "vmg", "VMG", "Velocity made good to wind", ItemFormat::Speed},
    {Item::Vmc, "vmc", "VMC", "Velocity made good to waypoint", ItemFormat::Speed},
    {Item::Dpt, "dpt", "DPT", "Depth", ItemFormat::Depth},
    {Item::Btw, "btw", "BTW", "Bearing to waypoint", ItemFormat::Bearing},
    {Item::Cog, "cog", "COG", "Course over ground", ItemFormat::Bearing},
    {Item::Mtw, "mtw", "MTW", "Water temperature", ItemFormat::Temperature},
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
    {Quantity::Btw, Item::Btw}, {Quantity::Cog, Item::Cog}, {Quantity::Mtw, Item::Mtw},
};

constexpr float kNauticalMile = 1852.0f;  // m
constexpr float kMile = 1609.344f;        // m
constexpr float kYard = 0.9144f;          // m
constexpr float kFoot = 0.3048f;          // m

float metresPer(DistanceUnit u) {
  switch (u) {
    case DistanceUnit::Kilometers: return 1000.0f;
    case DistanceUnit::Miles: return kMile;
    default: return kNauticalMile;
  }
}

const char* distanceLabel(DistanceUnit u) {
  switch (u) {
    case DistanceUnit::Kilometers: return "km";
    case DistanceUnit::Miles: return "mi";
    default: return "nm";
  }
}

float metresPer(ShortDistanceUnit u) {
  switch (u) {
    case ShortDistanceUnit::Yards: return kYard;
    case ShortDistanceUnit::Feet: return kFoot;
    default: return 1.0f;
  }
}

const char* shortDistanceLabel(ShortDistanceUnit u) {
  switch (u) {
    case ShortDistanceUnit::Yards: return "yd";
    case ShortDistanceUnit::Feet: return "ft";
    default: return "m";
  }
}

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
  q = magneticTwin(q);
  for (const QuantityItem& m : kQuantityItems) {
    if (m.q == q) return m.item;
  }
  return Item::None;
}

BoatModel::Reading readItem(const BoatModel& model, Item item, BearingRef ref, uint32_t nowMs) {
  if (item == Item::Speed) return model.speed(nowMs);
  Quantity q;
  if (!itemQuantity(item, q)) return BoatModel::Reading{};
  return model.bearing(q, ref, nowMs);
}

DisplayValue itemDisplay(Item item, float value, const DisplayUnits& units) {
  DisplayValue d;
  d.value = value;
  d.unit = itemUnit(item, units);
  switch (itemInfo(item).format) {
    case ItemFormat::Speed:
      d.value = convertSpeed(value, units.speed);
      break;
    case ItemFormat::Distance: {
      // Below 999.5 so a short distance never rounds to "1000".
      float shortValue = value / metresPer(units.shortDistance);
      if (units.shortDistance != ShortDistanceUnit::Off && shortValue < 999.5f) {
        d.value = shortValue;
        d.unit = shortDistanceLabel(units.shortDistance);
        d.shortDistance = true;
      } else {
        d.value = value / metresPer(units.distance);
      }
      break;
    }
    case ItemFormat::Depth:
      if (units.depth == DepthUnit::Feet) d.value = value / kFoot;
      break;
    case ItemFormat::Temperature:
      if (units.temperature == TempUnit::Fahrenheit) d.value = value * 1.8f + 32.0f;
      break;
    default:
      break;
  }
  return d;
}

const char* itemUnit(Item item, const DisplayUnits& units) {
  switch (itemInfo(item).format) {
    case ItemFormat::Speed: return unitLabel(units.speed);
    case ItemFormat::Bearing: return units.bearing == BearingRef::True ? "oT" : "oM";
    case ItemFormat::Distance: return distanceLabel(units.distance);
    case ItemFormat::Depth: return units.depth == DepthUnit::Feet ? "ft" : "m";
    case ItemFormat::Temperature: return units.temperature == TempUnit::Fahrenheit ? "oF" : "oC";
    default: return "";
  }
}

char itemSide(Item item, float v) {
  if (itemInfo(item).format != ItemFormat::WindAngle) return 0;
  float a = fabsf(v);
  if (a < 0.5f || a >= 179.5f) return 0;
  return v < 0 ? 'P' : 'S';
}

void formatItem(Item item, bool valid, float v, int8_t decimals, char* buf, size_t len, bool shortDistance) {
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
      if (shortDistance) {
        snprintf(buf, len, "%d", static_cast<int>(lroundf(v)));
        return;
      }
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
    case ItemFormat::Temperature: {
      if (!valid) return dashes(buf, len, 2, 1);
      int d = fabsf(v) < 99.95f ? 1 : 0;
      if (fabsf(v) < 0.05f) v = 0;  // no "-0.0"
      snprintf(buf, len, "%.*f", d, v);
      padInteger(buf, len, 2);
      return;
    }
  }
}
