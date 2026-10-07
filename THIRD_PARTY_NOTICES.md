# Third-party notices

Head-Up Display's own source code is licensed under the MIT License (see
[LICENSE](LICENSE)). No third-party source code is copied into this
repository. The libraries below are downloaded by PlatformIO at build time and
compiled into the firmware (`firmware.bin`). Their notices are reproduced here
because their licenses require it when the firmware is distributed in binary
form.

| Component | Version | License | Used for |
|---|---|---|---|
| [ESP32-HUB75-MatrixPanel-DMA](https://github.com/mrcodetastic/ESP32-HUB75-MatrixPanel-DMA) | 3.0.15 | MIT | HUB75 panel driver |
| [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) | 1.12.6 | BSD 2-clause | Drawing and text |
| TomThumb font (part of Adafruit GFX) | — | BSD 3-clause | Small labels on the panel |
| [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) | 1.17.4 | MIT | Dependency of Adafruit GFX |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | 7.4.3 | MIT | Settings and web API |
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) | 3.3.7 | LGPL-2.1-or-later | Wi-Fi, web server, OTA update, storage |
| [ESP-IDF](https://github.com/espressif/esp-idf) and its components (FreeRTOS, lwIP, mbedTLS, ...) | as bundled with the Arduino core | Apache-2.0, with MIT/BSD components | System software |

The host unit tests also use [Unity](https://github.com/ThrowTheSwitch/Unity)
(MIT), which is not part of the firmware.

## Arduino core for ESP32 (LGPL-2.1)

The firmware statically links the Arduino core for ESP32, which is licensed
under the GNU Lesser General Public License, version 2.1 or later
(<https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html>). Its source code is
available at <https://github.com/espressif/arduino-esp32>.

The complete source code of this firmware, and the build configuration
(`platformio.ini`) that pins the exact core version, are in this repository. To
build the firmware against a modified copy of the core, point PlatformIO's
`platform_packages` setting at your copy and rebuild as described in the
[README](README.md#development).

## ESP-IDF (Apache-2.0)

Copyright Espressif Systems (Shanghai) Co., Ltd. and contributors. Licensed
under the Apache License, Version 2.0
(<https://www.apache.org/licenses/LICENSE-2.0>). ESP-IDF bundles third-party
components under their own licenses (including FreeRTOS, MIT; lwIP, BSD
3-clause; mbedTLS, Apache-2.0); Espressif lists them with their copyright
notices at
<https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/COPYRIGHT.html>.

## ESP32-HUB75-MatrixPanel-DMA (MIT)

```
MIT License

Copyright (c) 2018-2032 Faptastic

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Adafruit GFX Library (BSD 2-clause)

```
Software License Agreement (BSD License)

Copyright (c) 2012 Adafruit Industries.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

## TomThumb font (BSD 3-clause)

From `Fonts/TomThumb.h` in Adafruit GFX. Modifications for readability by
Robey Pointer; conversion to Adafruit GFX format by William Skellenger;
bitmap compression by Andreas Merkle.

```
Copyright 1999 Brian J. Swetland
Copyright 1999 Vassilii Khachaturov
Portions (of vt100.c/vt100.h) copyright Dan Marks

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions, and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions, and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the authors may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Adafruit BusIO (MIT)

```
The MIT License (MIT)

Copyright (c) 2017 Adafruit Industries

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## ArduinoJson (MIT)

```
The MIT License (MIT)

Copyright © 2014-2026, Benoit BLANCHON

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the “Software”), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Hardware information

The Huidu HD-WF4 pin map and output gating come from community reverse
engineering, credited in [docs/flashing-hd-wf4.md](docs/flashing-hd-wf4.md#sources).
