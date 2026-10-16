#include "BoatModel.h"

#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <string.h>

namespace {

constexpr float kDegToRad = 0.017453293f;

}  // namespace

float convertSpeed(float mps, SpeedUnit unit) {
  switch (unit) {
    case SpeedUnit::Knots: return mps * 1.9438445f;
    case SpeedUnit::Kmh: return mps * 3.6f;
    case SpeedUnit::Mph: return mps * 2.2369363f;
    case SpeedUnit::Mps: return mps;
  }
  return mps;
}

const char* unitLabel(SpeedUnit unit) {
  switch (unit) {
    case SpeedUnit::Knots: return "KN";
    case SpeedUnit::Kmh: return "KM/H";
    case SpeedUnit::Mph: return "MPH";
    case SpeedUnit::Mps: return "M/S";
  }
  return "";
}

uint32_t n2kSourceId(uint8_t address) { return 0x10000u | address; }

uint32_t nmeaSourceId(const char* talker) {
  uint8_t a = talker && talker[0] ? static_cast<uint8_t>(talker[0]) : '?';
  uint8_t b = talker && talker[0] && talker[1] ? static_cast<uint8_t>(talker[1]) : '?';
  return 0x20000u | (static_cast<uint32_t>(a) << 8) | b;
}

void formatSourceId(uint32_t id, char* buf, size_t len) {
  if (id == 0) {
    snprintf(buf, len, "any");
  } else if ((id & 0xF0000u) == 0x10000u) {
    snprintf(buf, len, "N2K #%u", static_cast<unsigned>(id & 0xFF));
  } else if ((id & 0xF0000u) == 0x20000u) {
    snprintf(buf, len, "0183 %c%c", static_cast<char>((id >> 8) & 0xFF), static_cast<char>(id & 0xFF));
  } else {
    snprintf(buf, len, "0x%05X", static_cast<unsigned>(id));
  }
}

void BoatModel::configure(const ModelConfig& cfg) {
  bool filtersChanged = cfg.stwFilter != cfg_.stwFilter || cfg.sogFilter != cfg_.sogFilter;
  cfg_ = cfg;
  if (filtersChanged) {
    ch_[quantityIndex(Quantity::Stw)] = Channel{};
    ch_[quantityIndex(Quantity::Sog)] = Channel{};
  }
}

void BoatModel::reset() {
  for (Channel& c : ch_) c = Channel{};
  hasVariation_ = false;
  nSources_ = 0;
}

void BoatModel::recordSource(Quantity q, float value, uint32_t sourceId, const char* via, uint32_t nowMs) {
  SourceInfo* slot = nullptr;
  for (size_t i = 0; i < nSources_; i++) {
    SourceInfo& s = sources_[i];
    if (s.id == sourceId && s.q == q && strncmp(s.via, via ? via : "", sizeof(s.via)) == 0) {
      slot = &s;
      break;
    }
  }
  if (!slot) {
    if (nSources_ < kMaxSources) {
      slot = &sources_[nSources_++];
    } else {
      slot = &sources_[0];
      for (size_t i = 1; i < nSources_; i++) {
        if (nowMs - sources_[i].lastMs > nowMs - slot->lastMs) slot = &sources_[i];
      }
    }
    *slot = SourceInfo{};
    slot->id = sourceId;
    slot->q = q;
    strncpy(slot->via, via ? via : "", sizeof(slot->via) - 1);
  }
  slot->lastValue = value;
  slot->lastMs = nowMs;
  slot->count++;
}

bool BoatModel::variation(float& deg, uint32_t nowMs) const {
  if (hasVariation_ && nowMs - variationMs_ <= kVariationValidMs) {
    deg = variation_;
    return true;
  }
  if (cfg_.hasDefaultVariation) {
    deg = cfg_.defaultVariation;
    return true;
  }
  return false;
}

void BoatModel::ingest(const Decoded& d, uint32_t sourceId, const char* via, uint32_t nowMs) {
  // Variation first, so a true bearing in the same message can use it.
  for (uint8_t i = 0; i < d.count; i++) {
    if (d.samples[i].q == Quantity::Variation) ingest(Quantity::Variation, d.samples[i].value, sourceId, via, nowMs);
  }
  for (uint8_t i = 0; i < d.count; i++) {
    if (d.samples[i].q != Quantity::Variation) ingest(d.samples[i].q, d.samples[i].value, sourceId, via, nowMs);
  }
}

void BoatModel::ingest(Quantity q, float value, uint32_t sourceId, const char* via, uint32_t nowMs) {
  if (!(value == value)) return;  // NaN

  if (q == Quantity::Variation) {
    if (value < -180 || value > 180) return;
    hasVariation_ = true;
    variation_ = value;
    variationMs_ = nowMs;
    return;
  }
  if (q >= Quantity::Count) return;
  if (isBearing(q)) value = wrapBearing(value);

  switch (q) {
    case Quantity::Stw:
    case Quantity::Sog:
    case Quantity::Aws:
    case Quantity::Tws:
      if (value < 0 || value > 100) return;
      break;
    case Quantity::Vmg:
    case Quantity::Vmc:
      if (value < -100 || value > 100) return;
      break;
    case Quantity::Dpt:
    case Quantity::Dtw:
      if (value < 0) return;
      break;
    default:
      break;
  }

  recordSource(q, value, sourceId, via, nowMs);

  if (q == Quantity::Stw && cfg_.stwFilter != 0 && cfg_.stwFilter != sourceId) return;
  if (q == Quantity::Sog && cfg_.sogFilter != 0 && cfg_.sogFilter != sourceId) return;

  Channel& c = ch_[quantityIndex(q)];
  uint32_t dt = nowMs - c.lastMs;
  bool restart = !c.has || dt > cfg_.staleMs || cfg_.smoothingSec <= 0;
  float alpha = restart ? 1.0f : 1.0f - expf(-(dt / 1000.0f) / cfg_.smoothingSec);
  if (isAngular(q)) {
    float x = cosf(value * kDegToRad), y = sinf(value * kDegToRad);
    if (restart) {
      c.sx = x;
      c.sy = y;
    } else {
      c.sx += alpha * (x - c.sx);
      c.sy += alpha * (y - c.sy);
    }
  } else if (restart) {
    c.smoothed = value;
  } else {
    c.smoothed += alpha * (value - c.smoothed);
  }
  c.has = true;
  c.lastMs = nowMs;
}

