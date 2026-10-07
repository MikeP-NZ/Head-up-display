// Host stand-in for the parts of Arduino.h that Renderer and Adafruit GFX use.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Print.h"

#ifndef PROGMEM
#define PROGMEM
#endif

using std::max;
using std::min;

class __FlashStringHelper;
inline float radians(float deg) { return deg * 3.14159265f / 180.0f; }

class String {
 public:
  String() = default;
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  explicit String(int v) : s_(std::to_string(v)) {}
  size_t length() const { return s_.size(); }
  const char* c_str() const { return s_.c_str(); }
  String operator+(const String& o) const { return String(s_ + o.s_); }
  String operator+(const char* o) const { return String(s_ + o); }
  String& operator+=(const String& o) {
    s_ += o.s_;
    return *this;
  }
  bool operator==(const String& o) const { return s_ == o.s_; }

 private:
  std::string s_;
};

inline String operator+(const char* a, const String& b) { return String(a) + b; }
