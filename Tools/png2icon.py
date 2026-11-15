#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 SimoSbara
"""
png2icon.py - Turns a 32x32 PNG into the icon resources of Mac OS 9, as a
Rez file.

    png2icon.py icon32.png out.r [--id -16455] [--id 128] [--small icon16.png]
                [--knockout-white]

Resources (all with the same ID):
    ICN#  32x32 black and white + mask      ics#  16x16 black and white + mask
    icl4  32x32, 16 system colours          ics4  16x16, 16 colours
    icl8  32x32, 256 system colours         ics8  16x16, 256 colours
    icns  il32/l8mk and is32/s8mk: 32-bit colour with a soft mask (Mac OS
          8.5 and later, Icon Services)

ID -16455 is a file's custom icon (Finder flag "has custom icon"); the
icons of a bundle (BNDL) usually have ID 128. --id may be repeated: the
same icons are written once for each ID.
The 16x16 icons are made from the 32x32 one unless --small gives one.
--knockout-white makes the near-white background transparent (filled from
the border), for a PNG without an alpha channel.

Needs Pillow.
"""
import argparse
import sys

from PIL import Image

# The Mac OS 256-colour system palette ('clut' 8): the 6x6x6 colour cube
# without black, then ramps of red, green, blue and grey, then black.
def palette8():
    levels = [0xFF, 0xCC, 0x99, 0x66, 0x33, 0x00]
    pal = []
    for r in levels:
        for g in levels:
            for b in levels:
                pal.append((r, g, b))
    pal = pal[:-1]                                  # black goes last
    ramp = [0xEE, 0xDD, 0xBB, 0xAA, 0x88, 0x77, 0x55, 0x44, 0x22, 0x11]
    pal += [(v, 0, 0) for v in ramp]
    pal += [(0, v, 0) for v in ramp]
    pal += [(0, 0, v) for v in ramp]
    pal += [(v, v, v) for v in ramp]
    pal.append((0, 0, 0))
    assert len(pal) == 256
    return pal

# The 16-colour system palette ('clut' 4)
PALETTE4 = [
    (0xFF, 0xFF, 0xFF), (0xFC, 0xF3, 0x05), (0xFF, 0x64, 0x02), (0xDD, 0x08, 0x06),
    (0xF2, 0x08, 0x84), (0x46, 0x00, 0xA5), (0x00, 0x00, 0xD4), (0x02, 0xAB, 0xEA),
    (0x1F, 0xB7, 0x14), (0x00, 0x64, 0x11), (0x56, 0x2C, 0x05), (0x90, 0x71, 0x3A),
    (0xC0, 0xC0, 0xC0), (0x80, 0x80, 0x80), (0x40, 0x40, 0x40), (0x00, 0x00, 0x00),
]
PALETTE8 = palette8()


def nearest(pal, rgb, cache):
    if rgb in cache:
        return cache[rgb]
    r, g, b = rgb
    best, best_d = 0, None
    for i, (pr, pg, pb) in enumerate(pal):
        # Weighted distance: the eye is most sensitive to green
        d = 3 * (r - pr) ** 2 + 4 * (g - pg) ** 2 + 2 * (b - pb) ** 2
        if best_d is None or d < best_d:
            best, best_d = i, d
    cache[rgb] = best
    return best


def knockout_white(im):
    """Near-white pixels connected to the border become transparent."""
    im = im.convert("RGBA")
    w, h = im.size
    px = im.load()

    def whiteish(p):
        r, g, b, a = p
        return min(r, g, b) > 225 and max(r, g, b) - min(r, g, b) < 12

    stack = [(x, y) for x in range(w) for y in (0, h - 1)]
    stack += [(x, y) for y in range(h) for x in (0, w - 1)]
    seen = set()
    while stack:
        x, y = stack.pop()
        if (x, y) in seen or not (0 <= x < w and 0 <= y < h):
            continue
        seen.add((x, y))
        if not whiteish(px[x, y]):
            continue
        r, g, b, _ = px[x, y]
        px[x, y] = (r, g, b, 0)
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return im


def over_white(p):
    """Colour of a pixel drawn on white (for the palette icons)."""
    r, g, b, a = p
    return (
        (r * a + 255 * (255 - a)) // 255,
        (g * a + 255 * (255 - a)) // 255,
        (b * a + 255 * (255 - a)) // 255,
    )


def bits(values):
    """List of 0/1, row by row, to bytes (most significant bit first)."""
    out = bytearray()
    for i in range(0, len(values), 8):
        byte = 0
        for j in range(8):
            byte = (byte << 1) | (1 if values[i + j] else 0)
        out.append(byte)
    return bytes(out)


