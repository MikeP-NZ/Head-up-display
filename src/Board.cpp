#include "Board.h"

#include <string.h>

namespace {

#if defined(BOARD_HUIDU_HD_WF4)

// Huidu HD-WF4 v7.x (ESP32-S3). Pin map from community reverse engineering:
// github.com/andywwright/hd-wf4 and mrcodetastic/ESP32-HUB75-MatrixPanel-DMA#433.
// X1/X3 share one set of RGB lines and X2/X4 another; A-E, OE, CLK and LAT
// go to all four. GPIO45 gates CLK/LAT to X1+X2, GPIO14 to X3+X4.
constexpr PanelPins kX13 = {2, 6, 10, 3, 7, 11, 39, 38, 37, 36, 21, 33, 35, 34};
constexpr PanelPins kX24 = {4, 8, 12, 5, 9, 13, 39, 38, 37, 36, 21, 33, 35, 34};
constexpr int8_t kGateX12 = 45;
constexpr int8_t kGateX34 = 14;

constexpr HubPort kPorts[] = {
    {"X1", "X1 (75EX1)", kX13, {kGateX12, -1}, {4, 8, 12, 5, 9, 13}},
    {"X2", "X2 (75EX2)", kX24, {kGateX12, -1}, {2, 6, 10, 3, 7, 11}},
    {"X3", "X3 (75EX3)", kX13, {kGateX34, -1}, {4, 8, 12, 5, 9, 13}},
    {"X4", "X4 (75EX4)", kX24, {kGateX34, -1}, {2, 6, 10, 3, 7, 11}},
    {"X1+X3", "X1 + X3, same image on both", kX13, {kGateX12, kGateX34}, {4, 8, 12, 5, 9, 13}},
    {"X2+X4", "X2 + X4, same image on both", kX24, {kGateX12, kGateX34}, {2, 6, 10, 3, 7, 11}},
};

constexpr BoardInfo kBoard = {
    "huidu-hd-wf4",
    "Huidu HD-WF4",
    kX13,
    PanelDriver::ShiftReg,
    true,
    17,  // S1 "test" button
    "S1",
    40,  // RUN LED
    false,
    kPorts,
    sizeof(kPorts) / sizeof(kPorts[0]),
    "X1",
};

#else  // BOARD_WAVESHARE_S3_MATRIX (default)

// Waveshare ESP32-S3-RGB-Matrix (ESP32-S3-WROOM-2-N32R16V), pins from
// Waveshare's own examples. Their panels use an FM6126A and falling-edge clock.
constexpr BoardInfo kBoard = {
    "waveshare-s3-matrix",
    "Waveshare ESP32-S3-RGB-Matrix",
    {4, 5, 6, 7, 15, 16, 18, 8, 3, 42, 9, 40, 2, 41},
    PanelDriver::FM6126A,
    false,
    0,
    "BOOT",
    -1,  // no user LED
    true,
    nullptr,
    0,
    nullptr,
};

#endif

// The OTA check's own search string also appears in every image, followed by
// a NUL, so the scanner ignores matches with an empty id.
#define TAG_PREFIX "SPDISP-BOARD:"
__attribute__((used)) const char kFirmwareTag[] =
#if defined(BOARD_HUIDU_HD_WF4)
    TAG_PREFIX "huidu-hd-wf4";
#else
    TAG_PREFIX "waveshare-s3-matrix";
#endif

}  // namespace

const BoardInfo& board() { return kBoard; }

const HubPort* findPort(const char* id) {
  if (!id) return nullptr;
  for (size_t i = 0; i < kBoard.portCount; i++) {
    if (strcmp(kBoard.ports[i].id, id) == 0) return &kBoard.ports[i];
  }
  return nullptr;
}

bool isUsableOutputPin(int v) {
  // 19/20 are native USB; 22-25 do not exist on the S3; 26-32 are the SPI
  // flash; 33-37 also belong to octal flash/PSRAM where fitted.
  if (v < 0 || v > 48) return false;
  if (v == 19 || v == 20) return false;
  if (v >= 22 && v <= 32) return false;
  if (kBoard.octalMemory && v >= 33 && v <= 37) return false;
  if (v == kBoard.buttonPin || v == kBoard.statusLedPin) return false;
  return true;
}

const char* firmwareTag() { return kFirmwareTag; }
