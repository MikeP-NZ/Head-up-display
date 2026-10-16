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

// The largest digits that fit `text` in w x h. h is 0 when there are no
// digit slots.
DigitMetrics fitDigits(const char* text, int w, int h) {
  // Count digit slots; '.' takes no slot of its own.
  int digits = 0;
  bool dot = false;
  for (const char* p = text; *p; p++) {
    if (*p == '.') dot = true;
    else digits++;
  }
  if (digits == 0) return DigitMetrics{0, 0, 0, 0};
  for (int hh = h; hh >= 7; hh--) {
    DigitMetrics c = metricsFor(hh);
    if (numberWidth(c, digits, dot) <= w) return c;
  }
  return metricsFor(7);
}

// Large labels are used where they cost the digits at most this share of
// their height.
constexpr int kMaxDigitLossPercent = 15;

bool largeLabelsAffordable(const char* text, int w, int hSmall, int hLarge) {
  int small = fitDigits(text, w, hSmall).h;
  int large = fitDigits(text, w, hLarge).h;
  return large * 100 >= small * (100 - kMaxDigitLossPercent);
}

// Label rows: TomThumb capitals are 5 px, the 5x7 font's 7 px; plus a 1 px gap.
constexpr int kSmallRowH = 6;
constexpr int kLargeRowH = 8;

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

int Renderer::labelWidth(const char* text, bool large) {
  canvas_->setFont(large ? nullptr : &TomThumb);
  // The 5x7 font's bounds include the blank column after the last letter.
  return large && *text ? textWidth(text) - 1 : textWidth(text);
}

void Renderer::drawLabel(const char* text, int x, int capsBottom, bool large, uint16_t col) {
  canvas_->setFont(large ? nullptr : &TomThumb);
  canvas_->setTextColor(col);
  // 5x7: the cursor is the top of the cell. TomThumb: the cursor is the
  // baseline, one row below the capitals.
  canvas_->setCursor(x, large ? capsBottom - 6 : capsBottom + 1);
  canvas_->print(text);
}

void Renderer::drawNumber(const char* text, int x0, int y0, int w, int h, uint16_t col) {
  DigitMetrics m = fitDigits(text, w, h);
  if (m.h == 0) return;
  int digits = 0;
  bool dot = false;
  for (const char* p = text; *p; p++) {
    if (*p == '.') dot = true;
    else digits++;
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
  const char* title = "HEAD-UP";
  // Too wide for a single 64px panel at double size.
  if (textWidth(title) > cw) canvas_->setTextSize(1);
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
  } else {
    drawSections(v);
  }
  // Demo mode: shown for 1.5 s in every 5 s, so nobody mistakes it for real data.
  if (v.simulated && nowMs % 5000 < 1500) drawSimulatedBanner();
}

// "SIMULATED DATA" in a box over the middle of the screen ("SIM DATA" where
// "SIMULATED" does not fit).
void Renderer::drawSimulatedBanner() {
  canvas_->setFont(nullptr);
  canvas_->setTextSize(1);
  const int cw = canvas_->width(), ch = canvas_->height();
  const char* first = cw >= 58 ? "SIMULATED" : "SIM";
  const int bw = max(textWidth(first), textWidth("DATA")) + 5, bh = 21;
  const int x = (cw - bw) / 2, y = (ch - bh) / 2;
  canvas_->fillRect(x, y, bw, bh, 0);
  canvas_->drawRect(x, y, bw, bh, warnColor());
  drawCentered(first, y + 3, warnColor());
  drawCentered("DATA", y + 11, warnColor());
}

void Renderer::drawSections(const DashView& v) {
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
  int n = min(cols * rows, static_cast<int>(kMaxSections));
  auto cellRect = [&](int i, int& x0, int& y0, int& w, int& h) {
    int c = i % cols, r = i / cols;
    x0 = cw * c / cols;
    y0 = ch * r / rows;
    w = cw * (c + 1) / cols - x0;
    h = ch * (r + 1) / rows - y0;
    if (c > 0) {//except for the first column, leave space to draw a divider line and shrink the cell width by 1 px.
      x0++;
      w--;
    }
  };

  // Large labels and units where every section has room for them, so all
  // sections use the same sizes.
  CellFonts fonts{true, true};
  for (int i = 0; i < n; i++) {
    if (v.items[i].item == Item::None) continue;
    int x0, y0, w, h;
    cellRect(i, x0, y0, w, h);
    CellFonts f = cellFonts(v.items[i], w, h);
    fonts.largeLabel &= f.largeLabel;
    fonts.largeUnit &= f.largeUnit;
  }

  for (int i = 0; i < n; i++) {
    int x0, y0, w, h;
    cellRect(i, x0, y0, w, h);
    if (i % cols > 0) {
      // A faint divider keeps one section's unit apart from the next label.
      canvas_->drawFastVLine(x0 - 1, y0, h, rgb565(scaleRgb(night_ ? style_.nightRgb : style_.accentRgb, 25)));
    }
    drawCell(v.items[i], x0, y0, w, h, fonts);
  }
  canvas_->setFont(nullptr);
}

