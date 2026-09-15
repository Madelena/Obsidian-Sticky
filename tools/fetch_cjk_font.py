#!/usr/bin/env python3
# =============================================================================
# CJK FONT FETCHER
# =============================================================================
# Builds build/font_cjk.ttf, the subset Noto Sans TC that the top-level
# CMakeLists.txt flashes into the `font` partition read by main/ui/cjk_font.cpp.

import io
import os
import sys
import urllib.request

FONT_URL = "https://raw.githubusercontent.com/google/fonts/main/ofl/notosanstc/NotoSansTC%5Bwght%5D.ttf"
LICENSE_URL = "https://raw.githubusercontent.com/google/fonts/main/ofl/notosanstc/OFL.txt"

# The `font` partition in partitions.csv is 8 MiB, and the whole file is
# memory-mapped, so a subset larger than that cannot be flashed at all.
PARTITION_BYTES = 8 * 1024 * 1024

# Code point ranges worth carrying on a 800x480 e-ink screen; everything
# outside them still folds to ASCII in main/ui/text.cpp.
UNICODE_RANGES = [
    (0x00A0, 0x024F),  # Latin-1 Supplement and Latin Extended-A and -B
    (0x02C6, 0x02DC),  # Spacing modifier letters used by Latin text
    (0x2000, 0x206F),  # General punctuation, dashes and quotes
    (0x2190, 0x21FF),  # Arrows
    (0x2460, 0x24FF),  # Enclosed alphanumerics
    (0x2500, 0x257F),  # Box drawing
    (0x25A0, 0x25FF),  # Geometric shapes
    (0x2600, 0x26FF),  # Miscellaneous symbols
    (0x3000, 0x30FF),  # CJK punctuation, hiragana and katakana
    (0x3100, 0x312F),  # Bopomofo
    (0x31A0, 0x31BF),  # Bopomofo extended
    (0x3400, 0x4DBF),  # CJK unified ideographs extension A
    (0x4E00, 0x9FFF),  # CJK unified ideographs
    (0xF900, 0xFAFF),  # CJK compatibility ideographs
    (0xFE30, 0xFE4F),  # CJK compatibility forms
    (0xFF00, 0xFFEF),  # Halfwidth and fullwidth forms
]

# Tables that only matter for shaping, hinting or variation, none of which
# stb_truetype reads once the font is a pinned static instance.
DROP_TABLES = [
    "GSUB", "GPOS", "GDEF", "BASE", "JSTF", "DSIG",
    "fpgm", "prep", "cvt ", "gasp", "hdmx", "LTSH", "VDMX",
    "fvar", "gvar", "avar", "cvar", "HVAR", "VVAR", "MVAR", "STAT",
    "vhea", "vmtx", "VORG",
]


# FILE DOWNLOADER
# Fetches url into path unless the file is already there, and returns the path.
def download(url, path):
    if os.path.exists(path):
        print("Have {} already".format(os.path.basename(path)))
        return path
    print("Downloading {}".format(url))
    with urllib.request.urlopen(url) as response, open(path, "wb") as out:
        out.write(response.read())
    return path


# WEIGHT INSTANTIATOR
# Pins the variable font's wght axis at 400 so the outlines become a plain
# static TrueType instance.
def instantiate(font):
    from fontTools.varLib import instancer

    if "fvar" not in font:
        print("Not a variable font, using it as is")
        return font
    return instancer.instantiateVariableFont(font, {"wght": 400}, inplace=True)


# GLYPH SUBSETTER
# Keeps only the code points in UNICODE_RANGES and strips the layout and
# hinting tables, leaving glyf outlines and a cmap.
def subset(font):
    from fontTools import subset

    options = subset.Options()
    options.layout_features = []
    options.hinting = False
    options.glyph_names = False
    options.legacy_kern = False
    options.notdef_outline = False
    options.drop_tables += DROP_TABLES

    code_points = []
    for first, last in UNICODE_RANGES:
        code_points.extend(range(first, last + 1))

    subsetter = subset.Subsetter(options=options)
    subsetter.populate(unicodes=code_points)
    subsetter.subset(font)
    return font


# THE CJK FONT FETCHER
# Downloads, instantiates, subsets and writes build/font_cjk.ttf, refusing to
# write a file too large for the flash partition.
def main():
    try:
        from fontTools.ttLib import TTFont
    except ImportError:
        print("fontTools is missing. Install it with: pip install fonttools")
        return 1

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    build = os.path.join(root, "build")
    os.makedirs(build, exist_ok=True)

    source = download(FONT_URL, os.path.join(build, "NotoSansTC-variable.ttf"))
    download(LICENSE_URL, os.path.join(build, "NotoSansTC-OFL.txt"))

    font = TTFont(source)
    print("Source has {} glyphs".format(font["maxp"].numGlyphs))
    font = subset(instantiate(font))
    print("Subset has {} glyphs".format(font["maxp"].numGlyphs))

    # Size is only knowable after compilation, so the font is built in memory
    # and the partition check runs before anything reaches the disk.
    buffer = io.BytesIO()
    font.save(buffer)
    data = buffer.getvalue()
    print("Subset is {:.2f} MB ({} bytes)".format(len(data) / 1048576.0, len(data)))
    if len(data) > PARTITION_BYTES:
        print("Too large for the 8 MB font partition, nothing written")
        return 1

    target = os.path.join(build, "font_cjk.ttf")
    with open(target, "wb") as out:
        out.write(data)
    print("Wrote {}".format(target))
    return 0


if __name__ == "__main__":
    sys.exit(main())
