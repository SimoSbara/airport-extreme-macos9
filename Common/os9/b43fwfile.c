/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * b43fwfile.c - The b43 firmware read from files (see b43fwfile.h).
 */
#include <string.h>
#include <MacTypes.h>
#include <Files.h>
#include <Folders.h>

#include "b43fwfile.h"

#define FW_MAX_BYTES    (128UL * 1024)  /* ucode5.fw is about 22 KB */

static const char *kFiles[4] = {
    "ucode5.fw", "pcm5.fw", "b0g0initvals5.fw", "b0g0bsinitvals5.fw"
};
static const char kTypes[4] = { 'u', 'p', 'i', 'i' };

/* Joins up to four strings into dst (the driver avoids the C library's
 * printf family) */
static void join(char *dst, unsigned long size, const char *a, const char *b,
                 const char *c, const char *d)
{
    const char *parts[4];
    unsigned long n = 0;
    int i;

    parts[0] = a;
    parts[1] = b;
    parts[2] = c;
    parts[3] = d;
    for (i = 0; i < 4; i++) {
        const char *s = parts[i];

        while (s && *s && n + 1 < size)
            dst[n++] = *s++;
    }
    dst[n] = '\0';
}

/* FSSpec of a file by its partial path from the System Folder */
static OSErr spec_of(const char *path, FSSpec *spec)
{
    Str255 p;
    short vref;
    long dirid;
    unsigned long n = strlen(path);
    OSErr err;

    if (n > 255)
        return bdNamErr;
    p[0] = (unsigned char)n;
    memcpy(p + 1, path, n);
    err = FindFolder(kOnSystemDisk, kSystemFolderType, kDontCreateFolder, &vref, &dirid);
    if (err != noErr)
        return err;
    return FSMakeFSSpec(vref, dirid, p, spec);
}

/* The header b43 checks (struct b43_fw_header): type, version 1, size */
static int check(const unsigned char *d, unsigned long len, char type)
{
    unsigned long size;

    if (len < 8 || d[0] != (unsigned char)type || d[1] != 1)
        return -1;
    size = ((unsigned long)d[4] << 24) | ((unsigned long)d[5] << 16) |
           ((unsigned long)d[6] << 8) | d[7];
    if ((type == 'u' || type == 'p') && size != len - 8)
        return -1;
    return 0;
}

static int read_file(const char *path, char type, void *(*alloc)(unsigned long),
                     const unsigned char **data, unsigned long *len)
{
    FSSpec spec;
    short ref;
    long eof, count;
    unsigned char *buf;
    OSErr err;

    if (spec_of(path, &spec) != noErr || FSpOpenDF(&spec, fsRdPerm, &ref) != noErr)
        return B43FW_FILE_MISSING;
    err = GetEOF(ref, &eof);
    if (err != noErr || eof <= 0 || (unsigned long)eof > FW_MAX_BYTES) {
        FSClose(ref);
        return B43FW_FILE_BAD;
    }
    buf = (unsigned char *)alloc((unsigned long)eof);
    if (!buf) {
        FSClose(ref);
        return B43FW_FILE_NOMEM;
    }
    count = eof;
    err = FSRead(ref, &count, buf);
    FSClose(ref);
    if (err != noErr || count != eof || check(buf, (unsigned long)eof, type))
        return B43FW_FILE_BAD;
    *data = buf;
    *len = (unsigned long)eof;
    return B43FW_FILE_OK;
}

/* 1 = the folder (partial path ending with ':') holds ucode5.fw */
static int has_firmware(const char *dir)
{
    char path[300];
    FSSpec spec;

    join(path, sizeof(path), dir, kFiles[0], NULL, NULL);
    return spec_of(path, &spec) == noErr;
}

int b43_file_firmware(b43os9_firmware *fw, const char *version,
                      void *(*alloc)(unsigned long size), char *msg, unsigned long msg_size)
{
    char dir[200], path[300];
    const unsigned char *data[4];
    unsigned long len[4];
    FSSpec spec;
    int i, rc;

    msg[0] = '\0';
    join(dir, sizeof(dir), ":", B43FW_FOLDER_NAME, ":", NULL);
    if (spec_of(dir, &spec) != noErr) {
        join(msg, msg_size, "no \"", B43FW_FOLDER_NAME, "\" folder in the System Folder", NULL);
        return B43FW_FILE_NO_FOLDER;
    }
    if (version && version[0]) {
        join(dir, sizeof(dir), ":" B43FW_FOLDER_NAME ":", version, ":", NULL);
    } else if (!has_firmware(dir)) {
        join(path, sizeof(path), ":" B43FW_FOLDER_NAME ":b43:", NULL, NULL, NULL);
        if (has_firmware(path))
            strcpy(dir, path);          /* the folder b43-fwcutter wrote */
    }
    for (i = 0; i < 4; i++) {
        join(path, sizeof(path), dir, kFiles[i], NULL, NULL);
        rc = read_file(path, kTypes[i], alloc, &data[i], &len[i]);
        if (rc != B43FW_FILE_OK) {
            join(msg, msg_size, dir + 1, kFiles[i],
                 rc == B43FW_FILE_MISSING ? ": missing" :
                 rc == B43FW_FILE_NOMEM ? ": out of memory" : ": not a b43 firmware file", NULL);
            return rc;
        }
    }
    fw->ucode = data[0];
    fw->ucode_len = len[0];
    fw->pcm = data[1];
    fw->pcm_len = len[1];
    fw->iv = data[2];
    fw->iv_len = len[2];
    fw->bsiv = data[3];
    fw->bsiv_len = len[3];
    join(msg, msg_size, "files in ", dir + 1, NULL, NULL);
    return B43FW_FILE_OK;
}
