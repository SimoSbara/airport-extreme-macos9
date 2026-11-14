/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * drv_prefs.c - Reads "AirPort Extreme Prefs" from the Preferences folder.
 *
 * A text file (SimpleText is fine), one "key=value" setting per line:
 *
 *     ssid=MyNetwork
 *     password=my wpa password        (omit for an open network, or when the
 *                                      utility gives it from the Keychain)
 *     power=1                         (0 = AirPort off at startup)
 *     country=IT                      (regulatory domain, default world)
 *     memory_kb=4096                  (memory pool of the stack)
 *     dma=1                           (0 = PIO transfers, slower)
 *     dma_uncached=0                  (1 = descriptor rings in uncached pages)
 *     irq=1                           (0 = no chip interrupts, poll every 10 ms)
 *     firmware=784.2                  (another firmware version built in, see
 *                                      firmware/README.md; default: the main one)
 *     watchdog=0                      (1 = a timer logs where the stack is when a
 *                                      run does not end within 5 s)
 *     screen_log=0                    (1 = the log is also drawn on the screen:
 *                                      it stays readable when the Mac freezes)
 *     debug=0                         (1 = diagnostics in the log, see below)
 *     verbose=1                       (b43 log level, 0..3)
 *     wpa_debug=3                     (wpa_supplicant: 0 excessive .. 5 error)
 *     pr_debug=0                      (1 = debug messages of mac80211 etc.)
 *
 * debug=1 traces the dialogue with Open Transport, the first frames in both
 * directions, the chip registers and link counters every 10 s and whenever
 * reception stops, and raises verbose to 3 and pr_debug to 1 (unless they
 * are set explicitly). The log is read with AirPortStatus.
 *
 * Lines starting with '#' are comments. Called at task time, when the
 * driver is loaded, because it uses the File Manager.
 */
#include <MacTypes.h>
#include <Files.h>
#include <Folders.h>
#include <string.h>

#include "drv.h"

#define PREFS_MAX   4096

static const unsigned char kPrefsName[] = "\pAirPort Extreme Prefs";

/* Settings given in the file (the debug level only raises the others) */
static int g_set_verbose, g_set_pr_debug;

void drv_prefs_defaults(drv_prefs *p)
{
    memset(p, 0, sizeof(*p));
    p->memory_kb = 4096;
    p->power = 1;
    p->verbose = 1;
    p->wpa_debug = 3;           /* MSG_INFO */
    p->pr_debug = 0;
    p->debug = 0;
    p->dma = 1;
    p->dma_uncached = 0;
    p->irq = 1;
}

static unsigned long parse_ulong(const char *s)
{
    unsigned long v = 0;

    while (*s >= '0' && *s <= '9')
        v = v * 10 + (unsigned long)(*s++ - '0');
    return v;
}

static void set_str(char *dst, unsigned long size, const char *src)
{
    unsigned long n = strlen(src);

    if (n >= size)
        n = size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void apply(drv_prefs *p, const char *key, const char *val)
{
    if (!strcmp(key, "ssid"))
        set_str(p->ssid, sizeof(p->ssid), val);
    else if (!strcmp(key, "password"))
        set_str(p->password, sizeof(p->password), val);
    else if (!strcmp(key, "power"))
        p->power = (int)parse_ulong(val);
    else if (!strcmp(key, "country"))
        set_str(p->country, sizeof(p->country), val);
    else if (!strcmp(key, "memory_kb"))
        p->memory_kb = parse_ulong(val);
    else if (!strcmp(key, "dma"))
        p->dma = (int)parse_ulong(val);
    else if (!strcmp(key, "dma_uncached"))
        p->dma_uncached = (int)parse_ulong(val);
    else if (!strcmp(key, "irq"))
        p->irq = (int)parse_ulong(val);
    else if (!strcmp(key, "firmware"))
        set_str(p->firmware, sizeof(p->firmware), val);
    else if (!strcmp(key, "screen_log"))
        p->screen_log = (int)parse_ulong(val);
    else if (!strcmp(key, "watchdog"))
        p->watchdog = (int)parse_ulong(val);
    else if (!strcmp(key, "debug"))
        p->debug = (int)parse_ulong(val);
    else if (!strcmp(key, "verbose")) {
        p->verbose = (int)parse_ulong(val);
        g_set_verbose = 1;
    } else if (!strcmp(key, "wpa_debug"))
        p->wpa_debug = (int)parse_ulong(val);
    else if (!strcmp(key, "pr_debug")) {
        p->pr_debug = (int)parse_ulong(val);
        g_set_pr_debug = 1;
    }
}

/* Splits the text in lines (CR, LF or CRLF) and the lines in key=value.
 * Spaces around the key are dropped; the value is kept as it is, so that
 * SSIDs and passwords may contain spaces. */
static void parse(drv_prefs *p, char *text, long len)
{
    char *line = text, *end = text + len;

    while (line < end) {
        char *eol = line, *eq, *k, *kend;

        while (eol < end && *eol != '\r' && *eol != '\n')
            eol++;
        *eol = '\0';
        k = line;
        while (*k == ' ' || *k == '\t')
            k++;
        eq = strchr(k, '=');
        if (*k != '#' && eq) {
            kend = eq;
            while (kend > k && (kend[-1] == ' ' || kend[-1] == '\t'))
                kend--;
            *kend = '\0';
            apply(p, k, eq + 1);
        }
        line = eol + 1;
    }
}

int drv_prefs_load(drv_prefs *p)
{
    static char text[PREFS_MAX + 1];
    short vref, ref;
    long dirid, len;
    FSSpec spec;
    OSErr err;

    err = FindFolder(kOnSystemDisk, kPreferencesFolderType, kDontCreateFolder, &vref, &dirid);
    if (err == noErr)
        err = FSMakeFSSpec(vref, dirid, kPrefsName, &spec);
    if (err == noErr)
        err = FSpOpenDF(&spec, fsRdPerm, &ref);
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
    g_set_verbose = g_set_pr_debug = 0;
    parse(p, text, len);
    memset(text, 0, sizeof(text));      /* do not leave the password around */

    /* Diagnostics: b43 debug messages and mac80211 connection messages too */
    if (p->debug) {
        if (!g_set_verbose)
            p->verbose = 3;
        if (!g_set_pr_debug)
            p->pr_debug = 1;
    }
    return 0;
}
