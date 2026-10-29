#pragma once

#include <stdint.h>

// Where a panel pixel goes in the driver's buffer, for panels that light more
// than two rows per row address.
//
// A HUB75 panel has two data inputs (top half, bottom half). On a standard
// panel each row address lights one row in each half: two rows at once, so a
// 32 px high panel is 1/16 scan. Outdoor panels often light 4 (or 8) rows per
// address: an 80x40 panel at 1/10 scan lights 40 / 10 = 4. Each half then
// splits into rowsLit / 2 blocks of height / rowsLit rows, and the
// shift-register chain runs through every block for one address, so the
// driver sees a panel rowsLit / 2 times as wide and 2 / rowsLit times as high.
//
// The chain either runs a whole panel row through each block in turn
// (chunk = 0), or zig-zags between the blocks every `chunk` columns. Which
// block it fills first varies too (`reverse`).
//
// Panels whose width isn't a multiple of the 16-output driver chips leave
// some chip outputs unconnected: a 104 px row block has 7 chips, 112
// outputs. With `chipPad`, each row block's data is padded to whole chips
// (whole-row chains only), so the unused outputs get blanks: at the end of
// the row block, or at the positions in `skip` (counted from the first
// output of the row block). The P3.076 104x52 panel, for example, leaves
// 15, 31, 47, 48, 63, 64, 80 and 96 unused.
namespace panelmap {

constexpr uint16_t kChipOutputs = 16;
constexpr uint8_t kMaxSkip = 16;

struct Layout {
  uint16_t width = 64;   // one panel
  uint16_t height = 32;  // one panel
  uint8_t rowsLit = 2;   // rows lit per row address: 2 (standard), 4 or 8
  uint8_t chunk = 0;     // columns per zig-zag step; 0 = a whole panel row
  bool reverse = true;   // the chain fills the last block first
  bool chipPad = true;   // pad each row block to whole driver chips
  uint8_t skip[kMaxSkip] = {};  // unused outputs, ascending; none = at the end
  uint8_t skipCount = 0;

  uint16_t blocks() const { return rowsLit / 2; }
  uint16_t blockRows() const { return height / rowsLit; }
  // Only whole-row chains on widths that leave a chip partly unused; anywhere
  // else the skip list is ignored (and not checked), so a list left over from
  // another width or layout never blocks saving.
  bool padded() const { return chipPad && !chunk && width % kChipOutputs != 0; }
  // Driver outputs per row block, including unused ones.
  uint16_t blockWidth() const {
    if (!padded()) return width;
    if (skipCount) return width + skipCount;
    return (width + kChipOutputs - 1) / kChipOutputs * kChipOutputs;
  }
  // The output in its row block that drives panel column xl.
  uint16_t outputOf(uint16_t xl) const {
    if (!padded()) return xl;
    uint16_t o = xl;
    for (uint8_t i = 0; i < skipCount; i++) {
      if (skip[i] <= o) o++;
    }
    return o;
  }
  // The driver's view of one panel.
  uint16_t driverWidth() const { return blockWidth() * blocks(); }
  uint16_t driverHeight() const { return height * 2 / rowsLit; }
  bool valid() const {
    if (padded()) {
      for (uint8_t i = 1; i < skipCount; i++) {
        if (skip[i] <= skip[i - 1]) return false;
      }
      if (skipCount > kMaxSkip || (skipCount && skip[skipCount - 1] >= width + skipCount)) return false;
    }
    return (rowsLit == 2 || rowsLit == 4 || rowsLit == 8) && height % rowsLit == 0 &&
           (chunk == 0 || width % chunk == 0);
  }
};

// Panel (x, y) -> driver (px, py). x may run across chained panels.
inline void map(const Layout& l, uint16_t x, uint16_t y, uint16_t& px, uint16_t& py) {
  const uint16_t n = l.blocks();
  const uint16_t seg = l.blockWidth();
  const uint16_t panel = x / l.width;
  const uint16_t xl = l.outputOf(x % l.width);
  if (n <= 1) {
    px = panel * seg + xl;
    py = y;
    return;
  }
  const uint16_t blk = l.blockRows();
  const uint16_t halfRows = l.height / 2;
  const uint16_t half = y / halfRows;
  const uint16_t b = (y % halfRows) / blk;
  const uint16_t slot = l.reverse ? n - 1 - b : b;
  if (l.chunk) {
    const uint16_t c = l.chunk;
    px = panel * n * seg + (xl / c) * c * n + slot * c + xl % c;
  } else {
    px = panel * n * seg + slot * seg + xl;
  }
  py = half * blk + y % blk;
}

}  // namespace panelmap
