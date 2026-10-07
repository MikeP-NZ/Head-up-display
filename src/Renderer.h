#pragma once

#include <Arduino.h>

#include "BoatModel.h"
#include "Items.h"

class GFXcanvas16;

// One section of the dashboard.
struct ItemView {
  Item item = Item::None;
  bool valid = false;
  float value = 0;               // display units (see itemDisplay)
  const char* unit = "";         // see itemDisplay
  bool shortDistance = false;    // see itemDisplay
  Quantity kind = Quantity::Stw;  // Item::Speed: which speed is shown
};

struct DashView {
  uint8_t sections = 1;  // 1, 2 or 4
  ItemView items[kMaxSections];
  bool wifiUp = false;
  bool linkUp = false;
  // Problem text. Shown on the full-screen layout (with the first item), so
  // a multi-section layout falls back to it while there is a message.
  String message;
  bool messageIsError = false;
};

struct InfoView {
  bool setupMode = false;  // boat Wi-Fi not configured yet
  String apSsid;
  String apPass;
  bool apActive = false;
  String apIp;
  String staSsid;
  String staIp;
  String hostname;
};

struct RenderStyle {
  uint32_t speedRgb = 0xFFFFFF;
  uint32_t accentRgb = 0x00A0FF;
  uint32_t nightRgb = 0xFF0000;
  bool showSource = true;  // item label (STW/SOG, AWA, ...)
  bool showUnit = true;
  bool showStatus = true;
  int8_t decimals = -1;
  uint8_t rotation = 0;
};

// Draws every screen into an RGB565 canvas in panel orientation. Has no
// hardware dependency, so it also builds on the host (tools/preview).
class Renderer {
 public:
  Renderer(uint16_t width, uint16_t height);
  ~Renderer();

  void setStyle(const RenderStyle& style);
  void setNight(bool night) { night_ = night; }

  void drawSplash(const char* version);
  void drawDash(const DashView& v, uint32_t nowMs);
  // `page` is a free-running counter; the renderer wraps it.
  void drawInfo(const InfoView& v, uint32_t page);
  void drawTestPattern(uint32_t elapsedMs);
  void drawMessage(const char* title, const char* detail, int progressPercent, uint32_t nowMs);

  const uint16_t* frame() const;
  uint16_t width() const { return width_; }
  uint16_t height() const { return height_; }
  uint8_t rotation() const { return style_.rotation; }

 private:
  uint16_t speedColor() const;
  uint16_t accentColor() const;
  uint16_t dimColor() const;
  uint16_t warnColor() const;
  uint16_t okColor() const;
  uint16_t errColor() const;
  void drawSingle(const DashView& v, uint32_t nowMs);
  // Fonts for one section's label row, see drawDash().
  struct CellFonts {
    bool largeLabel = false;
    bool largeUnit = false;
  };
  CellFonts cellFonts(const ItemView& item, int w, int h);
  void drawCell(const ItemView& item, int x, int y, int w, int h, CellFonts fonts);
  const char* cellTag(const ItemView& item, bool forSizing) const;
  const char* itemLabel(const ItemView& item) const;
  uint16_t valueColor(const ItemView& item) const;
  void drawNumber(const char* text, int x0, int y0, int w, int h, uint16_t col);
  int textWidth(const char* text);
  // Label text in the 5x7 font (large) or TomThumb (small), placed by the
  // bottom row of its capitals so both sizes share a baseline.
  int labelWidth(const char* text, bool large);
  void drawLabel(const char* text, int x, int capsBottom, bool large, uint16_t col);
  void drawCentered(const char* text, int top, uint16_t col);
  void drawTicker(const char* text, int top, uint16_t col, uint32_t nowMs);

  GFXcanvas16* canvas_;
  uint16_t width_, height_;
  RenderStyle style_;
  bool night_ = false;
};
