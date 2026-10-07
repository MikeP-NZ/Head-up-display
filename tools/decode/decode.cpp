// Feeds lines from stdin through the firmware's decoder and prints the values
// the display would show. Handy for checking a gateway from a laptop:
//
//   nc 192.168.4.1 1456 | tools/decode/decode
//
// Build: c++ -std=c++17 -Ilib/BoatData/src tools/decode/decode.cpp lib/BoatData/src/*.cpp -o tools/decode/decode
//
// Windows (PowerShell, no netcat needed):
//
//   tools\decode\build.ps1
//   python tools\stream.py 192.168.4.1 1456 | tools\decode\decode.exe

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

#include "BoatModel.h"
#include "Items.h"
#include "LineDecoder.h"

static void report(const LineDecoder& decoder, const BoatModel& model, uint32_t now) {
  const LineDecoder::Stats& s = decoder.stats();
  printf("%-9s lines=%u samples=%u bad=%u ignored=%u ", protocolLabel(s.detected), s.lines, s.samples,
         s.badChecksum, s.ignored);
  for (size_t i = 0; i < kQuantityCount; i++) {
    Quantity q = static_cast<Quantity>(i);
    BoatModel::Reading r = model.get(q, now);
    if (!r.valid) continue;
    Item item = itemForQuantity(q);
    // Default units; bearings in the reference they were received in.
    DisplayUnits units;
    if (isTrueBearing(q)) units.bearing = BearingRef::True;
    DisplayValue d = itemDisplay(item, r.value, units);
    char text[12];
    formatItem(item, true, d.value, -1, text, sizeof(text), d.shortDistance);
    char side = itemSide(item, d.value);
    printf(" %s %s%s%c", quantityLabel(q), text[0] == ' ' ? text + strspn(text, " ") : text, d.unit,
           side ? side : ' ');
  }
  printf("\n");
}

int main() {
  BoatModel model;
  ModelConfig cfg;
  cfg.smoothingSec = 0;
  model.configure(cfg);
  LineDecoder decoder;

  auto start = std::chrono::steady_clock::now();
  std::string line;
  bool printed = false;
  uint32_t lastPrint = 0;
  while (std::getline(std::cin, line)) {
    uint32_t now = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    decoder.feed(line.c_str(), now, model);
    if (!printed || now - lastPrint >= 1000) {
      printed = true;
      lastPrint = now;
      report(decoder, model, now);
      fflush(stdout);
    }
  }
  report(decoder, model, lastPrint);
  for (size_t i = 0; i < model.sourceCount(); i++) {
    const SourceInfo& src = model.source(i);
    char label[16];
    formatSourceId(src.id, label, sizeof(label));
    printf("source %-8s %s via %-6s x%u\n", label, quantityLabel(src.q), src.via, src.count);
  }
  return 0;
}
