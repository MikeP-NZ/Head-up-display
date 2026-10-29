# Head-UP Marine Instrument Display

This project uses readily available RGB Matrix panels and ESP32 driver boards to provide a large daylight readable display of key navigation or performance data.
Racing yachts often have large displays mounted on the mast showing aparent wind angle and boat speed where everyone onboard can see them. This also helps the crew to keep their focus outside the cockpit, improving situational awareness and safety.

This project was completely coded by Claude. I just told it what I wanted to change and tried in vane to keep up.
I have tested the code on all 3 target boards and also with a few different LED Matrix panels.
I've added this paragraph so that you don't think I did it all myself.

The project receives performance and navigational data via Wifi from the ships instruments and displays it in large clear numerals. It can handle all the usual sailing performance numbers. Such as boat speed and heading, wind speed and angles, depth and navigation data.

It is designed to use one or more of the HUB75 LED matrix panels that are available.
These are usually 64×64 or 64×32 pixels and multiple panels can be chained off a single controller.
Obviously for a marine environment you'll want to use the "outdoor" panels or come up with some way to make them
waterproof.

You can display one value full-screen or 2 or 4 values at a time
Data is read over Wi-Fi from an NMEA 2000 gateway such as Yacht Devices YDWG-02,
YDNR-02, Hat Labs SH-wg, or anything else that serves NMEA 0183 or YD RAW on a
TCP/UDP port. Some chart plotters, such as a B&G Zeus and Vulcan have this feature built-in.
Everything is set up from a web portal. Just visit the page on your phone, ipad or a laptop. No app or cloud is involved.

Below are some preview images of a 64x64 panel and some 64x32 panel in portrait and landscape orientation. 32 pixels wide is about as small as you can go for usable data display.

![Panel screens](docs/panel/contact_sheet.png)

**Hardware.** One of these controller boards:

| Board | Build environment | Notes |
|---|---|---|
| Waveshare **ESP32-S3-RGB-Matrix** | `waveshare_s3_matrix` (default) | 32 MB octal flash, 16 MB PSRAM, one HUB75 output |
| Huidu **HD-WF4** (v7.x, ESP32-S3) | `huidu_hd_wf4` | 8 MB flash, no PSRAM, four HUB75 outputs in two pairs. See the [HD-WF4 flashing guide](docs/flashing-hd-wf4.md) |
| Adafruit **Matrix Portal S3** (5778) | `matrixportal_s3` | 8 MB flash, 2 MB PSRAM, one HUB75 socket. For 64×64 panels Check that the jumper for E is linked to pin 8 |

Plus:

- A HUB75 RGB LED panel: 64×64, 64×32, or chained panels (setup in the portal)
- A 5 V supply sized for the panel. A 64×64 panel at full white draws several amps, so USB power alone is only good for a dim bench test. A power bank or externally supplied 12v with a 12->5v buck converter are good choices.

Each board requires a different firmware image, because their flash layouts differ. So make sure you build the correct image for your board type.
Over-the-air updates will refuse an image built for a different board.

## How it works

```
NMEA 2000 bus ─▶ NMEA 2000 gateway ──Wi-Fi──▶ ESP32-S3 ──HUB75──▶ LED panel
 (instruments)   192.168.4.1:1456                 joins the gateway's
                 NMEA 0183 or RAW                 network as a client
```

The display can connect direclty to the gateway's own Wi-Fi network or, if
the boat has a router, both the gateway device and the display can connect to the router.
The display can understand both NMEA0183 or Yacht Devices YDRaw protocols. You can specify
which protocol to read or *Auto* accepts either.
The display will accept the following message types:

