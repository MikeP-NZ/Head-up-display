#include "Renderer.h"

#include <Adafruit_GFX.h>
#include <Fonts/TomThumb.h>

namespace {

// 7-segment layout: a=top, b=upper right, c=lower right, d=bottom,
// e=lower left, f=upper left, g=middle.
enum : uint8_t { SA = 1, SB = 2, SC = 4, SD = 8, SE = 16, SF = 32, SG = 64 };
constexpr uint8_t kDigitSegments[10] = {
    SA | SB | SC | SD | SE | SF,       // 0
    SB | SC,                           // 1
    SA | SB | SD | SE | SG,            // 2
    SA | SB | SC | SD | SG,            // 3
    SB | SC | SF | SG,                 // 4
    SA | SC | SD | SF | SG,            // 5
    SA | SC | SD | SE | SF | SG,       // 6
    SA | SB | SC,                      // 7
    SA | SB | SC | SD | SE | SF | SG,  // 8
    SA | SB | SC | SD | SF | SG,       // 9
};

uint16_t rgb565(uint32_t rgb) {
  return static_cast<uint16_t>(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

uint32_t scaleRgb(uint32_t rgb, uint8_t percent) {
  uint32_t r = ((rgb >> 16) & 0xFF) * percent / 100;
  uint32_t g = ((rgb >> 8) & 0xFF) * percent / 100;
  uint32_t b = (rgb & 0xFF) * percent / 100;
  return (r << 16) | (g << 8) | b;
}

struct DigitMetrics {
  int h, w, t, gap;
};

DigitMetrics metricsFor(int h) {
  DigitMetrics m;
  m.h = h;
  m.w = (h * 23 + 25) / 50;  // tall, narrow digits like a marine instrument
  m.t = max(2, (h + 3) / 7);
  m.gap = max(1, m.t / 2);
  return m;
}

int numberWidth(const DigitMetrics& m, int digits, bool dot) {
  return digits * m.w + (digits - 1) * m.gap + (dot ? m.t + m.gap : 0);
}

}  // namespace

Renderer::Renderer(uint16_t width, uint16_t height) : canvas_(new GFXcanvas16(width, height)), width_(width), height_(height) {
  canvas_->setTextWrap(false);
}

Renderer::~Renderer() { delete canvas_; }

void Renderer::setStyle(const RenderStyle& style) {
  style_ = style;
  canvas_->setRotation(style_.rotation);
}

uint16_t Renderer::speedColor() const { return rgb565(night_ ? style_.nightRgb : style_.speedRgb); }
uint16_t Renderer::accentColor() const { return rgb565(night_ ? scaleRgb(style_.nightRgb, 70) : style_.accentRgb); }
uint16_t Renderer::dimColor() const { return rgb565(scaleRgb(night_ ? style_.nightRgb : style_.speedRgb, 30)); }
uint16_t Renderer::warnColor() const { return rgb565(night_ ? style_.nightRgb : 0xFFA000); }
uint16_t Renderer::okColor() const { return rgb565(night_ ? scaleRgb(style_.nightRgb, 50) : 0x00C000); }
uint16_t Renderer::errColor() const { return rgb565(night_ ? style_.nightRgb : 0xFF2000); }

const uint16_t* Renderer::frame() const { return canvas_->getBuffer(); }

int Renderer::textWidth(const char* text) {
  int16_t x1, y1;
  uint16_t w, h;
  canvas_->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  return w;
}

// Default 5x7 font: `top` is the top of the 8px cell.
void Renderer::drawCentered(const char* text, int top, uint16_t col) {
  int w = textWidth(text);
  canvas_->setTextColor(col);
  canvas_->setCursor((canvas_->width() - w) / 2, top);
  canvas_->print(text);
}

void Renderer::drawTicker(const char* text, int top, uint16_t col, uint32_t nowMs) {
  int w = textWidth(text);
  int cw = canvas_->width();
  if (w <= cw) {
    drawCentered(text, top, col);
    return;
  }
  const int gap = 24;
  int period = w + gap;
  int offset = (nowMs / 40) % period;
  canvas_->setTextColor(col);
  canvas_->setCursor(-offset, top);
  canvas_->print(text);
  canvas_->setCursor(-offset + period, top);
  canvas_->print(text);
}

void Renderer::drawNumber(const char* text, int x0, int y0, int w, int h, uint16_t col) {
  // Count digit slots; '.' takes no slot of its own.
  int digits = 0;
  bool dot = false;
  for (const char* p = text; *p; p++) {
    if (*p == '.') dot = true;
    else digits++;
  }
  if (digits == 0) return;

  DigitMetrics m = metricsFor(7);
  for (int hh = h; hh >= 7; hh--) {
    DigitMetrics c = metricsFor(hh);
    if (numberWidth(c, digits, dot) <= w) {
      m = c;
      break;
    }
  }
  int total = numberWidth(m, digits, dot);
  int x = x0 + (w - total) / 2;
  int y = y0 + (h - m.h) / 2;
  int mid = (m.h - m.t) / 2;

  for (const char* p = text; *p; p++) {
    char ch = *p;
    if (ch == '.') {
      canvas_->fillRect(x, y + m.h - m.t, m.t, m.t, col);
      x += m.t + m.gap;
      continue;
    }
    uint8_t seg = 0;
    if (ch >= '0' && ch <= '9') seg = kDigitSegments[ch - '0'];
    else if (ch == '-') seg = SG;  // ' ' is a blank slot
    if (seg & SA) canvas_->fillRect(x, y, m.w, m.t, col);
    if (seg & SD) canvas_->fillRect(x, y + m.h - m.t, m.w, m.t, col);
    if (seg & SG) canvas_->fillRect(x, y + mid, m.w, m.t, col);
    if (seg & SF) canvas_->fillRect(x, y, m.t, mid + m.t, col);
    if (seg & SB) canvas_->fillRect(x + m.w - m.t, y, m.t, mid + m.t, col);
    if (seg & SE) canvas_->fillRect(x, y + mid, m.t, m.h - mid, col);
    if (seg & SC) canvas_->fillRect(x + m.w - m.t, y + mid, m.t, m.h - mid, col);
    if (m.t >= 3 && seg) {
      // Knock off the outer corners so the digits read as rounded.
      canvas_->drawPixel(x, y, 0);
      canvas_->drawPixel(x + m.w - 1, y, 0);
      canvas_->drawPixel(x, y + m.h - 1, 0);
      canvas_->drawPixel(x + m.w - 1, y + m.h - 1, 0);
    }
    x += m.w + m.gap;
  }
}

void Renderer::drawSplash(const char* version) {
  canvas_->fillScreen(0);
  canvas_->setFont(nullptr);
  canvas_->setTextSize(2);
  int cw = canvas_->width();
  canvas_->setTextColor(accentColor());
  const char* title = "SPEED";
  canvas_->setCursor((cw - textWidth(title)) / 2, canvas_->height() / 2 - 12);
  canvas_->print(title);
  canvas_->setTextSize(1);
  String v = String("v") + version;
  drawCentered(v.c_str(), canvas_->height() / 2 + 8, dimColor());
}

const char* Renderer::itemLabel(const ItemView& item) const {
  // Until a speed arrives it is not known whether it will be STW or SOG.
  if (item.item == Item::Speed) return item.valid ? quantityLabel(item.kind) : "SPD";
  return itemInfo(item.item).label;
}

// Wind angles are tinted red (port) / green (starboard), like the
// navigation lights, except in night mode.
uint16_t Renderer::valueColor(const ItemView& item) const {
  if (!item.valid) return dimColor();
  if (!night_) {
    char side = itemSide(item.item, item.value);
    if (side == 'P') return rgb565(0xFF3020);
    if (side == 'S') return rgb565(0x20E040);
  }
  return speedColor();
}

void Renderer::drawDash(const DashView& v, uint32_t nowMs) {
  if (v.sections <= 1 || v.message.length()) {
    drawSingle(v, nowMs);
    return;
  }
  canvas_->fillScreen(0);
  canvas_->setTextSize(1);
  int cw = canvas_->width();
  int ch = canvas_->height();

  // Split along the long side: on a 64x32 panel 4 sections are a 2x2 grid of
  // 32x16; rotated to portrait (32x64) they stack as 4 rows of 32x16.
  int cols, rows;
  if (v.sections == 2) {
    cols = ch >= cw ? 1 : 2;
    rows = 3 - cols;
  } else if (ch >= 2 * cw) {
    cols = 1;
    rows = 4;
  } else if (cw >= 4 * ch) {
    cols = 4;
    rows = 1;
  } else {
    cols = 2;
    rows = 2;
  }
  for (int i = 0; i < cols * rows && i < static_cast<int>(kMaxSections); i++) {
    int c = i % cols, r = i / cols;
    int x0 = cw * c / cols, x1 = cw * (c + 1) / cols;
    int y0 = ch * r / rows, y1 = ch * (r + 1) / rows;
    if (c > 0) {
      // A faint divider keeps one section's unit apart from the next label.
      canvas_->drawFastVLine(x0, y0, y1 - y0, rgb565(scaleRgb(night_ ? style_.nightRgb : style_.accentRgb, 25)));
      x0++;
    }
    drawCell(v.items[i], x0, y0, x1 - x0, y1 - y0);
  }
  canvas_->setFont(nullptr);
}

// One section: small label top left, unit (or P/S) top right, digits below.
void Renderer::drawCell(const ItemView& item, int x, int y, int w, int h) {
  if (item.item == Item::None) return;
  canvas_->setFont(&TomThumb);
  const int labelH = 6;  // TomThumb caps are 5 px, plus a 1 px gap

  if (style_.showSource) {
    canvas_->setTextColor(accentColor());
    canvas_->setCursor(x + 1, y + 5);
    canvas_->print(itemLabel(item));
  }

  char side = item.valid ? itemSide(item.item, item.value) : 0;
  char sideText[2] = {side, 0};
  const char* tag = side ? sideText : style_.showUnit ? itemUnit(item.item, style_.unit, style_.depthUnit) : "";
  if (*tag) {
    canvas_->setTextColor(side ? valueColor(item) : accentColor());
    canvas_->setCursor(x + w - textWidth(tag) - 1, y + 5);
    canvas_->print(tag);
  }

  char text[12];
  formatItem(item.item, item.valid, item.value, style_.decimals, text, sizeof(text));
  drawNumber(text, x + 1, y + labelH, w - 2, h - labelH, valueColor(item));
}

void Renderer::drawSingle(const DashView& dash, uint32_t nowMs) {
  const ItemView& v = dash.items[0];
  canvas_->fillScreen(0);
  canvas_->setFont(nullptr);
  canvas_->setTextSize(1);
  int cw = canvas_->width();
  int ch = canvas_->height();

  char text[12];
  formatItem(v.item, v.valid, v.value, style_.decimals, text, sizeof(text));
  uint16_t digitColor = valueColor(v);
  uint16_t wifiDot = dash.wifiUp ? okColor() : errColor();
  uint16_t dataDot = v.valid ? okColor() : dash.linkUp ? warnColor() : errColor();

  // Wind angles show the side where other items show their unit.
  char side = v.valid ? itemSide(v.item, v.value) : 0;
  const char* unit = itemUnit(v.item, style_.unit, style_.depthUnit);
  if (itemInfo(v.item).format == ItemFormat::WindAngle) unit = side == 'P' ? "PORT" : side == 'S' ? "STBD" : "";
  // Wind angles keep the unit row while dead ahead so the digits do not jump.
  bool showUnit = style_.showUnit && (*unit || itemInfo(v.item).format == ItemFormat::WindAngle);
  uint16_t unitColor = side ? digitColor : accentColor();
  const char* label = itemLabel(v);
  bool showLabel = style_.showSource && *label;
  bool labels = showLabel || showUnit || style_.showStatus;

  // Short panels (64x32): label rows above and below would halve the digits,
  // so put the labels in a narrow column on the right instead.
  if (ch < 48 && labels && dash.message.length() == 0) {
    canvas_->setFont(&TomThumb);
    int colW = 0;
    if (showUnit) colW = max(colW, textWidth(unit));
    if (showLabel) colW = max(colW, textWidth(label));
    if (style_.showStatus) colW = max(colW, 7);
    int colX = cw - colW - 1;
    drawNumber(text, 0, 1, colX - 1, ch - 2, digitColor);
    if (style_.showStatus) {
      canvas_->fillRect(cw - 7, 1, 3, 3, wifiDot);
      canvas_->fillRect(cw - 3, 1, 3, 3, dataDot);
    }
    if (showLabel && (v.valid || v.item != Item::Speed)) {
      canvas_->setTextColor(accentColor());
      canvas_->setCursor(colX, style_.showStatus ? 11 : 6);
      canvas_->print(label);
    }
    if (showUnit) {
      canvas_->setTextColor(unitColor);
      canvas_->setCursor(colX, ch - 2);
      canvas_->print(unit);
    }
    canvas_->setFont(nullptr);
    return;
  }

  bool top = showLabel || style_.showStatus;
  bool bottom = showUnit || dash.message.length() > 0;
  int y0 = top ? 9 : 1;
  int y1 = bottom ? ch - 9 : ch - 1;

  // The speed item only names STW/SOG once it knows which one it shows.
  if (showLabel && (v.valid || v.item != Item::Speed)) {
    canvas_->setTextColor(accentColor());
    canvas_->setCursor(1, 0);
    canvas_->print(label);
  }
  if (style_.showStatus) {
    canvas_->fillRect(cw - 8, 2, 3, 3, wifiDot);
    canvas_->fillRect(cw - 4, 2, 3, 3, dataDot);
  }

  drawNumber(text, 1, y0, cw - 2, y1 - y0, digitColor);

  if (dash.message.length()) {
    drawTicker(dash.message.c_str(), ch - 8, dash.messageIsError ? errColor() : warnColor(), nowMs);
  } else if (showUnit) {
    drawCentered(unit, ch - 8, unitColor);
  }
}

void Renderer::drawInfo(const InfoView& v, uint32_t page) {
  struct Line {
    String text;
    uint16_t color;
  };
  uint16_t title = warnColor(), label = accentColor(), value = speedColor();

  Line portal[7];
  int np = 0;
  portal[np++] = {v.setupMode ? "SETUP" : "PORTAL", title};
  if (v.apActive) {
    portal[np++] = {"JOIN WIFI:", label};
    portal[np++] = {v.apSsid, value};
    portal[np++] = {"PASSWORD:", label};
    portal[np++] = {v.apPass.length() ? v.apPass : String("(none)"), value};
    portal[np++] = {"THEN BROWSE:", label};
    portal[np++] = {v.apIp, value};
  } else {
    portal[np++] = {"PORTAL AP OFF", label};
    portal[np++] = {"HOLD BUTTON 3S", value};
  }

  Line boat[6];
  int nb = 0;
  if (!v.setupMode) {
    boat[nb++] = {"BOAT WIFI", title};
    boat[nb++] = {v.staSsid.length() ? v.staSsid : String("(not set)"), value};
    boat[nb++] = {"IP ADDRESS:", label};
    boat[nb++] = {v.staIp.length() ? v.staIp : String("not joined"), value};
    boat[nb++] = {"OR BROWSE:", label};
    boat[nb++] = {v.hostname + ".local", value};
  }

  // Each group starts on a fresh page; short panels split a group over pages.
  int perPage = max(1, (canvas_->height() - 1) / 7);
  int portalPages = (np + perPage - 1) / perPage;
  int boatPages = (nb + perPage - 1) / perPage;
  int p = page % (portalPages + boatPages);
  const Line* lines = p < portalPages ? portal : boat;
  int count = p < portalPages ? np : nb;
  int first = (p < portalPages ? p : p - portalPages) * perPage;

  canvas_->fillScreen(0);
  canvas_->setFont(&TomThumb);
  canvas_->setTextSize(1);
  int y = 6;
  for (int i = first; i < count && i < first + perPage; i++) {
    canvas_->setTextColor(lines[i].color);
    canvas_->setCursor(1, y);
    canvas_->print(lines[i].text);
    y += 7;
  }
  canvas_->setFont(nullptr);
}

void Renderer::drawTestPattern(uint32_t elapsedMs) {
  static const uint32_t kFills[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF};
  uint32_t step = elapsedMs / 1500;
  canvas_->setFont(nullptr);
  canvas_->setTextSize(1);
  int cw = canvas_->width(), ch = canvas_->height();
  if (step < 4) {
    canvas_->fillScreen(rgb565(kFills[step]));
    static const char* kNames[] = {"RED", "GREEN", "BLUE", "WHITE"};
    canvas_->fillRect(0, ch / 2 - 5, cw, 11, 0);
    drawCentered(kNames[step], ch / 2 - 3, rgb565(kFills[step] == 0xFFFFFF ? 0xFFFFFF : kFills[step]));
    return;
  }
  // Colour bars + border + corner markers to check orientation and chaining.
  canvas_->fillScreen(0);
  static const uint32_t kBars[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0x00FFFF, 0xFF00FF, 0xFFFFFF, 0x808080};
  int bw = cw / 8;
  for (int i = 0; i < 8; i++) canvas_->fillRect(i * bw, ch / 2, bw, ch / 2, rgb565(kBars[i]));
  canvas_->drawRect(0, 0, cw, ch, rgb565(0xFFFFFF));
  canvas_->fillRect(0, 0, 4, 4, rgb565(0xFF0000));  // top-left marker
  canvas_->setTextColor(rgb565(0xFF0000));
  canvas_->setCursor(6, 3);
  canvas_->print("R");
  canvas_->setTextColor(rgb565(0x00FF00));
  canvas_->print("G");
  canvas_->setTextColor(rgb565(0x0000FF));
  canvas_->print("B");
  canvas_->setTextColor(rgb565(0xFFFFFF));
  canvas_->setCursor(6, 14);
  canvas_->print("TOP L");
}

void Renderer::drawMessage(const char* title, const char* detail, int progressPercent, uint32_t nowMs) {
  canvas_->fillScreen(0);
  canvas_->setFont(nullptr);
  canvas_->setTextSize(1);
  int cw = canvas_->width(), ch = canvas_->height();
  drawCentered(title, ch / 2 - 14, warnColor());
  if (detail && *detail) drawTicker(detail, ch / 2 - 2, speedColor(), nowMs);
  if (progressPercent >= 0) {
    int w = cw - 8;
    canvas_->drawRect(4, ch / 2 + 10, w, 6, accentColor());
    canvas_->fillRect(5, ch / 2 + 11, (w - 2) * min(progressPercent, 100) / 100, 4, accentColor());
  }
}
