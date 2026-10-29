#include "Display.h"

#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>

#include "Sm16380Panel.h"

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
  PanelPins q = p;
  q.r1 = orderPin(o[0], p.r1, p.g1, p.b1);
  q.g1 = orderPin(o[1], p.r1, p.g1, p.b1);
  q.b1 = orderPin(o[2], p.r1, p.g1, p.b1);
  q.r2 = orderPin(o[0], p.r2, p.g2, p.b2);
  q.g2 = orderPin(o[1], p.r2, p.g2, p.b2);
  q.b2 = orderPin(o[2], p.r2, p.g2, p.b2);

  // The panel's DMA buffers are allocated before the canvas, while internal
  // RAM is least fragmented.
  if (s.driver == PanelDriver::SM16380) {
    beginSm16380(s, q);
    renderer_ = new Renderer(width, height);
    shadow_ = static_cast<uint16_t*>(calloc(static_cast<size_t>(width) * height, sizeof(uint16_t)));
    applySettings(s);
    return panelOk();
  }

  HUB75_I2S_CFG::i2s_pins pins = {q.r1, q.g1, q.b1, q.r2, q.g2, q.b2, q.a, q.b, q.c, q.d, q.e, q.lat, q.oe, q.clk};

  // A panel that lights 4 or 8 rows per row address shifts out more pixels
  // per address than a standard one, so the driver sees it as wider and less
  // high; present() folds the real image into that layout (PanelMap.h).
  layout_ = s.panelLayout();
  HUB75_I2S_CFG cfg(layout_.driverWidth(), layout_.driverHeight(), s.chain, pins);
  if (layout_.rowsLit > 2) {
    log_i("HUB75 %u rows per address (1/%u scan), %ux%u per panel to the driver", layout_.rowsLit,
          static_cast<unsigned>(layout_.height / layout_.rowsLit), layout_.driverWidth(), layout_.driverHeight());
  }
  cfg.driver = static_cast<HUB75_I2S_CFG::shift_driver>(s.driver);
  cfg.clkphase = s.clkPhase;
  cfg.i2sspeed = s.i2sSpeedMHz >= 20   ? HUB75_I2S_CFG::HZ_20M
                 : s.i2sSpeedMHz >= 16 ? HUB75_I2S_CFG::HZ_16M
                                       : HUB75_I2S_CFG::HZ_8M;
  cfg.latch_blanking = s.latchBlanking;
  cfg.double_buff = false;

  matrix_ = new MatrixPanel_I2S_DMA(cfg);
  if (!matrix_->begin()) {
    log_e("HUB75 init failed - check the portal's Hardware settings");
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

bool Display::beginSm16380(const Settings& s, const PanelPins& pins) {
  // Same folding as a HUB75 panel: e.g. 64x64 at 1/16 scan has two 64-pixel
  // row blocks on each data line's 8-chip chain.
  layout_ = s.panelLayout();
  Sm16380Panel::Config cfg;
  cfg.pins = pins;
  cfg.width = layout_.driverWidth() * s.chain;
  cfg.height = layout_.driverHeight();
  cfg.profile = s.spwmProfile < sm16380::kProfileCount ? s.spwmProfile : 0;
  cfg.grayBits = s.spwmGrayBits;
  cfg.clkPhase = s.clkPhase;
  cfg.speedMHz = s.i2sSpeedMHz;
  spwm_ = new Sm16380Panel();
  if (!spwm_->begin(cfg)) {
    log_e("SM16380 init failed - check the portal's Hardware settings");
    delete spwm_;
    spwm_ = nullptr;
  }
  return spwm_ != nullptr;
}

void Display::applySettings(const Settings& s) {
  RenderStyle style;
  style.speedRgb = s.speedColor;
  style.accentRgb = s.accentColor;
  style.nightRgb = s.nightColor;
  style.showSource = s.showSource;
  style.showUnit = s.showUnit;
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
  const uint8_t b = night_ ? nightBrightness_ : brightness_;
  if (matrix_) matrix_->setBrightness8(b);
  if (spwm_) {
    // Brightness is part of the pixel data, so everything is redrawn.
    spwm_->setBrightness(b);
    fullRefresh_ = true;
  }
}

void Display::present() {
  if ((!matrix_ && !spwm_) || !shadow_) return;
  const uint16_t* buf = renderer_->frame();
  uint16_t w = renderer_->width(), h = renderer_->height();
  for (uint16_t y = 0; y < h; y++) {
    for (uint16_t x = 0; x < w; x++) {
      size_t i = static_cast<size_t>(y) * w + x;
      if (fullRefresh_ || buf[i] != shadow_[i]) {
        uint16_t px, py;
        panelmap::map(layout_, x, y, px, py);
        if (matrix_) {
          matrix_->drawPixel(px, py, buf[i]);
        } else {
          spwm_->drawPixel(px, py, buf[i]);
        }
        shadow_[i] = buf[i];
      }
    }
  }
  fullRefresh_ = false;
}
