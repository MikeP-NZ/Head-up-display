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
  if (!wave_.configure(c.width, c.height, c.profile)) {
    log_e("SM16380: %ux%u is not supported (width a multiple of 16, height 4-64)", c.width, c.height);
    return false;
  }
  grayBits_ = c.grayBits;

  // One copy of each segment; only the register write differs between
  // frames, so the descriptor ring visits its variants with the rest shared.
  const size_t headLen = wave_.headLength(), regLen = wave_.regLength(), bodyLen = wave_.bodyLength();
  const size_t bytes = (headLen + regLen * sm16380::kRegVariants + bodyLen) * sizeof(uint16_t);
  uint16_t* head = allocSamples(headLen);
  uint16_t* regs = allocSamples(regLen * sm16380::kRegVariants);
  body_ = allocSamples(bodyLen);
  if (!head || !regs || !body_) {
    log_e("SM16380: not enough DMA memory (needs %u KB, largest free block %u KB)", bytes / 1024,
          heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) / 1024);
    heap_caps_free(head);
    heap_caps_free(regs);
    heap_caps_free(body_);
    body_ = nullptr;
    return false;
  }
  wave_.fillHead(head);
  for (size_t v = 0; v < sm16380::kRegVariants; v++) wave_.fillReg(v, regs + v * regLen);
  wave_.fillBody(body_);

  // Same bus layout as the HUB75 DMA library: CLK is the LCD write clock.
  Bus_Parallel16::config_t bc;
  bc.bus_freq = static_cast<uint32_t>(c.speedMHz) * 1000000;
  bc.pin_wr = c.pins.clk;
  bc.invert_pclk = c.clkPhase;
  const PanelPins& p = c.pins;
  const int8_t data[16] = {p.r1, p.g1, p.b1, p.r2, p.g2, p.b2, p.lat, p.oe, p.a, p.b, p.c, p.d, p.e, -1, -1, -1};
  memcpy(bc.pin_data, data, sizeof(data));

  const size_t bodyChunks = (bodyLen + kChunk - 1) / kChunk;
  bus_ = new Bus_Parallel16();
  bus_->config(bc);
  bus_->init();
  // The bus driver's slowest clock is 10 MHz (setting 8); 5 and 2.5 MHz
  // come from a larger divider on the 160 MHz source.
  if (c.speedMHz < 8) {
    LCD_CAM.lcd_clock.lcd_clkm_div_num = c.speedMHz >= 5 ? 32 : 64;
    LCD_CAM.lcd_user.lcd_update = 1;
  }
  if (!bus_->allocate_dma_desc_memory(sm16380::kRegVariants * (2 + bodyChunks))) {
    log_e("SM16380: not enough memory for DMA descriptors");
    return false;
  }
  for (size_t v = 0; v < sm16380::kRegVariants; v++) {
    bus_->create_dma_desc_link(head, headLen * sizeof(uint16_t));
    bus_->create_dma_desc_link(regs + v * regLen, regLen * sizeof(uint16_t));
    for (size_t i = 0; i < bodyChunks; i++) {
      const size_t n = i + 1 < bodyChunks ? kChunk : bodyLen - i * kChunk;
      bus_->create_dma_desc_link(body_ + i * kChunk, n * sizeof(uint16_t));
    }
  }
  bus_->dma_transfer_start();

  const size_t frameClocks = headLen + regLen + bodyLen;
  log_i("SM16380 %ux%u, %s, %u KB DMA, %u clocks per frame, %d GCLK per row", c.width, c.height,
        sm16380::profileName(c.profile), bytes / 1024, frameClocks, sm16380::Wave::kRowGclk);
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
