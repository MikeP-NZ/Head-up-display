#include "DemoBoat.h"

#include <math.h>
#include <stdio.h>

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kKnotMps = 0.514444f;

float wrap360(float a) {
  a = fmodf(a, 360.0f);
  return a < 0 ? a + 360.0f : a;
}

float smoothstep(float u) { return u * u * (3.0f - 2.0f * u); }

// +1 on starboard tack (wind from starboard), -1 on port tack.
float side(uint32_t leg) { return leg % 2 == 0 ? 1.0f : -1.0f; }

void emitSentence(DemoBoat::Emit emit, void* ctx, const char* body) {
  uint8_t cs = 0;
  for (const char* p = body; *p; p++) cs ^= static_cast<uint8_t>(*p);
  char line[100];
  snprintf(line, sizeof(line), "$%s*%02X", body, cs);
  emit(line, ctx);
}

}  // namespace

float DemoBoat::noise(float amplitude) {
  seed_ ^= seed_ << 13;
  seed_ ^= seed_ >> 17;
  seed_ ^= seed_ << 5;
  return amplitude * (static_cast<float>(seed_ % 2001) / 1000.0f - 1.0f);
}

DemoBoat::State DemoBoat::at(uint32_t ms) {
  State s;
  const float t = ms / 1000.0f;
  const uint32_t leg = ms / kLegMs;
  const uint32_t since = ms % kLegMs;

  // True wind angle: steady on each tack, swinging through head-to-wind
  // during a tack. The first leg starts already sailing.
  const bool tacking = leg > 0 && since < kTackMs;
  float twa = kCloseHauled * side(leg);
  if (tacking) {
    const float u = smoothstep(static_cast<float>(since) / kTackMs);
    twa = kCloseHauled * (side(leg - 1) + (side(leg) - side(leg - 1)) * u);
  }
  twa += 2.0f * sinf(t / 17.0f) + noise(1.0f);

  // Speed drops through the tack and builds again over the next minute.
  float factor = 1.0f;
  if (leg > 0) {
    if (tacking) {
      factor = 1.0f - 0.5f * smoothstep(static_cast<float>(since) / kTackMs);
    } else {
      factor = 1.0f - 0.5f * expf(-static_cast<float>(since - kTackMs) / 20000.0f);
    }
  }

  s.twdMag = wrap360(250.0f + 8.0f * sinf(t / 150.0f));
  s.twa = twa;
  s.headingMag = wrap360(s.twdMag - twa);
  s.tws = fmaxf(0.0f, 15.0f + 3.0f * sinf(t / 31.0f) + noise(0.5f));
  s.stw = fmaxf(0.0f, factor * (6.2f + 0.6f * sinf(t / 23.0f)) + 0.15f * sinf(t * 1.7f) + noise(0.05f));
  s.sog = fmaxf(0.0f, s.stw * 0.95f + 0.3f * sinf(t / 41.0f));
  // Leeway: the boat slips away from the wind.
  const float leeway = 4.0f * fmaxf(-1.0f, fminf(1.0f, twa / kCloseHauled));
  s.cogTrue = wrap360(s.headingMag + kVariation - leeway);

  const float a = twa * kPi / 180.0f;
  const float x = s.tws * cosf(a) + s.stw, y = s.tws * sinf(a);
  s.awa = atan2f(y, x) * 180.0f / kPi;
  s.aws = hypotf(x, y);
  s.vmg = s.stw * cosf(a);

  s.depth = fmaxf(1.0f, 12.4f + 4.0f * sinf(t / 60.0f) + noise(0.1f));
  s.dtw = 5.0f - fmodf(t * 4.0f / 3600.0f, 4.9f);  // a waypoint dead upwind
  s.waterTemp = 18.4f + 0.3f * sinf(t / 300.0f);
  return s;
}

void DemoBoat::sentences(uint32_t ms, Emit emit, void* ctx) {
  const State s = at(ms);
  const float twdTrue = wrap360(s.twdMag + kVariation);
  char b[90];
  snprintf(b, sizeof(b), "YDVHW,,T,%.1f,M,%.2f,N,%.2f,K", s.headingMag, s.stw, s.stw * 1.852f);
  emitSentence(emit, ctx, b);
  // No time or position, only what the display uses.
  snprintf(b, sizeof(b), "YDRMC,,A,,,,,%.2f,%.1f,,%.1f,E,A", s.sog, s.cogTrue, kVariation);
  emitSentence(emit, ctx, b);
  snprintf(b, sizeof(b), "YDVTG,%.1f,T,%.1f,M,%.2f,N,%.2f,K,A", s.cogTrue, wrap360(s.cogTrue - kVariation), s.sog,
           s.sog * 1.852f);
  emitSentence(emit, ctx, b);
  snprintf(b, sizeof(b), "YDMWV,%.1f,R,%.1f,N,A", wrap360(s.awa), s.aws);
  emitSentence(emit, ctx, b);
  snprintf(b, sizeof(b), "YDMWV,%.1f,T,%.1f,N,A", wrap360(s.twa), s.tws);
  emitSentence(emit, ctx, b);
  snprintf(b, sizeof(b), "YDMWD,%.1f,T,%.1f,M,%.1f,N,%.1f,M", twdTrue, s.twdMag, s.tws, s.tws * kKnotMps);
  emitSentence(emit, ctx, b);
  snprintf(b, sizeof(b), "YDHDG,%.1f,,,%.1f,E", s.headingMag, kVariation);
  emitSentence(emit, ctx, b);
  snprintf(b, sizeof(b), "YDDPT,%.1f,0.0,", s.depth);
  emitSentence(emit, ctx, b);
  snprintf(b, sizeof(b), "YDRMB,A,0.12,L,,WP1,,,,,%.2f,%.1f,%.1f,V,A", s.dtw, twdTrue, s.vmg);
  emitSentence(emit, ctx, b);
  snprintf(b, sizeof(b), "YDVPW,%.2f,N,,M", s.vmg);
  emitSentence(emit, ctx, b);
  snprintf(b, sizeof(b), "YDMTW,%.1f,C", s.waterTemp);
  emitSentence(emit, ctx, b);
}
