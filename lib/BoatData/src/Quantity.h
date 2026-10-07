#pragma once

#include <stddef.h>
#include <stdint.h>

// Every value the display tracks. Inside the firmware values are kept in
// SI-style units:
//   speeds (STW, SOG, AWS, TWS, VMG, VMC)  m/s; VMG/VMC may be negative
//   wind angles (AWA, TWA)                 degrees, -180..180, + = starboard
//   bearings (TWD, HDG, BTW, COG)          degrees, 0..360; magnetic, with a
//                                          *True twin for true bearings
//   distance (DTW), depth (DPT)            metres
enum class Quantity : uint8_t {
  Stw,  // speed through water
  Sog,  // speed over ground
  Awa,  // apparent wind angle
  Aws,  // apparent wind speed
  Twa,  // true wind angle
  Tws,  // true wind speed
  Twd,  // true wind direction (magnetic)
  Hdg,  // heading (magnetic)
  Dtw,  // distance to waypoint
  Vmg,  // velocity made good to wind
  Vmc,  // velocity made good to waypoint (closing velocity)
  Dpt,  // water depth
  Btw,  // bearing to waypoint (magnetic)
  Cog,  // course over ground (magnetic)
  // The same bearings referenced to true north. BoatModel converts between
  // the two with the most recent magnetic variation (see BoatModel::bearing).
  TwdTrue,
  HdgTrue,
  BtwTrue,
  CogTrue,
  Count,
  // Decoder-only value.
  Variation = Count,  // degrees, east positive
};

constexpr size_t kQuantityCount = static_cast<size_t>(Quantity::Count);

inline size_t quantityIndex(Quantity q) { return static_cast<size_t>(q); }

// "STW", "AWA", ... (also "HDG" for HdgTrue etc.)
const char* quantityLabel(Quantity q);

// AWA, TWA and the bearings are angles and are averaged as vectors.
bool isAngular(Quantity q);

// Bearings (TWD, HDG, BTW, COG and their true twins).
bool isBearing(Quantity q);
bool isTrueBearing(Quantity q);
// HdgTrue <-> Hdg etc. Other quantities are returned unchanged.
Quantity trueTwin(Quantity magnetic);
Quantity magneticTwin(Quantity trueBearing);

struct Sample {
  Quantity q;
  float value;
};

// Everything decoded from one NMEA 0183 sentence or NMEA 2000 PGN.
struct Decoded {
  static constexpr size_t kMax = 6;
  Sample samples[kMax];
  uint8_t count = 0;
  bool hasTime = false;
  int64_t epochSeconds = 0;

  void add(Quantity q, float v) {
    if (count < kMax) samples[count++] = Sample{q, v};
  }
  // Finds the first sample of `q`.
  bool get(Quantity q, float& v) const;
  bool has(Quantity q) const {
    float v;
    return get(q, v);
  }
};

// Wraps to -180..180 / 0..360.
float wrapSigned(float deg);
float wrapBearing(float deg);