BoatModel::Reading BoatModel::get(Quantity q, uint32_t nowMs) const {
  Reading r;
  r.q = q;
  if (q >= Quantity::Count) return r;
  const Channel& c = ch_[quantityIndex(q)];
  if (!c.has) return r;
  // A sample can be stamped just after the caller read the clock.
  int32_t age = static_cast<int32_t>(nowMs - c.lastMs);
  r.ageMs = age > 0 ? static_cast<uint32_t>(age) : 0;
  if (isAngular(q)) {
    float deg = atan2f(c.sy, c.sx) / kDegToRad;
    r.value = (q == Quantity::Awa || q == Quantity::Twa) ? wrapSigned(deg) : wrapBearing(deg);
  } else {
    r.value = c.smoothed;
  }
  r.valid = r.ageMs <= cfg_.staleMs;
  return r;
}

BoatModel::Reading BoatModel::speed(uint32_t nowMs) const {
  Reading stw = get(Quantity::Stw, nowMs);
  Reading sog = get(Quantity::Sog, nowMs);
  switch (cfg_.mode) {
    case SourceMode::Stw: return stw;
    case SourceMode::Sog: return sog;
    case SourceMode::PreferStw: return stw.valid || !sog.valid ? stw : sog;
    case SourceMode::PreferSog: return sog.valid || !stw.valid ? sog : stw;
  }
  return stw;
}

BoatModel::Reading BoatModel::bearing(Quantity q, BearingRef ref, uint32_t nowMs) const {
  if (!isBearing(q)) return get(q, nowMs);
  Quantity magnetic = magneticTwin(q);
  Quantity want = ref == BearingRef::True ? trueTwin(magnetic) : magnetic;
  Quantity other = ref == BearingRef::True ? magnetic : trueTwin(magnetic);
  Reading r = get(want, nowMs);
  if (r.valid) return r;
  Reading o = get(other, nowMs);
  float var;
  if (!o.valid || !variation(var, nowMs)) return r;
  // true = magnetic + variation (east positive)
  o.value = wrapBearing(ref == BearingRef::True ? o.value + var : o.value - var);
  o.q = want;
  return o;
}

BoatModel::Reading BoatModel::shown(Quantity q, BearingRef ref, uint32_t nowMs) const {
  Reading received = bearing(q, ref, nowMs);
  bool calculable = q == Quantity::Twa || q == Quantity::Tws || q == Quantity::Vmg || q == Quantity::Twd ||
                    q == Quantity::TwdTrue;
  if (!calculable || cfg_.trueWind == TrueWindMode::Received) return received;
  if (cfg_.trueWind == TrueWindMode::FillMissing && received.valid) return received;
  return calculated(q, ref, nowMs);
}

BoatModel::Reading BoatModel::calculated(Quantity q, BearingRef ref, uint32_t nowMs) const {
  Reading r;
  r.q = q;
  r.calculated = true;
  Reading boat = speed(nowMs);

  if (q == Quantity::Twa || q == Quantity::Tws) {
    // Apparent wind = true wind + the headwind of the boat's own speed.
    Reading awa = get(Quantity::Awa, nowMs), aws = get(Quantity::Aws, nowMs);
    if (!awa.valid || !aws.valid || !boat.valid) return r;
    float a = awa.value * kDegToRad;
    float x = aws.value * cosf(a) - boat.value, y = aws.value * sinf(a);
    r.value = q == Quantity::Tws ? hypotf(x, y) : wrapSigned(atan2f(y, x) / kDegToRad);
    r.ageMs = std::max({awa.ageMs, aws.ageMs, boat.ageMs});
    r.valid = true;
    return r;
  }

  Reading twa = shown(Quantity::Twa, ref, nowMs);
  if (!twa.valid) return r;
  if (q == Quantity::Vmg) {
    if (!boat.valid) return r;
    r.value = boat.value * cosf(twa.value * kDegToRad);
    r.ageMs = std::max(boat.ageMs, twa.ageMs);
  } else {  // TWD
    Reading heading = bearing(cfg_.windHeading == HeadingSource::Cog ? Quantity::Cog : Quantity::Hdg, ref, nowMs);
    if (!heading.valid) return r;
    r.q = ref == BearingRef::True ? Quantity::TwdTrue : Quantity::Twd;
    r.value = wrapBearing(heading.value + twa.value);
    r.ageMs = std::max(heading.ageMs, twa.ageMs);
  }
  r.valid = true;
  return r;
}