| Value | NMEA 0183 | YD RAW (NMEA 2000) |
|---|---|---|
| STW speed through water | `VHW`, `VBW` | PGN 128259 |
| SOG speed over ground | `RMC`, `VTG`, `VBW` | PGN 129026 (and 128259's ground field) |
| AWA / AWS apparent wind | `MWV` (R), `VWR` | PGN 130306 (apparent) |
| TWA / TWS true wind | `MWV` (T), `VWT` | PGN 130306 (true, boat or water referenced) |
| TWD true wind direction | `MWD` | PGN 130306 (true or magnetic, north referenced) |
| HDG heading | `HDG`, `HDM`, `HDT`, `VHW` | PGN 127250 |
| COG course over ground | `RMC`, `VTG` | PGN 129026 |
| DTW distance to waypoint | `RMB`, `BWC` | PGN 129284 (fast packet) |
| BTW bearing to waypoint | `RMB`, `BWC` | PGN 129284 (fast packet) |
| VMG to wind | `VPW` | — (no standard PGN) |
| VMC to waypoint | `RMB` (closing velocity) | PGN 129284 |
| DPT depth | `DPT` (with offset), else `DBT` | PGN 128267 (with offset) |
| MTW water temperature | `MTW` | PGN 130310, 130311, 130312, 130316 (sea temperature) |
| Clock (night schedule) | `RMC`, `ZDA` | PGN 126992 |

Bearings (HDG, COG, BTW, TWD) can be displayed as **magnetic** or **true**, as chosen on
the Data tab. A value sent in the other reference is converted with the
magnetic variation from `RMC`/`HDG` or PGN 127250/127258. If your system doesn't
send a variation, set a default variation for your area on the Data tab; without either, such a
value is not shown.

TWA, TWS, TWD and VMG can be calculated on the display if they aren't available from 
the instruments. On the Data tab, you can choose *Only use received values*,
*Calculate missing values* (the default) or *Only use calculated values*:

- **TWA and TWS** are calculated from the apparent wind (AWA, AWS) and the boat speed
  picked by the Data tab's *Speed source* (STW or SOG).
- **TWD** is the boat's heading plus TWA. *Heading for TWD* You can pick HDG(from compass)
 or COG(from GPS) to use for this calculation.
- **VMG** is the boat speed times cos(TWA).

TWD and VMG use the TWA that is shown, received or calculated. The Live tab
marks calculated values.

When multiple devices on the bus are sending the same data (eg. multiple GPS receivers) you can
choose which device's data to display, either by NMEA 2000 source address (RAW) or by talker ID (0183).

Until it's on the boat network, the display runs its own Wi-Fi network for
the setup portal at `http://192.168.71.1`. That network deliberately avoids
192.168.4.x, which the gateway uses. Once the display is on the boat network,
the portal is at `http://headup.local` or the display's IP address, and the
setup network turns off: running both on the one radio delays and loses the
boat network's traffic. It comes back if the boat network can't be joined for
20 seconds, or for 15 minutes when you hold the button for 3 seconds.

## Flashing

**Huidu HD-WF4:** follow [docs/flashing-hd-wf4.md](docs/flashing-hd-wf4.md). This board
flashes firmware through its USB-A socket, and the guide covers backing up Huidu's
firmware first.

**Waveshare ESP32-S3-RGB-Matrix** or **Adafruit Matrix Portal S3:**

1. Install [PlatformIO](https://platformio.org/install): the VS Code
   extension on any OS, or `brew install platformio` on macOS. On Windows,
   run the `pio` commands below from VS Code's PlatformIO terminal (the
   PlatformIO sidebar → *Miscellaneous* → *New Terminal*). A normal
   PowerShell window doesn't have `pio` on its PATH; there, use
   `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"` in place of `pio`.
2. Connect the board's USB-C port and run:

   ```bash
   pio run -e waveshare_s3_matrix -t upload   # Waveshare
   pio run -e matrixportal_s3 -t upload       # Matrix Portal S3
   ```

   The commands are the same on Windows. The board shows up as a COM port
   (*USB Serial Device* in Device Manager → *Ports (COM & LPT)*) without any
   driver. PlatformIO finds it by itself; add `--upload-port COM5` if more than
   one port is present.

   If the upload can't connect, hold **BOOT**, tap **RST** (**RESET** on the
   Matrix Portal), release **BOOT**, and try again. The Matrix Portal's
   CircuitPython and UF2 bootloader are overwritten; Adafruit's site has the
   factory reset image if you want them back.
3. Optional serial log:

   ```bash
   pio device monitor
   ```

