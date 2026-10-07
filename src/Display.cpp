#include "Display.h"

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

namespace {

int8_t orderPin(char which, int8_t r, int8_t g, int8_t b) {
  return which == 'R' ? r : which == 'G' ? g : b;
}

}  // namespace

bool Display::begin(const Settings& s) {
  uint16_t width = s.panelWidth * s.chain;
  uint16_t height = s.panelHeight;

  PanelPins p = s.pins;
  if (const HubPort* port = findPort(s.hubPort.c_str())) {
    p = port->pins;
    for (int8_t gate : port->enablePins) {
      if (gate < 0) continue;
      pinMode(gate, OUTPUT);
      digitalWrite(gate, HIGH);
    }
    // Keep panels on the other connector pair dark instead of floating.
    for (int8_t idle : port->idlePins) {
      pinMode(idle, OUTPUT);
      digitalWrite(idle, LOW);
    }
    log_i("HUB75 output %s", port->id);
  }

  // Colour order is a pin swap: colorOrder[0] names the panel input that
  // receives the red channel, and so on.
  const char* o = s.colorOrder.c_str();
  HUB75_I2S_CFG::i2s_pins pins = {
      orderPin(o[0], p.r1, p.g1, p.b1), orderPin(o[1], p.r1, p.g1, p.b1), orderPin(o[2], p.r1, p.g1, p.b1),
      orderPin(o[0], p.r2, p.g2, p.b2), orderPin(o[1], p.r2, p.g2, p.b2), orderPin(o[2], p.r2, p.g2, p.b2),
      p.a, p.b, p.c, p.d, p.e, p.lat, p.oe, p.clk};

  // A four-scan (e.g. 1/8 scan) panel shifts out twice as many pixels per
  // row address as a standard one, so the driver sees it as twice as wide and
  // half as high; mapPixel() folds the real image into that layout.
  scan_ = s.scan;
  panelWidth_ = s.panelWidth;
  panelHeight_ = s.panelHeight;
  const bool fourScan = scan_ != PanelScan::Standard;
  HUB75_I2S_CFG cfg(fourScan ? s.panelWidth * 2 : s.panelWidth, fourScan ? s.panelHeight / 2 : s.panelHeight,
                    s.chain, pins);
  if (fourScan) log_i("HUB75 four-scan mapping (1/%u scan)", static_cast<unsigned>(s.panelHeight / 4));
  cfg.driver = static_cast<HUB75_I2S_CFG::shift_driver>(s.driver);
  cfg.clkphase = s.clkPhase;
  cfg.i2sspeed = s.i2sSpeedMHz >= 20   ? HUB75_I2S_CFG::HZ_20M
                 : s.i2sSpeedMHz >= 16 ? HUB75_I2S_CFG::HZ_16M
                                       : HUB75_I2S_CFG::HZ_8M;
  cfg.latch_blanking = s.latchBlanking;
  cfg.double_buff = false;

  matrix_ = new MatrixPanel_I2S_DMA(cfg);
  if (!matrix_->begin()) {
    log_e("HUB75 init failed - check panel settings");
    delete matrix_;
    matrix_ = nullptr;
  } else {
    matrix_->clearScreen();
  }

  renderer_ = new Renderer(width, height);
  shadow_ = static_cast<uint16_t*>(calloc(static_cast<size_t>(width) * height, sizeof(uint16_t)));
  applySettings(s);
  return matrix_ != nullptr;
}

void Display::applySettings(const Settings& s) {
  RenderStyle style;
  style.speedRgb = s.speedColor;
  style.accentRgb = s.accentColor;
  style.nightRgb = s.nightColor;
  style.showSource = s.showSource;
  style.showUnit = s.showUnit;
  style.showStatus = s.showStatus;
  style.unit = s.unit;
  style.depthUnit = s.depthUnit;
  style.decimals = s.decimals;
  style.rotation = s.rotation;
  renderer_->setStyle(style);
  brightness_ = s.brightness;
  nightBrightness_ = s.nightBrightness;
  applyBrightness();
}

void Display::setNight(bool night) {
  if (night_ == night) return;
  night_ = night;
  renderer_->setNight(night);
  applyBrightness();
}

void Display::applyBrightness() {
  if (matrix_) matrix_->setBrightness8(night_ ? nightBrightness_ : brightness_);
}

// Panel (x, y) -> position in the DMA buffer. On a four-scan panel each row
// address drives four rows: the shift-register chain runs through one block
// of `blk` rows (blk = height / 4), then the next, so each half-height DMA
// row holds two panel rows side by side. Same mapping as the library's
// VirtualMatrixPanel FOUR_SCAN_32PX_HIGH / FOUR_SCAN_16PX_HIGH.
void Display::mapPixel(uint16_t x, uint16_t y, int16_t& px, int16_t& py) const {
  if (scan_ == PanelScan::Standard) {
    px = x;
    py = y;
    return;
  }
  const uint16_t w = panelWidth_;
  const uint16_t blk = panelHeight_ / 4;
  const uint16_t panel = x / w;
  bool firstBlock = (y & blk) == 0;
  if (scan_ == PanelScan::FourScanAlt) firstBlock = !firstBlock;
  px = panel * 2 * w + (firstBlock ? w : 0) + (x % w);
  py = (y / (2 * blk)) * blk + (y % blk);
}

void Display::present() {
  if (!matrix_ || !shadow_) return;
  const uint16_t* buf = renderer_->frame();
  uint16_t w = renderer_->width(), h = renderer_->height();
  for (uint16_t y = 0; y < h; y++) {
    for (uint16_t x = 0; x < w; x++) {
      size_t i = static_cast<size_t>(y) * w + x;
      if (fullRefresh_ || buf[i] != shadow_[i]) {
        int16_t px, py;
        mapPixel(x, y, px, py);
        matrix_->drawPixel(px, py, buf[i]);
        shadow_[i] = buf[i];
      }
    }
  }
  fullRefresh_ = false;
}
