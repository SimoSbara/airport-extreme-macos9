# AirPort Extreme for Mac OS 9

A driver for the original **AirPort Extreme** card (Broadcom BCM4306,
802.11b/g) on **Mac OS 9**, built from the Linux wireless stack.

Instead of writing a new driver, it runs the Linux 5.15 code **unchanged**:
`b43` (the chip), `ssb` (the bus), `mac80211` and `cfg80211` (802.11), plus
the WPA supplicant from `wpa_supplicant` 2.11. This project only adds the
layer that maps the Linux kernel interfaces onto Mac OS 9.

## Status

Tested on an iBook G4 (BCM4306 rev 3, Apple 106B:004E) running Mac OS 9:

- [x] chip and firmware initialization, scan
- [x] authentication and association
- [x] WPA2-PSK (4-way handshake, hardware CCMP)
- [x] encrypted data in both directions (DHCP)
- [ ] Open Transport driver (today: the `AirPortTester` test application)
- [ ] DMA (today: PIO)
- [ ] control panel / menu utility

## Layout

| Folder | Contents |
|---|---|
| `Common/b43`, `ssb`, `mac80211`, `cfg80211`, `lib`, `include` | Linux 5.15.225 sources, unchanged |
| `Common/wpa/src` | wpa_supplicant 2.11 sources, unchanged |
| `Common/compat` | Linux kernel API on Mac OS 9 (skb, net_device, timers, PCI, crypto...) |
| `Common/os9` | glue to Mac OS 9 (`b43os9.h`: the API used by applications/drivers) |
| `Common/wpa/os9` | wpa_supplicant port (os, eloop, supplicant glue) |
| `AirPortExtremeTester` | test application |
| `Build` | build with Retro68 |
| `Tools` | scripts that copy the Linux/wpa_supplicant sources into `Common` |
| `firmware` | the Broadcom firmware goes here (not included) |

## Building

1. Build [Retro68](https://github.com/autc04/Retro68) for PowerPC
   (`build-toolchain.bash --no-68k --no-carbon`).
2. Extract the firmware into `firmware/` (see [firmware/README.md](firmware/README.md)).
3. Build:
   ```sh
   cmake -S Build -B out -DCMAKE_TOOLCHAIN_FILE=<Retro68-build>/toolchain/powerpc-apple-macos/cmake/retroppc.toolchain.cmake
   cmake --build out
   ```
4. Copy `out/AirPortTester.bin` to the Mac and decode it with StuffIt Expander.

`Tools/regen_all.py` regenerates `Common/` from the original
`linux-5.15.225/` and `wpa_supplicant-2.11/` trees.

## License

GPL-2.0 (see [LICENSE](LICENSE)), the license of the Linux code it contains.
wpa_supplicant is BSD-licensed ([Common/wpa/COPYING](Common/wpa/COPYING)).
The Broadcom firmware is proprietary and is **not** part of this repository.

## Credits

All the hard work is by the authors of b43, ssb, mac80211, cfg80211 and
wpa_supplicant: see [CREDITS.md](CREDITS.md).
