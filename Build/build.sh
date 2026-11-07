#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 SimoSbara
# build.sh - Copies the sources to the Debian VM and builds with Retro68.
#
# Usage (from Git Bash, in the project directory):
#     Build/build.sh            build
#     Build/build.sh clean      rebuild from scratch
#
# Output in Build/out/: AirPortTester.bin (MacBinary, to be copied to the Mac
# and decoded with StuffIt Expander) and AirPortTester.dsk (disk image).

set -e
VM="simosbara@debian-vm"
KEY="$HOME/.ssh/id_ed25519_macos9"
SSH="ssh -i $KEY -o BatchMode=yes $VM"
REMOTE="macos9"
TOOLCHAIN='$HOME/retro/Retro68-build/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake'

cd "$(dirname "$0")/.."

# Sources: Common, Tester (without Old), Build
tar cf - --exclude='AirPortExtremeTester/Old' --exclude='AirPortExtremeTester/cwprobe' \
    --exclude='*.mcp' --exclude='*.xml' --exclude='Build/out' \
    Common AirPortExtremeTester Build Tools/fw2c.py firmware | $SSH "mkdir -p $REMOTE && tar xf - -C $REMOTE"

if [ "$1" = "clean" ]; then
    $SSH "rm -rf $REMOTE/out"
fi
$SSH "cd $REMOTE && \
      cmake -S Build -B out -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN -DCMAKE_BUILD_TYPE=Release > /dev/null && \
      cmake --build out -j8 -- -k 2>&1 | grep -E 'error|warning: implicit|undefined reference|Error|Linking|Built target' | head -200"

mkdir -p Build/out
scp -q -i "$KEY" "$VM:$REMOTE/out/AirPortTester.bin" "$VM:$REMOTE/out/AirPortTester.dsk" Build/out/ 2>/dev/null \
    && ls -l Build/out
