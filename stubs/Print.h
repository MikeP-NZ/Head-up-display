// Host stand-in for Arduino's Print: just enough for Adafruit_GFX text output.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

class String;

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t c) = 0;
  virtual size_t write(const uint8_t* buf, size_t n) {
    size_t written = 0;
    while (n--) written += write(*buf++);
    return written;
  }
  size_t print(const char* s) { return write(reinterpret_cast<const uint8_t*>(s), strlen(s)); }
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
  size_t print(const String& s);
};
