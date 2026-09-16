#!/usr/bin/env python3
# =============================================================================
# BITMAP FONT GENERATOR
# =============================================================================
# Renders a TrueType font into a 1-bit C header consumed by main/ui/font.cpp;
# the struct layout here must match main/ui/font.h.
#
# Usage: gen_font.py <font.ttf> <face> <c-identifier> <out.h>
# Example: gen_font.py tools/fonts/Inter-Regular.ttf body inter_body \
#              main/ui/fonts/inter_body.h

import sys
from PIL import Image, ImageDraw, ImageFont

FIRST, LAST = 0x20, 0x7E
# Pixels darker than this become black; e-ink has no gray to spend on
# anti-aliasing, so a lowish threshold keeps thin strokes intact.
THRESHOLD = 120

# Every family bakes to these numbers, so swapping one cannot move the layout.
# The nominal point size is derived per family instead of given, because the
# same nominal size draws visibly different letters in different faces: at
# 30 px Literata's box is 46 px against Inter's 38 px, while both put a 22 px
# capital on the screen. `cap` is what the eye reads as size, so it is the
# knob; `box` and `baseline` are fixed so Font::height and Font::baseline are
# family-independent, and `pitch` is the fixed line advance.
#           cap  box  baseline  pitch
FACES = {
    "small":   (16, 28, 22, 31),
    "body":    (22, 38, 30, 42),
    "large":   (29, 50, 39, 54),
    "xlarge":  (38, 66, 51, 70),
    "xxlarge": (47, 82, 63, 86),
}

# The widest nominal size worth probing when solving for a cap height.
MAX_SIZE = 160

# Scratch rows kept above and below the box, so a glyph that overflows can be
# seen overflowing instead of arriving silently cut.
PAD = 32


# CAPITAL HEIGHT MEASURER
# Returns the inked height of 'H' at a nominal size, which is the optical size
# the reader perceives, unlike the glyph box.
def cap_height(font):
    left, top, right, bottom = font.getbbox("H")
    width = max(1, right - left)
    ascent, descent = font.getmetrics()
    height = ascent + descent + 2 * PAD
    img = Image.new("L", (width, height), 0)
    ImageDraw.Draw(img).text((-left, PAD), "H", font=font, fill=255)
    px = img.load()
    for y in range(height):
        for x in range(width):
            if px[x, y] >= THRESHOLD:
                return PAD + ascent - y
    return 0


# INK BOUNDS MEASURER
# Returns how far the furthest-reaching ASCII glyph runs above and below the
# baseline, in pixels. Pillow's getbbox() is a layout box and sits wider than
# the ink, so measuring that instead would reject sizes that render fine.
def ink_bounds(font):
    ascent, descent = font.getmetrics()
    height = ascent + descent + 2 * PAD
    above = below = 0
    for code in range(FIRST, LAST + 1):
        ch = chr(code)
        left, top, right, bottom = font.getbbox(ch)
        width = max(1, right - left)
        # The pad matters: Inter's pipe reaches above the ascender line, and an
        # unpadded scratch would cut it and under-report the height needed.
        img = Image.new("L", (width, height), 0)
        ImageDraw.Draw(img).text((-left, PAD), ch, font=font, fill=255)
        px = img.load()
        rows = [y for y in range(height)
                if any(px[x, y] >= THRESHOLD for x in range(width))]
        if rows:
            above = max(above, PAD + ascent - rows[0])
            below = max(below, rows[-1] - PAD - ascent)
    return above, below


# NOMINAL SIZE SOLVER
# Returns the nominal size whose capitals land closest to the target height
# without any glyph's ink leaving the box, so a family lands on the requested
# optical size rather than on a shared number that means something different to
# each of them. Ties go to the smaller size.
def size_for_face(ttf, target, box, baseline):
    candidates = []
    for size in range(6, MAX_SIZE):
        cap = cap_height(ImageFont.truetype(ttf, size))
        if cap > target + 4:
            break
        if abs(cap - target) <= 4:
            candidates.append((abs(cap - target), size))
    candidates.sort()
    for _, size in candidates:
        above, below = ink_bounds(ImageFont.truetype(ttf, size))
        # The baseline occupies a row of its own, so a descender has one fewer
        # row beneath it than the arithmetic first suggests.
        if above <= baseline and below <= box - baseline - 1:
            return size
    sys.exit("{}: no size gives a {} px capital inside a {} px box".format(ttf, target, box))


