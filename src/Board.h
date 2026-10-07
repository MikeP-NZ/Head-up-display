#pragma once

#include <stddef.h>
#include <stdint.h>

// Controller-board description, selected at build time (BOARD_* flag set by
// the PlatformIO environment).

// HUB75 driver chips supported by ESP32-HUB75-MatrixPanel-DMA, same order as
// HUB75_I2S_CFG::shift_driver.
enum class PanelDriver : uint8_t { ShiftReg = 0, FM6124, FM6126A, ICN2038S, MBI5124, DP3246 };

struct PanelPins {
  int8_t r1, g1, b1, r2, g2, b2, a, b, c, d, e, lat, oe, clk;
};

// One selectable HUB75 output on boards with several connectors. Connectors
// that share data lines can be driven together, showing the same image.
struct HubPort {
  const char* id;     // stored in settings
  const char* label;  // shown in the portal
  PanelPins pins;
  int8_t enablePins[2];  // driven HIGH to gate the port's CLK/LAT through
  int8_t idlePins[6];    // the other connector pair's data lines, held LOW
};

struct BoardInfo {
  const char* id;  // also embedded in the firmware image, see firmwareTag()
  const char* name;
  PanelPins defaultPins;
  PanelDriver defaultDriver;
  bool defaultClkPhase;
  int8_t buttonPin;  // active low
  const char* buttonName;
  int8_t statusLedPin;
  bool octalMemory;  // GPIO 33-37 belong to octal flash/PSRAM
  const HubPort* ports;
  size_t portCount;
  const char* defaultPort;
};

const BoardInfo& board();

// nullptr when the board has no port selection or the id is unknown.
const HubPort* findPort(const char* id);

bool isUsableOutputPin(int gpio);

// "HEADUP-BOARD:<id>" lives in the firmware image so the OTA handler can
// refuse an image built for a different board.
const char* firmwareTag();
