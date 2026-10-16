#!/usr/bin/env python3
"""Draws the portal's home-screen icons: "6.2 KN" in LED dots, as on the panel.

    python tools/make_icons.py

Writes web/icon-180.png (iPhone/iPad) and web/icon-192.png, web/icon-512.png
(Android). The firmware build embeds them (scripts/embed_web.py). Only the
Python standard library is needed.
"""
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.join(HERE, "..", "web")

BACKGROUND = (14, 14, 14)  # the portal's --bg
UNLIT = (38, 38, 38)
WHITE = (255, 255, 255)
BLUE = (0, 160, 255)  # the panel's default accent colour

# 16x16 dots: "6.2" in the large digits, "KN" in the small font below.
ROWS = [
    "................",
    "..###......###..",
    ".#...#....#...#.",
    ".#............#.",
    ".#............#.",
    ".####........#..",
    ".#...#......#...",
    ".#...#.....#....",
    ".#...#....#.....",
    "..###..#..#####.",
    "................",
    "....b.b.b..b....",
    "....b.b.bb.b....",
    "....bb..b.bb....",
    "....b.b.b..b....",
    "................",
]


def dot_colour(ch):
    return {"#": WHITE, "b": BLUE}.get(ch, UNLIT)


def render(size):
    """RGB rows. The dots stay inside the middle 80%, the area Android keeps
    when it crops an icon to a circle or squircle."""
    n = len(ROWS)
    margin = size * 0.12
    pitch = (size - 2 * margin) / n
    radius = pitch * 0.36
    rows = []
    for py in range(size):
        line = bytearray()
        for px in range(size):
            colour = BACKGROUND
            gx = (px + 0.5 - margin) / pitch
            gy = (py + 0.5 - margin) / pitch
            cx, cy = int(gx), int(gy)
            if 0 <= cx < n and 0 <= cy < n:
                dx = (gx - cx - 0.5) * pitch
                dy = (gy - cy - 0.5) * pitch
                # Anti-aliased edge: cover = 1 inside, fading over one pixel.
                cover = max(0.0, min(1.0, radius + 0.5 - (dx * dx + dy * dy) ** 0.5))
                if cover > 0:
                    dot = dot_colour(ROWS[cy][cx])
                    colour = tuple(round(b + (d - b) * cover) for b, d in zip(BACKGROUND, dot))
            line += bytes(colour)
        rows.append(bytes(line))
    return rows


def write_png(path, rows):
    height, width = len(rows), len(rows[0]) // 3
    raw = b"".join(b"\0" + r for r in rows)

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def main():
    for size in (180, 192, 512):
        path = os.path.join(WEB, "icon-%d.png" % size)
        write_png(path, render(size))
        print("wrote", os.path.relpath(path, os.path.join(HERE, "..")), os.path.getsize(path), "bytes")


if __name__ == "__main__":
    main()