// The unit, or P/S for a wind angle. For sizing, a wind angle always counts
// its side so the fonts don't change as the wind crosses the bow.
const char* Renderer::cellTag(const ItemView& item, bool forSizing) const {
  bool windAngle = itemInfo(item.item).format == ItemFormat::WindAngle;
  if (forSizing && windAngle) return "<<";
  char side = item.valid ? itemSide(item.item, item.value) : 0;
  if (side == 'P') return "<<";
  if (side == 'S') return ">>";
  return style_.showUnit ? item.unit : "";
}

// Which parts of one section's label row can use the 5x7 font: the row must
// fit across the section, and the taller row must not shrink the digits much.
Renderer::CellFonts Renderer::cellFonts(const ItemView& item, int w, int h) {
  CellFonts f;//initializes all to false
  const char* label = style_.showSource ? itemLabel(item) : "";
  const char* tag = cellTag(item, true);
  char text[12];
  formatItem(item.item, item.valid, item.value, style_.decimals, text, sizeof(text), item.shortDistance);
  //First check if the tall label and tag would take up to much of the cell height If true, return false for both fonts.
  if (!largeLabelsAffordable(text, w - 2, h - kSmallRowH, h - kLargeRowH)) return f;
  // At least a letter's width between label and unit, so they don't read as
  // one word.
  int room = w;
  int gap = *label && *tag ? 4 : 0;
  int largeLabel = labelWidth(label, true), largeTag = labelWidth(tag, true);
  // check if both the label and tag can fit in the cell width using the large font.
  // if not then check if the label can fit with the large font and the tag with the small font.
  // otherwise both the label and tag will be drawn with the small font.
  f.largeUnit = largeLabel + gap + largeTag <= room;
  f.largeLabel = f.largeUnit || largeLabel + gap + labelWidth(tag, false) <= room;
  return f;
}

// One section: label top left, unit (or P/S) top right, digits below.
void Renderer::drawCell(const ItemView& item, int x, int y, int w, int h, CellFonts fonts) {
  if (item.item == Item::None) return;
  bool largeRow = fonts.largeLabel || fonts.largeUnit;
  const int labelH = largeRow ? kLargeRowH : kSmallRowH;
  const int capsBottom = y + labelH - 2;

  if (style_.showSource) drawLabel(itemLabel(item), x, capsBottom, fonts.largeLabel, accentColor());

  const char* tag = cellTag(item, false);
  if (*tag) {
    bool side = itemInfo(item.item).format == ItemFormat::WindAngle && item.valid && itemSide(item.item, item.value);
    int tw = labelWidth(tag, fonts.largeUnit);
    drawLabel(tag, x + w - tw, capsBottom, fonts.largeUnit, side ? valueColor(item) : accentColor());
  }

  char text[12];
  formatItem(item.item, item.valid, item.value, style_.decimals, text, sizeof(text), item.shortDistance);
  drawNumber(text, x, y + labelH, w, h - labelH, valueColor(item));
}