# THE GLYPH RASTERIZER
# Renders one character onto the face's fixed baseline row and returns
# (bitmap_bytes, width, advance, x_offset, clipped).
def rasterize(font, ch, box, baseline):
    left, top, right, bottom = font.getbbox(ch)
    width = max(1, right - left)
    advance = max(1, round(font.getlength(ch)))
    ascent, descent = font.getmetrics()
    # Draw into a taller scratch so overflow is visible rather than silently
    # cut, then keep the PAD..PAD+box window as the glyph.
    img = Image.new("L", (width, box + 2 * PAD), 0)
    ImageDraw.Draw(img).text((-left, PAD + baseline - ascent), ch, font=font, fill=255)
    px = img.load()
    rows = [y for y in range(box + 2 * PAD)
            if any(px[x, y] >= THRESHOLD for x in range(width))]
    clipped = bool(rows) and (rows[0] < PAD or rows[-1] >= PAD + box)
    row_bytes = (width + 7) // 8
    out = bytearray(row_bytes * box)
    for y in range(box):
        for x in range(width):
            if px[x, PAD + y] >= THRESHOLD:
                out[y * row_bytes + x // 8] |= 0x80 >> (x % 8)
    return bytes(out), width, advance, left, clipped


# THE HEADER WRITER
# Emits the glyph table and packed bitmap as C arrays.
def main(argv):
    if len(argv) != 5:
        sys.exit("usage: gen_font.py <font.ttf> <face> <c-identifier> <out.h>")
    ttf, face, name, out_path = argv[1], argv[2], argv[3], argv[4]
    if face not in FACES:
        sys.exit("unknown face {}, expected one of {}".format(face, ", ".join(sorted(FACES))))
    cap, box, baseline, pitch = FACES[face]

    size = size_for_face(ttf, cap, box, baseline)
    font = ImageFont.truetype(ttf, size)

    glyphs, bitmap, clipped = [], bytearray(), []
    for code in range(FIRST, LAST + 1):
        data, width, advance, x_offset, cut = rasterize(font, chr(code), box, baseline)
        if cut:
            clipped.append(chr(code))
        glyphs.append((len(bitmap), width, advance, x_offset))
        bitmap += data
    if clipped:
        sys.exit("{} at {} px overflows the {} px box: {}".format(
            ttf, size, box, " ".join(clipped)))

    with open(out_path, "w", newline="\n") as f:
        f.write("// =============================================================================\n")
        f.write("// {} BITMAP FONT\n".format(name.upper()))
        f.write("// =============================================================================\n")
        f.write("// Generated by tools/gen_font.py from {} at {} px, the size that gives the\n"
                .format(ttf.split("/")[-1], size))
        f.write("// {} face its {} px capital. Do not edit; rerun the generator instead.\n"
                .format(face, cap))
        f.write("#pragma once\n\n#include \"ui/font.h\"\n\n")
        f.write("static const uint8_t {}_bitmap[] = {{\n".format(name))
        for i in range(0, len(bitmap), 16):
            f.write("    " + ", ".join("0x{:02X}".format(b) for b in bitmap[i:i + 16]) + ",\n")
        f.write("};\n\n")
        f.write("static const FontGlyph {}_glyphs[] = {{\n".format(name))
        for (offset, width, advance, x_offset), code in zip(glyphs, range(FIRST, LAST + 1)):
            f.write("    {{{}, {}, {}, {}}},  // '{}'\n".format(
                offset, width, advance, x_offset, chr(code)))
        f.write("};\n\n")
        f.write("static const Font {} = {{\n".format(name))
        f.write("    {}, {}, {}, {}, {},\n".format(box, baseline, pitch, FIRST, LAST))
        f.write("    {}_glyphs, {}_bitmap,\n}};\n".format(name, name))
    print("{}: {} face at {} px, cap {}, box {}, {} bytes".format(
        out_path, face, size, cap, box, len(bitmap)))


if __name__ == "__main__":
    main(sys.argv)
