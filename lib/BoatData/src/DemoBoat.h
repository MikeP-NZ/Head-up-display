#pragma once

#include <stddef.h>
#include <stdint.h>

// A simulated sailing boat beating to windward, for demonstrating the display
// without instruments. It tacks every few minutes, swinging the heading to
// about 45 degrees either side of the true wind direction, so the apparent
// wind changes sides; speed drops through each tack and builds again.
//
// Produces NMEA 0183 sentences, so demo data takes the same path through the
// decoder and boat model as data from a gateway. No sentence carries the
// time, so the demo never sets the clock.
class DemoBoat {
 public:
  static constexpr uint32_t kLegMs = 180000;   // time on each tack
  static constexpr uint32_t kTackMs = 15000;   // turning through the wind
  static constexpr float kCloseHauled = 45.0f; // true wind angle on each tack

  struct State {
    float headingMag;  // degrees
    float cogTrue;     // degrees
    float twdMag;      // degrees
    float twa;         // -180..180, positive = wind from starboard
    float tws;         // knots
    float awa;         // -180..180, positive = wind from starboard
    float aws;         // knots
    float stw;         // knots
    float sog;         // knots
    float vmg;         // knots
    float depth;       // metres
    float dtw;         // nautical miles
    float waterTemp;   // Celsius
  };

  static constexpr float kVariation = 19.5f;  // degrees east

  // The boat `ms` milliseconds after the demo started.
  State at(uint32_t ms);

  using Emit = void (*)(const char* line, void* ctx);
  // Calls `emit` with each sentence describing the boat at `ms`.
  void sentences(uint32_t ms, Emit emit, void* ctx);

 private:
  float noise(float amplitude);  // repeatable pseudo-random jitter
  uint32_t seed_ = 12345;
};
