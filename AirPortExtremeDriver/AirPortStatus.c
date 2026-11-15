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
#include <Gestalt.h>

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

/* -------------------------------------------------------------------------
 * Problem report: everything useful to understand a problem, without the
 * network names and full addresses (it is meant to be posted publicly)
 * ------------------------------------------------------------------------- */
#define REPORT_NAME     "AirPort Extreme Report.txt"
#define MAX_NAMES       (APX_SCAN_MAX + 1)

static char g_names[MAX_NAMES][33];     /* network names to hide */
static int  g_name_count;

static void add_name(const char *s)
{
    int i;

    if (strlen(s) < 2 || g_name_count == MAX_NAMES)
        return;
    for (i = 0; i < g_name_count; i++)
        if (!strcmp(g_names[i], s))
            return;
    strncpy(g_names[g_name_count], s, 32);
    g_names[g_name_count][32] = '\0';
    g_name_count++;
}

/* A MAC address with only the manufacturer's part (first 3 bytes) */
static void print_mac_private(FILE *f, const unsigned char *m)
{
    fprintf(f, "%02X:%02X:%02X:xx:xx:xx", m[0], m[1], m[2]);
}

static const char *name_of(const char *ssid, int private)
{
    static char buf[24];
    int i;

    if (!private)
        return ssid;
    for (i = 0; i < g_name_count; i++)
        if (!strcmp(g_names[i], ssid)) {
            snprintf(buf, sizeof(buf), "<network %d>", i + 1);
            return buf;
        }
    return ssid[0] ? "<network>" : "";
}

static void print_status(FILE *f, apx_status *st, int private)
{
    fprintf(f, "Driver     %s, country %s\n", st->driver_version, st->country);
    if (st->driver_error)
        fprintf(f, "Startup    FAILED (%s)\n", st->driver_error == APX_DRV_NO_FIRMWARE
                ? "firmware missing" : "see the log");
    fprintf(f, "Firmware   %s", st->firmware);
    if (st->hw_valid && st->hw.fw_rev)
        fprintf(f, ", revision %u.%u", st->hw.fw_rev, st->hw.fw_patch);
    fprintf(f, "\nState      %s, AirPort %s\n", state_name(st->link_state),
            st->power ? "on" : "off");
    fprintf(f, "Problem    %s\n", error_name(st->link_error));
    fprintf(f, "Network    \"%s\", %s\n", name_of(st->ssid, private), security_name(st->security));
    fprintf(f, "MAC        ");
    if (private)
        print_mac_private(f, st->mac);
    else
        fprintf(f, "%02X:%02X:%02X:%02X:%02X:%02X", st->mac[0], st->mac[1], st->mac[2],
                st->mac[3], st->mac[4], st->mac[5]);
    fprintf(f, "\nAP         ");
    if (private)
        print_mac_private(f, st->bssid);
    else
        fprintf(f, "%02X:%02X:%02X:%02X:%02X:%02X", st->bssid[0], st->bssid[1], st->bssid[2],
                st->bssid[3], st->bssid[4], st->bssid[5]);
    fprintf(f, ", channel %u, %ld dBm (average %ld)\n", st->channel, st->signal_dbm,
            st->signal_avg_dbm);
    if (st->link_state == APX_LINK_CONNECTED)
        fprintf(f, "Rates      send %lu.%lu Mbit/s, receive %lu.%lu Mbit/s, connected for %lu s\n",
                st->tx_rate / 10, st->tx_rate % 10, st->rx_rate / 10, st->rx_rate % 10,
                st->link_seconds);
    fprintf(f, "Last code  %u, connections %lu\n", st->last_reason, st->connects);
    fprintf(f, "TX         %lu frames, %lu bytes, %lu dropped\n", st->tx_frames, st->tx_bytes,
            st->tx_drops);
    fprintf(f, "RX         %lu frames, %lu bytes, %lu dropped\n", st->rx_frames, st->rx_bytes,
            st->rx_drops);
    fprintf(f, "Memory     %lu KB used of %lu KB, peak %lu KB, %lu failures, %lu bad frees\n",
            st->mem_used / 1024, st->mem_size / 1024, st->mem_peak / 1024, st->mem_failures,
            st->mem_bad_frees);
    fprintf(f, "Stack      peak %lu KB of %lu KB\n", st->stack_peak / 1024, st->stack_size / 1024);
}

static void print_mac_info(FILE *f)
{
    long resp;
    StringPtr name;

    if (Gestalt('mnam', &resp) == noErr && resp) {
        name = (StringPtr)resp;
        fprintf(f, "Mac        %.*s", name[0], (const char *)name + 1);
        if (Gestalt(gestaltMachineType, &resp) == noErr)
            fprintf(f, " (machine type %ld)", resp);
        fprintf(f, "\n");
    } else if (Gestalt(gestaltMachineType, &resp) == noErr) {
        fprintf(f, "Mac        machine type %ld\n", resp);
    }
    if (Gestalt(gestaltSystemVersion, &resp) == noErr)
        fprintf(f, "Mac OS     %lx.%lx.%lx\n", (resp >> 8) & 0xFF, (resp >> 4) & 0xF, resp & 0xF);
    if (Gestalt('otvr', &resp) == noErr)
        fprintf(f, "Open Transport %lx.%lx.%lx\n", (resp >> 24) & 0xFF, (resp >> 20) & 0xF,
                (resp >> 16) & 0xF);
    if (Gestalt(gestaltPhysicalRAMSize, &resp) == noErr)
        fprintf(f, "Memory     %ld MB\n", resp / (1024L * 1024L));
}

