# Windows version of build.sh: builds tools/preview/preview.exe (host binary)
# from the firmware's Renderer.
# Needs the Adafruit GFX sources, which `pio run` downloads into .pio/libdeps,
# and a MinGW-w64 g++ (MSYS2, TDM-GCC or WinLibs) on PATH.
#
#   powershell -ExecutionPolicy Bypass -File tools\preview\build.ps1
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..\..')

$gfx = '.pio\libdeps\waveshare_s3_matrix\Adafruit GFX Library'
if (-not (Test-Path "$gfx\Adafruit_GFX.cpp")) {
  Write-Error "Adafruit GFX not found - run 'pio run' once first."
}

$cxx = @('c++', 'g++', 'clang++') | Where-Object { Get-Command $_ -ErrorAction SilentlyContinue } | Select-Object -First 1
if (-not $cxx) {
  Write-Error 'No C++ compiler found. Install MinGW-w64 (e.g. MSYS2: pacman -S mingw-w64-ucrt-x86_64-gcc) and add its bin folder to PATH.'
}

& $cxx -std=c++17 -O2 -DARDUINO=100 `
  -Itools/preview/stubs "-I$gfx" -Isrc -Ilib/BoatData/src `
  tools/preview/preview.cpp tools/preview/stubs/Print.cpp `
  src/Renderer.cpp lib/BoatData/src/BoatModel.cpp lib/BoatData/src/Items.cpp lib/BoatData/src/Quantity.cpp "$gfx\Adafruit_GFX.cpp" `
  -o tools/preview/preview.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host 'built tools/preview/preview.exe'
