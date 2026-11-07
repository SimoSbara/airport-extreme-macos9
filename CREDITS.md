# Credits

This project only exists because of the people who wrote the Linux wireless
stack and wpa_supplicant. Almost all of the code that drives the card is
theirs, used unchanged; this project adds the glue to Mac OS 9.

## b43 (drivers/net/wireless/broadcom/b43)

The free driver for Broadcom 43xx cards, written from the clean-room
specifications of the [bcm-v4 project](https://bcm-v4.sipsolutions.net/).

- Michael Buesch
- Rafał Miłecki
- Stefano Brivio
- Martin Langer
- Danny van Dyk
- Andreas Jaggi
- Gábor Stefanik
- Albert Herranz
- Pekka Pietikäinen
- Larry Finger, long-time maintainer, who also hosts the firmware archives
- the linux-wireless and b43-dev communities, and everyone who
  reverse-engineered the hardware and wrote the specifications

## ssb (drivers/ssb)

Sonics Silicon Backplane bus driver: Michael Buesch, Broadcom Corporation,
and contributors.

## mac80211 and cfg80211 (net/mac80211, net/wireless)

The Linux 802.11 stack: Jiri Benc, Johannes Berg, Jouni Malinen,
Instant802 Networks, Devicescape Software, Intel, and the many developers of
linux-wireless.

## wpa_supplicant

WPA/WPA2 supplicant: Jouni Malinen and contributors
(BSD license, see `Common/wpa/COPYING`).

## Linux kernel

Generic kernel code used unchanged (rbtree, AES, ARC4, CoDel/fq, headers):
the Linux kernel developers.

## b43-fwcutter

Michael Buesch and contributors: the tool that extracts the firmware.

## Retro68

[Retro68](https://github.com/autc04/Retro68) by Wolfgang Thaller and
contributors: the GCC toolchain for classic Mac OS that builds this project,
including the Multiversal Interfaces and the native driver support.

## This project

Mac OS 9 port by SimoSbara. The compatibility layer, the Mac OS 9 glue and
the tools were written with the assistance of Claude (Anthropic).