static void print_hw(FILE *f, const apx_status *st)
{
    const apx_hw *h = &st->hw;

    if (!st->hw_valid) {
        fprintf(f, "Card       not known (the stack did not start)\n");
        return;
    }
    fprintf(f, "Chip       %04X rev %u package %u, 802.11 core rev %u\n", h->chip_id, h->chip_rev,
            h->chip_pkg, h->core_rev);
    fprintf(f, "Board      vendor %04X type %04X rev %02X, SPROM rev %u, flags %04X %04X\n",
            h->board_vendor, h->board_type, h->board_rev, h->sprom_rev, h->boardflags_lo,
            h->boardflags_hi);
    fprintf(f, "PHY        type %u rev %u, radio %04X rev %u (manufacturer %03X)\n", h->phy_type,
            h->phy_rev, h->radio_ver, h->radio_rev, h->radio_manuf);
}

static int is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* The log with the network names replaced and the addresses cut: names
 * first (they could contain anything), then xx:xx:xx:xx:xx:xx patterns */
static void write_private_log(FILE *f, const char *log, unsigned long len)
{
    unsigned long i = 0;
    int k;

    while (i < len) {
        int done = 0;

        for (k = 0; k < g_name_count && !done; k++) {
            unsigned long n = strlen(g_names[k]);

            if (i + n <= len && !memcmp(log + i, g_names[k], n)) {
                fprintf(f, "<network %d>", k + 1);
                i += n;
                done = 1;
            }
        }
        if (done)
            continue;
        if (i + 17 <= len) {
            int mac = 1, j;

            for (j = 0; j < 17 && mac; j++)
                mac = (j % 3 == 2) ? log[i + j] == ':' : is_hex(log[i + j]);
            if (mac) {
                fwrite(log + i, 1, 9, f);           /* the manufacturer: "2c:91:ab:" */
                fputs("xx:xx:xx", f);
                i += 17;
                continue;
            }
        }
        fputc(log[i], f);
        i++;
    }
}

/* Mac text file: CR at the end of the lines */
static void to_mac_lines(const char *name)
{
    static char buf[160 * 1024];
    size_t n, i;
    FILE *f = fopen(name, "rb");

    if (!f)
        return;
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    for (i = 0; i < n; i++)
        if (buf[i] == '\n')
            buf[i] = '\r';
    f = fopen(name, "wb");
    if (!f)
        return;
    fwrite(buf, 1, n, f);
    fclose(f);
}

static int save_report(apx_status *st)
{
    static apx_network nets[APX_SCAN_MAX];
    static char log[128 * 1024];
    unsigned long head = st->log_head, start, n, len = 0, count, i;
    FILE *f;

    /* The names to hide: the configured network and every one in range,
     * longest first so that a name inside another one is not cut */
    g_name_count = 0;
    add_name(st->ssid);
    count = copy_scan(st, nets);
    for (i = 0; i < count; i++)
        add_name(nets[i].ssid);
    {
        int a, b;

        for (a = 0; a < g_name_count; a++)
            for (b = a + 1; b < g_name_count; b++)
                if (strlen(g_names[b]) > strlen(g_names[a])) {
                    char t[33];

                    strcpy(t, g_names[a]);
                    strcpy(g_names[a], g_names[b]);
                    strcpy(g_names[b], t);
                }
    }

    f = fopen(REPORT_NAME, "w");
    if (!f)
        return 0;
    fprintf(f, "AirPort Extreme problem report\n\n");
    print_mac_info(f);
    fprintf(f, "\n");
    print_status(f, st, 1);
    print_hw(f, st);
    fprintf(f, "\nNetworks in range: %lu\n", count);
    for (i = 0; i < count; i++)
        fprintf(f, "  %-14s channel %2u  %4ld dBm  %s\n", name_of(nets[i].ssid, 1),
                nets[i].channel, nets[i].signal_dbm, security_name(nets[i].security));

    if (st->log_buf && st->log_size) {
        start = head > st->log_size ? head - st->log_size : 0;
        for (n = start; n < head && len < sizeof(log); n++)
            log[len++] = st->log_buf[n % st->log_size];
        fprintf(f, "\nLog (%lu bytes)\n", len);
        write_private_log(f, log, len);
    }
    fclose(f);
    to_mac_lines(REPORT_NAME);
    return 1;
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

    print_status(stdout, st, 0);
    saved = save_log(st);
    printf("\nLog: %lu bytes saved to \"AirPort Extreme.log\".\n", saved);
    if (save_report(st))
        printf("Report saved to \"" REPORT_NAME "\": attach it to a problem report\n"
               "(network names and addresses are left out of it).\n\n");
    else
        printf("The report could not be saved.\n\n");

    commands(st);
    return 0;
}
