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
- [x] Open Transport driver: TCP/IP (DHCP or manual), AppleTalk, FTP transfers
- [x] chip interrupts and DMA transfers (`irq=0` / `dma=0` go back to
      polling and PIO)
- [ ] throughput: see "Known limitation" below
- [ ] utility to choose the network (today: a preferences file)

### Known limitation: throughput

The link works but is slow (a few KB/s). The access point often does not
hear the acknowledgements the card sends for the frames it receives, sends
every frame again several times and falls back to 1 Mbit/s. Linux 3.2 on the
same iBook, same place and same access point, runs at 24-36 Mbit/s.

What has been ruled out so far, each with a test on the iBook:

- DMA against PIO, chip interrupts against polling: same behaviour.
- The firmware version (784.2 against 508.1084, the one Linux 3.2 used).
- Which antenna receives and which one sends the acknowledgements.
- The three radio setup values that b43 of Linux 3.2 writes differently
  from 5.15 for this card (G PHY rev 2, radio 2050 rev 2), applied after
  the initialization.
- The "no slow clock" board flag, the Power Manager idling the processor,
  the card's own MAC address against a locally administered one.

The Tester (`AirPortTester`) has the tools used for these measurements: a
choice of firmware, an ARP and a ping test that count how many copies of
each frame the access point had to send, different ways of serving the
card, and a record of the register accesses. In one session it ran like
Linux (210 KB/s, next to no repeated frames); the same program later ran
as slowly as the driver, so the cause is still unknown. Help is welcome.

## Layout

| Folder | Contents |
|---|---|
| `Common/b43`, `ssb`, `mac80211`, `cfg80211`, `lib`, `include` | Linux 5.15.225 sources, unchanged |
| `Common/wpa/src` | wpa_supplicant 2.11 sources, unchanged |
| `Common/compat` | Linux kernel API on Mac OS 9 (skb, net_device, timers, PCI, crypto...) |
| `Common/os9` | glue to Mac OS 9 (`b43os9.h`: the API used by applications/drivers) |
| `Common/wpa/os9` | wpa_supplicant port (os, eloop, supplicant glue) |
| `AirPortExtremeTester` | test application |
| `AirPortExtremeDriver` | Open Transport driver (`ndrv`, DLPI through DLPISupportLib), `AirPortStatus` (state and log) and `OTCheck` (Open Transport diagnostics) |
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

The Open Transport driver also needs Apple's Universal Interfaces 3.4
(build Retro68 with `--universal`) and an import library for the Open
Transport kernel fragments (`OT_KERNEL_LIB`, made with Retro68's `MakeImport`
from the "Open Transport" extension of a Mac OS 9 system). Without it only
the Tester is built.

### Using the driver

1. Put `AirPortExtreme` in the Extensions folder.
2. Create a text file named `AirPort Extreme Prefs` in the Preferences
   folder:
   ```
   ssid=MyNetwork
   password=my wpa password
   country=IT
   ```
3. Restart and choose the AirPort Extreme port ("Ethernet Slot B" on an
   iBook G4) in the TCP/IP control panel.
4. `AirPortStatus` shows the connection and saves the driver's log.

The password is stored in clear text in that file until the utility exists.
`dma=0` makes the chip use PIO transfers instead of DMA, `irq=0` turns off
the chip interrupt (the driver then polls every 10 ms).
With `debug=1` the log also traces the dialogue with Open Transport, the
first frames, and the chip and link counters every 10 s; `verbose=` (b43,
0..3), `pr_debug=` (mac80211) and `wpa_debug=` (wpa_supplicant) tune the
other messages. `OTCheck` lists the Open Transport ports, loads TCP/IP and
prints the error it returns, if any.

If the Mac freezes, `screen_log=1` also draws the log on the screen, where
it can still be read (and photographed). `watchdog=1` adds a `[stuck]` line
with the return addresses of the code that does not come back: the link map
of the build (`AirPortExtreme.map`) turns them into function names.
`firmware=<version>` selects another firmware version built into the driver
(see [firmware/README.md](firmware/README.md)).

Note for Retro68 users writing Open Transport modules: GCC ignores the
`#pragma options align=power` that `OpenTransportProtocol.h` uses for
`struct module_info`, so the header's layout is wrong (`mi_idname` at
offset 2 instead of 4); the driver declares its own copy.

`Tools/regen_all.py` regenerates `Common/` from the original
`linux-5.15.225/` and `wpa_supplicant-2.11/` trees.

## License

GPL-2.0 (see [LICENSE](LICENSE)), the license of the Linux code it contains.
wpa_supplicant is BSD-licensed ([Common/wpa/COPYING](Common/wpa/COPYING)).
The Broadcom firmware is proprietary and is **not** part of this repository.

## Credits

All the hard work is by the authors of b43, ssb, mac80211, cfg80211 and
wpa_supplicant: see [CREDITS.md](CREDITS.md).
