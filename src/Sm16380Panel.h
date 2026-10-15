#pragma once

#include <Arduino.h>

#include "Board.h"
#include "Sm16380Wave.h"

class Bus_Parallel16;

// HUB75 panel with SM16380-family S-PWM drivers. The whole clock-by-clock
// waveform (see Sm16380Wave.h) lives in RAM and the ESP32-S3's LCD
// peripheral replays it in an endless DMA loop, so the drivers get the
// continuous clock they need. Drawing edits the pixel data in place.
class Sm16380Panel {
 public:
  struct Config {
    PanelPins pins;  // colour order already applied
    uint16_t width;  // total, including chained panels
    uint16_t height;
    uint8_t profile;
    uint8_t grayBits;  // 16, or 14 for drivers that ignore the top two bits
    bool clkPhase;
    uint8_t speedMHz;
  };

  bool begin(const Config& cfg);
  void setBrightness(uint8_t brightness);
  void drawPixel(uint16_t x, uint16_t y, uint16_t rgb565);

 private:
  sm16380::Wave wave_;
  Bus_Parallel16* bus_ = nullptr;
  uint16_t* body_ = nullptr;
  uint8_t grayBits_ = 16;
  uint16_t level5_[32] = {};  // 5-bit red/blue -> driver word
  uint16_t level6_[64] = {};  // 6-bit green -> driver word
};
