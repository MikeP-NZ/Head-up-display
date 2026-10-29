#include <unity.h>

#include <vector>

#include "PanelMap.h"

using panelmap::Layout;

void setUp() {}
void tearDown() {}

static Layout layout(uint16_t w, uint16_t h, uint8_t rowsLit, uint8_t chunk = 0, bool reverse = true) {
  Layout l;
  l.width = w;
  l.height = h;
  l.rowsLit = rowsLit;
  l.chunk = chunk;
  l.reverse = reverse;
  return l;
}

// Every panel pixel lands on its own driver pixel, inside the driver's buffer
// (which only has spare pixels when row blocks are padded to whole chips).
static void assertOneToOne(const Layout& l, uint16_t chain) {
  TEST_ASSERT_TRUE(l.valid());
  const uint16_t dw = l.driverWidth() * chain, dh = l.driverHeight();
  if (l.blockWidth() == l.width) TEST_ASSERT_EQUAL(l.width * chain * l.height, dw * dh);
  std::vector<bool> used(dw * dh, false);
  for (uint16_t y = 0; y < l.height; y++) {
    for (uint16_t x = 0; x < l.width * chain; x++) {
      uint16_t px, py;
      panelmap::map(l, x, y, px, py);
      TEST_ASSERT_TRUE(px < dw && py < dh);
      TEST_ASSERT_FALSE(used[py * dw + px]);
      used[py * dw + px] = true;
    }
  }
}

void test_standard_is_identity() {
  Layout l = layout(64, 32, 2);
  for (uint16_t y = 0; y < 32; y += 7) {
    for (uint16_t x = 0; x < 64; x += 9) {
      uint16_t px, py;
      panelmap::map(l, x, y, px, py);
      TEST_ASSERT_EQUAL(x, px);
      TEST_ASSERT_EQUAL(y, py);
    }
  }
}

// The four-scan mapping the firmware used before (the library's
// VirtualMatrixPanel FOUR_SCAN_32PX_HIGH), for both block orders.
void test_matches_old_four_scan() {
  for (int alt = 0; alt < 2; alt++) {
    Layout l = layout(64, 32, 4, 0, alt == 0);
    for (uint16_t y = 0; y < 32; y++) {
      for (uint16_t x = 0; x < 128; x++) {
        const uint16_t w = 64, blk = 8;
        bool firstBlock = (y & blk) == 0;
        if (alt) firstBlock = !firstBlock;
        const uint16_t ex = (x / w) * 2 * w + (firstBlock ? w : 0) + (x % w);
        const uint16_t ey = (y / (2 * blk)) * blk + (y % blk);
        uint16_t px, py;
        panelmap::map(l, x, y, px, py);
        TEST_ASSERT_EQUAL(ex, px);
        TEST_ASSERT_EQUAL(ey, py);
      }
    }
  }
}

// P4 80x40 at 1/10 scan: blocks of 10 rows, 10 row addresses.
void test_80x40_tenth_scan() {
  Layout l = layout(80, 40, 4);
  TEST_ASSERT_EQUAL(160, l.driverWidth());
  TEST_ASSERT_EQUAL(20, l.driverHeight());
  uint16_t px, py;
  panelmap::map(l, 5, 3, px, py);  // first block of the top half
  TEST_ASSERT_EQUAL(85, px);
  TEST_ASSERT_EQUAL(3, py);
  panelmap::map(l, 5, 13, px, py);  // second block, same address
  TEST_ASSERT_EQUAL(5, px);
  TEST_ASSERT_EQUAL(3, py);
  panelmap::map(l, 5, 33, px, py);  // second block of the bottom half
  TEST_ASSERT_EQUAL(5, px);
  TEST_ASSERT_EQUAL(13, py);
  assertOneToOne(l, 1);
  assertOneToOne(l, 2);
}

void test_zigzag_chunks() {
  Layout l = layout(80, 40, 4, 16, false);
  uint16_t px, py;
  panelmap::map(l, 17, 2, px, py);  // block 0, second chunk
  TEST_ASSERT_EQUAL(33, px);
  panelmap::map(l, 17, 12, px, py);  // block 1, second chunk
  TEST_ASSERT_EQUAL(49, px);
  TEST_ASSERT_EQUAL(2, py);
  assertOneToOne(l, 1);
  assertOneToOne(layout(104, 52, 4, 8), 2);
}

