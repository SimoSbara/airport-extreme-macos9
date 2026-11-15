#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 SimoSbara
"""
macbin_flags.py - Sets Finder flags in a MacBinary file.

    macbin_flags.py file.bin custom-icon bundle [--name "Mac file name"]

Retro68 writes MacBinary files with all Finder flags off. A file with a
bundle (BNDL, FREF and its signature resource) needs "has bundle" (0x2000)
for the Finder to register its icons; one with a custom icon (ID -16455)
needs "has custom icon" (0x0400). MacBinary II decoders clear bit 10 (it
was "changed" in System 6), so the bundle is the one that survives. The flags are at offset 73 (high
byte) and 101 (low byte, MacBinary II); the header CRC (CRC-16/XMODEM over
bytes 0..123) is at 124.

--name sets the name the file gets when it is decoded on the Mac (offset 1:
length, then up to 63 MacRoman characters), which Retro68 takes from the
target name.
"""
import sys

FLAGS = {
    "custom-icon": 0x0400,
    "bundle": 0x2000,
}


def crc16_xmodem(data):
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def main():
    args = sys.argv[1:]
    name = None
    if "--name" in args:
        i = args.index("--name")
        name = args[i + 1].encode("mac_roman")
        del args[i:i + 2]
        if not 1 <= len(name) <= 63 or b":" in name:
            sys.exit("--name: 1..63 characters, no colon")
    if len(args) < 1 or any(f not in FLAGS for f in args[1:]):
        sys.exit("usage: macbin_flags.py file.bin [%s...] [--name name]" % "|".join(FLAGS))
    path, flag_names = args[0], args[1:]
    with open(path, "rb") as f:
        data = bytearray(f.read())
    if len(data) < 128 or data[0] != 0 or data[74] != 0:
        sys.exit("%s: not a MacBinary file" % path)
    if crc16_xmodem(data[:124]) != int.from_bytes(data[124:126], "big"):
        sys.exit("%s: bad MacBinary header CRC" % path)
    flags = (data[73] << 8) | data[101]
    for flag in flag_names:
        flags |= FLAGS[flag]
    data[73] = flags >> 8
    data[101] = flags & 0xFF
    if name:
        data[1] = len(name)
        data[2:65] = name + bytes(63 - len(name))
    data[124:126] = crc16_xmodem(data[:124]).to_bytes(2, "big")
    with open(path, "wb") as f:
        f.write(data)


if __name__ == "__main__":
    main()
