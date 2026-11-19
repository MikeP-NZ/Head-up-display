"""Writes the ESP Launchpad config for the GitHub Pages site.

ESP Launchpad (https://espressif.github.io/esp-launchpad/) flashes an ESP32
from the browser over Web Serial. Its Quick Start page reads config.toml,
which lists each board as a "develop kit" of the ESP32-S3 with its own
combined image. The user picks their board, and Launchpad writes that image
at offset 0:

    https://espressif.github.io/esp-launchpad/?flashConfigURL=<site>/config.toml

The release workflow runs this with the version and the Pages URL, in a
folder that already holds the headup-<board>-<version>-combined.bin images:

    python scripts/make_launchpad.py 1.2.0 https://mikep-nz.github.io/Head-up-display site

Launchpad downloads the TOML and the images from the browser. Its
crossDomain=true option proxies only the TOML, so the images must be served
with CORS headers, which GitHub Pages does and GitHub release downloads don't.
"""
import html
import sys
from pathlib import Path
from urllib.parse import quote

# platformio.ini environment -> the kit name Launchpad shows (it is also a
# TOML key, so letters, digits and dashes only).
BOARDS = {
    "waveshare_s3_matrix": "Waveshare-ESP32-S3-RGB-Matrix",
    "huidu_hd_wf4": "Huidu-HD-WF4",
    "matrixportal_s3": "Adafruit-Matrix-Portal-S3",
}
APP = "Head-Up-Display"
LAUNCHPAD = "https://espressif.github.io/esp-launchpad/?flashConfigURL="


def main(version, base_url, out_dir):
    base_url = base_url.rstrip("/") + "/"
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    images = []
    for env, kit in BOARDS.items():
        image = "headup-%s-%s-combined.bin" % (env, version)
        if not (out / image).is_file():
            raise SystemExit("%s is missing from %s" % (image, out))
        images.append('image.%s = "%s"' % (kit, image))

    (out / "config.toml").write_text("\n".join([
        "# ESP Launchpad config, written by scripts/make_launchpad.py.",
        "esp_toml_version = 1.0",
        'firmware_images_url = "%s"' % base_url,
        'supported_apps = ["%s"]' % APP,
        "",
        "[%s]" % APP,
        'chipsets = ["ESP32-S3"]',
        "developKits.esp32-s3 = [%s]" % ", ".join('"%s"' % k for k in BOARDS.values()),
        *images,
        'description = "Head-Up Display %s: boat data from a Yacht Devices gateway on a HUB75 LED panel. '
        'Pick your board: each one needs its own image."' % version,
        "console_baudrate = 115200",
        "",
    ]), encoding="utf-8")

    link = LAUNCHPAD + quote(base_url + "config.toml", safe="")
    rows = "\n".join('<li>%s: <a href="headup-%s-%s-combined.bin">combined image</a></li>' % (
        html.escape(kit.replace("-", " ")), env, html.escape(version)) for env, kit in BOARDS.items())
    (out / "index.html").write_text("""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Head-Up Display firmware</title>
<style>
  body { font: 16px/1.5 system-ui, sans-serif; max-width: 40rem; margin: 2rem auto; padding: 0 16px; }
</style>
</head>
<body>
<h1>Head-Up Display firmware %s</h1>
<p>Flash a board for the first time from Chrome or Edge: connect it over USB,
open <a href="%s">ESP Launchpad</a>, press <b>Connect</b>, pick your board
under <b>ESP Develop Kits</b>, then press <b>Flash</b>. Make sure the right
board is picked: each one needs its own image.</p>
<p>The images, for other tools (write at offset 0):</p>
<ul>
%s
</ul>
<p>Later updates go through the display's portal (<b>System</b> tab) with the
<code>headup-&lt;board&gt;-&lt;version&gt;.bin</code> file from the
<a href="https://github.com/MikeP-NZ/Head-up-display/releases/latest">latest release</a>.</p>
</body>
</html>
""" % (html.escape(version), html.escape(link), rows), encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    main(*sys.argv[1:])
