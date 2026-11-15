/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * apx_prefs.c - Reads and rewrites "AirPort Extreme Prefs".
 */
#include <string.h>
#include <MacTypes.h>
#include <Files.h>
#include <Folders.h>
#include <Script.h>

#include "apx_prefs.h"

#define PREFS_MAX   8192

static const unsigned char kPrefsName[] = "\pAirPort Extreme Prefs";

/* The lines the utility does not manage, kept for apx_prefs_save */
static char g_other[PREFS_MAX + 1];
static long g_other_len;

static void set_str(char *dst, unsigned long size, const char *src)
{
    unsigned long n = strlen(src);

    if (n >= size)
        n = size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static OSErr prefs_spec(FSSpec *spec)
{
    short vref;
    long dirid;
    OSErr err = FindFolder(kOnSystemDisk, kPreferencesFolderType, kCreateFolder, &vref, &dirid);

    if (err == noErr)
        err = FSMakeFSSpec(vref, dirid, kPrefsName, spec);
    return err;
}

/* 1 = a line the utility manages (its value goes in p) */
static int managed(apx_prefs *p, const char *key, const char *val)
{
    if (!strcmp(key, "ssid")) {
        set_str(p->ssid, sizeof(p->ssid), val);
        return 1;
    }
    if (!strcmp(key, "password")) {
        set_str(p->password, sizeof(p->password), val);
        return 1;
    }
    if (!strcmp(key, "power")) {
        p->power = val[0] != '0';
        return 1;
    }
    if (!strcmp(key, "remember")) {
        if (val[0] && p->remembered_count < APX_REMEMBER_MAX &&
            !apx_prefs_is_remembered(p, val))
            set_str(p->remembered[p->remembered_count++], 33, val);
        return 1;
    }
    return 0;
}

static void append_other(const char *line, long len)
{
    if (g_other_len + len + 1 > PREFS_MAX)
        return;
    memcpy(g_other + g_other_len, line, len);
    g_other_len += len;
    g_other[g_other_len++] = '\r';
}

/* Same rules as the driver (drv_prefs.c): CR, LF or CRLF; spaces around
 * the key dropped; the value kept as it is */
static void parse(apx_prefs *p, char *text, long len)
{
    char *line = text, *end = text + len;

    while (line < end) {
        char *eol = line, *eq, *k, *kend;
        char saved;
        long line_len;

        while (eol < end && *eol != '\r' && *eol != '\n')
            eol++;
        line_len = eol - line;
        saved = *eol;
        *eol = '\0';
        k = line;
        while (*k == ' ' || *k == '\t')
            k++;
        eq = strchr(k, '=');
        if (*k != '#' && eq) {
            char key[32];
            long klen;

            kend = eq;
            while (kend > k && (kend[-1] == ' ' || kend[-1] == '\t'))
                kend--;
            klen = kend - k;
            if (klen >= (long)sizeof(key))
                klen = sizeof(key) - 1;
            memcpy(key, k, klen);
            key[klen] = '\0';
            if (!managed(p, key, eq + 1))
                append_other(line, line_len);
        } else if (line_len > 0) {
            append_other(line, line_len);
        }
        *eol = saved;
        line = eol + 1;
        if (saved == '\r' && line < end && *line == '\n')
            line++;
    }
}

int apx_prefs_load(apx_prefs *p)
{
    static char text[PREFS_MAX + 1];
    FSSpec spec;
    short ref;
    long len;
    OSErr err;

    memset(p, 0, sizeof(*p));
    p->power = 1;
    g_other_len = 0;
    err = prefs_spec(&spec);
    if (err == noErr)
        err = FSpOpenDF(&spec, fsRdPerm, &ref);
    if (err == fnfErr)
        return 0;                       /* no file yet: defaults */
    if (err != noErr)
        return err;
    err = GetEOF(ref, &len);
    if (err == noErr) {
        if (len > PREFS_MAX)
            len = PREFS_MAX;
        err = FSRead(ref, &len, text);
        if (err == eofErr)
            err = noErr;
    }
    FSClose(ref);
    if (err != noErr)
        return err;
    text[len] = '\0';
    parse(p, text, len);
    memset(text, 0, sizeof(text));      /* it may hold a password */
    return 0;
}

static void put(char *buf, long *len, const char *key, const char *val)
{
    long kl = (long)strlen(key), vl = (long)strlen(val);

    if (*len + kl + vl + 2 > PREFS_MAX)
        return;
    memcpy(buf + *len, key, kl);
    *len += kl;
    buf[(*len)++] = '=';
    memcpy(buf + *len, val, vl);
    *len += vl;
    buf[(*len)++] = '\r';
}

int apx_prefs_save(const apx_prefs *p)
{
    static char text[PREFS_MAX + 1];
    FSSpec spec;
    short ref;
    long len = 0, count;
    int i;
    OSErr err;

    if (p->ssid[0])
        put(text, &len, "ssid", p->ssid);
    if (p->password[0] && !p->drop_password)
        put(text, &len, "password", p->password);
    put(text, &len, "power", p->power ? "1" : "0");
    for (i = 0; i < p->remembered_count; i++)
        put(text, &len, "remember", p->remembered[i]);
    if (len + g_other_len <= PREFS_MAX) {
        memcpy(text + len, g_other, g_other_len);
        len += g_other_len;
    }

    err = prefs_spec(&spec);
    if (err == noErr) {
        err = FSpCreate(&spec, 'ttxt', 'TEXT', smSystemScript);
        if (err == dupFNErr)
            err = noErr;
    }
    if (err == noErr)
        err = FSpOpenDF(&spec, fsRdWrPerm, &ref);
    if (err != noErr) {
        memset(text, 0, sizeof(text));
        return err;
    }
    count = len;
    err = SetEOF(ref, 0);
    if (err == noErr)
        err = FSWrite(ref, &count, text);
    FSClose(ref);
    FlushVol(NULL, spec.vRefNum);
    memset(text, 0, sizeof(text));
    return err;
}

int apx_prefs_is_remembered(const apx_prefs *p, const char *ssid)
{
    int i;

    for (i = 0; i < p->remembered_count; i++)
        if (!strcmp(p->remembered[i], ssid))
            return 1;
    return 0;
}

void apx_prefs_remember(apx_prefs *p, const char *ssid)
{
    char name[33];
    int i;

    set_str(name, sizeof(name), ssid);
    apx_prefs_forget(p, name);
    if (p->remembered_count == APX_REMEMBER_MAX)
        p->remembered_count--;          /* the least preferred one goes */
    for (i = p->remembered_count; i > 0; i--)
        memcpy(p->remembered[i], p->remembered[i - 1], 33);
    memcpy(p->remembered[0], name, 33);
    p->remembered_count++;
}

void apx_prefs_forget(apx_prefs *p, const char *ssid)
{
    int i, j;

    for (i = 0; i < p->remembered_count; i++) {
        if (strcmp(p->remembered[i], ssid))
            continue;
        for (j = i; j < p->remembered_count - 1; j++)
            memcpy(p->remembered[j], p->remembered[j + 1], 33);
        p->remembered_count--;
        return;
    }
}
