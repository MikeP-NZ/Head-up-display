#pragma once

#include <stdint.h>

#include "BoatModel.h"
#include "YdRaw.h"

enum class DataProtocol : uint8_t { Auto = 0, Nmea0183 = 1, Raw = 2 };

const char* protocolLabel(DataProtocol p);

// Turns lines from a Yacht Devices server port into model samples, accepting
// NMEA 0183, YD RAW, or both (Auto).
class LineDecoder {
 public:
  struct Stats {
    uint32_t lines = 0;
    uint32_t nmeaLines = 0;
    uint32_t rawLines = 0;
    uint32_t badChecksum = 0;
    uint32_t ignored = 0;  // not parseable, or wrong protocol for the setting
    uint32_t samples = 0;  // values passed to the model
    DataProtocol detected = DataProtocol::Auto;  // Auto = nothing recognised yet
  };

  // DBT is ignored while DPT (which includes the transducer offset) arrives.
  static constexpr uint32_t kDptPreferMs = 10000;

  void setProtocol(DataProtocol p) { protocol_ = p; }
  DataProtocol protocol() const { return protocol_; }

  void feed(const char* line, uint32_t nowMs, BoatModel& model);

  // Returns true once per new UTC time seen on the bus.
  bool takeTime(int64_t& epochSeconds);

  const Stats& stats() const { return stats_; }
  void resetStats() { stats_ = Stats{}; }

 private:
  void deliver(const Decoded& d, uint32_t src, const char* via, uint32_t nowMs, BoatModel& model);

  DataProtocol protocol_ = DataProtocol::Auto;
  Stats stats_;
  ydraw::Decoder raw_;
  bool timePending_ = false;
  int64_t time_ = 0;
  bool seenDpt_ = false;
  uint32_t lastDptMs_ = 0;
};
