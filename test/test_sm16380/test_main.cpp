#include <unity.h>

#include <vector>

#include "Sm16380Wave.h"

using namespace sm16380;

void setUp() {}
void tearDown() {}

// 16 samples, MSB first, read back from one data lane.
static uint16_t lane(const uint16_t* s, uint16_t mask) {
  uint16_t w = 0;
  for (int i = 0; i < 16; i++) w = static_cast<uint16_t>(w << 1 | ((s[i] & mask) ? 1 : 0));
  return w;
}

static int rowOf(uint16_t s) { return s >> kAddrShift & 0x1F; }

// 64x64 at 1/16 scan, folded: 128 pixels (8 chips) per data line, 32 rows.
static Wave panel() {
  Wave w;
  TEST_ASSERT_TRUE(w.configure(128, 32, 0));
  return w;
}

// Register write and body back to back, as the DMA ring plays them.
static std::vector<uint16_t> mainSection(const Wave& w, size_t variant) {
  std::vector<uint16_t> s(w.regLength() + w.bodyLength());
  w.fillReg(variant, s.data());
  w.fillBody(s.data() + w.regLength());
  return s;
}

static void test_rejects_unsupported_geometry() {
  Wave w;
  TEST_ASSERT_FALSE(w.configure(60, 32, 0));   // not whole chips
  TEST_ASSERT_FALSE(w.configure(64, 128, 0));  // 64 rows per line needs a sixth address line
  TEST_ASSERT_FALSE(w.configure(64, 64, kProfileCount));
  TEST_ASSERT_TRUE(w.configure(64, 64, 1));
}

static void test_lengths_are_even() {
  for (uint16_t width : {16, 64, 128, 256}) {
    for (uint16_t height : {16, 32, 64}) {
      Wave w;
      TEST_ASSERT_TRUE(w.configure(width, height, 0));
      TEST_ASSERT_EQUAL(0, w.headLength() % 2);
      TEST_ASSERT_EQUAL(0, w.regLength() % 2);
      TEST_ASSERT_EQUAL(0, w.bodyLength() % 2);
    }
  }
}

static void test_head_is_vsync_then_pre_active() {
  Wave w = panel();
  std::vector<uint16_t> s(w.headLength());
  w.fillHead(s.data());
  for (int i = 0; i < 3; i++) TEST_ASSERT_EQUAL_HEX16(kLat, s[i]);
  for (int i = 3; i < 6; i++) TEST_ASSERT_EQUAL_HEX16(0, s[i]);
  for (int i = 6; i < 20; i++) TEST_ASSERT_EQUAL_HEX16(kLat, s[i]);
  for (size_t i = 20; i < s.size(); i++) TEST_ASSERT_EQUAL_HEX16(0, s[i]);
}

static void test_register_writes() {
  Wave w = panel();
  std::vector<uint16_t> s(w.regLength());
  const size_t lat[kRegVariants] = {4, 8, 16, 12, 6, 15, 11};
  for (size_t v = 0; v < kRegVariants; v++) {
    w.fillReg(v, s.data());
    size_t lats = 0;
    for (size_t i = 0; i < 128; i++) lats += (s[i] & kLat) ? 1 : 0;
    TEST_ASSERT_EQUAL(lat[v], lats);
    TEST_ASSERT_TRUE(s[127] & kLat);
    TEST_ASSERT_FALSE(s[128] & kLat);
  }
  // REG1: scan lines - 1 = 15 in b12-b7, the rest as captured.
  w.fillReg(0, s.data());
  for (int chip = 0; chip < 8; chip++) {
    TEST_ASSERT_EQUAL_HEX16(0xE7B9, lane(&s[chip * 16], kR1));
    TEST_ASSERT_EQUAL_HEX16(0xE7B1, lane(&s[chip * 16], kG2));
  }
  w.fillReg(2, s.data());
  TEST_ASSERT_EQUAL_HEX16(0x10A3, lane(s.data(), kR2));
  TEST_ASSERT_EQUAL_HEX16(0x1463, lane(s.data(), kG1));
  TEST_ASSERT_EQUAL_HEX16(0x1063, lane(&s[112], kB1));
}

