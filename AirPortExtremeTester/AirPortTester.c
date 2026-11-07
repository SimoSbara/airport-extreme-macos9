/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * AirPortTester.c - Test bench for the AirPort Extreme driver on the iBook G4.
 *
 * Only the Mac OS-specific parts live here: finding the card in the Name
 * Registry, PCI configuration, clock, memory and console. Underneath is the
 * Linux 5.15 wireless stack (b43 + ssb + mac80211 + cfg80211), used
 * through b43os9.h the way "iw" and wpa_supplicant would use it.
 *
 * Sequence:
 *   1. startup   cfg80211, mac80211, ssb, b43, firmware -> wlan0 interface
 *   2. up        "ip link set wlan0 up" (b43_op_start, calibrations)
 *   3. scan      "iw wlan0 scan" (cfg80211 + mac80211)
 *   4. connect   open network: "iw wlan0 connect" (cfg80211 SME, mac80211
 *                MLME); WPA/WPA2-PSK network: wpa_supplicant 2.11 (4-way
 *                handshake, keys in the chip, port authorized)
 *   5. data      listen for Ethernet frames
 *   6. DHCP      Discover -> Offer (also encrypted, on WPA2)
 *   7. shutdown  disconnect + down
 *
 * The previous version (b43 driven by hand) is in Old/AirPortTester_v2.c.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <Types.h>
#include <Memory.h>
#include <Errors.h>
#include <Timer.h>
#include <NameRegistry.h>
#include <PCI.h>
#include <Files.h>

#include "AirPortTester.h"
#include "b43os9.h"
#include "b43fw.h"

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;
#ifndef bool                /* stdbool.h (Retro68) already defines it */
typedef unsigned char  bool;
#endif
#ifndef true
#define true  1
#define false 0
#endif

#ifndef UNIVERSAL_INTERFACES_VERSION
/*
 * Multiversal Interfaces (Retro68): these functions exist in InterfaceLib
 * and NameRegistryLib but the headers do not declare them. Apple's signatures
 * (Timer.h, NameRegistry.h from Universal Interfaces 3.4).
 */
typedef struct UnsignedWide { UInt32 hi; UInt32 lo; } UnsignedWide;
pascal void Microseconds(UnsignedWide *microTickCount);
pascal OSStatus RegistryEntrySearch(RegEntryIter *cookie, RegEntryIterationOp relationship,
                                    RegEntryID *foundEntry, Boolean *done,
                                    const RegPropertyName *propertyName,
                                    const void *propertyValue,
                                    RegPropertyValueSize propertySize);
#endif

#define ETH_P_IP    0x0800
#define ETH_P_ARP   0x0806
#define ETH_P_PAE   0x888E

/* =========================================================================
 * Global state
 * ========================================================================= */
typedef struct {
    RegEntryID   entry;
    volatile u8 *mmio;
} TesterCtx;

static TesterCtx  g_ctx;

/* Copy of the log on disk ("AirPortTester.log" next to the application),
 * written line by line: stays readable even after a crash */
static FILE      *g_logf;
static b43_env    g_env;
static b43os9    *g_b43;
static u8         g_mac[6];

/* Network and password: asked at startup (defaults in AirPortTester.h) */
static char       g_ssid[33];
static char       g_pass[64];

/* Events */
static bool g_scan_done, g_scan_aborted;
static bool g_connect_done;
static int  g_connect_status;
static u8   g_connect_bssid[6];
static bool g_disconnected;
static u16  g_disconnect_reason;
static bool g_authorized;

/* Reception */
static u32  g_rx_frames, g_rx_eapol, g_rx_ip, g_rx_arp, g_rx_other;
static u32  g_dhcp_xid;
static bool g_dhcp_offer;

/* Milestones reached, summarized at the end */
static bool g_ok_init, g_ok_up, g_ok_scan, g_ok_assoc, g_ok_wpa, g_ok_rxdata, g_ok_dhcp;

/* wpa_supplicant log level (src/utils/wpa_debug.c) */
extern int wpa_debug_level;

