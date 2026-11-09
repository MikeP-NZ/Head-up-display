#pragma once

#include <stddef.h>
#include <stdint.h>

// Clock-by-clock output stream for HUB75 panels built on S-PWM drivers that
// hold the pixel data in their own SRAM. Hardware-free so it can be unit
// tested on the host.
//
// SM16380SF/SC: the drivers make the PWM from GCLK pulses on OE, a block of
// 128 + 3 pulses per row (+6 for the first row after a vertical sync), with
// the row stepping in the gap between blocks. CLK only shifts data and
// commands, which are told apart by how many clocks LAT stays high:
// 3 vertical sync, 14 pre-active, 4/8/16/12/6/15/11 write REG1-REG7, 1 store
// pixel data. Protocol and register values from an oscilloscope capture of a
// working panel, posted by wata-net in hzeller/rpi-rgb-led-matrix#466, and
// identical to Huidu's HDSet presets.
//
// SM16169SH: the SM16380SH-family protocol (as used for
// SM16380SH and SM16269S panels by angyalr/rpi-rgb-led-matrix_pwm_DP3265S).
// No GCLK: the chip makes its PWM from DCLK, which runs all the time, and OE
// goes to its RCLK pin as a row clock. Each frame: vertical sync and
// pre-active, then six register blocks with LAT 5, the third rotating
// through an address table frame by frame (see Sm16380Wave.cpp).
//
// Rows come from either a direct A-E decoder (RUC7258D, 74HC138...) or a
// shift-register row driver (ICND2018/ICND1018: A = shift clock, its pulse
// width the blanking time; C = data, high on the clock into the first row).
//
// Every sample is one bus clock: CLK pulses once, and GCLK is a 1-sample-
// high, 1-sample-low pattern on the OE bit. A frame is three segments:
//   head   vertical sync and pre-active, GCLK paused
//   reg    one register write, rotating through the chip's registers frame
//          by frame
//   body   pixel data, with GCLK row blocks running throughout
// The frame ends after a whole number of scans, back at row 0.

namespace sm16380 {

// Sample bit positions on the 16-bit parallel bus.
constexpr uint16_t kR1 = 1u << 0;
constexpr uint16_t kG1 = 1u << 1;
constexpr uint16_t kB1 = 1u << 2;
constexpr uint16_t kR2 = 1u << 3;
constexpr uint16_t kG2 = 1u << 4;
constexpr uint16_t kB2 = 1u << 5;
constexpr uint16_t kLat = 1u << 6;
constexpr uint16_t kOe = 1u << 7;
constexpr int kAddrShift = 8;  // A-E on bits 8-12
constexpr uint16_t kA = 1u << 8, kB = 1u << 9, kC = 1u << 10;

enum class Chip : uint8_t { SM16380, SM16169SH };
enum class RowDriver : uint8_t { Decoder, Shift };

constexpr size_t kRegVariants = 7;     // SM16380: REG1-REG7
constexpr size_t kProfileCount = 2;    // SM16380 register sets
constexpr size_t kMaxRegVariants = 33;  // SM16169SH: address table entries
constexpr size_t kMaxProfiles = 6;

uint8_t profileCount(Chip chip);
// Short description of a profile, for logs and the portal.
const char* profileName(Chip chip, uint8_t profile);
inline const char* profileName(uint8_t profile) { return profileName(Chip::SM16380, profile); }

class Wave {
 public:
  // width: pixels along one data line's chip chain, a multiple of 16.
  // height: rows y and height/2 + y are driven together (R1/G1/B1 and
  // R2/G2/B2), so the scan rate is 1/(height/2). Panels whose chains run
  // through two row blocks are passed in folded form (see Display::present).
  bool configure(Chip chip, uint16_t width, uint16_t height, uint8_t profile, RowDriver rows);
  bool configure(uint16_t width, uint16_t height, uint8_t profile) {
    return configure(Chip::SM16380, width, height, profile, RowDriver::Decoder);
  }

  // All lengths are even, so DMA buffers are whole 32-bit words.
  size_t headLength() const { return headLength_; }
  size_t regLength() const { return regLength_; }
  size_t bodyLength() const { return mainLength_ - regLength_; }
  // Register writes the frames rotate through.
  size_t regVariants() const { return regVariants_; }
  // GCLK pulses per row (after the first), or 0 when OE is a row clock.
  int rowGclk() const { return gclk_ ? rowClocks_ : 0; }

  // Each fill writes exactly the matching length; the body starts black.
  void fillHead(uint16_t* out) const;
  void fillReg(size_t variant, uint16_t* out) const;
  // Register samples [from, to) of `variant`, written to out[0...].
  void fillRegPart(size_t variant, uint16_t* out, size_t from, size_t to) const;
  // The part of the register segment that differs between variants; the
  // rest is the same in all of them, so it can be stored once.
  size_t regVaryingFrom() const;
  size_t regVaryingLength() const;
  void fillBody(uint16_t* out) const;

  // Sets pixel (x, y) in a filled body to the given 16-bit driver words.
  void setPixel(uint16_t* body, uint16_t x, uint16_t y, uint16_t r, uint16_t g, uint16_t b) const;

  // SM16380 GCLK pulses per row (after the first), for logs and tests.
  static constexpr int kRowGclk = 128 + 3;

 private:
  static constexpr size_t kHeadLength = 24;
  static constexpr size_t kShHeadLength = 32;
  static constexpr int kShRowGap = 28;
  static constexpr int kRowGap = 20;       // samples between GCLK blocks
  static constexpr int kAddressDelay = 2;  // row steps this far into the gap
  static constexpr int kShiftPulse = 12;   // row shift clock high (blanking), samples
  static constexpr int kRowClockAt = 16;   // row clock pulse on OE, after the shift clock
  static constexpr int kRegSpacer = 2;

  size_t regBits() const { return chips_ * 16u; }
  // The register write for `variant`: one word per colour lane, and how
  // many clocks LAT stays high at its end.
  void regWord(size_t variant, size_t block, uint16_t w[3], int& lat) const;
  // Main section (reg + body) samples [from, to), for register `variant`.
  void fillMain(uint16_t* out, size_t from, size_t to, size_t variant) const;
  size_t buildTrack(uint16_t* out, size_t from, size_t to) const;  // GCLK + rows

  Chip chip_ = Chip::SM16380;
  RowDriver rows_ = RowDriver::Decoder;
  uint16_t chips_ = 0;
  uint16_t scanRows_ = 0;
  uint8_t profile_ = 0;
  bool gclk_ = true;         // OE carries GCLK; otherwise one OE pulse per row
  int rowClocks_ = kRowGclk;  // GCLK pulses (or plain clocks) per row
  int firstRowExtra_ = 3;
  size_t regVariants_ = kRegVariants;
  size_t headLength_ = kHeadLength;
  size_t regBlocks_ = 1;
  int rowGap_ = kRowGap;
  size_t regLength_ = 0;
  size_t mainLength_ = 0;
};

}  // namespace sm16380
