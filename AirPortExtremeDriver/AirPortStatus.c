/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * AirPortStatus.c - Shows the state of the AirPort Extreme driver and
 * saves its log to "AirPort Extreme.log" (next to the application).
 *
 * The driver publishes the address of its status block (apx_status.h) in
 * the Name Registry node of the card.
 *
 * It also tries the command interface of the driver: it asks for a scan,
 * lists the networks found and can send a connect command (a test of what
 * the utility does).
 */
#include <stdio.h>
#include <string.h>
#include <Types.h>
#include <Events.h>
#include <NameRegistry.h>

#include "apx_status.h"

#define CMD_TIMEOUT_TICKS   60          /* 1 s: the driver ticks every 10 ms */
#define SCAN_TIMEOUT_TICKS  (15 * 60)

/* Sends a command (apx_status.h). 1 = taken, result in *result. */
static int send_cmd(apx_status *st, unsigned long cmd, unsigned long arg, const char *ssid,
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

/* Consistent copy of the scan list (see apx_status.h) */
static unsigned long copy_scan(apx_status *st, apx_network *out)
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

static const char *security_name(unsigned char s)
{
    switch (s) {
    case APX_SEC_OPEN:  return "open";
    case APX_SEC_WEP:   return "WEP";
    case APX_SEC_WPA:   return "WPA";
    case APX_SEC_WPA2:  return "WPA2";
    }
    return "?";
}

/* Reads a line from the console, without the end of line */
static void read_line(char *buf, int size)
{
    char *p;

    buf[0] = '\0';
    if (!fgets(buf, size, stdin))
        buf[0] = '\0';
    for (p = buf; *p; p++)
        if (*p == '\r' || *p == '\n') {
            *p = '\0';
            break;
        }
}

/* 1 = the scan list was printed */
static int scan(apx_status *st)
{
    static apx_network nets[APX_SCAN_MAX];
    unsigned long seq, n, i, t0;
    long result;

    seq = st->scan_seq;
    if (!send_cmd(st, APX_CMD_SCAN, 0, NULL, NULL, &result)) {
        printf("The driver does not answer (Open Transport has not loaded it).\n");
        return 0;
    }
    if (result == APX_ERR_NETDOWN) {
        printf("Scan: the interface is down (AirPort off, or no TCP/IP or AppleTalk"
               " on the card).\n");
        return 0;
    }
    if (result) {
        printf("Scan: error %ld\n", result);
        return 0;
    }
    printf("Scanning...\n");
    t0 = TickCount();
    while (st->scan_seq == seq || (st->scan_seq & 1))
        if (TickCount() - t0 > SCAN_TIMEOUT_TICKS) {
            printf("No scan results within 15 s.\n");
            return 0;
        }
    n = copy_scan(st, nets);
    printf("%lu access points:\n", n);
    for (i = 0; i < n; i++)
        printf("  %-32s %02X:%02X:%02X:%02X:%02X:%02X  ch %2u  %4ld dBm  %s\n",
               nets[i].ssid[0] ? nets[i].ssid : "(hidden)", nets[i].bssid[0],
               nets[i].bssid[1], nets[i].bssid[2], nets[i].bssid[3], nets[i].bssid[4],
               nets[i].bssid[5], nets[i].channel, nets[i].signal_dbm,
               security_name(nets[i].security));
    return 1;
}

static void report(const char *what, int answered, long result)
{
    if (!answered)
        printf("%s: the driver does not answer.\n", what);
    else if (result)
        printf("%s: error %ld\n", what, result);
    else
        printf("%s: done. Run AirPortStatus again in a few seconds to see the state.\n", what);
}

/* A test of every command of the driver */
static void commands(apx_status *st)
{
    static char line[64], password[80];
    long result = 0;
    int ok;

    (void)scan(st);
    printf("\nNetwork to join, or /off, /on, /disconnect (Return = nothing): ");
    fflush(stdout);
    read_line(line, sizeof(line));
    if (!line[0])
        return;
    if (!strcmp(line, "/off") || !strcmp(line, "/on")) {
        ok = send_cmd(st, APX_CMD_POWER, line[2] == 'n' ? 1UL : 0UL, NULL, NULL, &result);
        report(line[2] == 'n' ? "AirPort on" : "AirPort off", ok, result);
        return;
    }
    if (!strcmp(line, "/disconnect")) {
        ok = send_cmd(st, APX_CMD_DISCONNECT, 0, NULL, NULL, &result);
        report("Disconnect", ok, result);
        return;
    }
    printf("Password (Return = open network; it is shown as you type): ");
    fflush(stdout);
    read_line(password, sizeof(password));
    ok = send_cmd(st, APX_CMD_CONNECT, 0, line, password, &result);
    memset(password, 0, sizeof(password));
    if (ok && result == APX_ERR_INVAL)
        printf("Connect: the network name must have 1..32 characters, the password 8..63.\n");
    else
        report("Connect", ok, result);
}

static const char *error_name(unsigned long e)
{
    switch (e) {
    case APX_FAIL_NONE:             return "none";
    case APX_FAIL_NOT_FOUND:        return "network not found";
    case APX_FAIL_NEED_PASSWORD:    return "the network needs a password";
    case APX_FAIL_UNSUPPORTED:      return "security not supported (WEP or 802.1X)";
    case APX_FAIL_REFUSED:          return "refused by the access point";
    case APX_FAIL_NO_ANSWER:        return "no answer from the access point";
    case APX_FAIL_HANDSHAKE:        return "WPA handshake failed (wrong password?)";
    case APX_FAIL_LOST:             return "connection lost";
    case APX_FAIL_ERROR:            return "internal error (see the log)";
    }
    return "?";
}

static const char *state_name(unsigned long s)
{
    switch (s) {
    case APX_LINK_DOWN:         return "down (no Open Transport client)";
    case APX_LINK_IDLE:         return "idle";
    case APX_LINK_OFF:          return "AirPort off";
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

    printf("Driver     %s, country %s\n", st->driver_version, st->country);
    printf("State      %s\n", state_name(st->link_state));
    printf("Problem    %s\n", error_name(st->link_error));
    printf("Network    \"%s\", %s\n", st->ssid, security_name(st->security));
    printf("MAC        ");
    print_mac(st->mac);
    printf("\nAP         ");
    print_mac(st->bssid);
    printf(", channel %u, %ld dBm (average %ld)\n", st->channel, st->signal_dbm,
           st->signal_avg_dbm);
    if (st->link_state == APX_LINK_CONNECTED)
        printf("Rates      send %lu.%lu Mbit/s, receive %lu.%lu Mbit/s, connected for %lu s\n",
               st->tx_rate / 10, st->tx_rate % 10, st->rx_rate / 10, st->rx_rate % 10,
               st->link_seconds);
    printf("Last code  %u, connections %lu\n", st->last_reason, st->connects);
    printf("TX         %lu frames, %lu bytes, %lu dropped\n", st->tx_frames, st->tx_bytes, st->tx_drops);
    printf("RX         %lu frames, %lu bytes, %lu dropped\n", st->rx_frames, st->rx_bytes, st->rx_drops);
    printf("Memory     %lu KB used of %lu KB, peak %lu KB, %lu failures, %lu bad frees\n",
           st->mem_used / 1024, st->mem_size / 1024, st->mem_peak / 1024, st->mem_failures,
           st->mem_bad_frees);
    printf("Stack      peak %lu KB of %lu KB\n", st->stack_peak / 1024, st->stack_size / 1024);

    saved = save_log(st);
    printf("\nLog: %lu bytes saved to \"AirPort Extreme.log\".\n\n", saved);

    commands(st);
    return 0;
}
