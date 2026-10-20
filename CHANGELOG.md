# Changelog

Versions follow [semantic versioning](https://semver.org/): the major number
goes up when an update breaks something (saved settings that don't carry
over, a wiring change), the minor number for new features, and the patch
number for fixes only. The version is `FW_VERSION` in `platformio.ini`; the
display shows it on its splash screen and in the portal.

Each release on GitHub has a `firmware.bin` for every board. Upload the one
for your board from the portal's **System** tab; the display refuses images
built for another board. A board's first install still goes over USB, see
[Flashing](README.md#flashing).

## [1.1.0] - 2026-10-20

First public release.

- Boards: Waveshare ESP32-S3-RGB-Matrix, Huidu HD-WF4 and Adafruit Matrix
  Portal S3, on HUB75 panels including SM16380 S-PWM panels.
- Reads NMEA 0183 or YD RAW over TCP/UDP from Yacht Devices gateways, the
  Hat Labs SH-wg and B&G chart plotters, with **Find gateway** on the Source
  tab.
- STW, SOG, apparent and true wind, TWD, heading, COG, DTW, BTW, VMG, VMC,
  depth and water temperature, with true/magnetic bearings and a choice of
  units. True wind and VMG are calculated when the instruments don't send
  them.
- Web portal for setup, which can be added to a phone's home screen and
  reloads itself after a firmware update.
- Firmware updates over Wi-Fi from the portal or from PlatformIO.
- Demo mode with a simulated sailing boat.

[1.1.0]: https://github.com/MikeP-NZ/Head-up-display/releases/tag/v1.1.0