static void test_gclk_blocks_and_rows() {
  Wave w = panel();
  std::vector<uint16_t> s = mainSection(w, 0);

  // Split into GCLK blocks: runs of OE pulses (1 sample high, 1 low),
  // separated by gaps of 20 samples without.
  std::vector<int> pulses, rows;
  size_t i = 0;
  while (i < s.size()) {
    int n = 0;
    const int row = rowOf(s[i]);
    while (i < s.size() && (s[i] & kOe)) {
      TEST_ASSERT_EQUAL(row, rowOf(s[i]));
      TEST_ASSERT_FALSE(s[i + 1] & kOe);
      TEST_ASSERT_EQUAL(row, rowOf(s[i + 1]));
      n++;
      i += 2;
    }
    pulses.push_back(n);
    rows.push_back(row);
    // Gap: the address steps 2 samples in.
    for (int g = 0; g < 20; g++, i++) {
      TEST_ASSERT_FALSE(s[i] & kOe);
      TEST_ASSERT_EQUAL(g < 2 ? row : (row + 1) % 16, rowOf(s[i]));
    }
  }
  TEST_ASSERT_EQUAL(s.size(), i);
  TEST_ASSERT_EQUAL(0, pulses.size() % 16);  // whole scans
  TEST_ASSERT_EQUAL(134, pulses[0]);
  for (size_t k = 1; k < pulses.size(); k++) TEST_ASSERT_EQUAL(131, pulses[k]);
  for (size_t k = 0; k < rows.size(); k++) TEST_ASSERT_EQUAL(static_cast<int>(k % 16), rows[k]);
  TEST_ASSERT_EQUAL(0, rowOf(s.back()));
}

static void test_pixel_upload_latches() {
  Wave w = panel();
  std::vector<uint16_t> body(w.bodyLength());
  w.fillBody(body.data());
  size_t lats = 0;
  for (size_t i = 0; i < body.size(); i++) {
    if (!(body[i] & kLat)) continue;
    lats++;
    TEST_ASSERT_EQUAL(127, i % 128);
  }
  TEST_ASSERT_EQUAL(16 * 16, lats);  // one per channel per scan row
}

static void test_set_pixel_writes_one_lane() {
  Wave w = panel();
  std::vector<uint16_t> s(w.bodyLength()), fresh(w.bodyLength());
  w.fillBody(s.data());
  w.fillBody(fresh.data());

  // (100, 20): bottom line, scan row 4, chip 6, channel 4.
  w.setPixel(s.data(), 100, 20, 0x8001, 0x0000, 0xFFFF);
  const size_t at = (4 * 16 + 4) * 128 + 6 * 16;
  TEST_ASSERT_EQUAL_HEX16(0x8001, lane(&s[at], kR2));
  TEST_ASSERT_EQUAL_HEX16(0x0000, lane(&s[at], kG2));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, lane(&s[at], kB2));
  size_t differing = 0;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] != fresh[i]) differing++;
    TEST_ASSERT_EQUAL_HEX16(fresh[i] & ~(kR2 | kG2 | kB2), s[i] & ~(kR2 | kG2 | kB2));
  }
  TEST_ASSERT_EQUAL(16, differing);

  // The last chip's word ends on the latch clock, which must survive.
  w.setPixel(s.data(), 127, 0, 0xFFFF, 0xFFFF, 0xFFFF);
  TEST_ASSERT_TRUE(s[15 * 128 + 127] & kLat);
  TEST_ASSERT_TRUE(s[15 * 128 + 127] & kR1);
  w.setPixel(s.data(), 128, 0, 1, 1, 1);  // off the panel: ignored
  w.setPixel(s.data(), 0, 32, 1, 1, 1);
}

// SM16169SH, P4 80x40 at 1/10 scan folded: 160 pixels (10 chips) per data
// line, 20 rows. SM16380SH-family protocol.
static Wave shPanel(uint8_t profile, RowDriver rows = RowDriver::Shift) {
  Wave w;
  TEST_ASSERT_TRUE(w.configure(Chip::SM16169SH, 160, 20, profile, rows));
  return w;
}

static size_t latCount(const std::vector<uint16_t>& s, size_t from, size_t to) {
  size_t lats = 0;
  for (size_t i = from; i < to; i++) lats += (s[i] & kLat) ? 1 : 0;
  return lats;
}

static size_t latCount(const std::vector<uint16_t>& s, size_t n) { return latCount(s, 0, n); }

