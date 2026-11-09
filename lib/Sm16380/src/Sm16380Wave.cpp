#include "Sm16380Wave.h"

namespace sm16380 {

namespace {

// SM16380 REG1-REG7 per colour lane (R, G, B). REG1: b14-b13 PWM multiplier
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

// SM16169SH: the SM16380SH-family protocol, as driven for SM16380SH and
// SM16269S panels in angyalr/rpi-rgb-led-matrix_pwm_DP3265S. No GCLK: the
// chip makes its PWM from DCLK, which runs all the time, and OE goes to its
// RCLK pin as a row clock (4 clocks high per row, 10 into the first row).
// Each frame: LAT 3, LAT 14, then six register blocks, each one 16-bit word
// per chip with LAT over its last 5 clocks: 0x00AA, 0x01AA, one entry of an
// address table (address << 8 | value, the next entry each frame), 0xF003,
// 0x0055, 0x0155. A Huidu card's capture also shows six blocks per frame
// (mrcodetastic/ESP32-PWM-MatrixPanel-DMA#2).
constexpr size_t kShBlocks = 6;
constexpr size_t kShTableBlock = 2;  // the block that rotates through the table
constexpr int kShRegLat = 5;
constexpr uint16_t kShFixed[kShBlocks] = {0x00AA, 0x01AA, 0, 0xF003, 0x0055, 0x0155};
constexpr uint8_t kShScanAddress = 0x02;  // scan lines - 1
constexpr int kShRowClockLength = 4, kShFirstRowClockLength = 10;

// Address tables, per lane (R, G, B). Huidu's SM16169SH preset (HDSet, GKGD
// P3.076 104x52) with its addresses + 1, which lines them up with the
// SM16380SH capture (its 0x13 = 14/22/32 is the capture's 0x14, and so on).
constexpr uint16_t kShTableHuidu[kMaxRegVariants][3] = {
    {0x020C, 0x020C, 0x020C}, {0x0324, 0x0324, 0x0324}, {0x0400, 0x0400, 0x0400}, {0x0500, 0x0500, 0x0500},
    {0x0600, 0x0600, 0x0600}, {0x072C, 0x072C, 0x072C}, {0x0800, 0x0800, 0x0800}, {0x0900, 0x0900, 0x0900},
    {0x0A00, 0x0A00, 0x0A00}, {0x0B2C, 0x0B2C, 0x0B2C}, {0x0C00, 0x0C01, 0x0C03}, {0x0D00, 0x0D00, 0x0D00},
    {0x0E05, 0x0E05, 0x0E05}, {0x0F00, 0x0F00, 0x0F00}, {0x1000, 0x1000, 0x1000}, {0x1104, 0x111E, 0x1150},
    {0x1200, 0x1200, 0x1200}, {0x1300, 0x1300, 0x1300}, {0x1414, 0x1422, 0x1432}, {0x1500, 0x1500, 0x1500},
    {0x1630, 0x1630, 0x1630}, {0x1700, 0x1700, 0x1700}, {0x1801, 0x1801, 0x1801}, {0x1902, 0x1902, 0x1902},
    {0x1A00, 0x1A00, 0x1A00}, {0x1B00, 0x1B00, 0x1B00}, {0x1C00, 0x1C00, 0x1C00}, {0x1D00, 0x1D00, 0x1D00},
    {0x1E00, 0x1E00, 0x1E00}, {0x1F00, 0x1F00, 0x1F00}, {0x2000, 0x2000, 0x2000}, {0x2100, 0x2100, 0x2100},
    {0x2200, 0x2200, 0x2200},
};
// The SM16380SH capture's table, as published.
constexpr uint16_t kShTable16380sh[kMaxRegVariants][3] = {
    {0x0000, 0x0000, 0x0000}, {0x021f, 0x021f, 0x021f}, {0x0300, 0x0300, 0x0300}, {0x0400, 0x0400, 0x0400},
    {0x0500, 0x0500, 0x0500}, {0x0600, 0x0600, 0x0600}, {0x0750, 0x0751, 0x0753}, {0x0800, 0x0800, 0x0800},
    {0x0900, 0x0900, 0x0900}, {0x0a02, 0x0a02, 0x0a02}, {0x0b0c, 0x0b0c, 0x0b0c}, {0x0c08, 0x0c18, 0x0c30},
    {0x0d00, 0x0d00, 0x0d00}, {0x0e05, 0x0e05, 0x0e05}, {0x0f00, 0x0f00, 0x0f00}, {0x1000, 0x1000, 0x1000},
    {0x1100, 0x1100, 0x1100}, {0x1200, 0x1200, 0x1200}, {0x1300, 0x1300, 0x1300}, {0x1414, 0x1422, 0x1432},
    {0x1500, 0x1500, 0x1500}, {0x1630, 0x1630, 0x1630}, {0x1700, 0x1700, 0x1700}, {0x1801, 0x1801, 0x1801},
    {0x1904, 0x1903, 0x1903}, {0x1a03, 0x1a01, 0x1a01}, {0x1b14, 0x1b14, 0x1b14}, {0x1c12, 0x1c8f, 0x1c8f},
    {0x1d00, 0x1d00, 0x1d00}, {0x1e00, 0x1e00, 0x1e00}, {0x1f0c, 0x1f0c, 0x1f0c}, {0x2000, 0x2000, 0x2000},
    {0x2200, 0x2200, 0x2200},
};

struct ShProfile {
  const char* name;
  int rowClocks;  // DCLK cycles per row, not counting the gap
  const uint16_t (*table)[3];
};

constexpr ShProfile kShProfiles[] = {
    // 256 clocks per row works on a P4 80x40 1/10 panel (SM16169SH +
    // ICND1018); 112, as in the SM16380SH capture, leaves faint ghost lines.
    {"Huidu table, 256 clocks per row", 256, kShTableHuidu},
    {"SM16380SH table, 256 clocks per row", 256, kShTable16380sh},
    {"Huidu table, 112 clocks per row", 112, kShTableHuidu},
    {"SM16380SH table, 112 clocks per row", 112, kShTable16380sh},
};
constexpr size_t kShProfileCount = sizeof(kShProfiles) / sizeof(kShProfiles[0]);
static_assert(kShProfileCount <= kMaxProfiles && kProfileCount <= kMaxProfiles, "profiles");

constexpr uint16_t kRed = kR1 | kR2, kGreen = kG1 | kG2, kBlue = kB1 | kB2;

}  // namespace

uint8_t profileCount(Chip chip) { return chip == Chip::SM16169SH ? kShProfileCount : kProfileCount; }

const char* profileName(Chip chip, uint8_t profile) {
  if (profile >= profileCount(chip)) return "?";
  return chip == Chip::SM16169SH ? kShProfiles[profile].name : kProfiles[profile].name;
}

bool Wave::configure(Chip chip, uint16_t width, uint16_t height, uint8_t profile, RowDriver rows) {
  if (width == 0 || width % 16 != 0 || height % 2 != 0) return false;
  if (height / 2 < 2 || height / 2 > 32) return false;  // five address lines
  if (profile >= profileCount(chip)) return false;
  chip_ = chip;
  rows_ = rows;
  chips_ = width / 16;
  scanRows_ = height / 2;
  profile_ = profile;
  if (chip == Chip::SM16169SH) {
    gclk_ = false;  // OE goes to the SH's RCLK: a row clock
    rowClocks_ = kShProfiles[profile].rowClocks;
    firstRowExtra_ = 0;
    regBlocks_ = kShBlocks;
    regVariants_ = kMaxRegVariants;
    headLength_ = kShHeadLength;
    rowGap_ = kShRowGap;
  } else {
    gclk_ = true;
    rowClocks_ = kRowGclk;
    firstRowExtra_ = 3;
    regBlocks_ = 1;
    regVariants_ = kRegVariants;
    headLength_ = kHeadLength;
    rowGap_ = kRowGap;
  }
  regLength_ = regBlocks_ * regBits() + kRegSpacer;
  mainLength_ = buildTrack(nullptr, 0, 0);
  return true;
}

void Wave::fillHead(uint16_t* out) const {
  size_t n = 0;
  auto cmd = [&](int lat, int gap) {
    for (int i = 0; i < lat; i++) out[n++] = kLat;
    for (int i = 0; i < gap; i++) out[n++] = 0;
  };
  if (chip_ == Chip::SM16169SH) {
    cmd(3, 6);   // vertical sync
    cmd(14, 9);  // pre-active
    return;
  }
  cmd(3, 3);   // vertical sync
  cmd(14, 4);  // pre-active, before the register write
}

void Wave::fillReg(size_t variant, uint16_t* out) const { fillMain(out, 0, regLength_, variant % regVariants_); }

void Wave::fillRegPart(size_t variant, uint16_t* out, size_t from, size_t to) const {
  fillMain(out, from, to, variant % regVariants_);
}

void Wave::fillBody(uint16_t* out) const { fillMain(out, regLength_, mainLength_, 0); }

size_t Wave::regVaryingFrom() const { return chip_ == Chip::SM16169SH ? kShTableBlock * regBits() : 0; }

size_t Wave::regVaryingLength() const { return chip_ == Chip::SM16169SH ? regBits() : regLength_; }

// GCLK blocks and rows for main-section samples [from, to), written to
// out[i - from]. Without `out`, returns the length: whole scans, enough to
// cover the register write and the pixel upload.
size_t Wave::buildTrack(uint16_t* out, size_t from, size_t to) const {
  const size_t needed = regLength_ + static_cast<size_t>(scanRows_) * 16 * regBits();
  const bool decoder = rows_ == RowDriver::Decoder;
  size_t i = 0;
  auto put = [&](uint16_t v) {
    if (out && i >= from && i < to) out[i - from] = v;
    i++;
  };
  for (int scan = 0; out ? i < to : i < needed; scan++) {
    for (int row = 0; row < scanRows_; row++) {
      const int nextRow = (row + 1) % scanRows_;
      const uint16_t addr = decoder ? static_cast<uint16_t>(row << kAddrShift) : 0;
      const uint16_t next = decoder ? static_cast<uint16_t>(nextRow << kAddrShift) : 0;
      const int pulses = rowClocks_ + (scan == 0 && row == 0 ? firstRowExtra_ : 0);
      for (int p = 0; p < pulses; p++) {
        if (gclk_) put(kOe | addr);
        put(addr);
      }
      for (int g = 0; g < rowGap_; g++) {
        uint16_t v = g < kAddressDelay ? addr : next;
        const bool pulse = g > kAddressDelay && g <= kAddressDelay + kShiftPulse;
        // Shift row driver: C set up a sample before the clock rises and held
        // while it's high; the clock's width is the blanking time.
        if (!decoder) {
          if (nextRow == 0 && g >= kAddressDelay && g <= kAddressDelay + kShiftPulse) v |= kC;
          if (pulse) v |= kA;
        }
        // Row clock for drivers that take it (SM16169SH RCLK): a pulse after
        // the row has changed, longer into the first row.
        const int clockLength = nextRow == 0 ? kShFirstRowClockLength : kShRowClockLength;
        if (!gclk_ && g >= kRowClockAt && g < kRowClockAt + clockLength) v |= kOe;
        put(v);
      }
    }
  }
  return i;
}

void Wave::regWord(size_t variant, size_t block, uint16_t w[3], int& lat) const {
  if (chip_ == Chip::SM16169SH) {
    for (int lane = 0; lane < 3; lane++) {
      uint16_t v = kShFixed[block];
      if (block == kShTableBlock) {
        v = kShProfiles[profile_].table[variant][lane];
        if (v >> 8 == kShScanAddress) v = static_cast<uint16_t>(kShScanAddress << 8 | (scanRows_ - 1));
      }
      w[lane] = v;
    }
    lat = kShRegLat;
    return;
  }
  const Profile& p = kProfiles[profile_];
  for (int lane = 0; lane < 3; lane++) {
    w[lane] = p.regs[variant][lane];
    if (variant == 0) w[lane] = static_cast<uint16_t>((w[lane] & ~(0x3F << 7)) | ((scanRows_ - 1) << 7));
  }
  lat = kRegLat[variant];
}

void Wave::fillMain(uint16_t* out, size_t from, size_t to, size_t variant) const {
  buildTrack(out, from, to);

  // Register writes: each block's word repeated for every chip, LAT over
  // the block's tail.
  const size_t bits = regBits();
  for (size_t block = 0; block < regBlocks_; block++) {
    uint16_t w[3];
    int lat = 0;
    regWord(variant, block, w, lat);
    const size_t start = block * bits;
    for (size_t i = from > start ? from : start; i < to && i < start + bits; i++) {
      const size_t j = i - start;
      const int bit = 15 - static_cast<int>(j % 16);
      uint16_t v = 0;
      if (w[0] >> bit & 1) v |= kRed;
      if (w[1] >> bit & 1) v |= kGreen;
      if (w[2] >> bit & 1) v |= kBlue;
      if (j >= bits - lat) v |= kLat;
      out[i - from] |= v;
    }
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
