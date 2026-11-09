#include "Sm16380Panel.h"

#include <esp_heap_caps.h>
#include <math.h>
#include <platforms/esp32s3/gdma_lcd_parallel16.hpp>

namespace {

// Samples per DMA descriptor: 4092 bytes, the most one GDMA descriptor takes.
constexpr size_t kChunk = 2046;

uint16_t* allocSamples(size_t n) {
  return static_cast<uint16_t*>(heap_caps_malloc(n * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
}

}  // namespace

bool Sm16380Panel::begin(const Config& c) {
  const char* chipName = c.chip == sm16380::Chip::SM16169SH ? "SM16169SH" : "SM16380";
  if (!wave_.configure(c.chip, c.width, c.height, c.profile, c.rows)) {
    log_e("%s: %ux%u is not supported (width a multiple of 16, height 4-64)", chipName, c.width, c.height);
    return false;
  }
  grayBits_ = c.grayBits;

  // One copy of each segment; only part of the register write differs
  // between frames, so the descriptor ring visits its variants with the rest
  // shared: register samples [0, varFrom) and [varFrom + varLen, regLen) are
  // the same in every variant.
  const size_t variants = wave_.regVariants();
  const size_t headLen = wave_.headLength(), regLen = wave_.regLength(), bodyLen = wave_.bodyLength();
  const size_t varFrom = wave_.regVaryingFrom(), varLen = wave_.regVaryingLength();
  const size_t fixedLen = regLen - varLen;
  const size_t bytes = (headLen + fixedLen + varLen * variants + bodyLen) * sizeof(uint16_t);
  uint16_t* head = allocSamples(headLen);
  uint16_t* regs = allocSamples(fixedLen + varLen * variants);
  body_ = allocSamples(bodyLen);
  if (!head || !regs || !body_) {
    log_e("%s: not enough DMA memory (needs %u KB, largest free block %u KB)", chipName, bytes / 1024,
          heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) / 1024);
    heap_caps_free(head);
    heap_caps_free(regs);
    heap_caps_free(body_);
    body_ = nullptr;
    return false;
  }
  wave_.fillHead(head);
  // regs: shared prefix, shared suffix, then each variant's varying part.
  uint16_t* prefix = regs;
  uint16_t* suffix = regs + varFrom;
  uint16_t* varying = regs + fixedLen;
  wave_.fillRegPart(0, prefix, 0, varFrom);
  wave_.fillRegPart(0, suffix, varFrom + varLen, regLen);
  for (size_t v = 0; v < variants; v++) wave_.fillRegPart(v, varying + v * varLen, varFrom, varFrom + varLen);
  wave_.fillBody(body_);

  // Same bus layout as the HUB75 DMA library: CLK is the LCD write clock.
  Bus_Parallel16::config_t bc;
  bc.bus_freq = static_cast<uint32_t>(c.speedMHz) * 1000000;
  bc.pin_wr = c.pins.clk;
  bc.invert_pclk = c.clkPhase;
  const PanelPins& p = c.pins;
  const int8_t data[16] = {p.r1, p.g1, p.b1, p.r2, p.g2, p.b2, p.lat, p.oe, p.a, p.b, p.c, p.d, p.e, -1, -1, -1};
  memcpy(bc.pin_data, data, sizeof(data));

  auto chunks = [](size_t n) { return (n + kChunk - 1) / kChunk; };
  bus_ = new Bus_Parallel16();
  bus_->config(bc);
  bus_->init();
  // The bus driver's slowest clock is 10 MHz (setting 8); 5 and 2.5 MHz
  // come from a larger divider on the 160 MHz source.
  if (c.speedMHz < 8) {
    LCD_CAM.lcd_clock.lcd_clkm_div_num = c.speedMHz >= 5 ? 32 : 64;
    LCD_CAM.lcd_user.lcd_update = 1;
  }
  const size_t suffixLen = regLen - varFrom - varLen;
  const size_t perFrame = chunks(headLen) + chunks(varFrom) + chunks(varLen) + chunks(suffixLen) + chunks(bodyLen);
  if (!bus_->allocate_dma_desc_memory(variants * perFrame)) {
    log_e("%s: not enough memory for DMA descriptors", chipName);
    return false;
  }
  auto link = [&](uint16_t* p, size_t n) {
    for (size_t i = 0; i < n; i += kChunk) {
      bus_->create_dma_desc_link(p + i, (n - i < kChunk ? n - i : kChunk) * sizeof(uint16_t));
    }
  };
  for (size_t v = 0; v < variants; v++) {
    link(head, headLen);
    link(prefix, varFrom);
    link(varying + v * varLen, varLen);
    link(suffix, suffixLen);
    link(body_, bodyLen);
  }
  bus_->dma_transfer_start();

  const size_t frameClocks = headLen + regLen + bodyLen;
  log_i("%s %ux%u, %s, %s rows, %u KB DMA, %u clocks per frame, %d GCLK per row, %u register writes", chipName,
        c.width, c.height, sm16380::profileName(c.chip, c.profile),
        c.rows == sm16380::RowDriver::Shift ? "shift-register" : "decoded", bytes / 1024, frameClocks,
        wave_.rowGclk(), variants);
  return true;
}

// The drivers' own PWM has 16 bits, plenty to do gamma and brightness in
// software without losing the dark end.
void Sm16380Panel::setBrightness(uint8_t brightness) {
  const float top = static_cast<float>((1ul << grayBits_) - 1) * brightness / 255.0f;
  auto level = [&](int v, int max) -> uint16_t {
    if (v == 0 || brightness == 0) return 0;
    const long n = lroundf(top * powf(static_cast<float>(v) / max, 2.2f));
    return static_cast<uint16_t>(n < 1 ? 1 : n);
  };
  for (int i = 0; i < 32; i++) level5_[i] = level(i, 31);
  for (int i = 0; i < 64; i++) level6_[i] = level(i, 63);
}

void Sm16380Panel::drawPixel(uint16_t x, uint16_t y, uint16_t c) {
  if (!body_) return;
  wave_.setPixel(body_, x, y, level5_[c >> 11], level6_[c >> 5 & 0x3F], level5_[c & 0x1F]);
}