The web portal is compiled into the firmware (`scripts/embed_web.py` gzips
`web/index.html` on every build), so there is no filesystem image to upload.
Later updates can be uploaded from the portal's **System** tab: pick
`.pio/build/<environment>/firmware.bin` for your board, or download a ready-built
`headup-<environment>-<version>.bin` from the
[Releases](https://github.com/MikeP-NZ/Head-up-display/releases) page (changes
are listed in [CHANGELOG.md](CHANGELOG.md)). After any update, a
portal left open in a browser reloads itself once the display is back, if the
update changed the portal page.

### Uploading over Wi-Fi from PlatformIO

Once a display is running this firmware, PlatformIO can update it over Wi-Fi
instead of USB. It sends the image to the portal, the same as the System tab,
and the display refuses images built for another board. Your computer must be
on the same network as the display (the boat network, or the display's setup
network at `192.168.71.1`).

- **Wi-Fi environments:** every board has a second environment ending in
  `_ota` (`waveshare_s3_matrix_ota`, `huidu_hd_wf4_ota`, `matrixportal_s3_ota`)
  that builds the same firmware and uploads it over Wi-Fi to `custom_ota_host`
  (default `headup.local`). In VS Code, switch between USB and Wi-Fi by picking
  the environment in the status bar or the PlatformIO sidebar, then **Upload**:

  ```bash
  pio run -e huidu_hd_wf4 -t upload       # USB
  pio run -e huidu_hd_wf4_ota -t upload   # Wi-Fi
  ```

  The two environments build separately, so the first build of each takes the
  full time.
- **One upload over Wi-Fi:** give the display's address as the upload port:

  ```bash
  pio run -e waveshare_s3_matrix -t upload --upload-port headup.local
  pio run -e huidu_hd_wf4 -t upload --upload-port 192.168.71.1
  ```

- **Always over Wi-Fi:** in `platformio.ini`, set `custom_upload = ota`, and set
  `custom_ota_host` to the display's address (default `headup.local`). Both
  can go under one board's `[env:...]` section to affect only that board.
- **VS Code button:** PlatformIO sidebar → your environment → *Custom* →
  **Upload over Wi-Fi** (or `pio run -t ota`). It always uploads over Wi-Fi to
  `custom_ota_host`, whatever `custom_upload` says.

The uploader is `scripts/ota_upload.py`; it also works on its own:
`python scripts/ota_upload.py headup.local .pio/build/<environment>/firmware.bin`.

**Windows with Norton (or other antivirus that scans HTTPS):** if ESP32 builds
stop with *Failed to install Python dependencies into penv*, the antivirus is
re-signing HTTPS and PlatformIO's package installer rejects its certificate.
Set the user environment variable `UV_SYSTEM_CERTS` to `1` and restart VS Code.

## First-time setup

1. Power up. The panel shows **SETUP** with the display's network name
   (`HeadUp-XXXX`), its password (`headupdisplay` by default) and `192.168.71.1`.
2. Join that network from your phone. The portal usually opens by itself; if
   not, browse to `http://192.168.71.1`.
3. **Network** tab: tap **Scan for networks** and pick the gateway. Yacht Devices
   networks are tagged **YD**. For an open network, tick *Open network*;
   otherwise enter its password. Press **Save & connect**.
4. Check the **Live** tab. The setup checklist turns green step by step, and
   the simulated panel shows the speed (or whatever the Layout tab selects):

   1. Join the boat Wi-Fi
   2. Reach the gateway
   3. Receive NMEA data
   4. Find instrument data

The setup network stays on while your phone is connected to it. Once the
phone leaves, it turns off, and the portal is at `http://headup.local` on the
boat network.

With a Yacht Devices gateway on factory settings, nothing else needs changing.
The display defaults to TCP, port 1456, *Automatic* gateway address, and *Auto* protocol,
which matches Server #1 out of the box.

### Portal on your phone's home screen

Add the portal to your phone or tablet's home screen to open it with one tap.
Open it from the address you normally use, such as `http://headup.local`, then:

- **iPhone / iPad (Safari):** Share → **Add to Home Screen**. It opens full
  screen, like an app.
