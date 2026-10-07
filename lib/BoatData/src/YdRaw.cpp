#include "YdRaw.h"

namespace ydraw {

namespace {

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

const char* skipSpaces(const char* p) {
  while (*p == ' ' || *p == '\t') p++;
  return p;
}

// Reads a whitespace-delimited token of hex digits. Returns digit count, 0 on
// a non-hex token.
int readHex(const char*& p, uint32_t& value) {
  value = 0;
  int digits = 0;
  while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
    int v = hexNibble(*p);
    if (v < 0 || digits >= 8) return 0;
    value = (value << 4) | static_cast<uint32_t>(v);
    digits++;
    p++;
  }
  return digits;
}

uint16_t u16(const uint8_t* d) { return static_cast<uint16_t>(d[0] | (d[1] << 8)); }

uint32_t u32(const uint8_t* d) {
  return static_cast<uint32_t>(d[0]) | (static_cast<uint32_t>(d[1]) << 8) |
         (static_cast<uint32_t>(d[2]) << 16) | (static_cast<uint32_t>(d[3]) << 24);
}

// NMEA 2000 reserves the top values of each field: 0xFFFF n/a, 0xFFFE out of
// range, 0xFFFD reserved.
bool validU16(uint16_t v) { return v < 0xFFFD; }

}  // namespace

uint32_t pgnFromCanId(uint32_t canId) {
  uint32_t pf = (canId >> 16) & 0xFF;
  uint32_t pgn = (canId >> 8) & 0x3FFFF;  // EDP + DP + PF + PS
  if (pf < 240) pgn &= 0x3FF00;           // PDU1: PS is a destination address
  return pgn;
}

bool parseLine(const char* line, Frame& out) {
  out = Frame{};
  if (!line) return false;
  const char* p = skipSpaces(line);

  // Optional timestamp "hh:mm:ss.ddd"
  const char* q = p;
  while (*q && *q != ' ' && *q != '\t') q++;
  bool hasColon = false;
  for (const char* r = p; r < q; r++) hasColon |= (*r == ':');
  if (hasColon) p = skipSpaces(q);

  // Optional direction
  if ((p[0] == 'R' || p[0] == 'T') && (p[1] == ' ' || p[1] == '\t')) {
    out.direction = p[0];
    p = skipSpaces(p + 1);
  } else if (hasColon) {
    return false;  // timestamped lines always carry a direction
  }

  uint32_t id;
  int digits = readHex(p, id);
  if (digits < 3 || id > 0x1FFFFFFF) return false;
  out.canId = id;
  out.pgn = pgnFromCanId(id);
  out.source = id & 0xFF;
  out.priority = (id >> 26) & 0x7;

  for (;;) {
    p = skipSpaces(p);
    if (!*p || *p == '\r' || *p == '\n') break;
    if (out.len >= 8) return false;
    uint32_t b;
    if (readHex(p, b) != 2) return false;
    out.data[out.len++] = static_cast<uint8_t>(b);
  }
  return out.len > 0;
}

namespace {

constexpr float kRad = 57.29578f;  // degrees per radian

int16_t i16(const uint8_t* d) { return static_cast<int16_t>(u16(d)); }

// Signed fields reserve 0x7FFD..0x7FFF the same way.
bool validI16(int16_t v) { return v < 0x7FFD; }

}  // namespace

