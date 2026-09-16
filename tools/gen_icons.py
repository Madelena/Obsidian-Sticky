#!/usr/bin/env python3
# =============================================================================
# STATUS ICON GENERATOR
# =============================================================================
# Rasterizes the Material Symbols glyphs the status band uses into a 1-bit C
# header consumed by main/ui/icons.cpp; the struct layout must match the Icon
# in main/ui/icons.h.
#
# Usage: gen_icons.py [out.h]     (default main/ui/icons_data.h)
#
# The variable font is 10 MB and is never committed. It is downloaded once to
# build/material_symbols.ttf and reused, so a rerun needs the network only the
# first time.

import os
import sys
import urllib.request

from PIL import Image, ImageDraw, ImageFont

FONT_URL = ("https://raw.githubusercontent.com/google/material-design-icons/master/"
            "variablefont/MaterialSymbolsOutlined%5BFILL%2CGRAD%2Copsz%2Cwght%5D.ttf")
LICENSE_URL = "https://raw.githubusercontent.com/google/material-design-icons/master/LICENSE"
CACHE = "build/material_symbols.ttf"

# Outlines rather than fills, and a half step above the 400 default: a 1-bit
# panel has no anti-aliasing to soften a hairline, and 500 survives the
# threshold at 34 px where 400 breaks up.
FILL, GRAD, WEIGHT = 0, 0, 500
# Pixels darker than this become black, matching tools/gen_font.py.
THRESHOLD = 128

# The band uses two sizes: the sleep mark stands in for a headline, and the
# right-hand cluster is secondary.
FACE_PX = 40
SLOT_PX = 34

# C enum suffix, Material Symbols glyph name, pixel size. Order is the order
# of the generated IconId enum, so it must not be shuffled casually.
ICONS = [
    ("Bedtime", "bedtime", FACE_PX),
    ("Wifi", "wifi", SLOT_PX),
    ("WifiOff", "wifi_off", SLOT_PX),
    ("WifiBad", "signal_wifi_bad", SLOT_PX),
    ("Bolt", "bolt", SLOT_PX),
    ("Plug", "power", SLOT_PX),
    ("BatteryFull", "battery_android_full", SLOT_PX),
    ("Battery75", "battery_android_5", SLOT_PX),
    ("Battery50", "battery_android_3", SLOT_PX),
    ("Battery25", "battery_android_1", SLOT_PX),
    ("BatteryLow", "battery_android_alert", SLOT_PX),
    ("BatteryUnknown", "battery_android_question", SLOT_PX),
]


# THE FONT FETCHER
# Returns the path to the cached variable font, downloading it and the Apache
# licence text on the first run.
def fetch_font():
    if os.path.exists(CACHE):
        return CACHE
    os.makedirs("build", exist_ok=True)
    print(f"Downloading {FONT_URL}")
    urllib.request.urlretrieve(FONT_URL, CACHE)
    urllib.request.urlretrieve(LICENSE_URL, "build/material_symbols_LICENSE.txt")
    return CACHE


# CODE POINT MAPPER
# Returns {glyph name: code point} for the icon names, which Material Symbols
# addresses through a private-use code point rather than a ligature.
def code_points(path, names):
    from fontTools.ttLib import TTFont
    cmap = TTFont(path, lazy=True).getBestCmap()
    found = {}
    for code, name in sorted(cmap.items()):
        if name in names and name not in found:
            found[name] = code
    missing = set(names) - set(found)
    if missing:
        sys.exit(f"not in this font: {', '.join(sorted(missing))}")
    return found


# THE ICON RASTERIZER
# Renders one glyph on its baseline, crops to the ink and returns
# (bitmap_bytes, width, height, x_offset, y_offset), the offsets being the
# ink's corner relative to the center of the em square.
#
# Every Material Symbols glyph advances exactly one em and stands on the
# baseline, so measuring from that square rather than from the ink is what
# keeps the three Wi-Fi states, whose ink differs by 2 px of slash, on the
# same line in the band.
def rasterize(path, code, size):
    font = ImageFont.truetype(path, size)
    font.set_variation_by_axes([FILL, GRAD, min(max(size, 20), 48), WEIGHT])
    pad = size // 2
    img = Image.new("L", (size * 2, size * 2), 0)
    ImageDraw.Draw(img).text((pad, pad + size), chr(code), font=font, fill=255, anchor="ls")
    ink = img.point(lambda v: 255 if v >= THRESHOLD else 0).getbbox()
    if ink is None:
        sys.exit(f"glyph {code:#x} rendered blank at {size} px")
    left, top, right, bottom = ink
    width, height = right - left, bottom - top
    row_bytes = (width + 7) // 8
    out = bytearray(row_bytes * height)
    px = img.load()
    for y in range(height):
        for x in range(width):
            if px[left + x, top + y] >= THRESHOLD:
                out[y * row_bytes + x // 8] |= 0x80 >> (x % 8)
    center = pad + size // 2
    return bytes(out), width, height, left - center, top - center


# THE HEADER WRITER
# Emits the icon table and packed bitmap as C arrays.
def main(argv):
    out_path = argv[1] if len(argv) > 1 else "main/ui/icons_data.h"
    path = fetch_font()
    codes = code_points(path, {name for _, name, _ in ICONS})

    entries, bitmap = [], bytearray()
    for suffix, name, size in ICONS:
        data, width, height, dx, dy = rasterize(path, codes[name], size)
        entries.append((suffix, name, size, len(bitmap), width, height, dx, dy))
        bitmap += data

    with open(out_path, "w", newline="\n") as f:
        f.write("// =============================================================================\n")
        f.write("// STATUS ICON BITMAPS\n")
        f.write("// =============================================================================\n")
        f.write("// Generated by tools/gen_icons.py from Material Symbols Outlined.\n")
        f.write("// Do not edit; rerun the generator instead.\n")
        f.write('#pragma once\n\n#include "ui/icons.h"\n\n')
        f.write("static const uint8_t icon_bitmap[] = {\n")
        for i in range(0, len(bitmap), 16):
            f.write("    " + ", ".join(f"0x{b:02X}" for b in bitmap[i:i + 16]) + ",\n")
        f.write("};\n\n")
        f.write("enum IconId {\n")
        for entry in entries:
            f.write(f"    kIcon{entry[0]},\n")
        f.write("    kIconCount,\n};\n\n")
        f.write("static const Icon icon_table[kIconCount] = {\n")
        for _, name, size, offset, width, height, dx, dy in entries:
            f.write(f"    {{{offset}, {width}, {height}, {dx}, {dy}}},"
                    f"  // {name} at {size} px\n")
        f.write("};\n")
    print(f"{out_path}: {len(entries)} icons, {len(bitmap)} bytes")


if __name__ == "__main__":
    main(sys.argv)
