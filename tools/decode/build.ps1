# Windows build of tools/decode/decode.exe (see the top of decode.cpp).
# Needs a MinGW-w64 g++ (MSYS2, TDM-GCC or WinLibs) on PATH.
#
#   powershell -ExecutionPolicy Bypass -File tools\decode\build.ps1
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..\..')

$cxx = @('c++', 'g++', 'clang++') | Where-Object { Get-Command $_ -ErrorAction SilentlyContinue } | Select-Object -First 1
if (-not $cxx) {
  Write-Error 'No C++ compiler found. Install MinGW-w64 (e.g. MSYS2: pacman -S mingw-w64-ucrt-x86_64-gcc) and add its bin folder to PATH.'
}

$lib = Get-ChildItem lib/BoatData/src/*.cpp | ForEach-Object { $_.FullName }
& $cxx -std=c++17 -O2 -Ilib/BoatData/src tools/decode/decode.cpp @lib -o tools/decode/decode.exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host 'built tools/decode/decode.exe'
