#!/usr/bin/env python3
"""Render the panel's screens to PNGs (LED-dot look) using the firmware Renderer.

    tools/preview/build.sh && python3 tools/preview/render_png.py [outdir]
"""
import os
import struct
import subprocess
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
BINARY = os.path.join(HERE, "preview.exe")

SCENARIOS = [
    ("speed_stw", ["speed", "value=6.4", "kind=stw"]),
    ("speed_sog_kmh", ["speed", "value=23.7", "kind=sog", "unit=kmh"]),
    ("speed_fast", ["speed", "value=12.3", "kind=sog"]),
    ("speed_two_decimals", ["speed", "value=7.45", "decimals=2"]),
    ("speed_night", ["speed", "value=5.8", "night=1"]),
    ("no_data", ["speed", "valid=0", "msg=NO DATA"]),
    ("no_wifi", ["speed", "valid=0", "wifi=0", "link=0", "err=1", "msg=NO WIFI YDWG  SETUP: HeadUp-1A2B 192.168.71.1", "now=1200"]),
    ("setup", ["info", "setup=1"]),
    ("info_boat", ["info", "page=1"]),
    ("test_bars", ["test", "t=7000"]),
    ("update", ["message", "title=UPDATE", "progress=40"]),
    ("splash", ["splash"]),
    ("dash4_64x32", ["dash", "width=64", "height=32", "sections=4", "items=stw,awa,aws,dpt", "values=6.4,-38,14.2,8.3"]),
    ("dash4_nav", ["dash", "width=64", "height=32", "sections=4", "items=twd,hdg,dtw,vmg", "values=255,203,2.53,4.6"]),
    ("dash4_portrait", ["dash", "width=64", "height=32", "rotation=1", "sections=4", "items=stw,twa,tws,dpt",
                        "values=7.1,135,22.5,0", "valid=1,1,1,0"]),
    ("dash2_64x32", ["dash", "width=64", "height=32", "sections=2", "items=aws,awa", "values=14.2,38"]),
    ("dash4_64x64", ["dash", "sections=4", "items=stw,awa,tws,dpt", "values=6.4,52,18.3,4.2"]),
    ("awa_64x32", ["dash", "width=64", "height=32", "sections=1", "items=awa", "values=-38"]),
]


def frame(args):
    raw = subprocess.run([BINARY] + args, check=True, capture_output=True).stdout
    w = h = 64
    for a in args:
        if a.startswith("width="):
            w = int(a[6:])
        if a.startswith("height="):
            h = int(a[7:])
    return w, h, struct.unpack("<%dH" % (w * h), raw)


def to_image(w, h, pixels, cell=6):
    img = Image.new("RGB", (w * cell, h * cell), (0, 0, 0))
    d = ImageDraw.Draw(img)
    r = cell * 0.25  # dot diameter = 50% of pitch
    for y in range(h):
        for x in range(w):
            v = pixels[y * w + x]
            rgb = (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
            if rgb == (0, 0, 0):
                rgb = (22, 22, 22)
            cx, cy = x * cell + cell / 2, y * cell + cell / 2
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=rgb)
    return img


def upright(img, args):
    """Turn a rotated layout the way it reads on the mounted panel."""
    rot = next((int(a[9:]) for a in args if a.startswith("rotation=")), 0)
    return img.rotate({0: 0, 1: 90, 2: 180, 3: -90}[rot % 4], expand=True)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "docs", "panel")
    os.makedirs(out, exist_ok=True)
    images = []
    for name, args in SCENARIOS:
        img = upright(to_image(*frame(args)), args)
        img.save(os.path.join(out, name + ".png"))
        images.append((name, img))
    cols = 4
    tw = max(img.size[0] for _, img in images)
    th = max(img.size[1] for _, img in images)
    pad = 16
    rows = (len(images) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (tw + pad) + pad, rows * (th + pad + 18) + pad), (40, 44, 52))
    d = ImageDraw.Draw(sheet)
    for i, (name, img) in enumerate(images):
        x = pad + (i % cols) * (tw + pad)
        y = pad + (i // cols) * (th + pad + 18)
        sheet.paste(img, (x, y))
        d.text((x, y + img.size[1] + 3), name, fill=(220, 220, 220))
    sheet.save(os.path.join(out, "contact_sheet.png"))
    print("wrote %d screens to %s" % (len(images), os.path.normpath(out)))


if __name__ == "__main__":
    main()
