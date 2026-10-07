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
    case Quantity::HdgTrue: return "HDG";
    case Quantity::TwdTrue: return "TWD";
    case Quantity::Variation: return "VAR";
  }
  return "?";
}

bool isAngular(Quantity q) {
  return q == Quantity::Awa || q == Quantity::Twa || q == Quantity::Twd || q == Quantity::Hdg;
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
