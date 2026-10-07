#pragma once

#include <stddef.h>
#include <stdint.h>

#include "Quantity.h"

// Minimal NMEA 0183 decoder for the sentences a Yacht Devices gateway emits
// that carry values the display shows (or UTC time).
//
//   VHW  speed through water, heading        (PGN 128259, 127250)
//   VBW  dual ground/water speed             (PGN 130578)
//   RMC  SOG, COG, variation, time           (PGN 129026, 127258, 129029)
//   VTG  speed and course over ground        (PGN 129026)
//   ZDA  date/time                           (PGN 126992, 129029)
//   MWV  apparent (R) or true (T) wind       (PGN 130306)
//   VWR  apparent wind, VWT true wind        (older instruments)
//   MWD  true wind direction                 (PGN 130306)
//   HDG  heading + deviation + variation     (PGN 127250)
//   HDM  heading magnetic, HDT heading true  (PGN 127250)
//   DPT  depth + offset, DBT depth           (PGN 128267)
//   RMB  distance/bearing to waypoint, VMC   (PGN 129284)
//   BWC  distance/bearing to waypoint        (PGN 129284)
//   VPW  speed parallel to wind (VMG)
namespace nmea0183 {

struct Sentence {
  char talker[3] = {0};  // "YD", "GP", "II" ...
  char type[4] = {0};    // "VHW", "RMC" ...
  Decoded data;          // values and time carried by the sentence
};

enum class ParseResult : uint8_t {
  Ok,           // well-formed sentence (may or may not carry values)
  NotNmea,      // does not start with '$' or '!'
  BadChecksum,  // checksum present and wrong
  Malformed,
};

ParseResult parse(const char* line, Sentence& out);

// Days since 1970-01-01 for a proleptic Gregorian date.
int64_t daysFromCivil(int y, unsigned m, unsigned d);

}  // namespace nmea0183