- **Android (Chrome):** ⋮ → **Add to Home screen**. It opens in a browser tab.

The icon only works while the phone is connected to the same wifi network as the display. One
added from the setup network (`192.168.71.1`) needs that network.

### Gateway settings worth knowing

Configure these in the Yacht Devices gateway's own web page, `http://192.168.4.1` (login
`admin` / `admin` unless changed), under **NMEA Server**:

- **TCP allows 3 clients per server port.** If chart-plotter apps already use
  Server #1, give the display its own server, or switch to **UDP**, which has
  no client limit. Then set the same transport and port on the display's
  **Source** tab.
- **NMEA 0183** needs the transmit filter to let through the sentences for the
  values you show (see the table above).
- **RAW** sends NMEA 2000 frames unfiltered, which makes it the most reliable
  choice when several devices are on the bus.
- **Client mode:** if the gateway is joined to a boat router instead of running
  its own access point, join the display to the router and enter the
  gateway's IP on the **Source** tab. **Find gateway** scans the network for
  open TCP data ports: the configured one, 1456 (Yacht Devices), 2223 (SH-wg
  YDWG RAW) and 10110 (NMEA 0183, e.g. chart plotters).

The **Source** tab shows the raw lines arriving from the gateway, with lines
that carry values highlighted. This is the quickest way to see what the gateway actually
sends.

### Other data sources

Anything that serves NMEA 0183 or YD RAW on a TCP or UDP port should work. Set
its address, transport and port on the **Source** tab.