static void test_sh_profiles() {
  TEST_ASSERT_EQUAL(4, profileCount(Chip::SM16169SH));
  TEST_ASSERT_EQUAL(kProfileCount, profileCount(Chip::SM16380));
  Wave w;
  TEST_ASSERT_FALSE(w.configure(Chip::SM16169SH, 160, 20, 4, RowDriver::Shift));
  TEST_ASSERT_EQUAL(33, shPanel(0).regVariants());
  TEST_ASSERT_EQUAL(0, shPanel(0).rowGclk());  // OE is a row clock
  TEST_ASSERT_EQUAL(kRegVariants, panel().regVariants());
  for (uint8_t p = 0; p < 4; p++) {
    Wave s = shPanel(p);
    TEST_ASSERT_EQUAL(0, s.headLength() % 2);
    TEST_ASSERT_EQUAL(0, s.regLength() % 2);
    TEST_ASSERT_EQUAL(0, s.bodyLength() % 2);
  }
}

// Runs of LAT-high samples in the head: vertical sync, pre-active.
static void test_sh_head() {
  Wave w = shPanel(0);
  std::vector<uint16_t> s(w.headLength());
  w.fillHead(s.data());
  std::vector<int> runs;
  int run = 0;
  for (uint16_t v : s) {
    TEST_ASSERT_EQUAL(0, v & ~kLat);
    if (v & kLat) {
      run++;
    } else if (run) {
      runs.push_back(run);
      run = 0;
    }
  }
  TEST_ASSERT_EQUAL(0, run);
  TEST_ASSERT_EQUAL(2, runs.size());
  TEST_ASSERT_EQUAL(3, runs[0]);
  TEST_ASSERT_EQUAL(14, runs[1]);
  TEST_ASSERT_EQUAL(24, panel().headLength());  // SM16380 unchanged
}

// Six blocks of one word per chip, LAT over the last 5 clocks of each.
static void test_sh_register_blocks() {
  Wave w = shPanel(0);
  const size_t bits = 160;
  std::vector<uint16_t> s(w.regLength());
  TEST_ASSERT_EQUAL(6 * bits + 2, w.regLength());
  w.fillReg(0, s.data());
  const uint16_t fixed[6] = {0x00AA, 0x01AA, 0, 0xF003, 0x0055, 0x0155};
  for (size_t b = 0; b < 6; b++) {
    TEST_ASSERT_EQUAL(5, latCount(s, b * bits, (b + 1) * bits));
    for (size_t k = 1; k <= 5; k++) TEST_ASSERT_TRUE(s[(b + 1) * bits - k] & kLat);
    if (b == 2) continue;
    for (int chip = 0; chip < 10; chip++) TEST_ASSERT_EQUAL_HEX16(fixed[b], lane(&s[b * bits + chip * 16], kR2));
  }
  // Block 3 rotates through the table; address 0x02 = scan lines - 1.
  TEST_ASSERT_EQUAL_HEX16(0x0209, lane(&s[2 * bits], kG1));
  w.fillReg(18, s.data());
  TEST_ASSERT_EQUAL_HEX16(0x1414, lane(&s[2 * bits], kR1));
  TEST_ASSERT_EQUAL_HEX16(0x1422, lane(&s[2 * bits], kG1));
  TEST_ASSERT_EQUAL_HEX16(0x1432, lane(&s[2 * bits], kB2));
  Wave cap = shPanel(1);  // the SM16380SH capture's table
  cap.fillReg(0, s.data());
  TEST_ASSERT_EQUAL_HEX16(0x0000, lane(&s[2 * bits], kR1));
  cap.fillReg(1, s.data());
  TEST_ASSERT_EQUAL_HEX16(0x0209, lane(&s[2 * bits], kR1));
}

// The panel stores the shared register blocks once: the pieces must match
// a whole register write.
static void test_sh_register_pieces() {
  Wave w = shPanel(0);
  const size_t from = w.regVaryingFrom(), len = w.regVaryingLength(), n = w.regLength();
  TEST_ASSERT_EQUAL(2 * 160, from);
  TEST_ASSERT_EQUAL(160, len);
  for (size_t v : {0u, 7u, 32u}) {
    std::vector<uint16_t> whole(n), piece(n);
    w.fillReg(v, whole.data());
    w.fillRegPart(0, piece.data(), 0, from);
    w.fillRegPart(v, piece.data() + from, from, from + len);
    w.fillRegPart(0, piece.data() + from + len, from + len, n);
    for (size_t i = 0; i < n; i++) TEST_ASSERT_EQUAL_HEX16(whole[i], piece[i]);
  }
  TEST_ASSERT_EQUAL(0, panel().regVaryingFrom());  // SM16380: all of it varies
  TEST_ASSERT_EQUAL(panel().regLength(), panel().regVaryingLength());
}