// P3.076 104x52 at 1/13 scan (ICND2037): 7 chips per row block, so each
// block's 104 columns are followed by 8 unused outputs.
void test_104_wide_pads_to_whole_chips() {
  Layout l = layout(104, 52, 4);
  TEST_ASSERT_EQUAL(112, l.blockWidth());
  TEST_ASSERT_EQUAL(224, l.driverWidth());
  TEST_ASSERT_EQUAL(26, l.driverHeight());
  uint16_t px, py;
  panelmap::map(l, 0, 13, px, py);  // second block is shifted first
  TEST_ASSERT_EQUAL(0, px);
  TEST_ASSERT_EQUAL(0, py);
  panelmap::map(l, 0, 0, px, py);  // first block after the padding
  TEST_ASSERT_EQUAL(112, px);
  panelmap::map(l, 103, 51, px, py);
  TEST_ASSERT_EQUAL(103, px);
  TEST_ASSERT_EQUAL(25, py);
  panelmap::map(l, 104, 0, px, py);  // second chained panel
  TEST_ASSERT_EQUAL(224 + 112, px);
  assertOneToOne(l, 2);

  l.chipPad = false;
  TEST_ASSERT_EQUAL(208, l.driverWidth());
  assertOneToOne(l, 1);
  // Zig-zag chains aren't padded.
  TEST_ASSERT_EQUAL(208, layout(104, 52, 4, 8).driverWidth());
  // Standard scan pads the single block.
  Layout s = layout(40, 20, 2);
  TEST_ASSERT_EQUAL(48, s.driverWidth());
  panelmap::map(s, 41, 3, px, py);
  TEST_ASSERT_EQUAL(49, px);
  TEST_ASSERT_EQUAL(3, py);
}

// The same panel as read from the column-code test: its chips use outputs
// 0-14, 0-14, 0-14, 1-14, 1-15, 1-15, 1-15.
void test_104_wide_skip_list() {
  static const uint8_t kSkip[] = {15, 31, 47, 48, 63, 64, 80, 96};
  Layout l = layout(104, 52, 4);
  for (uint8_t s : kSkip) l.skip[l.skipCount++] = s;
  TEST_ASSERT_TRUE(l.valid());
  TEST_ASSERT_EQUAL(112, l.blockWidth());
  TEST_ASSERT_EQUAL(14, l.outputOf(14));
  TEST_ASSERT_EQUAL(16, l.outputOf(15));  // chip 1 output 0
  TEST_ASSERT_EQUAL(30, l.outputOf(29));
  TEST_ASSERT_EQUAL(49, l.outputOf(45));  // chip 3 output 1
  TEST_ASSERT_EQUAL(62, l.outputOf(58));  // chip 3 output 14
  TEST_ASSERT_EQUAL(65, l.outputOf(59));  // chip 4 output 1
  TEST_ASSERT_EQUAL(97, l.outputOf(89));  // chip 6 output 1
  TEST_ASSERT_EQUAL(111, l.outputOf(103));
  uint16_t px, py;
  panelmap::map(l, 45, 0, px, py);  // first block, after the second block
  TEST_ASSERT_EQUAL(112 + 49, px);
  assertOneToOne(l, 2);
  // No skipped output is ever written.
  for (uint16_t x = 0; x < 104; x++) {
    for (uint8_t s : kSkip) TEST_ASSERT_NOT_EQUAL(s, l.outputOf(x));
  }

  Layout bad = l;
  bad.skip[1] = 15;  // not ascending
  TEST_ASSERT_FALSE(bad.valid());
  bad = l;
  bad.skip[7] = 112;  // past the row block
  TEST_ASSERT_FALSE(bad.valid());

  // A list left over after switching to a layout it doesn't apply to is
  // ignored, not an error.
  Layout p4 = l;
  p4.width = 80;
  p4.height = 40;
  TEST_ASSERT_TRUE(p4.valid());
  TEST_ASSERT_EQUAL(80, p4.blockWidth());
  TEST_ASSERT_EQUAL(79, p4.outputOf(79));
  Layout zigzag = l;
  zigzag.chunk = 8;
  TEST_ASSERT_TRUE(zigzag.valid());
  TEST_ASSERT_EQUAL(208, zigzag.driverWidth());
  Layout off = l;
  off.chipPad = false;
  TEST_ASSERT_TRUE(off.valid());
  TEST_ASSERT_EQUAL(208, off.driverWidth());
}

void test_other_sizes() {
  assertOneToOne(layout(104, 52, 4), 1);  // P3.076, 1/13
  assertOneToOne(layout(40, 20, 4), 3);   // P8, 1/5
  assertOneToOne(layout(64, 64, 4), 1);   // SM16380 64x64 at 1/16
  assertOneToOne(layout(32, 16, 8), 2);   // P10, 1/2
  assertOneToOne(layout(32, 16, 8, 8, false), 1);
  assertOneToOne(layout(80, 40, 2), 1);
}

void test_valid() {
  TEST_ASSERT_TRUE(layout(80, 40, 4).valid());
  TEST_ASSERT_FALSE(layout(104, 52, 8).valid());  // 52 / 8 isn't whole
  TEST_ASSERT_FALSE(layout(64, 30, 4).valid());
  TEST_ASSERT_FALSE(layout(80, 40, 4, 32).valid());
  TEST_ASSERT_FALSE(layout(64, 32, 3).valid());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_standard_is_identity);
  RUN_TEST(test_matches_old_four_scan);
  RUN_TEST(test_80x40_tenth_scan);
  RUN_TEST(test_zigzag_chunks);
  RUN_TEST(test_104_wide_pads_to_whole_chips);
  RUN_TEST(test_104_wide_skip_list);
  RUN_TEST(test_other_sizes);
  RUN_TEST(test_valid);
  return UNITY_END();
}