- **[Hat Labs SH-wg](https://shop.hatlabs.fi/products/sh-wg)**: an NMEA 2000
  Wi-Fi gateway that can send YDWG RAW (the Yacht Devices RAW format) or
  NMEA 0183 over TCP or UDP, from its own access point or on the boat network.
  With YDWG RAW the display reads it the same way as a Yacht Devices gateway.
  Its default ports ([firmware config](https://github.com/hatlabs/SH-wg-firmware/blob/main/src/config.h)):

  | SH-wg output | TCP | UDP |
  |---|---|---|
  | YDWG RAW | 2223 | 2002 |
  | NMEA 0183 | 2222 | 2000 |

- **B&G chart plotters** can share NMEA 0183 over the network: turn on NMEA
  0183 output over TCP in the plotter's settings. It is served on port
  **10110**. Join the display to the same network, then enter the plotter's IP
  address and port 10110 (TCP, *NMEA 0183* or *Auto*) on the **Source** tab.

## Portal tabs

| Tab | What it does |
|---|---|
| Live | Current speed, every instrument value, live LED preview, setup checklist, every data source seen on the bus |
| Network | Boat network (with scan); setup network name, password, hostname |
| Source | Demo mode; TCP/UDP, gateway address and port, NMEA 0183 / RAW / Auto, gateway discovery, connection stats, live data view |
| Data | STW vs SOG for *Boat speed* (with fallback), received or calculated true wind and VMG, heading for TWD, default magnetic variation, speed units (kn, km/h, mph, m/s), depth units (m, ft), temperature units (°C, °F), distance units (NM, km, mi) with optional m/yd/ft for distances under 1000 of those, magnetic or true bearings, decimals, smoothing, stale timeout, per-device source selection |
| Layout | Layout (1, 2 or 4 sections) and the value in each, brightness, colours, rotation, which labels to show, night mode (manual or scheduled), test pattern |
| Hardware | HUB75 driver chip (including SM16380 S-PWM panels), colour order, clock speed and phase, panel size and chaining; GPIO pins (Waveshare, Matrix Portal) or output connector (HD-WF4). Needs a reboot |
| System | Firmware info, OTA update, settings export and import, reboot, factory reset |

## The panel

**Layout** (Layout tab): 1, 2 or 4 sections, each showing one of *Boat
speed* (STW or SOG per the Data tab), STW, SOG, AWA, AWS, TWA, TWS, TWD, HDG,
DTW, BTW, COG, VMG, VMC, DPT or MTW. The split follows the panel's shape after rotation:

| Panel (after rotation) | 2 sections | 4 sections |
|---|---|---|
| 64×32 landscape | side by side, 32×32 | 2×2 grid of 32×16 |
| 32×64 portrait (rotation 90°/270°) | stacked, 32×32 | 4 rows of 32×16 |
| 64×64 | stacked, 64×32 | 2×2 grid of 32×32 |
| 128×32 (two chained) | side by side, 64×32 | 4 columns of 32×32 |

Each section has a label top left (`STW`, `AWA`, …), the unit top right,
and the value below. Labels and units use a 5×7 font where the section has
room for it without shrinking the digits much (for example 32×32 sections),
and a tiny 3×5 font otherwise. All sections on screen use the same sizes.
Wind angles show 0–180° with arrows **<<**/**>>** for port or starboard, the digits
tinted red for port and green for starboard (single colour in night mode).
Headings and TWD show three digits (`005`). Sections without data show
dashes. While Wi-Fi, the gateway link or the data stream is down, the panel
switches to the full-screen layout with a scrolling problem description.

**Full-screen layout** (1 section):

- **Big digits**: the value. The digit slots stay fixed, so 9.9 → 10.0 doesn't
  change the digit size.
- **Top left**: the label (`STW`/`SOG` for *Boat speed*, whichever is shown).
- **Top right**: the unit, or for wind angles the side: `<<<` port (red),
  `>>>` starboard (green). On a 32 px wide portrait panel the side shortens to
  `<<`/`>>`, and other units, and if need be the label, use the small font.
- **Bottom line**: only while there is a problem, a scrolling description
  (`NO WIFI …`, `NO LINK …`, `NO DATA`, `NO SPEED DATA`, `NO AWA DATA` …).
  Otherwise the digits use that row too.

On short panels (64×32), the labels move to a narrow column on the right so the
digits can use the full height. The column uses the 5×7 font when the panel
is wide enough (e.g. two chained panels), and the 3×5 font otherwise. The
connection info screen likewise uses the 5×7 font when its lines fit across the
panel.

## Button

The **BOOT** button on the Waveshare board, **S1** on the HD-WF4, or **UP** on the Matrix Portal S3:

| Press | Action |
|---|---|
| Short (< 1 s) | Toggle night mode (dim, single colour; red by default) |
| Hold 3 s, release | Show connection info on the panel for 30 s, and turn on the setup network for 15 min |
| Hold 10 s | Factory reset |

## Demo mode

To show the display without instruments or a gateway, tick **Show simulated
data** on the portal's **Source** tab. The display then sails a simulated boat
beating to windward. It tacks every 3 minutes, swinging about 45° either side
of the wind so the apparent wind changes sides, and loses speed in each tack.
Every value on the Layout tab is simulated, so any layout can be shown.

While demo mode is on:

- the panel flashes **SIMULATED DATA** over the values for 1.5 s in every 5 s;
- the portal shows a banner with a **Turn off** button;
- the gateway connection is paused, and no boat Wi-Fi is needed;
- the clock is not set from the simulated data.

The setting survives a reboot, so the display stays in demo mode until it is
turned off.

## Troubleshooting

| Symptom | Try |
|---|---|
| Panel dark or garbled | **Hardware** tab: driver *FM6126A*, then *Generic*; lower the clock speed; check the panel's 5 V supply |
| Panel with SM16380 chips (often with a RUC7258D row driver) | **Hardware** → Driver chip → *SM16380*. Count the driver chips: 16 per colour on a 64×64 panel means 1/16 scan (Scan rate → *1/16 scan (4 rows per address)*); 8 per colour means 1/32 (*2 rows per address, standard*). Leave *Clock data on the rising edge* unticked (ticked gives vertical ghosting and white blocks of columns). Known good on a 64×64 P2.5 SM16380SF/RUC7258 panel: 1/16 scan, *Captured settings*, 16 bit, 20 MHz. If the picture is wrong, try the other block order, the other register set, slower clock speeds, then *14 bit* grey levels. SM16380SH chips use a different protocol and are not supported |
| Rows doubled, interleaved or in blocks | The panel lights more than two rows per address. Its scan rate is usually printed on the back, e.g. *P4-80x40-10S* = 1/10. Set the panel's width and height first, then **Hardware** → Scan rate → the matching 1/N. If the picture is still in the wrong rows, try *other block order*, then *Column blocks* 16 or 8. Columns shifted or missing on a panel whose width isn't a multiple of 16 (e.g. 104 px)? Some driver chip outputs have no LED: run the test pattern, read the column code (from 24 s) and enter the numbers that never appear under *Unused chip outputs*. The P3.076 104×52 1/13 scan panel (ICND2037 + ICN2012) needs `15,31,47,48,63,64,80,96` |
| Colours wrong in the test pattern | **Hardware** → Colour order |
| Upside down / sideways | **Layout** → Rotation |
| "network not found" | Is the gateway in access point mode and in range? Its SSID is case-sensitive |
| Reaches Wi-Fi, "no answer from 192.168.4.1:1456" | The server port is off, or 3 TCP clients are already connected. Use another server or UDP |
| Data flows but "NO SPEED DATA" | Check the 0183 transmit filters, or switch to RAW; make sure the Data tab's source mode allows the speed that exists (STW vs SOG) |
| A section shows dashes | The bus doesn't carry that value: the **Live** tab lists what arrives. Check the gateway's 0183 transmit filters, or use RAW. A bearing sent only in the other reference (true vs magnetic) needs a magnetic variation (`RMC`, `HDG`, PGN 127258) or a default one on the Data tab. Missing TWA, TWS, TWD or VMG can be calculated (Data tab) |
| VMG stays empty with RAW | NMEA 2000 has no standard VMG message. Let the display calculate it (Data → *Calculate missing values*), or use NMEA 0183 with `VPW` from your instruments |
| Phone keeps dropping the setup network | While the display can't find the boat Wi-Fi it scans for it, which moves its own network between channels. Set up the boat Wi-Fi first |

## Development

macOS / Linux:

```bash
pio run -e waveshare_s3_matrix -e huidu_hd_wf4 -e matrixportal_s3   # build all boards
pio test -e native                       # parser / model / item unit tests on the host
tools/preview/build.sh                   # host build of the panel renderer
python3 tools/preview/render_png.py      # regenerate docs/panel/*.png
python3 tools/mock_device.py --fresh     # portal against a fake device: http://127.0.0.1:8765
python3 tools/mock_device.py --board wf4 # ... pretending to be an HD-WF4
python3 tools/mock_device.py --board matrixportal # ... or a Matrix Portal S3
python3 tools/yd_simulator.py            # fake YD gateway on TCP 1456 (NMEA 0183): the demo mode boat
python3 tools/yd_simulator.py --protocol raw --transport udp --port 1457
python3 tools/make_icons.py              # redraw the portal's home-screen icons in web/
```

Windows (PowerShell, in the VS Code PlatformIO terminal so `pio` is on the PATH):

```powershell
pio run -e waveshare_s3_matrix -e huidu_hd_wf4 -e matrixportal_s3   # build all boards
pio test -e native                       # unit tests; needs a MinGW-w64 gcc on PATH
tools\preview\build.ps1                  # host build of the panel renderer
python tools\preview\render_png.py       # regenerate docs\panel\*.png (pip install pillow)
python tools\mock_device.py --fresh      # portal against a fake device: http://127.0.0.1:8765
python tools\mock_device.py --board wf4  # ... pretending to be an HD-WF4
python tools\yd_simulator.py             # fake YD gateway on TCP 1456 (NMEA 0183): the demo mode boat
python tools\yd_simulator.py --protocol raw --transport udp --port 1457
python tools\make_icons.py               # redraw the portal's home-screen icons in web\
```

Windows notes:

- **C++ compiler.** `pio test -e native` and the `.ps1` build scripts need a
  MinGW-w64 `g++` on the PATH. Install it with [MSYS2](https://www.msys2.org/)
  (`pacman -S mingw-w64-ucrt-x86_64-gcc`, then add `C:\msys64\ucrt64\bin` to
  the PATH), [WinLibs](https://winlibs.com/) or TDM-GCC. Visual Studio's
  compiler isn't supported by these scripts.
- **Scripts blocked.** If PowerShell refuses to run a `.ps1`, run it as
  `powershell -ExecutionPolicy Bypass -File tools\preview\build.ps1`, or allow
  local scripts once with `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.
- **Python.** Use `python` or `py`; `python3` may open the Microsoft Store
  instead. Git Bash can also run the `.sh` scripts as they are.
- **Firewall.** The first time `yd_simulator.py` listens, allow Python through
  Windows Defender Firewall on *private* networks, or the display can't
  connect. Mark the boat or bench Wi-Fi as a *private* network.

To bench-test the real display without the boat, use demo mode, or run
`yd_simulator.py` on a laptop to test the gateway connection as well. The
simulator sails the same boat as demo mode, and adds the time and position.
Join the display and the laptop to the same network, then enter the
laptop's IP and port on the display's **Source** tab. Find the laptop's IP with
`ipconfig getifaddr en0` (macOS), `hostname -I` (Linux) or `ipconfig`
(Windows, *IPv4 Address* under the Wi-Fi adapter).

`tools/decode/decode.cpp` runs the firmware's decoder against any stream, for
example `nc 192.168.4.1 1456 | tools/decode/decode`. Build it with the command
at the top of that file. Windows has no `nc`; use the included Python
equivalent instead:

```powershell
tools\decode\build.ps1
python tools\stream.py 192.168.4.1 1456 | tools\decode\decode.exe
python tools\stream.py --udp 1457 | tools\decode\decode.exe   # UDP server
```

### Releases

GitHub Actions ([.github/workflows/build.yml](.github/workflows/build.yml))
builds every board and runs the unit tests on each push and pull request. To
publish a release:

1. Bump `FW_VERSION` in `platformio.ini`, following [semantic versioning](https://semver.org/).
2. Add a section for the version to [CHANGELOG.md](CHANGELOG.md) and commit.
3. Tag the commit and push the tag:

   ```bash
   git tag -a v1.2.0 -m "v1.2.0"
   git push origin v1.2.0
   ```

The workflow checks the tag matches `FW_VERSION`, then creates the GitHub
Release with the changelog section as its notes and each board's
`firmware.bin` attached.

### Layout

```
lib/BoatData/     portable NMEA 0183 + YD RAW parsers, the boat model, the display items and the demo boat (unit-tested)
lib/Sm16380/      clock-by-clock waveform for SM16380 S-PWM panels (unit-tested)
src/
  Board.*         per-board pins, defaults, button/LED, HD-WF4 output ports
  main.cpp        boot, render loop, night schedule, button
  BoatWifi.*      station link to the gateway + setup access point
  DataLink.*      TCP/UDP reader task, live data view, gateway discovery
  Portal.*        HTTP API + captive portal
  Settings.*      settings model, JSON validation, NVS storage
  Renderer.*      all panel screens (hardware-free; also builds on the host)
  Display.*       HUB75 driver glue
  Sm16380Panel.*  SM16380 output: replays the waveform through the S3's LCD peripheral
web/              the portal (index.html) and its home-screen manifest and icons, embedded at build time
tools/            simulator, mock device, preview renderer, decoder CLI, stream.py (nc for Windows), icon generator
```

HUB75 pin map for the Waveshare board, taken from Waveshare's examples:
R1 4, G1 5, B1 6, R2 7, G2 15, B2 16, A 18, B 8, C 3, D 42, E 9, LAT 40, OE 2, CLK 41.
Matrix Portal S3, from Adafruit's Protomatter examples:
R1 42, G1 41, B1 40, R2 38, G2 39, B2 37, A 45, B 36, C 48, D 35, E 21, LAT 47, OE 14, CLK 2.
The HD-WF4 map is in its [flashing guide](docs/flashing-hd-wf4.md#how-the-four-outputs-work).

## License

MIT, see [LICENSE](LICENSE). The firmware is built with third-party libraries
under MIT, BSD, Apache-2.0 and LGPL-2.1 licenses; their notices are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Include that file when you
share a `firmware.bin`.
