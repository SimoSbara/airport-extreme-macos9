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

Then copy these four files from the `b43/` folder it creates into this folder:

```
ucode5.fw  pcm5.fw  b0g0initvals5.fw  b0g0bsinitvals5.fw
```

The build turns them into `b43fw.c`, which ends up inside the program.
Because of that, **do not publish binaries built with the firmware**.
