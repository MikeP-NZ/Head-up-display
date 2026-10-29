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

## [1.2.0] - 2026-10-29

### Added

- Outdoor panels with odd sizes and scan rates, such as P4 80×40 at 1/10
  scan and P3.076 104×52 at 1/13. The Hardware tab's scan choices now follow
  the panel height (2, 4 or 8 rows per address). New settings cover panels
  whose chain zig-zags between row blocks (**Column blocks**), and panels
  whose driver chips have outputs with no LED (**Unused chip outputs**). The
  P3.076 104×52 panel (ICND2037 + ICN2012) needs
  `15,31,47,48,63,64,80,96`.
- The test pattern adds a scan test and a column code, for working out how a
  panel is wired from a photo. It now runs for 60 s.
- System tab: IP and MAC addresses, why the display last restarted, a Wi-Fi
  history with the reason and signal strength for each drop, and readings
  for the portal's link to the display.

### Changed

- The setup network now only runs while the display isn't on the boat
  network, like most devices: before the boat network is set up, when it
  can't be joined for 20 s, or for 15 minutes after holding the button for
  3 s. It stays on while a phone is connected to it. The option to keep it
  on always is gone, because running both networks on one radio delayed and
  lost the boat network's traffic. Once the display is on the boat network,
  open the portal at `http://headup.local` or the display's IP address.

### Fixed

- The portal often showed *Lost contact with the display*, especially from a
  phone. The display now answers reliably, and one late reply no longer
  shows the message.
- The YD simulator (`tools/yd_simulator.py`) refused connections after a
  while, because closed clients kept its 3 slots.

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

[1.2.0]: https://github.com/MikeP-NZ/Head-up-display/releases/tag/v1.2.0
[1.1.0]: https://github.com/MikeP-NZ/Head-up-display/releases/tag/v1.1.0