void Renderer::drawSingle(const DashView& dash, uint32_t nowMs) {
  const ItemView& v = dash.items[0];
  canvas_->fillScreen(0);
  canvas_->setFont(nullptr);
  canvas_->setTextSize(1);
  int cw = canvas_->width();
  int ch = canvas_->height();

  char text[12];
  formatItem(v.item, v.valid, v.value, style_.decimals, text, sizeof(text), v.shortDistance);
  uint16_t digitColor = valueColor(v);

  // Wind angles show the side where other items show their unit: <<< port,
  // >>> starboard (<< and >> where space is short).
  const bool windAngle = itemInfo(v.item).format == ItemFormat::WindAngle;
  char side = v.valid ? itemSide(v.item, v.value) : 0;
  auto sideMarker = [&](bool shortForm) {
    if (side == 'P') return shortForm ? "<<" : "<<<";
    if (side == 'S') return shortForm ? ">>" : ">>>";
    return "";
  };
  const char* unit = windAngle ? sideMarker(false) : v.unit;
  // Wind angles keep the unit row while dead ahead so the digits do not jump.
  bool showUnit = style_.showUnit && (*unit || windAngle);
  uint16_t unitColor = side ? digitColor : accentColor();
  const char* label = itemLabel(v);
  bool showLabel = style_.showSource && *label;
  bool labels = showLabel || showUnit;

  // Short panels (64x32): label rows above and below would halve the digits,
  // so put the labels in a narrow column on the right instead.
  if (ch < 48 && labels && dash.message.length() == 0) {
    // Wind angles size the column for the side marker, so the font doesn't
    // change as the wind crosses the bow.
    const char* sizingUnit = windAngle ? "<<<" : unit;
    auto columnWidth = [&](bool large) {
      int colW = 0;
      if (showUnit) colW = max(colW, labelWidth(sizingUnit, large));
      if (showLabel) colW = max(colW, labelWidth(label, large));
      return colW;
    };
    // The 5x7 font where the wider column costs the digits little height.
    int smallW = columnWidth(false), largeW = columnWidth(true);
    bool large = fitDigits(text, cw - largeW - 1, ch).h * 100 >=
                 fitDigits(text, cw - smallW - 1, ch).h * (100 - kMaxDigitLossPercent);
    int colX = cw - (large ? largeW : smallW);
    drawNumber(text, 0, 0, colX - 1, ch, digitColor);
    if (showLabel && (v.valid || v.item != Item::Speed)) {
      drawLabel(label, colX, large ? 7 : 5, large, accentColor());
    }
    if (showUnit) drawLabel(unit, colX, ch - 2, large, unitColor);
    canvas_->setFont(nullptr);
    return;
  }

  // Label top left, unit top right; the bottom row is only for a problem
  // message, so the digits get the rest of the height.
  bool top = showLabel || showUnit;
  bool bottom = dash.message.length() > 0;
  int y0 = top ? 9 : 1;
  int y1 = bottom ? ch - 9 : ch - 1;

  // Where label and unit don't both fit in the 5x7 font (32 px wide
  // portrait), a wind angle's side marker shortens to << / >>; otherwise the
  // unit and then the label too use the small font. Wind angles size for the
  // side marker so the fonts stay put as the wind crosses the bow.
  const char* sizingUnit = windAngle ? "<<<" : unit;
  bool largeLabel = true, largeUnit = true;
  auto fits = [&]() {
    int w = showLabel ? labelWidth(label, largeLabel) : 0;
    if (showUnit) w += (showLabel ? 4 : 0) + labelWidth(sizingUnit, largeUnit);
    return w <= cw - 2;
  };
  if (windAngle && !fits()) {
    unit = sideMarker(true);
    sizingUnit = "<<";
  }
  if (!fits()) largeUnit = false;
  if (!fits()) largeLabel = false;

  // The speed item only names STW/SOG once it knows which one it shows.
  if (showLabel && (v.valid || v.item != Item::Speed)) drawLabel(label, 1, 6, largeLabel, accentColor());
  if (showUnit && *unit) drawLabel(unit, cw - 1 - labelWidth(unit, largeUnit), 6, largeUnit, unitColor);
  canvas_->setFont(nullptr);

  drawNumber(text, 0, y0, cw - 1, y1 - y0, digitColor);

  if (dash.message.length()) {
    drawTicker(dash.message.c_str(), ch - 8, dash.messageIsError ? errColor() : warnColor(), nowMs);
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

  // A group uses the 5x7 font when all its lines fit across the panel.
  canvas_->setTextSize(1);
  auto fitsLarge = [&](const Line* lines, int n) {
    for (int i = 0; i < n; i++) {
      if (labelWidth(lines[i].text.c_str(), true) > canvas_->width() - 2) return false;
    }
    return true;
  };
  bool portalLarge = fitsLarge(portal, np), boatLarge = fitsLarge(boat, nb);
  // 5x7 lines are 9 px apart (with descenders), TomThumb lines 7 px.
  auto perPage = [&](bool large) { return max(1, (canvas_->height() - 1) / (large ? 9 : 7)); };

  // Each group starts on a fresh page; short panels split a group over pages.
  int portalPages = (np + perPage(portalLarge) - 1) / perPage(portalLarge);
  int boatPages = (nb + perPage(boatLarge) - 1) / perPage(boatLarge);
  int p = page % (portalPages + boatPages);
  bool onPortal = p < portalPages;
  const Line* lines = onPortal ? portal : boat;
  int count = onPortal ? np : nb;
  bool large = onPortal ? portalLarge : boatLarge;
  int first = (onPortal ? p : p - portalPages) * perPage(large);

  canvas_->fillScreen(0);
  int capsBottom = large ? 7 : 5;
  for (int i = first; i < count && i < first + perPage(large); i++) {
    drawLabel(lines[i].text.c_str(), 1, capsBottom, large, lines[i].color);
    capsBottom += large ? 9 : 7;
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
