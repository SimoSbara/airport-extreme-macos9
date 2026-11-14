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
 *   7. link      ARP requests to the router, one unicast reply for each: how
 *                many replies come back, and how many times the AP had to
 *                repeat them because it did not hear our acknowledgement
 *   8. load      large pings to the router, close together: the same
 *                counts with full-size frames, as in a download
 *   9. shutdown  disconnect + down
 *
 * The firmware version is asked at startup when more than one is built in
 * (firmware/fw-<version>/): the log goes to "AirPortTester <version>.log".
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
#include <Events.h>

#include "AirPortTester.h"
#include "b43os9.h"
#include "b43fw.h"
#include "drv_pool.h"

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
static u8   g_router_ip[4], g_offered_ip[4];     /* from the DHCP offer */
static u32  g_arp_sent, g_arp_replies;
static u8   g_router_mac[6];                     /* from its ARP replies */
static u32  g_ping_sent, g_ping_replies;
static bool g_arp_asked;                         /* the router asked for our address */
static u8   g_arp_asker_mac[6], g_arp_asker_ip[4];
static char g_fw_name[16];
static bool g_radio_3_2;                         /* radio values of Linux 3.2 */
static bool g_random_mac = TEST_RANDOM_MAC;      /* or the address of the card */
static bool g_noslow;                            /* board flag "no slow clock" */
/* How often the card is served: c = continuously (the Tester as it always
 * was), b = every 10 ms, busy waiting in between (the card is left alone,
 * the processor is not), i = about every 10 ms, giving the time in between
 * back to the system (card and processor both idle, as with the driver) */
static char g_cadence = 'c';
static Ptr  g_iotrace;                           /* record of the register accesses */
static unsigned long g_iotrace_max;
static bool g_auto;                            /* every way of serving the card in turn */
static bool g_chip_off;                         /* after the shutdown: no register reads */
static u32  g_touches;                           /* register reads between two runs */

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

static drv_pool_stats g_pool;
static int            g_use_pool;

/* Code address of main (a function pointer points to its transition
 * vector, whose first word is the code): with it, the code addresses in the
 * log can be turned into offsets in the binary */
int main(void);
static unsigned long main_addr(void)
{
    return *(const unsigned long *)(const void *)main;
}

/* Block written after its free: the addresses are resolved on the PC
 * with the link map (tbname.py) */
static void pool_report(void *block, unsigned long size, unsigned long offset,
                        unsigned long alloc_caller, unsigned long free_caller)
{
    tprintf(" [POOL] block %p (%lu bytes) written after free at offset %lu:"
            " allocated by %08lX, freed by %08lX, main at %08lX\n",
            block, size, offset, alloc_caller, free_caller, (unsigned long)main_addr());
}

static void *tester_alloc(void *ctx, unsigned long size)
{
    (void)ctx;
    return g_use_pool ? drv_pool_alloc(size) : malloc(size);
}

