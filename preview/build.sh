#!/bin/sh
# Builds tools/preview/preview (host binary) from the firmware's Renderer.
# Needs the Adafruit GFX sources, which `pio run` downloads into .pio/libdeps.
set -e
cd "$(dirname "$0")/../.."
GFX=".pio/libdeps/waveshare_s3_matrix/Adafruit GFX Library"
if [ ! -f "$GFX/Adafruit_GFX.cpp" ]; then
  echo "Adafruit GFX not found - run 'pio run' once first." >&2
  exit 1
fi
c++ -std=c++17 -O2 -DARDUINO=100 \
  -Itools/preview/stubs -I"$GFX" -Isrc -Ilib/BoatData/src \
  tools/preview/preview.cpp tools/preview/stubs/Print.cpp \
  src/Renderer.cpp lib/BoatData/src/BoatModel.cpp lib/BoatData/src/Items.cpp lib/BoatData/src/Quantity.cpp "$GFX/Adafruit_GFX.cpp" \
  -o tools/preview/preview.exe
echo "built tools/preview/preview"
