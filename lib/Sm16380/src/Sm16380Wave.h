#pragma once

#include <stddef.h>
#include <stdint.h>

// Clock-by-clock output stream for HUB75 panels built on SM16380 S-PWM
// drivers (SM16380SF/SC: chips with a GCLK pin, wired to the HUB75 OE line)
// with a direct A-E row decoder such as RUC7258D. Hardware-free so it can be
// unit tested on the host.
//
// The drivers hold the pixel data in their own SRAM and make the PWM from
// GCLK pulses on OE: a block of 128 + 3 pulses per row (+6 for the first row
// after a vertical sync), with the row address stepping in the gap between
// blocks. CLK only shifts data and commands, which are told apart by how
// many clocks LAT stays high: 3 vertical sync, 14 pre-active, 4/8/16/12/6/
// 15/11 write REG1-REG7, 1 store pixel data. Protocol and register values
// from an oscilloscope capture of a working panel, posted by wata-net in
// hzeller/rpi-rgb-led-matrix#466.
//
// Every sample is one bus clock: CLK pulses once, and GCLK is a 1-sample-
// high, 1-sample-low pattern on the OE bit. A frame is three segments:
//   head   vertical sync and pre-active, GCLK paused
//   reg    one register write, rotating through REG1-REG7 frame by frame
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

constexpr size_t kRegVariants = 7;  // REG1-REG7
constexpr size_t kProfileCount = 2;

// Short description of a register profile, for logs.
const char* profileName(uint8_t profile);

class Wave {
 public:
  // width: pixels along one data line's chip chain, a multiple of 16.
  // height: rows y and height/2 + y are driven together (R1/G1/B1 and
  // R2/G2/B2), so the scan rate is 1/(height/2). Panels whose chains run
  // through two row blocks are passed in folded form (see Display::mapPixel).
  bool configure(uint16_t width, uint16_t height, uint8_t profile);

  // All lengths are even, so DMA buffers are whole 32-bit words.
  size_t headLength() const { return kHeadLength; }
  size_t regLength() const { return regLength_; }
  size_t bodyLength() const { return mainLength_ - regLength_; }

  // Each fill writes exactly the matching length; the body starts black.
  void fillHead(uint16_t* out) const;
  void fillReg(size_t variant, uint16_t* out) const;
  void fillBody(uint16_t* out) const;

  // Sets pixel (x, y) in a filled body to the given 16-bit driver words.
  void setPixel(uint16_t* body, uint16_t x, uint16_t y, uint16_t r, uint16_t g, uint16_t b) const;

  // GCLK pulses per row (after the first), for logs and tests.
  static constexpr int kRowGclk = 128 + 3;

 private:
  static constexpr size_t kHeadLength = 24;
  static constexpr int kFirstRowExtra = 3;  // first row after vsync: 128 + 6
  static constexpr int kRowGap = 20;        // samples between GCLK blocks
  static constexpr int kAddressDelay = 2;   // address steps this far into the gap
  static constexpr int kRegSpacer = 2;

  size_t regBits() const { return chips_ * 16u; }
  // Main section (reg + body) samples [from, to), for register `variant`.
  void fillMain(uint16_t* out, size_t from, size_t to, size_t variant) const;
  size_t buildTrack(uint16_t* out, size_t from, size_t to) const;  // GCLK + address

  uint16_t chips_ = 0;
  uint16_t scanRows_ = 0;
  uint8_t profile_ = 0;
  size_t regLength_ = 0;
  size_t mainLength_ = 0;
};

}  // namespace sm16380
