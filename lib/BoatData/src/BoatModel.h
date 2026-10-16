#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Quantity.h"

enum class SourceMode : uint8_t {
  Stw = 0,        // speed through water only
  Sog = 1,        // speed over ground only
  PreferStw = 2,  // STW, fall back to SOG when STW is stale
  PreferSog = 3,  // SOG, fall back to STW when SOG is stale
};

enum class SpeedUnit : uint8_t { Knots = 0, Kmh = 1, Mph = 2, Mps = 3 };

// Which north bearings are shown against.
enum class BearingRef : uint8_t { Magnetic = 0, True = 1 };

// Where TWA, TWS, TWD and VMG come from.
enum class TrueWindMode : uint8_t {
  Received = 0,     // only as sent by the instruments
  FillMissing = 1,  // as sent, else calculated
  Calculated = 2,   // always calculated, ignoring any that are sent
};

// The heading a calculated TWD is based on.
enum class HeadingSource : uint8_t { Hdg = 0, Cog = 1 };

float convertSpeed(float mps, SpeedUnit unit);
const char* unitLabel(SpeedUnit unit);

// Source identifiers. 0 means "any source".
//   NMEA 2000 (RAW):  0x10000 | source address
//   NMEA 0183:        0x20000 | talker id
uint32_t n2kSourceId(uint8_t address);
uint32_t nmeaSourceId(const char* talker);
void formatSourceId(uint32_t id, char* buf, size_t len);

struct SourceInfo {
  uint32_t id = 0;
  Quantity q = Quantity::Stw;
  char via[8] = {0};  // sentence or PGN the value came from
  float lastValue = 0;  // internal units (see Quantity.h)
  uint32_t lastMs = 0;
  uint32_t count = 0;
};

struct ModelConfig {
  SourceMode mode = SourceMode::PreferStw;  // for the combined "speed" value
  float smoothingSec = 1.0f;
  uint32_t staleMs = 5000;
  uint32_t stwFilter = 0;
  uint32_t sogFilter = 0;
  TrueWindMode trueWind = TrueWindMode::FillMissing;
  HeadingSource windHeading = HeadingSource::Hdg;
  // Used while no variation is received (degrees, east positive).
  bool hasDefaultVariation = false;
  float defaultVariation = 0;
};

// Latest smoothed value of every quantity, plus which bus devices sent them.
class BoatModel {
 public:
  static constexpr size_t kMaxSources = 24;
  // Magnetic variation changes slowly; keep using it for a while.
  static constexpr uint32_t kVariationValidMs = 30UL * 60 * 1000;

  struct Reading {
    bool valid = false;
    Quantity q = Quantity::Stw;
    float value = 0;  // internal units
    uint32_t ageMs = 0;
    bool calculated = false;  // worked out from other values, not received
  };

  void configure(const ModelConfig& cfg);
  const ModelConfig& config() const { return cfg_; }

  // Accepts every Quantity, including the decoder-only ones.
  void ingest(Quantity q, float value, uint32_t sourceId, const char* via, uint32_t nowMs);
  void ingest(const Decoded& d, uint32_t sourceId, const char* via, uint32_t nowMs);

  Reading get(Quantity q, uint32_t nowMs) const;
  // STW or SOG, picked by the source mode.
  Reading speed(uint32_t nowMs) const;
  // A bearing (either twin of it) in the requested reference: the value
  // received in that reference while it is current, otherwise the other one
  // converted with the magnetic variation. Other quantities are as get().
  Reading bearing(Quantity q, BearingRef ref, uint32_t nowMs) const;
  // What the display shows for `q`, bearings in `ref`: as bearing(), except
  // that TWA, TWS, TWD and VMG may be calculated, per the true wind mode.
  //   TWA, TWS  from AWA, AWS and the boat speed (see speed())
  //   TWD       heading (HDG or COG, per windHeading) + TWA
  //   VMG       boat speed x cos(TWA)
  // TWD and VMG use the TWA that would be shown, received or calculated.
  Reading shown(Quantity q, BearingRef ref, uint32_t nowMs) const;

  // The received variation while current, else the default, if set.
  bool variation(float& deg, uint32_t nowMs) const;

  size_t sourceCount() const { return nSources_; }
  const SourceInfo& source(size_t i) const { return sources_[i]; }

  void reset();

 private:
  struct Channel {
    bool has = false;
    float smoothed = 0;      // linear quantities
    float sx = 0, sy = 0;    // angular quantities: averaged unit vector
    uint32_t lastMs = 0;
  };

  void recordSource(Quantity q, float value, uint32_t sourceId, const char* via, uint32_t nowMs);
  Reading calculated(Quantity q, BearingRef ref, uint32_t nowMs) const;

  ModelConfig cfg_;
  Channel ch_[kQuantityCount];
  bool hasVariation_ = false;
  float variation_ = 0;
  uint32_t variationMs_ = 0;
  SourceInfo sources_[kMaxSources];
  size_t nSources_ = 0;
};
