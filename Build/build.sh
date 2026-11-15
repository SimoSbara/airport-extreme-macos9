#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 SimoSbara
# build.sh - Copies the sources to the Debian VM and builds with Retro68.
#
# Usage (from Git Bash, in the project directory):
#     Build/build.sh            development build, in Build/out/
#     Build/build.sh clean      the same, from scratch
#     Build/build.sh release    build to publish, in Build/release/, with the two
#                               archives of the release (Tools/release_zips.py)
#
# Output: MacBinary files, to be copied to the Mac and decoded with StuffIt
# Expander. A development build has the firmware of firmware/ built in, so
# its programs must not be published. A release build has no firmware (the
# programs read it from files, see firmware/README.md); the script checks
# that no published file contains it, and adds the link maps of the driver
# and the Tester (to turn MacsBug addresses into function names).

set -e
VM="simosbara@debian-vm"
KEY="$HOME/.ssh/id_ed25519_macos9"
SSH="ssh -i $KEY -o BatchMode=yes $VM"
REMOTE="macos9"
TOOLCHAIN='$HOME/retro/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake'
PROGRAMS="AirPortExtreme.bin AirPortExtremeUtility.bin AirPortExtremeStartup.bin AirPortStatus.bin
          AirPortTester.bin OTCheck.bin"

cd "$(dirname "$0")/.."

if [ "$1" = "release" ]; then
    BUILD_DIR=out-release
    OUT=Build/release
    OPTIONS="-DAPX_BUILTIN_FIRMWARE=OFF"
    FIRMWARE=""                     # the firmware does not even leave this PC
else
    BUILD_DIR=out
    OUT=Build/out
    OPTIONS="-DAPX_BUILTIN_FIRMWARE=ON"
    FIRMWARE=firmware
fi

# Sources: Common, Tester (without Old), driver, utility, Build
tar cf - --exclude='AirPortExtremeTester/Old' --exclude='AirPortExtremeTester/cwprobe' \
    --exclude='*.mcp' --exclude='*.xml' --exclude='Build/out' --exclude='Build/release' \
    Common AirPortExtremeTester AirPortExtremeDriver AirPortExtremeUtility Build Tools/fw2c.py \
    $FIRMWARE | $SSH "mkdir -p $REMOTE && tar xf - -C $REMOTE"

if [ "$1" = "clean" ] || [ "$1" = "release" ]; then
    $SSH "rm -rf $REMOTE/$BUILD_DIR"
fi
$SSH "cd $REMOTE && \
      cmake -S Build -B $BUILD_DIR -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN -DCMAKE_BUILD_TYPE=Release \
            $OPTIONS > /dev/null && \
      cmake --build $BUILD_DIR -j8 -- -k 2>&1 | grep -E 'error|warning: implicit|undefined reference|Error|Linking|Built target' | head -200"

# Only the files the build makes go away: Build/out also holds the logs
# copied back from the Mac
mkdir -p "$OUT"
rm -f "$OUT"/*.zip
for f in $PROGRAMS AirPortTester.dsk AirPortExtreme.map AirPortTester.map; do
    rm -f "$OUT/$f"
done
for f in $PROGRAMS AirPortTester.dsk; do
    scp -q -i "$KEY" "$VM:$REMOTE/$BUILD_DIR/$f" "$OUT/" 2>/dev/null || echo "missing: $f"
done
# Icons: a bundle (BNDL) and a custom icon; the Finder needs the flags.
# And the names the files get on the Mac.
flags() { [ -f "$OUT/$1" ] && python Tools/macbin_flags.py "$OUT/$1" custom-icon bundle --name "$2"; }
flags AirPortExtreme.bin "AirPortExtreme"
flags AirPortExtremeUtility.bin "AirPort Extreme"
flags AirPortExtremeStartup.bin "AirPort Extreme Startup"

if [ "$1" = "release" ]; then
    for f in AirPortExtreme.map AirPortTester.map; do
        scp -q -i "$KEY" "$VM:$REMOTE/$BUILD_DIR/$f" "$OUT/" 2>/dev/null || echo "missing: $f"
    done
    rm -f "$OUT/AirPortTester.dsk"
    python Tools/check_no_firmware.py firmware "$OUT"/*
    python Tools/release_zips.py
fi
ls -l "$OUT"