bool decodePgn(uint32_t pgn, const uint8_t* d, size_t len, Decoded& out) {
  out = Decoded{};
  switch (pgn) {
    case kPgnSpeedWater: {
      // SID, STW u16 0.01 m/s, SOG u16 0.01 m/s, type, direction
      if (len < 5) return false;
      uint16_t stw = u16(&d[1]);
      uint16_t sog = u16(&d[3]);
      if (validU16(stw)) out.add(Quantity::Stw, stw * 0.01f);
      if (validU16(sog)) out.add(Quantity::Sog, sog * 0.01f);
      return true;
    }
    case kPgnCogSogRapid: {
      // SID, COG ref (2 bits), COG u16 1e-4 rad, SOG u16 0.01 m/s
      if (len < 6) return false;
      uint16_t sog = u16(&d[4]);
      if (validU16(sog)) out.add(Quantity::Sog, sog * 0.01f);
      return true;
    }
    case kPgnSystemTime: {
      // SID, source (4 bits), days since 1970 u16, seconds since midnight u32 1e-4 s
      if (len < 8) return false;
      uint16_t days = u16(&d[2]);
      uint32_t t = u32(&d[4]);
      if (validU16(days) && t < 0xFFFFFFFD && t / 10000 < 86400) {
        out.hasTime = true;
        out.epochSeconds = static_cast<int64_t>(days) * 86400 + t / 10000;
      }
      return true;
    }
    case kPgnWindData: {
      // SID, speed u16 0.01 m/s, angle u16 1e-4 rad, reference (3 bits):
      // 0 true (ground, north), 1 magnetic (ground, north), 2 apparent,
      // 3 true (boat), 4 true (water)
      if (len < 6) return false;
      uint16_t speed = u16(&d[1]);
      uint16_t angle = u16(&d[3]);
      uint8_t ref = d[5] & 0x07;
      float deg = angle * 1e-4f * kRad;
      bool hasAngle = validU16(angle), hasSpeed = validU16(speed);
      switch (ref) {
        case 0:
          if (hasAngle) out.add(Quantity::TwdTrue, wrapBearing(deg));
          break;
        case 1:
          if (hasAngle) out.add(Quantity::Twd, wrapBearing(deg));
          break;
        case 2:
          if (hasAngle) out.add(Quantity::Awa, wrapSigned(deg));
          if (hasSpeed) out.add(Quantity::Aws, speed * 0.01f);
          break;
        case 3:
        case 4:
          if (hasAngle) out.add(Quantity::Twa, wrapSigned(deg));
          if (hasSpeed) out.add(Quantity::Tws, speed * 0.01f);
          break;
        default:
          break;
      }
      return true;
    }
    case kPgnVesselHeading: {
      // SID, heading u16 1e-4 rad, deviation i16, variation i16 (1e-4 rad),
      // reference (2 bits): 0 true, 1 magnetic
      if (len < 8) return false;
      uint16_t hdg = u16(&d[1]);
      int16_t dev = i16(&d[3]);
      int16_t var = i16(&d[5]);
      uint8_t ref = d[7] & 0x03;
      if (validU16(hdg)) {
        float deg = hdg * 1e-4f * kRad;
        if (ref == 1) {
          if (validI16(dev)) deg += dev * 1e-4f * kRad;
          out.add(Quantity::Hdg, wrapBearing(deg));
        } else if (ref == 0) {
          out.add(Quantity::HdgTrue, wrapBearing(deg));
        }
      }
      if (validI16(var)) out.add(Quantity::Variation, var * 1e-4f * kRad);
      return true;
    }
    case kPgnMagneticVariation: {
      // SID, source (4 bits), age of service (days) u16, variation i16 1e-4 rad
      if (len < 6) return false;
      int16_t var = i16(&d[4]);
      if (validI16(var)) out.add(Quantity::Variation, var * 1e-4f * kRad);
      return true;
    }
    case kPgnWaterDepth: {
      // SID, depth below transducer u32 0.01 m, offset i16 0.001 m, range
      if (len < 7) return false;
      uint32_t depth = u32(&d[1]);
      int16_t offset = i16(&d[5]);
      if (depth < 0xFFFFFFFD) {
        float m = depth * 0.01f;
        if (validI16(offset)) m += offset * 0.001f;
        if (m >= 0) out.add(Quantity::Dpt, m);
      }
      return true;
    }
    case kPgnNavigationData: {
      // SID, distance to waypoint u32 0.01 m, flags, ETA time u32, ETA date
      // u16, bearing orig->dest u16, bearing pos->dest u16, origin id u32,
      // destination id u32, lat i32, lon i32, closing velocity i16 0.01 m/s
      if (len < 34) return false;
      uint32_t dtw = u32(&d[1]);
      int16_t vmc = i16(&d[32]);
      if (dtw < 0xFFFFFFFD) out.add(Quantity::Dtw, dtw * 0.01f);
      if (validI16(vmc)) out.add(Quantity::Vmc, vmc * 0.01f);
      return true;
    }
    default:
      return false;
  }
}

bool Decoder::isFastPacket(uint32_t pgn) { return pgn == kPgnNavigationData; }

bool Decoder::feed(const Frame& f, Decoded& out) {
  if (!isFastPacket(f.pgn)) return decodePgn(f.pgn, f.data, f.len, out);
  if (f.len < 2) return false;

  // Fast packet: byte 0 = sequence (3 bits) | frame counter (5 bits).
  // Frame 0 carries the total length in byte 1 and 6 payload bytes; the
  // following frames carry 7 payload bytes each.
  uint8_t seq = f.data[0] >> 5;
  uint8_t frame = f.data[0] & 0x1F;
  Assembly* a = nullptr;
  for (Assembly& s : slots_) {
    if (s.active && s.pgn == f.pgn && s.source == f.source) {
      a = &s;
      break;
    }
  }
  if (frame == 0) {
    if (!a) {
      for (Assembly& s : slots_) {
        if (!s.active) {
          a = &s;
          break;
        }
      }
    }
    if (!a) {
      a = &slots_[nextSlot_];
      nextSlot_ = (nextSlot_ + 1) % kSlots;
    }
    *a = Assembly{};
    a->active = true;
    a->pgn = f.pgn;
    a->source = f.source;
    a->sequence = seq;
    a->expected = f.data[1];
    if (a->expected > kMaxPayload || a->expected == 0) {
      a->active = false;
      return false;
    }
    for (uint8_t i = 2; i < f.len && a->received < a->expected; i++) a->data[a->received++] = f.data[i];
    a->nextFrame = 1;
  } else {
    if (!a || a->sequence != seq || a->nextFrame != frame) {
      if (a) a->active = false;  // lost a frame
      return false;
    }
    for (uint8_t i = 1; i < f.len && a->received < a->expected; i++) a->data[a->received++] = f.data[i];
    a->nextFrame++;
  }
  if (a->received < a->expected) return false;
  a->active = false;
  return decodePgn(a->pgn, a->data, a->received, out);
}

}  // namespace ydraw
