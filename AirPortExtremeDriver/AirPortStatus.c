/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * AirPortStatus.c - Shows the state of the AirPort Extreme driver and
 * saves its log to "AirPort Extreme.log" (next to the application).
 *
 * The driver publishes the address of its status block (apx_status.h) in
 * the Name Registry node of the card.
 */
#include <stdio.h>
#include <string.h>
#include <Types.h>
#include <NameRegistry.h>

#include "apx_status.h"

static const char *state_name(unsigned long s)
{
    switch (s) {
    case APX_LINK_DOWN:         return "down (no Open Transport client)";
    case APX_LINK_IDLE:         return "idle (see the log)";
    case APX_LINK_SCANNING:     return "scanning";
    case APX_LINK_CONNECTING:   return "connecting";
    case APX_LINK_HANDSHAKE:    return "WPA handshake";
    case APX_LINK_CONNECTED:    return "connected";
    case APX_LINK_RETRY:        return "waiting to retry";
    }
    return "?";
}

static void print_mac(const unsigned char *m)
{
    printf("%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

/* Copies the ring buffer in order, oldest byte first */
static unsigned long save_log(const apx_status *st)
{
    unsigned long head = st->log_head, start, n;
    FILE *f;

    if (!st->log_buf || !st->log_size)
        return 0;
    start = head > st->log_size ? head - st->log_size : 0;
    f = fopen("AirPort Extreme.log", "w");
    if (!f)
        return 0;
    for (n = start; n < head; n++) {
        char c = st->log_buf[n % st->log_size];
        fputc(c == '\n' ? '\r' : c, f);         /* Mac text file */
    }
    fclose(f);
    return head - start;
}

int main(void)
{
    RegEntryIter cookie;
    RegEntryID entry;
    Boolean done = false;
    apx_status *st = NULL;
    RegPropertyValueSize size = sizeof(st);
    OSStatus err;
    unsigned long saved;

    printf("AirPort Extreme status\n\n");

    RegistryEntryIterateCreate(&cookie);
    err = RegistryEntrySearch(&cookie, kRegIterContinue, &entry, &done,
                              "name", "pci80211", 9);
    RegistryEntryIterateDispose(&cookie);
    if (err != noErr || done) {
        printf("No AirPort Extreme card in the Name Registry.\n");
        return 0;
    }
    err = RegistryPropertyGet(&entry, APX_STATUS_PROPERTY, &st, &size);
    RegistryEntryIDDispose(&entry);
    if (err != noErr || !st) {
        printf("The driver is not loaded (no \"%s\" property).\n"
               "It is loaded when a TCP/IP or AppleTalk configuration uses the card.\n",
               APX_STATUS_PROPERTY);
        return 0;
    }
    if (st->magic != APX_STATUS_MAGIC || st->version != APX_STATUS_VERSION) {
        printf("Unknown status block (magic %08lX, version %lu).\n", st->magic, st->version);
        return 0;
    }

    printf("State      %s\n", state_name(st->link_state));
    printf("Network    \"%s\"\n", st->ssid);
    printf("MAC        ");
    print_mac(st->mac);
    printf("\nAP         ");
    print_mac(st->bssid);
    printf(", channel %u, %ld dBm\n", st->channel, st->signal_dbm);
    printf("Last code  %u, connections %lu\n", st->last_reason, st->connects);
    printf("TX         %lu frames, %lu bytes, %lu dropped\n", st->tx_frames, st->tx_bytes, st->tx_drops);
    printf("RX         %lu frames, %lu bytes, %lu dropped\n", st->rx_frames, st->rx_bytes, st->rx_drops);
    printf("Memory     %lu KB used of %lu KB, peak %lu KB, %lu failures, %lu bad frees\n",
           st->mem_used / 1024, st->mem_size / 1024, st->mem_peak / 1024, st->mem_failures,
           st->mem_bad_frees);
    printf("Stack      peak %lu KB of %lu KB\n", st->stack_peak / 1024, st->stack_size / 1024);

    saved = save_log(st);
    printf("\nLog: %lu bytes saved to \"AirPort Extreme.log\".\n", saved);
    return 0;
}