/* =========================================================================
 * Environment for Common/ (b43env.h)
 * ========================================================================= */
static u32 now_us(void)
{
    UnsignedWide t;
    Microseconds(&t);
    return t.lo;
}

static unsigned long tester_time_us(void *ctx)
{
    (void)ctx;
    return now_us();
}

static void tester_delay_us(void *ctx, unsigned long us)
{
    u32 start = now_us();
    (void)ctx;
    while ((u32)(now_us() - start) < us) {
    }
}

/* Write to disk immediately: file and volume (the File Manager cache
 * would be lost in a crash) */
static void log_flush(void)
{
    if (g_logf) {
        fflush(g_logf);
        FlushVol(NULL, 0);
    }
}

static void tester_log(void *ctx, const char *text)
{
    (void)ctx;
    fputs(text, stdout);
    if (g_logf) {
        fputs(text, g_logf);
        log_flush();
    }
}

/* Tester printf: also goes to the log file */
static void tprintf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    tester_log(NULL, buf);
}

static void *tester_alloc(void *ctx, unsigned long size)
{
    (void)ctx;
    return malloc(size);
}

static void tester_free(void *ctx, void *p)
{
    (void)ctx;
    free(p);
}

static unsigned long tester_cfg_read32(void *ctx, unsigned short offset)
{
    UInt32 v = 0;
    (void)ctx;
    ExpMgrConfigReadLong(&g_ctx.entry, (LogicalAddress)(unsigned long)offset, &v);
    return v;
}

static void tester_cfg_write32(void *ctx, unsigned short offset, unsigned long value)
{
    (void)ctx;
    ExpMgrConfigWriteLong(&g_ctx.entry, (LogicalAddress)(unsigned long)offset, value);
}

/* =========================================================================
 * Utilities
 * ========================================================================= */
