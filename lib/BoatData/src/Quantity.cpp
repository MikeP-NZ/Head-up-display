#include "Quantity.h"

#include <math.h>

const char* quantityLabel(Quantity q) {
  switch (q) {
    case Quantity::Stw: return "STW";
    case Quantity::Sog: return "SOG";
    case Quantity::Awa: return "AWA";
    case Quantity::Aws: return "AWS";
    case Quantity::Twa: return "TWA";
    case Quantity::Tws: return "TWS";
    case Quantity::Twd: return "TWD";
    case Quantity::Hdg: return "HDG";
    case Quantity::Dtw: return "DTW";
    case Quantity::Vmg: return "VMG";
    case Quantity::Vmc: return "VMC";
    case Quantity::Dpt: return "DPT";
    case Quantity::Btw: return "BTW";
    case Quantity::Cog: return "COG";
    case Quantity::TwdTrue: return "TWD";
    case Quantity::HdgTrue: return "HDG";
    case Quantity::BtwTrue: return "BTW";
    case Quantity::CogTrue: return "COG";
    case Quantity::Variation: return "VAR";
  }
  return "?";
}

namespace {

struct BearingPair {
  Quantity magnetic, trueNorth;
};

constexpr BearingPair kBearings[] = {{Quantity::Twd, Quantity::TwdTrue},
                                     {Quantity::Hdg, Quantity::HdgTrue},
                                     {Quantity::Btw, Quantity::BtwTrue},
                                     {Quantity::Cog, Quantity::CogTrue}};

}  // namespace

bool isAngular(Quantity q) { return q == Quantity::Awa || q == Quantity::Twa || isBearing(q); }

bool isBearing(Quantity q) {
  for (const BearingPair& b : kBearings) {
    if (b.magnetic == q || b.trueNorth == q) return true;
  }
  return false;
}

bool isTrueBearing(Quantity q) {
  for (const BearingPair& b : kBearings) {
    if (b.trueNorth == q) return true;
  }
  return false;
}

Quantity trueTwin(Quantity magnetic) {
  for (const BearingPair& b : kBearings) {
    if (b.magnetic == magnetic) return b.trueNorth;
  }
  return magnetic;
}

Quantity magneticTwin(Quantity trueBearing) {
  for (const BearingPair& b : kBearings) {
    if (b.trueNorth == trueBearing) return b.magnetic;
  }
  return trueBearing;
}

bool Decoded::get(Quantity q, float& v) const {
  for (uint8_t i = 0; i < count; i++) {
    if (samples[i].q == q) {
      v = samples[i].value;
      return true;
    }
  }
  return false;
}

float wrapSigned(float deg) {
  float d = fmodf(deg, 360.0f);
  if (d > 180.0f) d -= 360.0f;
  if (d <= -180.0f) d += 360.0f;
  return d;
}

float wrapBearing(float deg) {
  float d = fmodf(deg, 360.0f);
  if (d < 0) d += 360.0f;
  if (d >= 360.0f) d -= 360.0f;
  return d;
}