// Walks the body: per row, plain clocks, then the gap with the shift row
// driver's clock pulse and the row clock on OE.
static void checkShiftRows(const Wave& w, int rowClocks) {
  std::vector<uint16_t> s = mainSection(w, 0);
  const uint16_t decoderOnly = (1u << 11) | (1u << 12) | kB;
  int rowsSeen = 0;
  size_t i = 0;
  while (i < s.size()) {
    for (int k = 0; k < rowClocks; k++, i++) TEST_ASSERT_FALSE(s[i] & (kOe | kA));
    // Gap: C from sample 2, the clock (A) high for samples 3-14; C only on
    // the clock into row 0. Then OE for 4 samples, 10 into row 0.
    const bool toFirst = (rowsSeen + 1) % 10 == 0;
    for (int g = 0; g < 28; g++, i++) {
      const bool clock = g >= 3 && g <= 14;
      TEST_ASSERT_EQUAL(clock, (s[i] & kA) != 0);
      TEST_ASSERT_EQUAL(toFirst && g >= 2 && g <= 14, (s[i] & kC) != 0);
      TEST_ASSERT_EQUAL(g >= 16 && g < 16 + (toFirst ? 10 : 4), (s[i] & kOe) != 0);
      TEST_ASSERT_EQUAL(0, s[i] & decoderOnly);
    }
    rowsSeen++;
  }
  TEST_ASSERT_EQUAL(s.size(), i);
  TEST_ASSERT_EQUAL(0, rowsSeen % 10);  // whole scans
}

static void test_sh_shift_rows() {
  checkShiftRows(shPanel(0), 256);
  checkShiftRows(shPanel(2), 112);
}

// The SM16380 output on a shift row driver: same GCLK blocks, rows by clock.
static void test_sm16380_shift_rows() {
  Wave w;
  TEST_ASSERT_TRUE(w.configure(Chip::SM16380, 128, 32, 0, RowDriver::Shift));
  std::vector<uint16_t> s = mainSection(w, 0);
  size_t clocks = 0;
  for (size_t i = 1; i < s.size(); i++) {
    if ((s[i] & kA) && !(s[i - 1] & kA)) clocks++;
    TEST_ASSERT_EQUAL(0, rowOf(s[i]) & ~0x5);  // only A and C ever set
  }
  TEST_ASSERT_EQUAL(0, clocks % 16);
  TEST_ASSERT_TRUE(clocks > 0);
}

static void test_sh_pixels_match_sm16380_layout() {
  Wave w = shPanel(0);
  std::vector<uint16_t> s(w.bodyLength());
  w.fillBody(s.data());
  TEST_ASSERT_EQUAL(10 * 16, latCount(s, s.size()));
  // (95, 13): bottom line, scan row 3, chip 5, channel 15.
  w.setPixel(s.data(), 95, 13, 0x1234, 0, 0);
  TEST_ASSERT_EQUAL_HEX16(0x1234, lane(&s[(3 * 16 + 15) * 160 + 5 * 16], kR2));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_sh_profiles);
  RUN_TEST(test_sh_head);
  RUN_TEST(test_sh_register_blocks);
  RUN_TEST(test_sh_register_pieces);
  RUN_TEST(test_sh_shift_rows);
  RUN_TEST(test_sm16380_shift_rows);
  RUN_TEST(test_sh_pixels_match_sm16380_layout);
  RUN_TEST(test_rejects_unsupported_geometry);
  RUN_TEST(test_lengths_are_even);
  RUN_TEST(test_head_is_vsync_then_pre_active);
  RUN_TEST(test_register_writes);
  RUN_TEST(test_gclk_blocks_and_rows);
  RUN_TEST(test_pixel_upload_latches);
  RUN_TEST(test_set_pixel_writes_one_lane);
  return UNITY_END();
}