static void print_mac(const u8 *m)
{
    tprintf("%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

/* Run the stack for ms milliseconds, or until *flag becomes true */
static void run_for(u32 ms, const bool *flag)
{
    u32 start = now_us();
    while ((u32)(now_us() - start) < ms * 1000UL) {
        b43os9_poll(g_b43);
        if (flag && *flag)
            break;
    }
}

/* Find an information element in a list of IEs */
static const u8 *find_ie(const u8 *ies, unsigned int len, u8 id)
{
    while (len >= 2 && (unsigned int)ies[1] + 2 <= len) {
        if (ies[0] == id)
            return ies;
        len -= (unsigned int)ies[1] + 2;
        ies += ies[1] + 2;
    }
    return NULL;
}

/* Ask for a line on the console; empty line = default value */
static void ask_line(const char *question, const char *def, int show_def,
                     char *buf, size_t size)
{
    size_t n;

    if (show_def)
        tprintf("%s [%s]: ", question, def);
    else
        tprintf("%s%s: ", question, def[0] ? " [Enter = the one from AirPortTester.h]" : "");
    fflush(stdout);
    if (!fgets(buf, (int)size, stdin))
        buf[0] = '\0';
    n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = '\0';
    if (!n) {
        strncpy(buf, def, size - 1);
        buf[size - 1] = '\0';
    }
}

/* =========================================================================
 * DHCP Discover (open or WPA network)
 * ========================================================================= */
static u16 ip_checksum(const u8 *p, u32 len)
{
    u32 sum = 0;
    u32 i;

    for (i = 0; i + 1 < len; i += 2)
        sum += ((u32)p[i] << 8) | p[i + 1];
    if (len & 1)
        sum += (u32)p[len - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (u16)~sum;
}

/* Ethernet II frame + IPv4 + UDP + BOOTP/DHCPDISCOVER. Returns the length. */
static u32 build_dhcp_discover(u8 *f, const u8 *mac, u32 xid)
{
    const u32 bootp_len = 300;
    const u32 udp_len = 8 + bootp_len;
    const u32 ip_len = 20 + udp_len;
    u8 *ip = f + 14, *udp = ip + 20, *bootp = ip + 28, *opt;
    u16 csum;

    memset(f, 0, 14 + ip_len);
    memset(f, 0xFF, 6);                 /* broadcast destination */
    memcpy(f + 6, mac, 6);
    f[12] = 0x08; f[13] = 0x00;         /* IPv4 */

    ip[0] = 0x45;                       /* IPv4, IHL 5 */
    ip[2] = (u8)(ip_len >> 8); ip[3] = (u8)ip_len;
    ip[8] = 64;                         /* TTL */
    ip[9] = 17;                         /* UDP */
    memset(ip + 16, 0xFF, 4);           /* dst 255.255.255.255, src 0.0.0.0 */
    csum = ip_checksum(ip, 20);
    ip[10] = (u8)(csum >> 8); ip[11] = (u8)csum;

    udp[0] = 0; udp[1] = 68;            /* sport 68 */
    udp[2] = 0; udp[3] = 67;            /* dport 67 */
    udp[4] = (u8)(udp_len >> 8); udp[5] = (u8)udp_len;
    bootp[0] = 1;                       /* BOOTREQUEST */
    bootp[1] = 1;                       /* Ethernet */
    bootp[2] = 6;
    bootp[4] = (u8)(xid >> 24); bootp[5] = (u8)(xid >> 16);
    bootp[6] = (u8)(xid >> 8);  bootp[7] = (u8)xid;
    bootp[10] = 0x80;                   /* broadcast reply */
    memcpy(bootp + 28, mac, 6);         /* chaddr */
    bootp[236] = 0x63; bootp[237] = 0x82; bootp[238] = 0x53; bootp[239] = 0x63;
    opt = bootp + 240;
    *opt++ = 53; *opt++ = 1; *opt++ = 1;                    /* DHCPDISCOVER */
    *opt++ = 61; *opt++ = 7; *opt++ = 1;                    /* client id */
    memcpy(opt, mac, 6); opt += 6;
    *opt++ = 55; *opt++ = 4; *opt++ = 1; *opt++ = 3; *opt++ = 6; *opt++ = 15;
    *opt++ = 255;
    return 14 + ip_len;
}

/* DHCPOFFER for our transaction? (Ethernet II frame) */
static void check_dhcp_offer(const u8 *f, u32 len)
{
    const u8 *ip = f + 14, *bootp, *opt, *end = f + len;
    u32 ihl;

    if (!g_dhcp_xid || len < 14 + 20 + 8 + 240)
        return;
    ihl = (u32)(ip[0] & 0x0F) * 4;
    if (ip[9] != 17 || len < 14 + ihl + 8 + 240)
        return;
    if (ip[ihl + 2] != 0 || ip[ihl + 3] != 68)              /* dport 68 */
        return;
    bootp = ip + ihl + 8;
    if (bootp[0] != 2 || bootp[4] != (u8)(g_dhcp_xid >> 24) ||
        bootp[5] != (u8)(g_dhcp_xid >> 16) || bootp[6] != (u8)(g_dhcp_xid >> 8) ||
        bootp[7] != (u8)g_dhcp_xid)
        return;
    for (opt = bootp + 240; opt + 2 <= end && opt[0] != 255; ) {
        if (opt[0] == 0) {
            opt++;
            continue;
        }
        if (opt[0] == 53 && opt[1] == 1 && opt[2] == 2) {
            tprintf(" [RX] DHCPOFFER: offered address %u.%u.%u.%u from server %u.%u.%u.%u\n",
                   bootp[16], bootp[17], bootp[18], bootp[19],
                   ip[12], ip[13], ip[14], ip[15]);
            g_dhcp_offer = true;
            return;
        }
        opt += 2 + opt[1];
    }
}

/* =========================================================================
 * Events from the stack (b43os9_events)
 * ========================================================================= */
static void ev_scan_done(void *ctx, int aborted)
{
    (void)ctx;
    g_scan_done = true;
    g_scan_aborted = aborted ? true : false;
}

static void ev_connect_result(void *ctx, int status, const unsigned char *bssid)
{
    (void)ctx;
    g_connect_done = true;
    g_connect_status = status;
    if (bssid)
        memcpy(g_connect_bssid, bssid, 6);
}

static void ev_disconnected(void *ctx, unsigned short reason, int from_ap)
{
    (void)ctx;
    g_disconnected = true;
    g_disconnect_reason = reason;
    tprintf(" [EV] Disconnected: reason %u (%s)\n", reason, from_ap ? "by the AP" : "local");
}

static void ev_rx(void *ctx, const unsigned char *frame, unsigned long len)
{
    u16 type;

    (void)ctx;
    if (len < 14)
        return;
    g_rx_frames++;
    type = (u16)((frame[12] << 8) | frame[13]);
    if (type == ETH_P_PAE) {
        g_rx_eapol++;
        tprintf(" [RX] EAPOL from ");
        print_mac(frame + 6);
        tprintf(", %lu bytes (type %u, key info 0x%02X%02X)\n", len,
               len > 15 ? frame[15] : 0, len > 20 ? frame[19] : 0, len > 20 ? frame[20] : 0);
    } else if (type == ETH_P_IP) {
        g_rx_ip++;
        check_dhcp_offer(frame, len);
    } else if (type == ETH_P_ARP) {
        g_rx_arp++;
    } else {
        g_rx_other++;
    }
    if (TEST_VERBOSE_RX) {
        tprintf(" [RX] ");
        print_mac(frame + 6);
        tprintf(" -> ");
        print_mac(frame);
        tprintf(" type 0x%04X, %lu bytes\n", type, len);
    }
}

static void ev_authorized(void *ctx, const unsigned char *bssid)
{
    (void)ctx;
    g_authorized = true;
    tprintf(" [EV] WPA handshake completed with ");
    print_mac(bssid);
    tprintf(": keys installed, port authorized\n");
}

static void ev_mic_failure(void *ctx, const unsigned char *addr, int group)
{
    (void)ctx;
    tprintf(" [EV] Michael MIC failure (%s) from ", group ? "group" : "pairwise");
    print_mac(addr);
    tprintf("\n");
}

/* =========================================================================
 * Test steps
 * ========================================================================= */
static void print_hwinfo(void)
{
    b43os9_hwinfo hw;

    b43os9_get_hwinfo(g_b43, &hw);
    tprintf(" Chip BCM%04X rev %u pkg %u, core 0x%03X rev %u, board %04X:%04X rev 0x%02X\n",
           hw.chip_id, hw.chip_rev, hw.chip_pkg, hw.core_id, hw.core_rev,
           hw.board_vendor, hw.board_type, hw.board_rev);
    tprintf(" PHY type %u rev %u analog %u, radio 0x%03X:%04X rev %u\n",
           hw.phy_type, hw.phy_rev, hw.phy_analog, hw.radio_manuf, hw.radio_ver, hw.radio_rev);
    tprintf(" SPROM rev %u: boardflags 0x%04X/0x%04X, antennas 0x%X, maxpwr %u.%02u dBm,"
           " pa0b 0x%04X 0x%04X 0x%04X, itssi %u\n",
           hw.sprom_rev, hw.boardflags_lo, hw.boardflags_hi, hw.ant_available_bg,
           hw.maxpwr_bg / 4, (hw.maxpwr_bg % 4) * 25, hw.pa0b0, hw.pa0b1, hw.pa0b2,
           hw.itssi_bg);
}

static void print_txpower(const char *when)
{
    b43os9_hwinfo hw;

    b43os9_get_hwinfo(g_b43, &hw);
    tprintf(" Firmware %u.%u. TX power (%s): desired %d dBm, bbatt %d, rfatt %d,"
           " tx_control 0x%02X, LO tx_bias 0x%02X tx_magn 0x%02X\n",
           hw.fw_rev, hw.fw_patch, when, hw.desired_txpower,
           hw.bbatt, hw.rfatt, hw.tx_control, hw.tx_bias, hw.tx_magn);
}

/* [3] Scan; returns the index of network g_ssid or -1 */
static int do_scan(b43os9_bss *target)
{
    static b43os9_bss bss;
    unsigned int i;
    int found = -1, rc;

    tprintf("\n[3] Scan (\"%s\" active + generic)\n", g_ssid);
    g_scan_done = false;
    rc = b43os9_scan(g_b43, g_ssid);
    if (rc) {
        tprintf(" [ERR] b43os9_scan: %d\n", rc);
        return -1;
    }
    run_for(TEST_SCAN_TIMEOUT_MS, &g_scan_done);
    if (!g_scan_done) {
        tprintf(" [ERR] Scan not finished within %u ms\n", TEST_SCAN_TIMEOUT_MS);
        return -1;
    }
    if (g_scan_aborted)
        tprintf(" Scan aborted\n");

    for (i = 0; b43os9_scan_result(g_b43, i, &bss); i++) {
        tprintf("  %2u ", i);
        print_mac(bss.bssid);
        tprintf("  ch %2u  %4d dBm  %s%s \"%s\"\n", bss.channel, bss.signal_dbm,
               find_ie(bss.ies, bss.ies_len, 48) ? "WPA2" :
               ((bss.capability & 0x10) ? "WEP " : "    "),
               "", (const char *)bss.ssid);
        if (!strcmp((const char *)bss.ssid, g_ssid) &&
            (found < 0 || bss.signal_dbm > target->signal_dbm)) {
            *target = bss;
            found = (int)i;
        }
    }
    tprintf(" %u networks found\n", i);
    if (i)
        g_ok_scan = true;
    return found;
}

/* [4] Connection */
/* WPA1: vendor element 00:50:F2 type 1 */
static bool has_wpa1(const b43os9_bss *bss)
{
    const u8 *p = bss->ies;
    unsigned int len = bss->ies_len;

    while (len >= 2 && (unsigned int)p[1] + 2 <= len) {
        if (p[0] == 221 && p[1] >= 4 && p[2] == 0x00 && p[3] == 0x50 &&
            p[4] == 0xF2 && p[5] == 1)
            return true;
        len -= (unsigned int)p[1] + 2;
        p += p[1] + 2;
    }
    return false;
}

static bool do_connect(const b43os9_bss *bss)
{
    bool wpa = find_ie(bss->ies, bss->ies_len, 48) != NULL || has_wpa1(bss);
    int rc;

    tprintf("\n[4] Connecting to \"%s\" ", (const char *)bss->ssid);
    print_mac(bss->bssid);
    tprintf(" (channel %u, %d dBm)\n", bss->channel, bss->signal_dbm);
    g_connect_done = false;
    g_disconnected = false;
    g_authorized = false;
    if (wpa) {
        tprintf(" WPA/WPA2 network: handshake with wpa_supplicant\n");
        rc = b43os9_connect_psk(g_b43, g_ssid, bss->bssid, g_pass);
    } else if (bss->capability & 0x10) {
        tprintf(" WEP network: keys not configured by the test\n");
        return false;
    } else {
        rc = b43os9_connect(g_b43, g_ssid, bss->bssid, NULL, 0, 0);
    }
    if (rc) {
        tprintf(" [ERR] b43os9_connect: %d\n", rc);
        return false;
    }
    run_for(TEST_CONNECT_TIMEOUT_MS, &g_connect_done);
    if (!g_connect_done) {
        tprintf(" [ERR] No result within %u ms\n", TEST_CONNECT_TIMEOUT_MS);
        return false;
    }
    if (g_connect_status != 0) {
        tprintf(" [ERR] Connection failed: status %d\n", g_connect_status);
        return false;
    }
    tprintf(" Associated with ");
    print_mac(g_connect_bssid);
    tprintf("\n");
    if (wpa) {
        tprintf(" Waiting for the 4-way handshake (max %u ms)\n", TEST_HANDSHAKE_TIMEOUT_MS);
        run_for(TEST_HANDSHAKE_TIMEOUT_MS, &g_authorized);
        if (!g_authorized) {
            tprintf(" [ERR] Handshake not completed (EAPOL received: %lu). Correct password?\n",
                   g_rx_eapol);
            return false;
        }
        g_ok_wpa = true;
    }
    return true;
}

/* [5] Listening for data */
static void listen_data(void)
{
    u32 before = g_rx_frames;

    tprintf("\n[5] Listening for data frames for %u ms\n", TEST_DATA_LISTEN_MS);
    run_for(TEST_DATA_LISTEN_MS, &g_disconnected);
    tprintf(" Received %lu frames (EAPOL %lu, IPv4 %lu, ARP %lu, other %lu)\n",
           g_rx_frames - before, g_rx_eapol, g_rx_ip, g_rx_arp, g_rx_other);
    if (g_rx_frames > before)
        g_ok_rxdata = true;
}

/* [6] DHCP */
static void do_dhcp(void)
{
    static u8 frame[400];
    u32 len;
    int tries, rc;

    tprintf("\n[6] DHCP Discover (data TX and RX test)\n");
    for (tries = 0; tries < TEST_DHCP_TRIES && !g_dhcp_offer; tries++) {
        g_dhcp_xid = 0x1B430000UL | (now_us() & 0xFFFF);
        len = build_dhcp_discover(frame, g_mac, g_dhcp_xid);
        rc = b43os9_tx(g_b43, frame, len);
        tprintf(" DHCPDISCOVER sent (xid 0x%08lX, result %d)\n", g_dhcp_xid, rc);
        run_for(TEST_DHCP_WAIT_MS, &g_dhcp_offer);
        if (!g_dhcp_offer)
            tprintf(" No DHCPOFFER (attempt %d/%d)\n", tries + 1, TEST_DHCP_TRIES);
    }
    g_ok_dhcp = g_dhcp_offer;
}

static void print_summary(void)
{
    tprintf("\n============================================================\n");
    tprintf(" SUMMARY\n");
    tprintf("  stack/firmware start . %s\n", g_ok_init ? "OK" : "--");
    tprintf("  interface up ......... %s\n", g_ok_up ? "OK" : "--");
    tprintf("  scan ................. %s\n", g_ok_scan ? "OK" : "--");
    tprintf("  association .......... %s\n", g_ok_assoc ? "OK" : "--");
    tprintf("  WPA handshake ........ %s\n", g_ok_wpa ? "OK" : "--");
    tprintf("  RX data frames ....... %s\n", g_ok_rxdata ? "OK" : "--");
    tprintf("  DHCP (TX+RX data) .... %s\n", g_ok_dhcp ? "OK" : "--");
    tprintf("============================================================\n");
}

/* =========================================================================
 * main
 * ========================================================================= */
int main(void)
{
    OSStatus err;
    RegEntryIter cookie;
    Boolean done = false;
    const char *nameBuf = "pci80211";
    RegPropertyValueSize propSize;
    b43os9_firmware fw;
    b43os9_options opt;
    b43os9_events ev;
    static b43os9_bss target;
    int rc;

    /* Larger stack: must be done before any allocation */
    SetApplLimit((Ptr)((char *)GetApplLimit() - TEST_EXTRA_STACK));
    MaxApplZone();

    tprintf("============================================================\n");
    tprintf(" AirPort Extreme Tester - Linux 5.15 wireless on Mac OS 9\n");
    tprintf("============================================================\n");

    g_logf = fopen("AirPortTester.log", "w");
    {
        char probe;
        tprintf(" Stack: SP %p, application limit %p, space %lu KB\n",
                (void *)&probe, (void *)GetApplLimit(),
                (unsigned long)(((char *)&probe - (char *)GetApplLimit()) / 1024));
    }
    ask_line(" Network (SSID)", TEST_SSID, 1, g_ssid, sizeof(g_ssid));
    ask_line(" WPA password (empty for an open network)", TEST_PASSPHRASE, 0, g_pass, sizeof(g_pass));

    /* Card in the Name Registry */
    RegistryEntryIterateCreate(&cookie);
    err = RegistryEntrySearch(&cookie, kRegIterContinue, &g_ctx.entry, &done,
                              "name", nameBuf, 9);
    RegistryEntryIterateDispose(&cookie);
    if (err != noErr || done) {
        tprintf(" [ERR] Node \"%s\" not found in the Name Registry\n", nameBuf);
        return 0;
    }
    propSize = sizeof(g_ctx.mmio);
    err = RegistryPropertyGet(&g_ctx.entry, "AAPL,address", (void *)&g_ctx.mmio, &propSize);
    if (err != noErr || g_ctx.mmio == NULL) {
        tprintf(" [ERR] AAPL,address property not available (%ld)\n", (long)err);
        return 0;
    }
    tprintf(" MMIO %p\n", (void *)g_ctx.mmio);

    /* Environment */
    memset(&g_env, 0, sizeof(g_env));
    g_env.ctx = &g_ctx;
    g_env.delay_us = tester_delay_us;
    g_env.time_us = tester_time_us;
    g_env.log = tester_log;
    g_env.alloc = tester_alloc;
    g_env.free = tester_free;
    g_env.pci_cfg_read32 = tester_cfg_read32;
    g_env.pci_cfg_write32 = tester_cfg_write32;

    memset(&opt, 0, sizeof(opt));
    opt.verbose = TEST_B43_VERBOSE;
    opt.pr_debug = TEST_PR_DEBUG;
    opt.hwcrypt = 1;
    opt.qos = 1;
    opt.regdom = TEST_REGDOM;

    memset(&ev, 0, sizeof(ev));
    ev.scan_done = ev_scan_done;
    ev.connect_result = ev_connect_result;
    ev.disconnected = ev_disconnected;
    ev.rx = ev_rx;
    ev.mic_failure = ev_mic_failure;
    ev.authorized = ev_authorized;
    wpa_debug_level = TEST_WPA_DEBUG_LEVEL;

    /* [1] Stack startup */
    tprintf("\n[1] Startup: cfg80211, mac80211, ssb, b43, firmware\n");
    b43_builtin_firmware(&fw);
    rc = b43os9_attach(&g_b43, g_ctx.mmio, &g_env, &fw, &opt, &ev);
    if (rc != B43OS9_OK) {
        tprintf(" [ERR] b43os9_attach: %d\n", rc);
        print_summary();
        return 0;
    }
    g_ok_init = true;
    print_hwinfo();

    /* [2] Interface up */
    b43os9_get_mac(g_b43, g_mac);
    if (TEST_RANDOM_MAC) {
        u32 r = now_us();
        g_mac[0] = 0x02;        /* locally administered, unicast */
        g_mac[1] = 0x0D;
        g_mac[2] = 0x93;
        g_mac[3] = (u8)(r >> 16);
        g_mac[4] = (u8)(r >> 8);
        g_mac[5] = (u8)r;
    }
    tprintf("\n[2] Interface up, MAC ");
    print_mac(g_mac);
    tprintf("\n");
    rc = b43os9_up(g_b43, g_mac);
    if (rc) {
        tprintf(" [ERR] b43os9_up: %d\n", rc);
        print_summary();
        return 0;
    }
    g_ok_up = true;
    run_for(500, NULL);         /* first b43 periodic work */
    print_txpower("after startup");

    /* [3] Scan */
    if (do_scan(&target) < 0) {
        tprintf("\n Network \"%s\" not found\n", g_ssid);
    } else if (do_connect(&target)) {
        g_ok_assoc = true;
        listen_data();
        if (!g_disconnected)
            do_dhcp();
        tprintf("\n[7] Shutdown\n");
        b43os9_disconnect(g_b43, 3);        /* STA leaving */
        run_for(300, NULL);
    }

    print_txpower("end of test");
    b43os9_down(g_b43);
    run_for(100, NULL);
    print_summary();
    return 0;
}
