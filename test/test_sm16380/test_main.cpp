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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rejects_unsupported_geometry);
  RUN_TEST(test_lengths_are_even);
  RUN_TEST(test_head_is_vsync_then_pre_active);
  RUN_TEST(test_register_writes);
  RUN_TEST(test_gclk_blocks_and_rows);
  RUN_TEST(test_pixel_upload_latches);
  RUN_TEST(test_set_pixel_writes_one_lane);
  return UNITY_END();
}
