# Flashing the Huidu HD-WF4

This guide covers **HD-WF4 v7.x boards, which use an ESP32-S3**. Check the chip
next to the antenna before you start: it should read `ESP32-S3`. Older WF4
revisions (spec v6) use a different processor and cannot run this firmware.

The pin map and flashing details come from community reverse engineering of the
board (see [Sources](#sources)). They have been verified on hardware by this
project, but still do the backup step: it is your way back to the stock firmware.

## Why not a USB drive?

Huidu's own firmware updates programs (and itself) from a USB stick, but it only
accepts Huidu's packages made with their HD2020 software. Custom firmware goes
in through the **same USB-A socket**, with a computer on the other end instead
of a stick. That socket is wired straight to the ESP32-S3's built-in USB.

## What you need

- **5 V power** to the board's screw terminal (your panel supply is fine).
- **A cable from your computer to the board's USB-A socket.** Either:
  - a **USB-C to USB-A cable** (a phone-charger cable): C end in the computer, A end in the board. This is reported to work on the closely related HD-WF2; or
  - a **USB-A to USB-A cable** (through a USB-C-to-A adapter or hub if your computer only has USB-C), if the computer doesn't see the board with the first option. An A-to-A cable connects your computer's 5 V to the board's 5 V, so plug it in through a powered hub rather than straight into the laptop.
- **esptool**. It is already installed with PlatformIO; otherwise run `brew install esptool` (macOS) or `python -m pip install esptool` (Windows, Linux).
- **Windows:** no driver is needed on Windows 10 or 11. Run the `pio` commands from VS Code's PlatformIO terminal (see the [README](../README.md#flashing)), and use `python -m esptool` wherever this guide says `esptool.py`.
- A **jumper wire or tweezers**, in case the board needs forcing into download mode.

## Board features you will use

| Part | What it is |
|---|---|
| **S1** button (left edge) | GPIO17: night mode, info screen and factory reset (the same functions as BOOT on the Waveshare board) |
| **P2** pads (next to the USB socket) | GND and GPIO0: bridge them to force download mode |
| **S4** pads (lower left) | GPIO46, the second boot strapping pin: bridge it along with P2 if P2 alone doesn't work |
| **RUN** LED | GPIO40: slow blink while showing a speed, fast blink while not |
| **75EX1–75EX4** | HUB75 outputs X1–X4 |

## 1. Put the board into download mode

1. Unplug the LED panels. This is optional, but makes it easier to tell what's going on.
2. Connect the USB cable between the board and the computer.
3. Enter download mode in one of these ways:
   - **A (try first):** hold **S1** while you switch on the 5 V supply, then release it.(didn't work for me)
   - **B (always works):** bridge the two **P2** pads with a jumper wire or tweezers, and also bridge **S4**. Switch on the power while they are bridged, then remove the bridge.
4. Check that the board shows up:

I soldered a 2-pin header into both P2 and S4 and had to fit a jumper to both when I wanted to re-flash the firmware.  That
provided the motivation to add upload over wifi to this project.

   ```bash
   ls /dev/cu.usbmodem*      # macOS
   ls /dev/ttyACM*           # Linux
   ```

   ```powershell
   # Windows: lists COM ports; the board is the one that appears when you plug it in
   Get-CimInstance Win32_PnPEntity | Where-Object Name -match '\(COM\d+\)' | Select-Object Name, DeviceID
   ```

   It enumerates as Espressif's *USB JTAG/serial debug unit* (USB ID `303A:1001`;
   on Windows a *USB Serial Device (COMn)* whose DeviceID contains `VID_303A&PID_1001`).
   Use that port in place of `/dev/cu.usbmodemXXXX` below: `/dev/ttyACM0` on
   Linux, `COM5` (or whichever number it got) on Windows.

   On Linux, if you get *permission denied*, add yourself to the serial group
   (`sudo usermod -aG dialout $USER`, or `uucp` on Arch) and log in again.

## 2. Back up Huidu's firmware

This reads the whole 8 MB flash. It takes a few minutes. Keep the file somewhere safe.

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX read_flash 0 0x800000 hd-wf4-stock.bin
```

```powershell
# Windows
python -m esptool --chip esp32s3 --port COM5 read_flash 0 0x800000 hd-wf4-stock.bin
```

## 3. Erase and flash

From the project folder:

```bash
pio run -e huidu_hd_wf4 -t erase --upload-port /dev/cu.usbmodemXXXX
```

```bash
pio run -e huidu_hd_wf4 -t upload --upload-port /dev/cu.usbmodemXXXX
```

On Windows, use the COM port: `--upload-port COM5`.

The `huidu_hd_wf4` environment sets the board's flash mode. The cFeon flash on
the WF4 needs DIO; QIO, the usual ESP32-S3 default, is unreliable on it. **Don't
flash the Waveshare image (`waveshare_s3_matrix`) to this board**: it expects
32 MB octal flash and won't boot.

## 4. First start

1. Switch the power off and on again. If you bridged P2/S4, make sure the bridge is removed.
2. Connect the panel to **X1 (75EX1)**, the default output.
3. The panel shows **SETUP**. From here the steps are the same as in the main
   [README](../README.md#first-time-setup): join `HeadUp-XXXX`, open
   `http://192.168.71.1`, and pick the Yacht Devices network.
4. On the **Hardware** tab, choose the connector you're using. For a two-sided
   display, pick **X1 + X3** (or **X2 + X4**) to show the same image on two
   panels. Most panels sold for Huidu cards use a generic shift-register
   driver, which is the default here. If yours stays dark, try FM6126A.

## Updating later

- **Over Wi-Fi:** in the portal, go to System → Firmware update and upload
  `.pio/build/huidu_hd_wf4/firmware.bin`. Images built for another board are
  refused, so you can't brick it by picking the wrong file. From PlatformIO:
  `pio run -e huidu_hd_wf4 -t upload --upload-port headup.local` (see
  [Uploading over Wi-Fi](../README.md#uploading-over-wi-fi-from-platformio)).
  The same check applies.
- **Over USB:** `pio run -e huidu_hd_wf4 -t upload`. No buttons or bridges are
  needed, because this firmware leaves the USB serial port on. If it ever
  can't connect (for example a crash loop), use download mode method B.

## Going back to Huidu's firmware

Put the board in download mode, then:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodemXXXX write_flash 0 hd-wf4-stock.bin
```

```powershell
# Windows
python -m esptool --chip esp32s3 --port COM5 write_flash 0 hd-wf4-stock.bin
```

## How the four outputs work

The WF4 has one set of HUB75 signals split over two connector pairs:

| | X1 / X3 | X2 / X4 |
|---|---|---|
| R1 G1 B1 | 2, 6, 10 | 4, 8, 12 |
| R2 G2 B2 | 3, 7, 11 | 5, 9, 13 |
| A B C D E | 39, 38, 37, 36, 21 | same |
| LAT OE CLK | 33, 35, 34 | same |

GPIO45 passes CLK/LAT through to X1 and X2, and GPIO14 to X3 and X4. The
firmware opens only the gate(s) for the connector you choose, and holds the
other pair's colour lines low.

As a result, one connector pair carries one image. You can mirror it across
both connectors of a pair, or chain panels from one connector. Driving
different images on X1 and X2 at the same time isn't possible.

The board has **no PSRAM**. 64×64 is comfortable, and two chained 64×64 panels
should fit. Very long chains may run out of memory.

## Troubleshooting

| Problem | Try |
|---|---|
| No `/dev/cu.usbmodem*` port (no new COM port on Windows) | Make sure the 5 V terminal is powered; try the other cable option or a hub; use method B |
| Windows: `could not open port 'COM5'` / access denied | Another program has the port open: close serial monitors (PlatformIO's included), Arduino IDE, Cura and the like |
| `Failed to connect to ESP32-S3` | Download mode wasn't entered: bridge P2 and S4 while powering on |
| Flashed, but nothing at all happens | You probably flashed the Waveshare image. Re-flash with `-e huidu_hd_wf4` |
| Panel dark | Hardware tab: check the connector matches the ribbon cable; try driver FM6126A; check panel power |
| 64×64 panel shows only the top half | The panel's E line isn't connected; all four WF4 outputs carry E on GPIO21 |

## Sources

- [andywwright/hd-wf4](https://github.com/andywwright/hd-wf4): WF4 pinout, the GPIO45/GPIO14 gates, DIO flash, P2/S4 download mode
- [kreier/huidu_hd-wf4](https://github.com/kreier/huidu_hd-wf4): WF4 v7 board notes
- [mrcodetastic/HD-WF1-WF2-LED-MatrixPanel-DMA](https://github.com/mrcodetastic/HD-WF1-WF2-LED-MatrixPanel-DMA): flashing the related WF2 over its USB-A socket
- [ESP32-HUB75-MatrixPanel-DMA issue #433](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-DMA/issues/433): original Huidu pin reverse engineering