static void tester_free(void *ctx, void *p)
{
    unsigned long bad = g_pool.bad_frees;

    (void)ctx;
    if (!g_use_pool) {
        free(p);
        return;
    }
    drv_pool_free(p);
    if (g_pool.bad_frees != bad)
        tprintf(" [ERR] kfree of an invalid block %p\n", p);
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
        /* As the driver without interrupts: the card is left alone for
         * 10 ms between two visits */
        if (g_cadence == 'b') {
            u32 t = now_us();
            while ((u32)(now_us() - t) < 10000UL) {
            }
        } else if (g_cadence == 'p') {
            /* The stack runs every 10 ms, but the bus of the card is kept
             * busy with reads of its PCI configuration space (no register
             * of the chip itself is touched) */
            u32 t = now_us();
            while ((u32)(now_us() - t) < 10000UL)
                (void)tester_cfg_read32(NULL, 0);
        } else if (g_cadence == 'm' || g_cadence == 'f' || g_cadence == 'r') {
            /* The stack runs every 10 ms; in between one register of the
             * chip is read (the interrupt reason, which reading does not
             * change): every 1 ms (m), every 100 us (f), all the time (r) */
            u32 gap = g_cadence == 'm' ? 1000UL : g_cadence == 'f' ? 100UL : 0UL;
            u32 t = now_us(), last = t, now;
            volatile u32 sink;

            while ((u32)((now = now_us()) - t) < 10000UL) {
                if (!g_chip_off && (u32)(now - last) >= gap) {
                    last = now;
                    sink = *(volatile u32 *)(g_ctx.mmio + 0x128);
                    g_touches++;
                }
            }
            (void)sink;
        } else if (g_cadence >= '1' && g_cadence <= '9') {
            /* The whole stack runs every 10 ms; in between, all the time,
             * only some of its parts: 1 interrupts, 2 timers and work items,
             * 4 supplicant, 8 deferred frees (3 = 1 + 2, ...) */
            u32 t = now_us();
            while ((u32)(now_us() - t) < 10000UL) {
                b43os9_poll_parts(g_b43, g_cadence - '0');
                if (flag && *flag)
                    break;
            }
        } else if (g_cadence == 'i') {
            /* No event is asked for (mask 0): the call only sleeps, one
             * tick at most, and the system may idle the processor */
            EventRecord ev;
            (void)WaitNextEvent(0, &ev, 1, NULL);
        }
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
            memcpy(g_offered_ip, bootp + 16, 4);
            memcpy(g_router_ip, ip + 12, 4);
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
        /* ICMP echo reply of the router to the ping test */
        if (len >= 14 + 20 + 8 && (frame[14] & 0x0F) == 5 && frame[23] == 1 &&
            frame[34] == 0 && !memcmp(frame + 26, g_router_ip, 4) &&
            frame[38] == 0x1B && frame[39] == 0x43)
            g_ping_replies++;
    } else if (type == ETH_P_ARP) {
        g_rx_arp++;
        /* Reply (opcode 2) of the router to one of our requests */
        if (len >= 42 && frame[20] == 0 && frame[21] == 2 &&
            !memcmp(frame, g_mac, 6) && !memcmp(frame + 28, g_router_ip, 4))
        {
            g_arp_replies++;
            memcpy(g_router_mac, frame + 22, 6);
        }
        /* Request (opcode 1) for the address the DHCP server offered us:
         * answered by the ping test, so that the router can reply to it */
        if (len >= 42 && frame[20] == 0 && frame[21] == 1 && g_dhcp_offer &&
            !memcmp(frame + 38, g_offered_ip, 4)) {
            memcpy(g_arp_asker_mac, frame + 22, 6);
            memcpy(g_arp_asker_ip, frame + 28, 4);
            g_arp_asked = true;
        }
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

/* The AP as mac80211 sees it, and the chip counters */
static bool print_station(const char *when, b43os9_station *st)
{
    b43os9_hwinfo hw;

    if (b43os9_get_station(g_b43, st)) {
        tprintf(" Station (%s): not connected\n", when);
        return false;
    }
    b43os9_get_hwinfo(g_b43, &hw);
    tprintf(" Station (%s): signal %d dBm (avg %d), rate tx %lu.%lu rx %lu.%lu Mbit/s |"
            " tx %lu, retries %lu, failed %lu | rx %lu, dropped %lu (duplicates %lu) |"
            " chip: tx unacked %lu, rx bad FCS %lu\n", when, st->signal, st->signal_avg,
            st->tx_rate / 10, st->tx_rate % 10, st->rx_rate / 10, st->rx_rate % 10,
            st->tx_packets, st->tx_retries, st->tx_failed, st->rx_packets, st->rx_dropped,
            st->rx_duplicates, hw.tx_ack_failures, hw.rx_fcs_errors);
    return true;
}

/* Ethernet II frame + ARP request for target_ip. Returns the length. */
static u32 build_arp_request(u8 *f, const u8 *mac, const u8 *sender_ip, const u8 *target_ip)
{
    u8 *arp = f + 14;

    memset(f, 0, 60);
    memset(f, 0xFF, 6);                 /* broadcast destination */
    memcpy(f + 6, mac, 6);
    f[12] = 0x08; f[13] = 0x06;         /* ARP */
    arp[1] = 1;                         /* Ethernet */
    arp[2] = 0x08;                      /* IPv4 */
    arp[4] = 6; arp[5] = 4;
    arp[7] = 1;                         /* request */
    memcpy(arp + 8, mac, 6);
    memcpy(arp + 14, sender_ip, 4);
    memcpy(arp + 24, target_ip, 4);
    return 60;
}

/* [7] Link quality. Every request makes the router send one unicast frame
 * to us (the reply), which the chip must acknowledge: when the AP does not
 * hear the acknowledgement it sends the frame again, and mac80211 counts
 * those copies as duplicates. A good link has about as many frames as
 * replies and next to no duplicates. */
static void do_arp_test(void)
{
    static u8 frame[60];
    static const u8 no_ip[4];
    b43os9_station before, after;
    const u8 *sender = no_ip;
    bool have;
    u32 i;

    tprintf("\n[7] Link quality: %u ARP requests to %u.%u.%u.%u, one unicast reply each\n",
            TEST_ARP_REQUESTS, g_router_ip[0], g_router_ip[1], g_router_ip[2], g_router_ip[3]);
    if (g_radio_3_2)
        tprintf(" Radio setup of Linux 3.2: result %d (0 = still set)\n",
                b43os9_radio_setup_3_2(g_b43));
    have = print_station("before", &before);
    g_arp_sent = g_arp_replies = 0;
    for (i = 0; i < TEST_ARP_REQUESTS && !g_disconnected; i++) {
        /* The first ones are address probes (sender 0.0.0.0, RFC 5227). A
         * router that does not answer those gets the offered address. */
        if (i == 10 && !g_arp_replies) {
            sender = g_offered_ip;
            tprintf(" No reply to the probes from 0.0.0.0: sender %u.%u.%u.%u from now on\n",
                    sender[0], sender[1], sender[2], sender[3]);
        }
        if (b43os9_tx(g_b43, frame, build_arp_request(frame, g_mac, sender, g_router_ip)) == 0)
            g_arp_sent++;
        run_for(TEST_ARP_INTERVAL_MS, NULL);
    }
    run_for(500, NULL);
    if (print_station("after", &after) && have) {
        u32 dup = after.rx_duplicates - before.rx_duplicates;

        tprintf(" ARP: %lu requests sent, %lu replies received | from the AP: +%lu frames,"
                " +%lu duplicates | ours: +%lu frames, +%lu retries, +%lu given up\n",
                g_arp_sent, g_arp_replies, after.rx_packets - before.rx_packets, dup,
                after.tx_packets - before.tx_packets, after.tx_retries - before.tx_retries,
                after.tx_failed - before.tx_failed);
        if (g_arp_replies)
            tprintf(" Copies the AP sent again for each reply: %lu.%lu (0 = it hears our"
                    " acknowledgements)\n", dup / g_arp_replies,
                    dup * 10 / g_arp_replies % 10);
    }
}

/* Ethernet II frame + IPv4 + ICMP echo request with TEST_PING_BYTES of data */
static u32 build_ping(u8 *f, u16 seq)
{
    const u32 icmp_len = 8 + TEST_PING_BYTES;
    const u32 ip_len = 20 + icmp_len;
    u8 *ip = f + 14, *icmp = ip + 20;
    u16 csum;
    u32 i;

    memset(f, 0, 14 + 28);
    memcpy(f, g_router_mac, 6);
    memcpy(f + 6, g_mac, 6);
    f[12] = 0x08; f[13] = 0x00;         /* IPv4 */
    ip[0] = 0x45;
    ip[2] = (u8)(ip_len >> 8); ip[3] = (u8)ip_len;
    ip[4] = (u8)(seq >> 8); ip[5] = (u8)seq;
    ip[8] = 64;                         /* TTL */
    ip[9] = 1;                          /* ICMP */
    memcpy(ip + 12, g_offered_ip, 4);
    memcpy(ip + 16, g_router_ip, 4);
    csum = ip_checksum(ip, 20);
    ip[10] = (u8)(csum >> 8); ip[11] = (u8)csum;
    icmp[0] = 8;                        /* echo request */
    icmp[4] = 0x1B; icmp[5] = 0x43;     /* identifier */
    icmp[6] = (u8)(seq >> 8); icmp[7] = (u8)seq;
    for (i = 0; i < TEST_PING_BYTES; i++)
        icmp[8 + i] = (u8)(i * 7 + seq);
    csum = ip_checksum(icmp, icmp_len);
    icmp[2] = (u8)(csum >> 8); icmp[3] = (u8)csum;
    return 14 + ip_len;
}

/* Answers the ARP request the router made for the offered address */
static void answer_arp(void)
{
    static u8 f[60];
    u8 *arp = f + 14;

    if (!g_arp_asked)
        return;
    g_arp_asked = false;
    memset(f, 0, sizeof(f));
    memcpy(f, g_arp_asker_mac, 6);
    memcpy(f + 6, g_mac, 6);
    f[12] = 0x08; f[13] = 0x06;
    arp[1] = 1; arp[2] = 0x08; arp[4] = 6; arp[5] = 4;
    arp[7] = 2;                         /* reply */
    memcpy(arp + 8, g_mac, 6);
    memcpy(arp + 14, g_offered_ip, 4);
    memcpy(arp + 18, g_arp_asker_mac, 6);
    memcpy(arp + 24, g_arp_asker_ip, 4);
    (void)b43os9_tx(g_b43, f, sizeof(f));
}

/* [8] Link under load: large pings to the router, close together, so that
 * the AP sends full-size unicast frames at its best rate as in a download.
 * Uses the address of the DHCP offer for the few seconds of the test. */
static void do_ping_test(void)
{
    static u8 frame[14 + 20 + 8 + TEST_PING_BYTES];
    b43os9_station before, after;
    u32 i, start, ms;
    bool have;

    tprintf("\n[8] Link under load: %u pings of %u bytes to %u.%u.%u.%u, every %u ms\n",
            TEST_PING_COUNT, TEST_PING_BYTES, g_router_ip[0], g_router_ip[1], g_router_ip[2],
            g_router_ip[3], TEST_PING_INTERVAL_MS);
    if (!g_arp_replies) {
        tprintf(" Skipped: the address of the router is not known (no ARP reply)\n");
        return;
    }
    have = print_station("before", &before);
    g_ping_sent = g_ping_replies = 0;
    start = now_us();
    for (i = 0; i < TEST_PING_COUNT && !g_disconnected; i++) {
        answer_arp();
        if (b43os9_tx(g_b43, frame, build_ping(frame, (u16)i)) == 0)
            g_ping_sent++;
        run_for(TEST_PING_INTERVAL_MS, NULL);
        /* The router first asks who has our address: wait for that once */
        if (i == 0) {
            run_for(300, &g_arp_asked);
            answer_arp();
            run_for(100, NULL);
        }
    }
    run_for(500, NULL);
    ms = (now_us() - start) / 1000;
    if (print_station("after", &after) && have) {
        u32 dup = after.rx_duplicates - before.rx_duplicates;

        tprintf(" Ping: %lu sent, %lu replies in %lu ms (%lu KB/s received) | from the AP:"
                " +%lu frames, +%lu duplicates | ours: +%lu frames, +%lu retries, +%lu given up\n",
                g_ping_sent, g_ping_replies, ms,
                ms ? g_ping_replies * (TEST_PING_BYTES + 28) / ms * 1000 / 1024 : 0,
                after.rx_packets - before.rx_packets, dup, after.tx_packets - before.tx_packets,
                after.tx_retries - before.tx_retries, after.tx_failed - before.tx_failed);
        if (g_ping_replies)
            tprintf(" Copies the AP sent again for each reply: %lu.%lu (0 = it hears our"
                    " acknowledgements)\n", dup / g_ping_replies,
                    dup * 10 / g_ping_replies % 10);
    }
}

static void print_summary(void)
{
    if (g_use_pool)
        tprintf("\n Memory (driver allocator): %lu KB used, peak %lu KB of %lu KB,"
                " %lu failures, %lu bad frees, %lu written after free\n",
                g_pool.used / 1024, g_pool.peak / 1024, g_pool.size / 1024,
                g_pool.failures, g_pool.bad_frees, g_pool.poison_errors);
    tprintf("\n============================================================\n");
    tprintf(" SUMMARY\n");
    tprintf("  stack/firmware start . %s\n", g_ok_init ? "OK" : "--");
    tprintf("  interface up ......... %s\n", g_ok_up ? "OK" : "--");
    tprintf("  scan ................. %s\n", g_ok_scan ? "OK" : "--");
    tprintf("  association .......... %s\n", g_ok_assoc ? "OK" : "--");
    tprintf("  WPA handshake ........ %s\n", g_ok_wpa ? "OK" : "--");
    tprintf("  RX data frames ....... %s\n", g_ok_rxdata ? "OK" : "--");
    tprintf("  DHCP (TX+RX data) .... %s\n", g_ok_dhcp ? "OK" : "--");
    tprintf("  ARP replies .......... %lu of %lu\n", g_arp_replies, g_arp_sent);
    tprintf("  ping replies ......... %lu of %lu\n", g_ping_replies, g_ping_sent);
    tprintf("  firmware ............. %s\n", g_fw_name);
    tprintf("  radio setup .......... Linux %s\n", g_radio_3_2 ? "3.2" : "5.15");
    tprintf("  MAC address .......... %s\n", g_random_mac ? "random" : "of the card");
    tprintf("  card served .......... %s\n", g_cadence == 'b' ? "every 10 ms, busy waiting" :
            g_cadence == 'i' ? "every tick, system idle in between" :
            g_cadence == 'p' ? "every 10 ms, PCI configuration reads in between" :
            g_cadence == 'm' ? "every 10 ms, one register read every 1 ms" :
            g_cadence == 'f' ? "every 10 ms, one register read every 100 us" :
            g_cadence == 'r' ? "every 10 ms, register reads all the time" :
            g_cadence == '1' ? "every 10 ms, interrupts all the time" :
            g_cadence == '2' ? "every 10 ms, timers and work items all the time" :
            g_cadence == '4' ? "every 10 ms, supplicant all the time" :
            g_cadence == '8' ? "every 10 ms, deferred frees all the time" :
            (g_cadence > '0' && g_cadence <= '9') ? "every 10 ms, several parts all the time" :
            "continuously");
    if (g_touches)
        tprintf("  register reads ....... %lu\n", g_touches);
    tprintf("  slow clock ........... %s\n", g_noslow ? "off (board flag)" : "as the SPROM says");
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

    /* Firmware: the default one, or another version built in */
    b43_builtin_firmware(&fw);
    strncpy(g_fw_name, b43_builtin_firmware_name(0), sizeof(g_fw_name) - 1);
    if (b43_builtin_firmware_name(1)) {
        char ans[16];
        int i;

        tprintf(" Firmware versions built in:");
        for (i = 0; b43_builtin_firmware_name(i); i++)
            tprintf(" %s", b43_builtin_firmware_name(i));
        tprintf("\n");
        ask_line(" Firmware", g_fw_name, 1, ans, sizeof(ans));
        if (strcmp(ans, g_fw_name)) {
            if (b43_builtin_firmware_named(ans, &fw) == 0)
                strcpy(g_fw_name, ans);
            else
                tprintf(" \"%s\" is not built in: using %s\n", ans, g_fw_name);
        }
    }
    {
        char name[48];
        snprintf(name, sizeof(name), "AirPortTester %s %lu.log", g_fw_name, now_us() % 1000UL);
        g_logf = fopen(name, "w");
        tprintf(" Firmware %s, log file \"%s\"\n", g_fw_name, name);
    }
    {
        char probe;
        tprintf(" Stack: SP %p, application limit %p, space %lu KB\n",
                (void *)&probe, (void *)GetApplLimit(),
                (unsigned long)(((char *)&probe - (char *)GetApplLimit()) / 1024));
    }
    {
        char ans[8];
        ask_line(" Radio setup of Linux 3.2 instead of 5.15 (y/n)", "n", 1, ans, sizeof(ans));
        g_radio_3_2 = (ans[0] == 'y' || ans[0] == 'Y');
    }
    {
        char ans[8];
        ask_line(" Random MAC address instead of the one of the card (y/n)",
                 TEST_RANDOM_MAC ? "y" : "n", 1, ans, sizeof(ans));
        g_random_mac = (ans[0] == 'y' || ans[0] == 'Y');
    }
    {
        char ans[8];
        tprintf(" Serve the card: c = continuously; every 10 ms and in between: b = nothing,"
                " p = PCI configuration reads, m = a register read every 1 ms, f = every"
                " 100 us, r = all the time; i = every tick with the system idle\n");
        tprintf(" Or parts of the stack all the time: 1 = interrupts, 2 = timers and work items,"
                " 4 = supplicant, 8 = deferred frees\n");
        tprintf(" Or a = all of c, b, 1, 2, 4, 8 in turn, with a table at the end (about 5 minutes)\n");
        ask_line(" Serve the card", "a", 1, ans, sizeof(ans));
        g_auto = (ans[0] == 'a' || ans[0] == 'A');
        ask_line(" Record the register accesses to a file (y/n)", "n", 1, ans, sizeof(ans));
        if (ans[0] == 'y' || ans[0] == 'Y')
            g_iotrace_max = TEST_IOTRACE_ENTRIES;
        g_cadence = strchr("bpmfri123456789", ans[0]) && ans[0] ? ans[0] : 'c';
        ask_line(" Board flag \"no slow clock\" (y/n)", "n", 1, ans, sizeof(ans));
        g_noslow = (ans[0] == 'y' || ans[0] == 'Y');
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

    /* Memory */
    {
        char ans[8];
        ask_line(" Driver allocator (y/n)", TEST_DRIVER_POOL ? "y" : "n", 1, ans, sizeof(ans));
        g_use_pool = (ans[0] == 'y' || ans[0] == 'Y');
    }
    if (g_use_pool) {
        void *mem = NewPtr(TEST_POOL_KB * 1024UL);
        if (!mem) {
            tprintf(" [ERR] no memory for the %u KB pool\n", TEST_POOL_KB);
            return 0;
        }
        g_pool.report = pool_report;
        drv_pool_init(mem, TEST_POOL_KB * 1024UL, &g_pool);
        tprintf(" Memory: driver allocator, %u KB\n", TEST_POOL_KB);
    } else {
        tprintf(" Memory: malloc\n");
    }

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
    {
        /* Trace: every message of the stack, and every work item and timer
         * before it runs (code addresses, for the link map) */
        char ans[8];
        ask_line(" Trace everything (y/n)", "n", 1, ans, sizeof(ans));
        if (ans[0] == 'y' || ans[0] == 'Y') {
            opt.verbose = 3;
            opt.pr_debug = 2;
            tprintf(" Trace on: main at %08lX\n", (unsigned long)main_addr());
        }
    }
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
    rc = b43os9_attach(&g_b43, g_ctx.mmio, &g_env, &fw, &opt, &ev);
    if (rc != B43OS9_OK) {
        tprintf(" [ERR] b43os9_attach: %d\n", rc);
        print_summary();
        return 0;
    }
    g_ok_init = true;
    if (g_noslow)
        (void)b43os9_set_boardflags(g_b43, B43OS9_BFL_XTAL_NOSLOW);
    print_hwinfo();

    /* [2] Interface up */
    b43os9_get_mac(g_b43, g_mac);
    if (g_random_mac) {
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
    if (g_iotrace_max) {
        g_iotrace = NewPtr(g_iotrace_max * 12UL);
        if (g_iotrace)
            b43os9_iotrace(g_iotrace, g_iotrace_max);
        else
            tprintf(" [ERR] no memory for the register record\n");
    }
    rc = b43os9_up(g_b43, g_mac);
    if (rc) {
        tprintf(" [ERR] b43os9_up: %d\n", rc);
        print_summary();
        return 0;
    }
    g_ok_up = true;
    run_for(500, NULL);         /* first b43 periodic work */
    if (g_radio_3_2)
        tprintf(" Radio setup of Linux 3.2: result %d\n", b43os9_radio_setup_3_2(g_b43));
    print_txpower("after startup");

    if (g_auto) {
        /* Every way of serving the card in turn, on a new connection each
         * time ("c" first and last: the reference), then one table */
        static const char order[] = "cb1248c";
        static struct {
            unsigned long conn_dup, arp_rep, arp_dup, ping_sent, ping_rep, ping_dup, kbs, rx_rate;
            bool ok;
        } res[sizeof(order)];
        unsigned int n;

        for (n = 0; order[n]; n++) {
            b43os9_station s0, s1, s2;
            u32 t0, ms;

            g_cadence = 'c';
            run_for(1000, NULL);
            g_cadence = order[n];
            tprintf("\n================ Serving the card: %c ================\n", g_cadence);
            if (do_scan(&target) < 0 || !do_connect(&target))
                continue;
            g_ok_assoc = true;
            if (!print_station("connected", &s0))
                continue;
            res[n].conn_dup = s0.rx_duplicates;
            g_dhcp_offer = false;
            do_dhcp();
            if (g_dhcp_offer && !g_disconnected) {
                (void)b43os9_get_station(g_b43, &s0);
                do_arp_test();
                (void)b43os9_get_station(g_b43, &s1);
                t0 = now_us();
                do_ping_test();
                ms = (now_us() - t0) / 1000;
                if (b43os9_get_station(g_b43, &s2) == 0) {
                    res[n].arp_rep = g_arp_replies;
                    res[n].arp_dup = s1.rx_duplicates - s0.rx_duplicates;
                    res[n].ping_sent = g_ping_sent;
                    res[n].ping_rep = g_ping_replies;
                    res[n].ping_dup = s2.rx_duplicates - s1.rx_duplicates;
                    res[n].kbs = ms ? g_ping_replies * (TEST_PING_BYTES + 28) / ms * 1000 / 1024 : 0;
                    res[n].rx_rate = s2.rx_rate;
                    res[n].ok = true;
                }
            }
            g_cadence = 'c';
            b43os9_disconnect(g_b43, 3);
            run_for(500, NULL);
        }
        tprintf("\n============================================================\n");
        tprintf(" RESULTS (c continuously, b every 10 ms; every 10 ms plus, all the time:"
                " 1 interrupts, 2 timers and work items, 4 supplicant, 8 deferred frees)\n");
        tprintf(" serve | copies at connection | ARP replies, copies | ping replies of sent, copies,"
                " KB/s | AP rate\n");
        for (n = 0; order[n]; n++) {
            if (!res[n].ok) {
                tprintf("   %c   | not completed (copies at connection %lu)\n", order[n], res[n].conn_dup);
                continue;
            }
            tprintf("   %c   | %lu | %lu, %lu | %lu of %lu, %lu, %lu KB/s | %lu.%lu Mbit/s\n", order[n],
                    res[n].conn_dup, res[n].arp_rep, res[n].arp_dup, res[n].ping_rep,
                    res[n].ping_sent, res[n].ping_dup, res[n].kbs, res[n].rx_rate / 10,
                    res[n].rx_rate % 10);
        }
    } else
    /* [3] Scan */
    if (do_scan(&target) < 0) {
        tprintf("\n Network \"%s\" not found\n", g_ssid);
    } else if (do_connect(&target)) {
        b43os9_station st;

        g_ok_assoc = true;
        (void)print_station("connected", &st);
        listen_data();
        if (!g_disconnected)
            do_dhcp();
        if (g_dhcp_offer && !g_disconnected)
            do_arp_test();
        if (g_dhcp_offer && !g_disconnected)
            do_ping_test();
        tprintf("\n[9] Shutdown\n");
        b43os9_disconnect(g_b43, 3);        /* STA leaving */
        run_for(300, NULL);
    }

    print_txpower("end of test");
    /* The chip is switched off by the shutdown: no register read of ours
     * after it (a read of a disabled core hangs the bus) */
    if (g_iotrace) {
        /* The register record, next to the log: for the PC */
        unsigned long n = b43os9_iotrace_count();
        char name[48];
        FILE *f;

        b43os9_iotrace(NULL, 0);
        snprintf(name, sizeof(name), "AirPortTester %c %lu.io", g_auto ? 'a' : g_cadence,
                 now_us() % 1000UL);
        f = fopen(name, "wb");
        if (f) {
            fwrite(g_iotrace, 12, n, f);
            fclose(f);
        }
        tprintf(" Register record: %lu accesses%s, file \"%s\"\n", n,
                n >= g_iotrace_max ? " (full)" : "", f ? name : "NOT WRITTEN");
    }
    g_chip_off = true;
    b43os9_down(g_b43);
    run_for(100, NULL);
    print_summary();
    return 0;
}
