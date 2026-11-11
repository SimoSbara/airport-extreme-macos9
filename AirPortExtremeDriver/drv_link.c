/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * drv_link.c - Connection manager of the driver.
 *
 * What the AirPort software does on top of the card, with the commands of
 * b43os9.h (the same sequence as the Tester): interface up when Open
 * Transport binds the first client, scan, connect to the configured network
 * (wpa_supplicant for WPA/WPA2-PSK), and reconnect when the connection is
 * lost. A state machine, advanced by drv_link_step() at every tick; the
 * events of the stack only record what happened.
 */
#include <MacTypes.h>
#include <string.h>

#include "drv.h"

#define SCAN_TIMEOUT_MS         10000
#define CONNECT_TIMEOUT_MS      10000
#define HANDSHAKE_TIMEOUT_MS    10000
#define RETRY_NOT_FOUND_MS      5000
#define RETRY_FAILED_MS         3000
#define RETRY_LOST_MS           1000

static b43os9          *g_b;
static const drv_prefs *g_prefs;
static apx_status      *g_st;
static b43os9_events    g_ev;

static int           g_up;
static unsigned long g_deadline;
static int           g_wpa;

/* Events, consumed by the state machine */
static int           ev_scan_done;
static int           ev_connected, ev_connect_status;
static int           ev_authorized;
static int           ev_disconnected;

/* -------------------------------------------------------------------------
 * Events of the stack (called inside b43os9_poll, lock held)
 * ------------------------------------------------------------------------- */
static void on_scan_done(void *ctx, int aborted)
{
    (void)ctx;
    (void)aborted;
    ev_scan_done = 1;
}

static void on_connect_result(void *ctx, int status, const unsigned char *bssid)
{
    (void)ctx;
    ev_connected = 1;
    ev_connect_status = status;
    if (status == 0 && bssid)
        memcpy(g_st->bssid, bssid, 6);
}

static void on_disconnected(void *ctx, unsigned short reason, int from_ap)
{
    (void)ctx;
    ev_disconnected = 1;
    g_st->last_reason = reason;
    drv_log("apx: disconnected, reason %u (%s)\n", reason, from_ap ? "by the AP" : "local");
}

static void on_rx(void *ctx, const unsigned char *frame, unsigned long len)
{
    (void)ctx;
    /* EAPOL is for wpa_supplicant only (b43os9.c already gave it to it) */
    if (len >= 14 && frame[12] == 0x88 && frame[13] == 0x8E)
        return;
    drv_deliver(frame, len);
}

static void on_mic_failure(void *ctx, const unsigned char *addr, int group)
{
    (void)ctx;
    drv_log("apx: Michael MIC failure (%s) from %pM\n", group ? "group" : "pairwise", addr);
}

static void on_authorized(void *ctx, const unsigned char *bssid)
{
    (void)ctx;
    (void)bssid;
    ev_authorized = 1;
}

const b43os9_events *drv_link_events(void)
{
    memset(&g_ev, 0, sizeof(g_ev));
    g_ev.scan_done = on_scan_done;
    g_ev.connect_result = on_connect_result;
    g_ev.disconnected = on_disconnected;
    g_ev.rx = on_rx;
    g_ev.mic_failure = on_mic_failure;
    g_ev.authorized = on_authorized;
    return &g_ev;
}

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */
static const unsigned char *find_ie(const unsigned char *ies, unsigned int len, unsigned char id)
{
    while (len >= 2 && (unsigned int)ies[1] + 2 <= len) {
        if (ies[0] == id)
            return ies;
        len -= (unsigned int)ies[1] + 2;
        ies += ies[1] + 2;
    }
    return NULL;
}

/* WPA1: vendor element 00:50:F2 type 1 */
static int has_wpa1(const b43os9_bss *bss)
{
    const unsigned char *p = bss->ies;
    unsigned int len = bss->ies_len;

    while (len >= 2 && (unsigned int)p[1] + 2 <= len) {
        if (p[0] == 221 && p[1] >= 4 && p[2] == 0x00 && p[3] == 0x50 &&
            p[4] == 0xF2 && p[5] == 1)
            return 1;
        len -= (unsigned int)p[1] + 2;
        p += p[1] + 2;
    }
    return 0;
}

static int expired(void)
{
    return (long)(drv_time_ms() - g_deadline) >= 0;
}

static void set_state(unsigned long state, unsigned long timeout_ms)
{
    g_st->link_state = state;
    g_deadline = drv_time_ms() + timeout_ms;
}

static void retry(unsigned long ms)
{
    set_state(APX_LINK_RETRY, ms);
}

/* -------------------------------------------------------------------------
 * State machine
 * ------------------------------------------------------------------------- */
static void start_scan(void)
{
    int rc;

    if (!g_prefs->ssid[0]) {
        drv_log("apx: no network configured (ssid= in \"AirPort Extreme Prefs\")\n");
        set_state(APX_LINK_IDLE, 0);
        return;
    }
    ev_scan_done = 0;
    rc = b43os9_scan(g_b, g_prefs->ssid);
    if (rc) {
        drv_log("apx: scan failed: %d\n", rc);
        retry(RETRY_FAILED_MS);
        return;
    }
    set_state(APX_LINK_SCANNING, SCAN_TIMEOUT_MS);
}

