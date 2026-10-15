#include "Sm16380Wave.h"

namespace sm16380 {

namespace {

// REG1-REG7 per colour lane (R, G, B). REG1: b14-b13 PWM multiplier
// (3 = x8, which sets the 128 GCLK per row), b12-b7 scan lines - 1
// (rewritten for the panel), b5-b0 current gain. Values from wata-net's
// capture; what the other registers do is only partly known (ghosting,
// low-brightness compensation, power saving).
struct Profile {
  const char* name;
  uint16_t regs[kRegVariants][3];
};

constexpr Profile kProfiles[kProfileCount] = {
    {"captured settings",
     {{0xFFB9, 0xFFB1, 0xFFB9},
      {0x0003, 0x0003, 0x0003},
      {0x10A3, 0x1463, 0x1063},
      {0x0000, 0x0000, 0x0000},
      {0x0000, 0x0000, 0x0000},
      {0x0005, 0x0019, 0x002E},
      {0x0000, 0x0000, 0x0000}}},
    {"REG1 only",
     {{0xFFB9, 0xFFB1, 0xFFB9},
      {0x0000, 0x0000, 0x0000},
      {0x0000, 0x0000, 0x0000},
      {0x0000, 0x0000, 0x0000},
      {0x0000, 0x0000, 0x0000},
      {0x0000, 0x0000, 0x0000},
      {0x0000, 0x0000, 0x0000}}},
};

// Clocks LAT stays high to write REG1-REG7.
constexpr int kRegLat[kRegVariants] = {4, 8, 16, 12, 6, 15, 11};

constexpr uint16_t kRed = kR1 | kR2, kGreen = kG1 | kG2, kBlue = kB1 | kB2;

}  // namespace

const char* profileName(uint8_t profile) {
  return profile < kProfileCount ? kProfiles[profile].name : "?";
}

bool Wave::configure(uint16_t width, uint16_t height, uint8_t profile) {
  if (width == 0 || width % 16 != 0 || height % 2 != 0) return false;
  if (height / 2 < 2 || height / 2 > 32) return false;  // five address lines
  if (profile >= kProfileCount) return false;
  chips_ = width / 16;
  scanRows_ = height / 2;
  profile_ = profile;
  regLength_ = regBits() + kRegSpacer;
  mainLength_ = buildTrack(nullptr, 0, 0);
  return true;
}

void Wave::fillHead(uint16_t* out) const {
  size_t n = 0;
  for (int i = 0; i < 3; i++) out[n++] = kLat;  // vertical sync
  for (int i = 0; i < 3; i++) out[n++] = 0;
  for (int i = 0; i < 14; i++) out[n++] = kLat;  // pre-active
  for (int i = 0; i < 4; i++) out[n++] = 0;
}

void Wave::fillReg(size_t variant, uint16_t* out) const { fillMain(out, 0, regLength_, variant % kRegVariants); }

void Wave::fillBody(uint16_t* out) const { fillMain(out, regLength_, mainLength_, 0); }

// GCLK blocks and row address for main-section samples [from, to), written
// to out[i - from]. Without `out`, returns the length: whole scans, enough
// to cover the register write and the pixel upload.
size_t Wave::buildTrack(uint16_t* out, size_t from, size_t to) const {
  const size_t needed = regLength_ + static_cast<size_t>(scanRows_) * 16 * regBits();
  size_t i = 0;
  auto put = [&](uint16_t v) {
    if (out && i >= from && i < to) out[i - from] = v;
    i++;
  };
  for (int scan = 0; out ? i < to : i < needed; scan++) {
    for (int row = 0; row < scanRows_; row++) {
      const uint16_t addr = static_cast<uint16_t>(row << kAddrShift);
      const uint16_t next = static_cast<uint16_t>(((row + 1) % scanRows_) << kAddrShift);
      const int pulses = kRowGclk + (scan == 0 && row == 0 ? kFirstRowExtra : 0);
      for (int p = 0; p < pulses; p++) {
        put(kOe | addr);
        put(addr);
      }
      for (int g = 0; g < kRowGap; g++) put(g < kAddressDelay ? addr : next);
    }
  }
  return i;
}

void Wave::fillMain(uint16_t* out, size_t from, size_t to, size_t variant) const {
  buildTrack(out, from, to);

  // Register write: the word repeated for every chip, LAT over the tail.
  const Profile& p = kProfiles[profile_];
  uint16_t w[3];
  for (int lane = 0; lane < 3; lane++) {
    w[lane] = p.regs[variant][lane];
    if (variant == 0) w[lane] = static_cast<uint16_t>((w[lane] & ~(0x3F << 7)) | ((scanRows_ - 1) << 7));
  }
  const size_t bits = regBits();
  for (size_t i = from; i < to && i < bits; i++) {
    const int bit = 15 - static_cast<int>(i % 16);
    uint16_t v = 0;
    if (w[0] >> bit & 1) v |= kRed;
    if (w[1] >> bit & 1) v |= kGreen;
    if (w[2] >> bit & 1) v |= kBlue;
    if (i >= bits - kRegLat[variant]) v |= kLat;
    out[i - from] |= v;
  }

  // Pixel upload (black): LAT on the last clock of each channel group.
  const size_t uploadEnd = regLength_ + static_cast<size_t>(scanRows_) * 16 * bits;
  for (size_t i = regLength_ + bits - 1; i < uploadEnd; i += bits) {
    if (i >= from && i < to) out[i - from] |= kLat;
  }
}

void Wave::setPixel(uint16_t* body, uint16_t x, uint16_t y, uint16_t r, uint16_t g, uint16_t b) const {
  if (x >= chips_ * 16u || y >= scanRows_ * 2u) return;
  const bool bottom = y >= scanRows_;
  const uint16_t rb = bottom ? kR2 : kR1, gb = bottom ? kG2 : kG1, bb = bottom ? kB2 : kB1;
  const size_t row = y % scanRows_;
  uint16_t* p = body + (row * 16 + x % 16) * regBits() + (x / 16) * 16u;
  for (int bit = 15; bit >= 0; bit--, p++) {
    uint16_t v = *p & ~(rb | gb | bb);
    if (r >> bit & 1) v |= rb;
    if (g >> bit & 1) v |= gb;
    if (b >> bit & 1) v |= bb;
    *p = v;
  }
}

}  // namespace sm16380
