/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef B43FWFILE_H_
#define B43FWFILE_H_

/*
 * b43fwfile.h - The b43 firmware read from files, as Linux does with
 * request_firmware(): the Broadcom firmware cannot be redistributed, so
 * the published programs do not contain it and the user extracts it with
 * b43-fwcutter (see firmware/README.md).
 *
 * The four files (ucode5.fw, pcm5.fw, b0g0initvals5.fw, b0g0bsinitvals5.fw)
 * go in a folder named "AirPort Extreme Firmware" in the System Folder:
 * directly, in its "b43" subfolder (the folder b43-fwcutter writes), or in
 * a subfolder named after a version, chosen with the version argument.
 *
 * Task time only (File Manager). Compiled with the Mac OS headers, outside
 * the Linux sources.
 */
#include "b43os9.h"

#define B43FW_FOLDER_NAME   "AirPort Extreme Firmware"

enum {
    B43FW_FILE_OK = 0,
    B43FW_FILE_NO_FOLDER = -1,      /* no "AirPort Extreme Firmware" folder */
    B43FW_FILE_MISSING = -2,        /* a file is missing (msg names it) */
    B43FW_FILE_BAD = -3,            /* not a b43 firmware file */
    B43FW_FILE_NOMEM = -4
};

/* Reads the four files into memory from alloc (never freed: b43 uses
 * them while the program runs). version: a subfolder name, NULL or "" for
 * the folder itself (or its b43 subfolder). msg: what was loaded, or what
 * is wrong, for the log. */
int b43_file_firmware(b43os9_firmware *fw, const char *version,
                      void *(*alloc)(unsigned long size), char *msg, unsigned long msg_size);

#endif /* B43FWFILE_H_ */
