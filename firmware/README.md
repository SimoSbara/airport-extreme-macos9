# b43 firmware

The Broadcom firmware is **not** included: it is proprietary and cannot be
redistributed. Extract it yourself from Broadcom's driver with
[b43-fwcutter](https://wireless.docs.kernel.org/en/latest/en/users/drivers/b43/developers.html),
exactly as Linux users do.

The driver is tested with firmware **784.2**, from `broadcom-wl-6.30.163.46`:

```sh
wget https://www.lwfinger.com/b43-firmware/broadcom-wl-6.30.163.46.tar.bz2
tar xjf broadcom-wl-6.30.163.46.tar.bz2
b43-fwcutter -w . broadcom-wl-6.30.163.46.wl_apsta.o
```

It writes a `b43/` folder. Four of its files are used by this card (802.11
core revision 5, G PHY):

```
ucode5.fw  pcm5.fw  b0g0initvals5.fw  b0g0bsinitvals5.fw
```

## On the Mac

The driver and the Tester read the firmware when they start, as Linux reads
it from `/lib/firmware`. Create a folder named **`AirPort Extreme
Firmware`** in the System Folder and put in it either:

- the whole `b43` folder written by b43-fwcutter, or
- the four files above.

To try another firmware version, put its four files in a subfolder (for
example `AirPort Extreme Firmware:508.1084:`) and add `firmware=508.1084`
to the `AirPort Extreme Prefs` file in the Preferences folder.

If the firmware is missing, the AirPort Extreme application says so, and
the driver's log (`AirPortStatus`) names the file it could not read.

## Development builds

A build with `-DAPX_BUILTIN_FIRMWARE=ON` (the default of
`Build/CMakeLists.txt`) also builds the files of this folder into the
driver and the Tester (`Tools/fw2c.py` writes `b43fw.c`), as a fallback
when the Mac has no firmware folder. Put the four files here; subfolders
named `fw-<version>` hold other versions, chosen with `firmware=<version>`
or by the Tester's expert mode.

Those programs contain the firmware: **do not publish them**. The files to
publish come from `Build/build.sh release`, which builds without it and
checks that no file contains it.
