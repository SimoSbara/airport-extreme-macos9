#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 SimoSbara
"""
regen_all.py - Regenerates all the files taken from Linux and wpa_supplicant.

Usage (from the project directory):
    python Tools/regen_all.py

Sources: the Linux 5.15.225 tree in linux-5.15.225/ and wpa_supplicant-2.11/.
  - b43 (drivers/net/wireless/broadcom/b43) -> Common/b43/
  - ssb (drivers/ssb)                       -> Common/ssb/ (.c files with ssb_ prefix)
  - Linux headers (include/...)             -> Common/include/ (same layout)
  - mac80211, cfg80211, lib                -> Common/mac80211, cfg80211, lib
  - wpa_supplicant (src/...)                -> Common/wpa/src/ (same layout)
The Broadcom firmware is not handled here: the build converts it (fw2c.py).
Files listed in VERBATIM are copied as-is; from those too tied to the
kernel (main.c, pci.c, ...) Tools/extract_b43.py copies only the definitions
listed in the Tools/*_extract.txt files.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINUX = os.path.join(ROOT, "linux-5.15.225")
TOOLS = os.path.join(ROOT, "Tools")
COMMON = os.path.join(ROOT, "Common")

B43_SRC = os.path.join(LINUX, "drivers", "net", "wireless", "broadcom", "b43")
B43_DST = os.path.join(COMMON, "b43")
SSB_SRC = os.path.join(LINUX, "drivers", "ssb")
SSB_DST = os.path.join(COMMON, "ssb")
INC_SRC = os.path.join(LINUX, "include")
INC_DST = os.path.join(COMMON, "include")

# (source directory, destination directory, files copied unmodified)
VERBATIM = [
    (B43_SRC, B43_DST, [
        "b43.h", "bus.h", "main.h", "debugfs.h", "leds.h", "rfkill.h", "sdio.h",
        "lo.h", "lo.c", "phy_common.h", "phy_common.c", "phy_g.h", "phy_g.c",
        "phy_a.h", "phy_n.h", "phy_lp.h", "phy_ht.h", "phy_lcn.h", "phy_ac.h",
        "ppr.h", "tables.h", "tables.c", "wa.h", "wa.c", "xmit.h", "pio.h", "dma.h",
        "sysfs.h", "main.c", "bus.c", "dma.c", "pio.c", "xmit.c", "sysfs.c",
        "rfkill.c", "ppr.c",
    ]),
    # The ssb .c files get the "ssb_" prefix: CodeWarrior does not accept two
    # files with the same name in the same target (mac80211 has main.c, scan.c).
    # The content stays identical.
    (SSB_SRC, SSB_DST, [
        "ssb_private.h",
        ("main.c", "ssb_main.c"), ("scan.c", "ssb_scan.c"),
        ("pci.c", "ssb_pci.c"), ("pcihost_wrapper.c", "ssb_pcihost_wrapper.c"),
        ("b43_pci_bridge.c", "ssb_b43_pci_bridge.c"), ("sprom.c", "ssb_sprom.c"),
        ("driver_chipcommon.c", "ssb_driver_chipcommon.c"),
        ("driver_chipcommon_pmu.c", "ssb_driver_chipcommon_pmu.c"),
        ("driver_pcicore.c", "ssb_driver_pcicore.c"),
        ("driver_extif.c", "ssb_driver_extif.c"),
    ]),
    (INC_SRC, INC_DST, [
        "linux/ssb/ssb.h", "linux/ssb/ssb_regs.h",
        "linux/ssb/ssb_driver_chipcommon.h", "linux/ssb/ssb_driver_mips.h",
        "linux/ssb/ssb_driver_extif.h", "linux/ssb/ssb_driver_pci.h",
        "linux/ssb/ssb_embedded.h", "linux/ssb/ssb_driver_gige.h",
        "linux/pci_ids.h",
        # mac80211/cfg80211 (public headers)
        "net/mac80211.h", "net/cfg80211.h", "net/regulatory.h",
        "net/ieee80211_radiotap.h", "linux/ieee80211.h",
        ("uapi/linux/nl80211.h", "linux/nl80211.h"),
        "uapi/linux/rfkill.h", "uapi/linux/if_ether.h",
        # without CONFIG_RFKILL / CONFIG_FW_LOADER these contain the empty stubs
        "linux/rfkill.h", "linux/firmware.h",
        # generic algorithms used by mac80211 (TX queues, averages, hashing)
        "net/codel.h", "net/codel_impl.h", "net/fq.h", "net/fq_impl.h",
        "linux/average.h", "linux/jhash.h", "linux/unaligned/packed_struct.h",
        "linux/rbtree.h", "linux/rbtree_types.h", "linux/rbtree_augmented.h",
        "crypto/aes.h", "crypto/arc4.h",
        "linux/bcm47xx_wdt.h", "linux/bcm47xx_nvram.h",
    ]),
]

# mac80211: the files from the Makefile without LED, debugfs, mesh, PM and tracing;
# plus the minstrel_ht rate control algorithm. Prefix "mac_".
MAC_SRC = os.path.join(LINUX, "net", "mac80211")
MAC_DST = os.path.join(COMMON, "mac80211")
MAC_C = ["main", "status", "driver-ops", "sta_info", "wep", "aead_api", "wpa",
         "scan", "offchannel", "ht", "agg-tx", "agg-rx", "vht", "he", "s1g",
         "ibss", "iface", "rate", "michael", "tkip", "aes_cmac", "aes_gmac",
         "fils_aead", "cfg", "ethtool", "rx", "spectmgmt", "tx", "key", "util",
         "wme", "chan", "mlme", "tdls", "ocb", "airtime", "rc80211_minstrel_ht"]
MAC_H = ["aead_api.h", "aes_ccm.h", "aes_cmac.h", "aes_gcm.h", "aes_gmac.h",
         "debug.h", "debugfs.h", "debugfs_key.h", "debugfs_netdev.h",
         "debugfs_sta.h", "driver-ops.h", "fils_aead.h", "ieee80211_i.h", "key.h",
         "led.h", "mesh.h", "michael.h", "rate.h", "rc80211_minstrel_ht.h",
         "sta_info.h", "tkip.h", "trace.h", "trace_msg.h", "wep.h", "wme.h", "wpa.h"]
# cfg80211 (net/wireless) without nl80211.c (the netlink interface to user
# space: on Mac OS 9 Common/os9 replaces it) and pmsr.c (FTM measurements, only
# via nl80211), wext, debugfs, sysfs, OF.
# Prefix "cfg_".
CFG_SRC = os.path.join(LINUX, "net", "wireless")
CFG_DST = os.path.join(COMMON, "cfg80211")
CFG_C = ["core", "reg", "scan", "util", "mlme", "ibss", "sme", "chan", "ethtool",
         "mesh", "ap", "ocb", "radiotap"]
CFG_H = ["core.h", "debugfs.h", "nl80211.h", "rdev-ops.h", "reg.h", "sysfs.h",
         "trace.h", "wext-compat.h"]
VERBATIM += [
    (MAC_SRC, MAC_DST, MAC_H + [(c + ".c", "mac_" + c + ".c") for c in MAC_C]),
    (CFG_SRC, CFG_DST, CFG_H + [(c + ".c", "cfg_" + c + ".c") for c in CFG_C]),
]

# wpa_supplicant 2.11 (BSD): only the RSN supplicant (src/rsn_supp) with the
# internal crypto. All the headers in src/ (except utils/build_config.h,
# which for Mac OS 9 is Common/wpa/os9/build_config.h) and the .c files used, in
# the same directory layout. It is a separate library in CodeWarrior:
# some headers have the same path as Linux ones (crypto/aes.h).
WPA_ROOT = os.path.join(ROOT, "wpa_supplicant-2.11", "src")
WPA_DST = os.path.join(COMMON, "wpa", "src")
WPA_C = [
    "utils/common.c", "utils/wpa_debug.c", "utils/wpabuf.c",
    "common/wpa_common.c", "common/ieee802_11_common.c",
    "rsn_supp/wpa.c", "rsn_supp/wpa_ie.c", "rsn_supp/pmksa_cache.c",
    "crypto/sha1.c", "crypto/sha1-internal.c", "crypto/sha1-pbkdf2.c",
    "crypto/sha1-prf.c", "crypto/md5.c", "crypto/md5-internal.c",
    "crypto/sha256.c", "crypto/sha256-internal.c", "crypto/sha256-prf.c",
    "crypto/aes-internal.c", "crypto/aes-internal-enc.c",
    "crypto/aes-internal-dec.c", "crypto/aes-unwrap.c", "crypto/aes-wrap.c",
    "crypto/aes-omac1.c", "crypto/rc4.c",
]

def wpa_files():
    files = []
    for d, _, fs in os.walk(WPA_ROOT):
        for f in fs:
            rel = os.path.relpath(os.path.join(d, f), WPA_ROOT).replace("\\", "/")
            if f.endswith(".h") and rel != "utils/build_config.h":
                files.append(rel)
    return files + WPA_C

# lib/: kernel library functions used by mac80211/cfg80211
VERBATIM += [
    (os.path.join(LINUX, "lib"), os.path.join(COMMON, "lib"),
     ["rbtree.c", ("crypto/aes.c", "aes.c"), ("crypto/arc4.c", "arc4.c")]),
]

# (source, list, output, includes)
# Extractions: definitions copied unmodified from sources that do not
# compile as a whole (nl80211.c is the netlink interface to user space)
EXTRACTS = [
    (os.path.join(CFG_SRC, "nl80211.c"), "cfg_nl80211_extract.txt",
     os.path.join(CFG_DST, "cfg_nl80211_extract.c"),
     ["core.h", "nl80211.h", "rdev-ops.h"]),
]


VERBATIM += [(WPA_ROOT, WPA_DST, wpa_files()),
             # wpa_supplicant license (BSD) and notes, next to the code
             (os.path.join(ROOT, "wpa_supplicant-2.11"), os.path.join(COMMON, "wpa"),
              ["COPYING", "README"])]


def run(args):
    print(">", " ".join(args))
    subprocess.check_call([sys.executable, "-I"] + args)


def main():
    n = 0
    for src, dst, files in VERBATIM:
        for f in files:
            f_in, f_out = f if isinstance(f, tuple) else (f, f)
            out = os.path.join(dst, *f_out.split("/"))
            os.makedirs(os.path.dirname(out), exist_ok=True)
            shutil.copy2(os.path.join(src, *f_in.split("/")), out)
            n += 1
    print("Copied %d Linux and wpa_supplicant files unmodified" % n)
    for src, lst, out, incs in EXTRACTS:
        run([os.path.join(TOOLS, "extract_b43.py"), src,
             os.path.join(TOOLS, lst), out] + incs)
    # The Broadcom firmware is not redistributable: b43fw.c is generated by the
    # build (Build/CMakeLists.txt) from the files in firmware/.


if __name__ == "__main__":
    main()
