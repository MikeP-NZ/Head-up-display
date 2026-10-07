#pragma once

#include <Arduino.h>

#include "Renderer.h"
#include "Settings.h"

class MatrixPanel_I2S_DMA;

// The HUB75 panel: owns the DMA driver and copies changed pixels from the
// renderer's canvas to it.
class Display {
 public:
  bool begin(const Settings& s);
  void applySettings(const Settings& s);
  void setNight(bool night);
  bool panelOk() const { return matrix_ != nullptr; }

  Renderer& renderer() { return *renderer_; }
  const Renderer& renderer() const { return *renderer_; }

  void present();

 private:
  void applyBrightness();
  void mapPixel(uint16_t x, uint16_t y, int16_t& px, int16_t& py) const;

  MatrixPanel_I2S_DMA* matrix_ = nullptr;
  Renderer* renderer_ = nullptr;
  uint16_t* shadow_ = nullptr;
  bool fullRefresh_ = true;
  bool night_ = false;
  uint8_t brightness_ = 160;
  uint8_t nightBrightness_ = 24;
  PanelScan scan_ = PanelScan::Standard;
  uint16_t panelWidth_ = 64;
  uint16_t panelHeight_ = 32;
};
