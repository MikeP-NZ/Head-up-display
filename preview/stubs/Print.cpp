#include "Print.h"

#include "Arduino.h"

size_t Print::print(const String& s) { return print(s.c_str()); }