/* Best access point of the configured network, then connect */
static void connect_best(void)
{
    static b43os9_bss bss, best;
    unsigned int i;
    int found = 0, rc;

    for (i = 0; b43os9_scan_result(g_b, i, &bss); i++) {
        if (strcmp((const char *)bss.ssid, g_prefs->ssid))
            continue;
        if (!found || bss.signal_dbm > best.signal_dbm) {
            best = bss;
            found = 1;
        }
    }
    if (!found) {
        drv_log("apx: \"%s\" not found (%u networks)\n", g_prefs->ssid, i);
        retry(RETRY_NOT_FOUND_MS);
        return;
    }
    memcpy(g_st->bssid, best.bssid, 6);
    g_st->channel = best.channel;
    g_st->signal_dbm = best.signal_dbm;
    g_wpa = find_ie(best.ies, best.ies_len, 48) != NULL || has_wpa1(&best);
    drv_log("apx: connecting to \"%s\" %pM, channel %u, %d dBm, %s\n", g_prefs->ssid,
            best.bssid, best.channel, best.signal_dbm, g_wpa ? "WPA" : "open");

    ev_connected = 0;
    ev_authorized = 0;
    ev_disconnected = 0;
    if (g_wpa) {
        if (!g_prefs->password[0]) {
            drv_log("apx: the network is protected, but no password= is configured\n");
            set_state(APX_LINK_IDLE, 0);
            return;
        }
        rc = b43os9_connect_psk(g_b, g_prefs->ssid, best.bssid, g_prefs->password);
    } else if (best.capability & 0x10) {
        drv_log("apx: WEP networks are not supported\n");
        set_state(APX_LINK_IDLE, 0);
        return;
    } else {
        rc = b43os9_connect(g_b, g_prefs->ssid, best.bssid, NULL, 0, 0);
    }
    if (rc) {
        drv_log("apx: connect failed: %d\n", rc);
        retry(RETRY_FAILED_MS);
        return;
    }
    set_state(APX_LINK_CONNECTING, CONNECT_TIMEOUT_MS);
}

static void connected(void)
{
    g_st->connects++;
    drv_log("apx: connected to %pM\n", g_st->bssid);
    set_state(APX_LINK_CONNECTED, 0);
}

void drv_link_init(b43os9 *b, const drv_prefs *prefs, apx_status *st)
{
    g_b = b;
    g_prefs = prefs;
    g_st = st;
    g_up = 0;
    strncpy(st->ssid, prefs->ssid, sizeof(st->ssid) - 1);
    b43os9_get_mac(b, st->mac);
    st->link_state = APX_LINK_DOWN;
}

void drv_link_step(int want_up)
{
    int rc;

    /* Interface up/down follows the Open Transport clients */
    if (want_up && !g_up) {
        drv_log("apx: interface up, MAC %pM\n", g_st->mac);
        rc = b43os9_up(g_b, g_st->mac);
        if (rc) {
            drv_log("apx: b43os9_up failed: %d\n", rc);
            return;
        }
        g_up = 1;
        retry(500);             /* let b43 run its first periodic work */
        return;
    }
    if (!want_up) {
        if (g_up) {
            drv_log("apx: interface down\n");
            if (g_st->link_state >= APX_LINK_CONNECTING && g_st->link_state <= APX_LINK_CONNECTED)
                b43os9_disconnect(g_b, 3);      /* STA leaving */
            b43os9_down(g_b);
            g_up = 0;
        }
        g_st->link_state = APX_LINK_DOWN;
        return;
    }

    switch (g_st->link_state) {
    case APX_LINK_DOWN:
    case APX_LINK_RETRY:
        if (expired())
            start_scan();
        break;

    case APX_LINK_IDLE:
        break;

    case APX_LINK_SCANNING:
        if (ev_scan_done)
            connect_best();
        else if (expired()) {
            drv_log("apx: scan timeout\n");
            retry(RETRY_FAILED_MS);
        }
        break;

    case APX_LINK_CONNECTING:
        if (ev_connected) {
            if (ev_connect_status != 0) {
                drv_log("apx: connection refused, status %d\n", ev_connect_status);
                g_st->last_reason = (unsigned short)ev_connect_status;
                retry(RETRY_FAILED_MS);
            } else if (g_wpa) {
                set_state(APX_LINK_HANDSHAKE, HANDSHAKE_TIMEOUT_MS);
            } else {
                connected();
            }
        } else if (expired()) {
            drv_log("apx: no answer from the access point\n");
            b43os9_disconnect(g_b, 3);
            retry(RETRY_FAILED_MS);
        }
        break;

    case APX_LINK_HANDSHAKE:
        if (ev_authorized) {
            connected();
        } else if (ev_disconnected) {
            retry(RETRY_FAILED_MS);
        } else if (expired()) {
            drv_log("apx: WPA handshake not completed: wrong password?\n");
            b43os9_disconnect(g_b, 3);
            retry(RETRY_FAILED_MS);
        }
        break;

    case APX_LINK_CONNECTED:
        if (ev_disconnected) {
            ev_disconnected = 0;
            retry(RETRY_LOST_MS);
        }
        break;
    }
}

int drv_link_can_send(void)
{
    return g_up && g_st->link_state == APX_LINK_CONNECTED;
}
