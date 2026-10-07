#include "Nmea0183.h"

#include <stdlib.h>
#include <string.h>

namespace nmea0183 {

namespace {

constexpr size_t kMaxLen = 120;  // spec max is 82, be lenient
constexpr size_t kMaxFields = 24;

int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

bool parseFloat(const char* s, float& out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  double v = strtod(s, &end);
  if (end == s) return false;
  out = static_cast<float>(v);
  return true;
}

// "hhmmss[.ss]" -> seconds since midnight
bool parseTimeOfDay(const char* s, int& secs) {
  if (!s || strlen(s) < 6) return false;
  for (int i = 0; i < 6; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  int hh = (s[0] - '0') * 10 + (s[1] - '0');
  int mm = (s[2] - '0') * 10 + (s[3] - '0');
  int ss = (s[4] - '0') * 10 + (s[5] - '0');
  if (hh > 23 || mm > 59 || ss > 60) return false;
  secs = hh * 3600 + mm * 60 + ss;
  return true;
}

constexpr float kKnot = 0.514444f;           // m/s
constexpr float kNauticalMile = 1852.0f;     // m

// Wind speed with its unit letter: K km/h, M m/s, N knots, S statute mph.
bool speedToMps(float v, char unit, float& out) {
  switch (unit) {
    case 'N': out = v * kKnot; return true;
    case 'M': out = v; return true;
    case 'K': out = v / 3.6f; return true;
    case 'S': out = v * 0.44704f; return true;
  }
  return false;
}

bool plausible(Quantity q, float v) {
  if (!(v == v)) return false;  // NaN
  switch (q) {
    case Quantity::Stw:
    case Quantity::Sog: return v >= 0 && v <= 100;
    case Quantity::Aws:
    case Quantity::Tws: return v >= 0 && v <= 100;
    case Quantity::Vmg:
    case Quantity::Vmc: return v >= -100 && v <= 100;
    case Quantity::Dpt: return v >= 0 && v <= 12000;
    case Quantity::Dtw: return v >= 0 && v <= 4.0e7f;
    case Quantity::Variation: return v >= -180 && v <= 180;
    default: return v >= -360 && v <= 360;
  }
}

bool isValidMode(const char* mode) {
  // NMEA 2.3+ mode indicator: N = data not valid
  return !(mode && mode[0] == 'N');
}

}  // namespace

int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

ParseResult parse(const char* line, Sentence& out) {
  out = Sentence{};
  if (!line) return ParseResult::NotNmea;
  while (*line == ' ' || *line == '\t') line++;
  if (*line != '$' && *line != '!') return ParseResult::NotNmea;

  char buf[kMaxLen + 1];
  size_t n = 0;
  for (const char* p = line + 1; *p && *p != '\r' && *p != '\n'; p++) {
    if (n >= kMaxLen) return ParseResult::Malformed;
    buf[n++] = *p;
  }
  buf[n] = 0;

  char* star = strchr(buf, '*');
  if (star) {
    uint8_t sum = 0;
    for (char* p = buf; p < star; p++) sum ^= static_cast<uint8_t>(*p);
    int hi = hexNibble(star[1]);
    int lo = hi >= 0 ? hexNibble(star[2]) : -1;
    if (hi < 0 || lo < 0) return ParseResult::Malformed;
    if (((hi << 4) | lo) != sum) return ParseResult::BadChecksum;
    *star = 0;
  }

  const char* f[kMaxFields] = {nullptr};
  size_t nf = 0;
  f[nf++] = buf;
  for (char* p = buf; *p; p++) {
    if (*p == ',') {
      *p = 0;
      if (nf < kMaxFields) f[nf++] = p + 1;
    }
  }

  const char* addr = f[0];
  size_t addrLen = strlen(addr);
  if (addrLen < 5) return ParseResult::Malformed;
  // Proprietary sentences ($Pxxx) have no talker id.
  if (addr[0] == 'P') {
    strncpy(out.type, addr + 1, 3);
    return ParseResult::Ok;
  }
  out.talker[0] = addr[0];
  out.talker[1] = addr[1];
  memcpy(out.type, addr + addrLen - 3, 3);

  auto field = [&](size_t i) -> const char* { return i < nf ? f[i] : ""; };
  auto num = [&](size_t i, float& v) { return parseFloat(field(i), v); };
  auto is = [&](const char* t) { return strcmp(out.type, t) == 0; };
  Decoded& d = out.data;
  float v;

  if (is("VHW")) {
    // $--VHW,hdgT,T,hdgM,M,kn,N,kmh,K
    if (num(5, v)) d.add(Quantity::Stw, v * kKnot);
    else if (num(7, v)) d.add(Quantity::Stw, v / 3.6f);
    if (num(3, v)) d.add(Quantity::Hdg, v);
    else if (num(1, v)) d.add(Quantity::HdgTrue, v);
  } else if (is("VBW")) {
    // $--VBW,longW,transW,A,longG,transG,A,...
    if (field(3)[0] == 'A' && num(1, v)) d.add(Quantity::Stw, v * kKnot);
    if (field(6)[0] == 'A' && num(4, v)) d.add(Quantity::Sog, v * kKnot);
  } else if (is("RMC")) {
    // $--RMC,hhmmss,A,lat,N,lon,E,sog,cog,ddmmyy,var,E[,mode[,nav]]
    bool valid = field(2)[0] == 'A' && isValidMode(field(12));
    if (valid && num(7, v)) d.add(Quantity::Sog, v * kKnot);
    if (valid && num(10, v) && (field(11)[0] == 'E' || field(11)[0] == 'W')) {
      d.add(Quantity::Variation, field(11)[0] == 'W' ? -v : v);
    }
    int secs;
    const char* date = field(9);
    if (parseTimeOfDay(field(1), secs) && strlen(date) == 6) {
      int dd = (date[0] - '0') * 10 + (date[1] - '0');
      int mo = (date[2] - '0') * 10 + (date[3] - '0');
      int yy = (date[4] - '0') * 10 + (date[5] - '0');
      if (dd >= 1 && dd <= 31 && mo >= 1 && mo <= 12) {
        int year = yy < 80 ? 2000 + yy : 1900 + yy;
        d.hasTime = true;
        d.epochSeconds = daysFromCivil(year, mo, dd) * 86400 + secs;
      }
    }
  } else if (is("VTG")) {
    // $--VTG,cogT,T,cogM,M,kn,N,kmh,K[,mode]
    if (isValidMode(field(9))) {
      if (num(5, v)) d.add(Quantity::Sog, v * kKnot);
      else if (num(7, v)) d.add(Quantity::Sog, v / 3.6f);
    }
  } else if (is("ZDA")) {
    // $--ZDA,hhmmss,dd,mm,yyyy,zh,zm
    int secs;
    int dd = atoi(field(2)), mo = atoi(field(3)), year = atoi(field(4));
    if (parseTimeOfDay(field(1), secs) && dd >= 1 && dd <= 31 && mo >= 1 && mo <= 12 && year >= 1980) {
      d.hasTime = true;
      d.epochSeconds = daysFromCivil(year, mo, dd) * 86400 + secs;
    }
  } else if (is("MWV")) {
    // $--MWV,angle,R|T,speed,K|M|N|S,A   angle 0..360 clockwise from the bow
    float a, sp;
    if (field(5)[0] == 'A') {
      bool apparent = field(2)[0] == 'R';
      bool trueWind = field(2)[0] == 'T';
      if ((apparent || trueWind) && num(1, a)) d.add(apparent ? Quantity::Awa : Quantity::Twa, wrapSigned(a));
      if ((apparent || trueWind) && num(3, sp) && speedToMps(sp, field(4)[0], sp)) {
        d.add(apparent ? Quantity::Aws : Quantity::Tws, sp);
      }
    }
  } else if (is("VWR") || is("VWT")) {
    // $--VWR,angle,L|R,kn,N,m/s,M,km/h,K   angle 0..180 off the bow
    bool apparent = is("VWR");
    float a, sp;
    char side = field(2)[0];
    if (num(1, a) && (side == 'L' || side == 'R')) {
      d.add(apparent ? Quantity::Awa : Quantity::Twa, wrapSigned(side == 'L' ? -a : a));
    }
    Quantity speedQ = apparent ? Quantity::Aws : Quantity::Tws;
    if (num(3, sp)) d.add(speedQ, sp * kKnot);
    else if (num(5, sp)) d.add(speedQ, sp);
    else if (num(7, sp)) d.add(speedQ, sp / 3.6f);
  } else if (is("MWD")) {
    // $--MWD,dirT,T,dirM,M,kn,N,m/s,M   (speed is ground-referenced; not used)
    if (num(3, v)) d.add(Quantity::Twd, wrapBearing(v));
    else if (num(1, v)) d.add(Quantity::TwdTrue, wrapBearing(v));
  } else if (is("HDG")) {
    // $--HDG,sensor,dev,E|W,var,E|W   magnetic = sensor + deviation
    if (num(1, v)) {
      float dev;
      if (num(2, dev) && (field(3)[0] == 'E' || field(3)[0] == 'W')) v += field(3)[0] == 'W' ? -dev : dev;
      d.add(Quantity::Hdg, wrapBearing(v));
    }
    float var;
    if (num(4, var) && (field(5)[0] == 'E' || field(5)[0] == 'W')) {
      d.add(Quantity::Variation, field(5)[0] == 'W' ? -var : var);
    }
  } else if (is("HDM")) {
    if (num(1, v)) d.add(Quantity::Hdg, wrapBearing(v));
  } else if (is("HDT")) {
    if (num(1, v)) d.add(Quantity::HdgTrue, wrapBearing(v));
  } else if (is("DPT")) {
    // $--DPT,depth,offset[,range]  depth below transducer; offset + to the
    // waterline, - to the keel
    if (num(1, v)) {
      float off;
      if (num(2, off)) v += off;
      d.add(Quantity::Dpt, v);
    }
  } else if (is("DBT")) {
    // $--DBT,feet,f,metres,M,fathoms,F
    if (num(3, v)) d.add(Quantity::Dpt, v);
    else if (num(1, v)) d.add(Quantity::Dpt, v * 0.3048f);
    else if (num(5, v)) d.add(Quantity::Dpt, v * 1.8288f);
  } else if (is("RMB")) {
    // $--RMB,A,xte,L,orig,dest,lat,N,lon,E,range,bearing,closing,arrival[,mode]
    if (field(1)[0] == 'A' && isValidMode(field(14))) {
      if (num(10, v)) d.add(Quantity::Dtw, v * kNauticalMile);
      if (num(12, v)) d.add(Quantity::Vmc, v * kKnot);
    }
  } else if (is("BWC")) {
    // $--BWC,utc,lat,N,lon,E,brgT,T,brgM,M,dist,N,id[,mode]
    if (isValidMode(field(13)) && num(10, v)) d.add(Quantity::Dtw, v * kNauticalMile);
  } else if (is("VPW")) {
    // $--VPW,kn,N,m/s,M   speed parallel to the true wind
    if (num(1, v)) d.add(Quantity::Vmg, v * kKnot);
    else if (num(3, v)) d.add(Quantity::Vmg, v);
  }

  // Drop values outside plausible ranges (also NaN).
  Decoded checked;
  checked.hasTime = d.hasTime;
  checked.epochSeconds = d.epochSeconds;
  for (uint8_t i = 0; i < d.count; i++) {
    if (plausible(d.samples[i].q, d.samples[i].value)) checked.add(d.samples[i].q, d.samples[i].value);
  }
  d = checked;
  return ParseResult::Ok;
}

}  // namespace nmea0183