def classic_icons(im):
    """(bw+mask, 4-bit, 8-bit) data of one size."""
    pixels = list(im.getdata())
    mask = [1 if p[3] >= 128 else 0 for p in pixels]
    colours = [over_white(p) for p in pixels]
    # Black and white: dark pixels black (light colours, such as a light
    # blue, count as dark so that the drawing does not vanish)
    bw = [1 if m and (0.30 * c[0] + 0.59 * c[1] + 0.11 * c[2]) < 210 else 0
          for c, m in zip(colours, mask)]
    icn = bits(bw) + bits(mask)
    c4, c8 = {}, {}
    idx4 = [nearest(PALETTE4, c, c4) if m else 0 for c, m in zip(colours, mask)]
    idx8 = [nearest(PALETTE8, c, c8) if m else 0 for c, m in zip(colours, mask)]
    icl4 = bytes((idx4[i] << 4) | idx4[i + 1] for i in range(0, len(idx4), 2))
    icl8 = bytes(idx8)
    return icn, icl4, icl8


def rle_channel(data):
    """icns run-length encoding of one channel: 0x80+n-3 repeats the next
    byte n times (3..130), n-1 is followed by n literal bytes (1..128)."""
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        run = 1
        while i + run < n and run < 130 and data[i + run] == data[i]:
            run += 1
        if run >= 3:
            out += bytes([0x80 + run - 3, data[i]])
            i += run
            continue
        start = i
        while i < n and i - start < 128:
            run = 1
            while i + run < n and run < 3 and data[i + run] == data[i]:
                run += 1
            if run >= 3:
                break
            i += 1
        out.append(i - start - 1)
        out += data[start:i]
    return bytes(out)


def icns_rgb(im):
    pixels = list(im.getdata())
    r = bytes(p[0] for p in pixels)
    g = bytes(p[1] for p in pixels)
    b = bytes(p[2] for p in pixels)
    return rle_channel(r) + rle_channel(g) + rle_channel(b)


def icns_mask(im):
    return bytes(p[3] for p in im.getdata())


def icns(elements):
    body = b""
    for kind, data in elements:
        body += kind.encode("ascii") + (len(data) + 8).to_bytes(4, "big") + data
    return b"icns" + (len(body) + 8).to_bytes(4, "big") + body


def rez_data(kind, rid, data, comment):
    lines = ["/* %s */" % comment, "data '%s' (%d) {" % (kind, rid)]
    for i in range(0, len(data), 32):
        lines.append('    $"%s"' % data[i:i + 32].hex().upper())
    lines.append("};")
    return "\n".join(lines) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("png32")
    ap.add_argument("out")
    ap.add_argument("--id", type=int, action="append")
    ap.add_argument("--small", help="16x16 PNG (default: made from the 32x32 one)")
    ap.add_argument("--knockout-white", action="store_true")
    args = ap.parse_args()

    big = Image.open(args.png32)
    big = knockout_white(big) if args.knockout_white else big.convert("RGBA")
    if big.size != (32, 32):
        sys.exit("%s: the icon must be 32x32, not %dx%d" % (args.png32, *big.size))
    if args.small:
        small = Image.open(args.small)
        small = knockout_white(small) if args.knockout_white else small.convert("RGBA")
    else:
        small = big.resize((16, 16), Image.LANCZOS)
    if small.size != (16, 16):
        sys.exit("%s: the small icon must be 16x16" % args.small)

    icn, icl4, icl8 = classic_icons(big)
    ics, ics4, ics8 = classic_icons(small)
    family = icns([
        ("il32", icns_rgb(big)), ("l8mk", icns_mask(big)),
        ("is32", icns_rgb(small)), ("s8mk", icns_mask(small)),
    ])

    with open(args.out, "w", newline="\n") as f:
        f.write("/* Generated by Tools/png2icon.py from %s: do not edit. */\n\n"
                % args.png32.replace("\\", "/"))
        for rid in args.id or [-16455]:
            f.write(rez_data("ICN#", rid, icn, "32x32 black and white + mask"))
            f.write(rez_data("icl4", rid, icl4, "32x32, 16 colours"))
            f.write(rez_data("icl8", rid, icl8, "32x32, 256 colours"))
            f.write(rez_data("ics#", rid, ics, "16x16 black and white + mask"))
            f.write(rez_data("ics4", rid, ics4, "16x16, 16 colours"))
            f.write(rez_data("ics8", rid, ics8, "16x16, 256 colours"))
            f.write(rez_data("icns", rid, family, "32-bit colour, soft mask"))


if __name__ == "__main__":
    main()
