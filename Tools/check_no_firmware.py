#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 SimoSbara
"""
check_no_firmware.py - Makes sure that files about to be published do not
contain the Broadcom firmware (which cannot be redistributed).

    check_no_firmware.py <firmware_dir> file...

Takes pieces of every .fw file under firmware_dir (each version) and looks
for them in the files. Exit status 1, and the names, if one is found. With
no .fw file to compare against it cannot check anything and says so.
"""
import os
import sys

PIECE = 32          # bytes per piece
PIECES = 8          # pieces per firmware file


def pieces(path):
    with open(path, "rb") as f:
        data = f.read()[8:]                 # after the b43 header
    if len(data) < PIECE:
        return []
    step = max(1, (len(data) - PIECE) // PIECES)
    return [data[i:i + PIECE] for i in range(0, len(data) - PIECE + 1, step)][:PIECES]


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    fw_dir, files = sys.argv[1], sys.argv[2:]
    probes = []
    for root, _, names in os.walk(fw_dir):
        for name in names:
            if name.endswith(".fw"):
                probes += [(os.path.join(root, name), p) for p in pieces(os.path.join(root, name))]
    if not probes:
        print("check_no_firmware: no .fw files in %s: nothing to compare against" % fw_dir)
        return 0
    bad = []
    for path in files:
        with open(path, "rb") as f:
            data = f.read()
        for fw, probe in probes:
            if probe in data:
                bad.append("%s contains %s" % (path, fw))
                break
    if bad:
        print("FIRMWARE FOUND, do not publish:\n  " + "\n  ".join(bad))
        return 1
    print("check_no_firmware: %d files, no firmware (%d pieces of %d bytes)" %
          (len(files), len(probes), PIECE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
