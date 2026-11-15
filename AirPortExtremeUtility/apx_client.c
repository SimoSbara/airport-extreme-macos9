/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * apx_client.c - The application side of the driver's status block.
 */
#include <string.h>
#include <MacTypes.h>
#include <Events.h>
#include <NameRegistry.h>

#include "apx_client.h"

#define CMD_TIMEOUT_TICKS   60          /* 1 s: the driver ticks every 10 ms */

apx_status *apx_find(void)
{
    RegEntryIter cookie;
    RegEntryID entry;
    Boolean done = false;
    apx_status *st = NULL;
    RegPropertyValueSize size = sizeof(st);
    OSStatus err;

    if (RegistryEntryIterateCreate(&cookie) != noErr)
        return NULL;
    err = RegistryEntrySearch(&cookie, kRegIterContinue, &entry, &done, "name", "pci80211", 9);
    RegistryEntryIterateDispose(&cookie);
    if (err != noErr || done)
        return NULL;
    err = RegistryPropertyGet(&entry, APX_STATUS_PROPERTY, &st, &size);
    RegistryEntryIDDispose(&entry);
    if (err != noErr || !st)
        return NULL;
    if (st->magic != APX_STATUS_MAGIC || st->version != APX_STATUS_VERSION)
        return NULL;
    return st;
}

int apx_send(apx_status *st, unsigned long cmd, unsigned long arg, const char *ssid,
             const char *password, long *result)
{
    unsigned long t0 = TickCount();

    while (st->cmd_done != st->cmd_seq)
        if (TickCount() - t0 > CMD_TIMEOUT_TICKS)
            return 0;
    st->cmd = cmd;
    st->cmd_arg = arg;
    strncpy(st->cmd_ssid, ssid ? ssid : "", sizeof(st->cmd_ssid) - 1);
    st->cmd_ssid[sizeof(st->cmd_ssid) - 1] = '\0';
    strncpy(st->cmd_password, password ? password : "", sizeof(st->cmd_password) - 1);
    st->cmd_password[sizeof(st->cmd_password) - 1] = '\0';
    APX_BARRIER();
    st->cmd_seq = st->cmd_seq + 1;
    t0 = TickCount();
    while (st->cmd_done != st->cmd_seq)
        if (TickCount() - t0 > CMD_TIMEOUT_TICKS)
            return 0;
    APX_BARRIER();
    *result = st->cmd_result;
    return 1;
}

unsigned long apx_copy_scan(apx_status *st, apx_network *out)
{
    unsigned long seq, n;

    do {
        while ((seq = st->scan_seq) & 1) {
        }
        APX_BARRIER();
        n = st->scan_count;
        if (n > APX_SCAN_MAX)
            n = APX_SCAN_MAX;
        memcpy(out, st->scan, n * sizeof(*out));
        APX_BARRIER();
    } while (st->scan_seq != seq);
    return n;
}

unsigned long apx_networks(apx_status *st, apx_network *out)
{
    static apx_network all[APX_SCAN_MAX];
    unsigned long n = apx_copy_scan(st, all), count = 0, i, j;

    for (i = 0; i < n; i++) {
        if (!all[i].ssid[0])
            continue;
        for (j = 0; j < count; j++)
            if (!strcmp(out[j].ssid, all[i].ssid))
                break;
        if (j < count) {
            if (all[i].signal_dbm > out[j].signal_dbm)
                out[j] = all[i];
        } else {
            out[count++] = all[i];
        }
    }
    /* Strongest first (insertion sort: 32 entries at most) */
    for (i = 1; i < count; i++) {
        apx_network t = out[i];

        for (j = i; j > 0 && out[j - 1].signal_dbm < t.signal_dbm; j--)
            out[j] = out[j - 1];
        out[j] = t;
    }
    return count;
}

int apx_bars(long dbm)
{
    if (dbm == 0)
        return 0;                       /* unknown */
    if (dbm >= -55)
        return 4;
    if (dbm >= -65)
        return 3;
    if (dbm >= -75)
        return 2;
    if (dbm >= -85)
        return 1;
    return 0;
}
