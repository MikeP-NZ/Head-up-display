#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Quantity.h"

// Yacht Devices "RAW" protocol (YDWG-02 / YDNR-02 / YDNU-02 manual, Appendix E):
//
//   hh:mm:ss.ddd D msgid b0 b1 b2 b3 b4 b5 b6 b7<CR><LF>
//   17:33:21.107 R 19F51323 01 2F 30 70 00 2F 30 70
//
// D is 'R' (NMEA 2000 -> app) or 'T' (app -> NMEA 2000), msgid is the 29-bit
// CAN identifier in hex, followed by 1..8 data bytes in hex.
namespace ydraw {

struct Frame {
  char direction = 'R';
  uint32_t canId = 0;
  uint32_t pgn = 0;
  uint8_t source = 0;
  uint8_t priority = 0;
  uint8_t len = 0;
  uint8_t data[8] = {0};
};

bool parseLine(const char* line, Frame& out);

uint32_t pgnFromCanId(uint32_t canId);

constexpr uint32_t kPgnSystemTime = 126992;
constexpr uint32_t kPgnVesselHeading = 127250;
constexpr uint32_t kPgnMagneticVariation = 127258;
constexpr uint32_t kPgnSpeedWater = 128259;
constexpr uint32_t kPgnWaterDepth = 128267;
constexpr uint32_t kPgnCogSogRapid = 129026;
constexpr uint32_t kPgnNavigationData = 129284;  // fast packet
constexpr uint32_t kPgnWindData = 130306;

// Decodes a complete PGN payload. Returns true if the PGN is one we
// understand (even if all its fields are "not available").
bool decodePgn(uint32_t pgn, const uint8_t* data, size_t len, Decoded& out);

// Turns RAW frames into values, reassembling NMEA 2000 fast-packet PGNs
// (those longer than 8 bytes) from their frames.
class Decoder {
 public:
  // Returns true when `f` completed a PGN we understand.
  bool feed(const Frame& f, Decoded& out);

  static bool isFastPacket(uint32_t pgn);

 private:
  static constexpr size_t kMaxPayload = 64;
  static constexpr size_t kSlots = 4;

  struct Assembly {
    bool active = false;
    uint32_t pgn = 0;
    uint8_t source = 0;
    uint8_t sequence = 0;
    uint8_t nextFrame = 0;
    uint8_t expected = 0;
    uint8_t received = 0;
    uint8_t data[kMaxPayload] = {0};
  };

  Assembly slots_[kSlots];
  size_t nextSlot_ = 0;
};

}  // namespace ydraw
