#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 SimoSbara
"""
release_zips.py - Packs a release build (Build/build.sh release) into the
two archives to publish:

    AirPortExtreme-<version>.zip     the programs and a short Read Me
    debug-symbols-<version>.zip      the link maps of the driver and the Tester

The version is the driver's (APX_DRIVER_VERSION in AirPortExtremeDriver/drv.h).
"""
import os
import re
import zipfile

with open("AirPortExtremeDriver/drv.h") as f:
    VERSION = re.search(r'APX_DRIVER_VERSION\s+"([^"]+)"', f.read()).group(1)
REL = "Build/release"

README = """AirPort Extreme for Mac OS 9 - version {v}
https://github.com/SimoSbara/airport-extreme-macos9

Driver for the AirPort Extreme card (Broadcom BCM4306) on Mac OS 9,
built from the Linux 5.15 wireless stack and wpa_supplicant 2.11.

Decode the .bin files with StuffIt Expander, then:

1. FIRMWARE (not included: Broadcom does not allow it)
   On a computer with Linux (WSL on Windows is fine):
     sudo apt install b43-fwcutter
     wget https://www.lwfinger.com/b43-firmware/broadcom-wl-6.30.163.46.tar.bz2
     tar xjf broadcom-wl-6.30.163.46.tar.bz2
     b43-fwcutter -w . broadcom-wl-6.30.163.46.wl_apsta.o
   It writes a folder named "b43". On the Mac, create a folder named
   "AirPort Extreme Firmware" in the System Folder and copy "b43" into it.

2. FILES
   AirPortExtreme            System Folder : Extensions
   AirPort Extreme Startup   System Folder : Startup Items
   AirPort Extreme           anywhere (for example Applications)
   AirPortStatus, AirPortTester, OTCheck: only for problems

3. Restart, choose the AirPort Extreme port in the TCP/IP control panel
   ("Ethernet Slot B" on an iBook G4), open AirPort Extreme and join your
   network.

Known limitation: the connection works but is slow (a few KB/s).

Problems: open AirPortStatus and attach "AirPort Extreme Report.txt" to an
issue on the project page. Free software, GNU GPL version 2.
""".format(v=VERSION)

programs = ["AirPortExtreme.bin", "AirPortExtremeUtility.bin", "AirPortExtremeStartup.bin",
            "AirPortStatus.bin", "AirPortTester.bin", "OTCheck.bin"]
maps = ["AirPortExtreme.map", "AirPortTester.map"]

main_zip = os.path.join(REL, "AirPortExtreme-%s.zip" % VERSION)
with zipfile.ZipFile(main_zip, "w", zipfile.ZIP_DEFLATED) as z:
    folder = "AirPort Extreme %s/" % VERSION
    z.writestr(folder + "Read Me.txt", README.replace("\n", "\r").encode("mac_roman"))
    for f in programs:
        z.write(os.path.join(REL, f), folder + f)

sym_zip = os.path.join(REL, "debug-symbols-%s.zip" % VERSION)
with zipfile.ZipFile(sym_zip, "w", zipfile.ZIP_DEFLATED) as z:
    folder = "debug-symbols-%s/" % VERSION
    z.writestr(folder + "README.txt",
               "Link maps of AirPort Extreme %s (driver and Tester): they turn the\n"
               "addresses on a MacsBug screen into function names. They match only\n"
               "the programs of the same release.\n" % VERSION)
    for f in maps:
        z.write(os.path.join(REL, f), folder + f)

for p in (main_zip, sym_zip):
    with zipfile.ZipFile(p) as z:
        print(p, os.path.getsize(p), "bytes")
        for i in z.infolist():
            print("   ", i.filename, i.file_size)
