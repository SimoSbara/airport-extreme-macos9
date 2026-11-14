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
 *
 * The network comes from the preferences file at startup, and then from
 * the commands of the applications (apx_status.h): scan, connect to another
 * network, disconnect, AirPort on/off. Every scan, ours or the
 * application's, replaces the scan list in the status block; while
 * connected, the signal and rates of the access point are copied there
 * every second.
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
#define HANDSHAKE_MAX_FAILURES  3       /* then stop: wrong password */

static b43os9          *g_b;
static apx_status      *g_st;
static b43os9_events    g_ev;

static int           g_up;
static unsigned long g_deadline;
static int           g_wpa;

/* The network to join: from the prefs, then from APX_CMD_CONNECT */
static char          g_ssid[33];
static char          g_password[64];
static int           g_hold;            /* do not (re)connect: disconnected or failed */
static int           g_power;           /* AirPort on (APX_CMD_POWER) */
static int           g_handshake_failures;

/* Scan asked by an application, and our own scan for it running */
static int           g_scan_wanted;
static int           g_scan_mine;
static unsigned long g_scan_mine_ms;    /* when it started */
static unsigned long g_scan_start_ms;   /* when the last scan (any) started */

/* A connection was made: Open Transport must hear of it (drv_link_take_network_change) */
static int           g_net_change;

/* Connection time and the last update of the station data */
static unsigned long g_connected_ms;
static unsigned long g_station_ms;

/* Events, consumed by the state machine */
static int           ev_scan_done;
static int           ev_scan_list;      /* new results for the scan list */
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
    ev_scan_list = 1;
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

static unsigned char security_of(const b43os9_bss *bss)
{
    if (find_ie(bss->ies, bss->ies_len, 48))
        return APX_SEC_WPA2;
    if (has_wpa1(bss))
        return APX_SEC_WPA;
    if (bss->capability & 0x10)
        return APX_SEC_WEP;
    return APX_SEC_OPEN;
}

/* Copies the cfg80211 BSS table into the scan list of the status block.
 * cfg80211 keeps an access point for 30 s after it was last heard: only the
 * ones heard during the last scan go in the list. */
static void publish_scan(void)
{
    static b43os9_bss bss;
    unsigned long max_age = drv_time_ms() - g_scan_start_ms + 1000;
    unsigned int i, n = 0;

    g_st->scan_seq++;                   /* odd: the list is changing */
    APX_BARRIER();
    for (i = 0; n < APX_SCAN_MAX && b43os9_scan_result(g_b, i, &bss); i++) {
        apx_network *net;

        if (bss.age_ms > max_age)
            continue;
        net = &g_st->scan[n++];

        memcpy(net->ssid, bss.ssid, sizeof(net->ssid));
        net->ssid[sizeof(net->ssid) - 1] = '\0';
        memcpy(net->bssid, bss.bssid, 6);
        net->channel = bss.channel;
        net->security = security_of(&bss);
        net->signal_dbm = bss.signal_dbm;
    }
    g_st->scan_count = n;
    APX_BARRIER();
    g_st->scan_seq++;                   /* even: done */
}

/* The access point as mac80211 sees it, once a second while connected */
static void update_station(void)
{
    b43os9_station sta;
    unsigned long now = drv_time_ms();

    if (now - g_station_ms < 1000)
        return;
    g_station_ms = now;
    g_st->link_seconds = (now - g_connected_ms) / 1000;
    if (b43os9_get_station(g_b, &sta))
        return;
    g_st->signal_dbm = sta.signal;
    g_st->signal_avg_dbm = sta.signal_avg;
    g_st->tx_rate = sta.tx_rate;
    g_st->rx_rate = sta.rx_rate;
}

static int expired(void)
{
    return (long)(drv_time_ms() - g_deadline) >= 0;
}

static void set_state(unsigned long state, unsigned long timeout_ms)
{
    if (g_st->link_state == APX_LINK_CONNECTED && state != APX_LINK_CONNECTED) {
        g_st->tx_rate = 0;
        g_st->rx_rate = 0;
    }
    g_st->link_state = state;
    g_deadline = drv_time_ms() + timeout_ms;
}

static void retry(unsigned long ms)
{
    set_state(APX_LINK_RETRY, ms);
}

/* The attempt failed: try again after ms, or stop (ms = 0) */
static void fail(unsigned long error, unsigned long ms)
{
    g_st->link_error = error;
    if (ms) {
        retry(ms);
    } else {
        g_hold = 1;
        set_state(APX_LINK_IDLE, 0);
    }
}

/* -------------------------------------------------------------------------
 * State machine
 * ------------------------------------------------------------------------- */
static void start_scan(void)
{
    int rc;

    if (g_hold) {
        set_state(APX_LINK_IDLE, 0);
        return;
    }
    if (!g_ssid[0]) {
        drv_log("apx: no network configured\n");
        set_state(APX_LINK_IDLE, 0);
        return;
    }
    ev_scan_done = 0;
    rc = b43os9_scan(g_b, g_ssid);
    if (rc == 0)
        g_scan_start_ms = drv_time_ms();
    if (rc) {
        drv_log("apx: scan failed: %d\n", rc);
        fail(APX_FAIL_ERROR, RETRY_FAILED_MS);
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
        if (strcmp((const char *)bss.ssid, g_ssid))
            continue;
        if (!found || bss.signal_dbm > best.signal_dbm) {
            best = bss;
            found = 1;
        }
    }
    if (!found) {
        drv_log("apx: \"%s\" not found (%u networks)\n", g_ssid, i);
        fail(APX_FAIL_NOT_FOUND, RETRY_NOT_FOUND_MS);
        return;
    }
    memcpy(g_st->bssid, best.bssid, 6);
    g_st->channel = best.channel;
    g_st->signal_dbm = best.signal_dbm;
    g_st->signal_avg_dbm = best.signal_dbm;
    g_st->security = security_of(&best);
    g_wpa = g_st->security == APX_SEC_WPA || g_st->security == APX_SEC_WPA2;
    drv_log("apx: connecting to \"%s\" %pM, channel %u, %d dBm, %s\n", g_ssid,
            best.bssid, best.channel, best.signal_dbm, g_wpa ? "WPA" : "open");

    ev_connected = 0;
    ev_authorized = 0;
    ev_disconnected = 0;
    if (g_wpa) {
        if (!g_password[0]) {
            drv_log("apx: the network is protected, but no password is configured\n");
            fail(APX_FAIL_NEED_PASSWORD, 0);
            return;
        }
        rc = b43os9_connect_psk(g_b, g_ssid, best.bssid, g_password);
        if (rc == B43OS9_ERR_INVAL) {
            drv_log("apx: the network does not accept a WPA password (802.1X?)\n");
            fail(APX_FAIL_UNSUPPORTED, 0);
            return;
        }
    } else if (g_st->security == APX_SEC_WEP) {
        drv_log("apx: WEP networks are not supported\n");
        fail(APX_FAIL_UNSUPPORTED, 0);
        return;
    } else {
        rc = b43os9_connect(g_b, g_ssid, best.bssid, NULL, 0, 0);
    }
    if (rc) {
        drv_log("apx: connect failed: %d\n", rc);
        fail(APX_FAIL_ERROR, RETRY_FAILED_MS);
        return;
    }
    set_state(APX_LINK_CONNECTING, CONNECT_TIMEOUT_MS);
}

static void connected(void)
{
    g_st->connects++;
    g_st->link_error = APX_FAIL_NONE;
    g_st->link_seconds = 0;
    g_handshake_failures = 0;
    g_connected_ms = drv_time_ms();
    g_station_ms = g_connected_ms;
    g_net_change = 1;
    drv_log("apx: connected to %pM\n", g_st->bssid);
    set_state(APX_LINK_CONNECTED, 0);
}

/* 1 once after every connection: the caller tells Open Transport that the
 * port may be on another network, and TCP/IP asks DHCP again, as with an
 * Ethernet cable plugged into another network */
int drv_link_take_network_change(void)
{
    int change = g_net_change;

    g_net_change = 0;
    return change;
}

/* The WPA handshake did not end: the password is wrong, most of the time.
 * After a few attempts in a row, stop and let the user type it again. */
static void handshake_failed(void)
{
    if (++g_handshake_failures >= HANDSHAKE_MAX_FAILURES) {
        drv_log("apx: %d WPA handshakes in a row not completed: giving up\n",
                g_handshake_failures);
        g_handshake_failures = 0;
        fail(APX_FAIL_HANDSHAKE, 0);
    } else {
        fail(APX_FAIL_HANDSHAKE, RETRY_FAILED_MS);
    }
}

void drv_link_init(b43os9 *b, const drv_prefs *prefs, apx_status *st)
{
    g_b = b;
    g_st = st;
    g_up = 0;
    g_hold = 0;
    g_power = prefs->power != 0;
    memcpy(g_ssid, prefs->ssid, sizeof(g_ssid));
    memcpy(g_password, prefs->password, sizeof(g_password));
    memcpy(st->ssid, g_ssid, sizeof(st->ssid));
    strncpy(st->driver_version, APX_DRIVER_VERSION, sizeof(st->driver_version) - 1);
    strncpy(st->country, prefs->country[0] ? prefs->country : "00", sizeof(st->country) - 1);
    b43os9_get_mac(b, st->mac);
    st->power = (unsigned long)g_power;
    st->link_state = g_power ? APX_LINK_DOWN : APX_LINK_OFF;
    st->cmd_done = st->cmd_seq;         /* nothing pending from before */
}

/* Leaves the current network, if any (the disconnected event follows) */
static void leave(void)
{
    if (g_up && g_st->link_state >= APX_LINK_CONNECTING && g_st->link_state <= APX_LINK_CONNECTED)
        b43os9_disconnect(g_b, 3);      /* STA leaving */
}

static long cmd_connect(const char *ssid, const char *password)
{
    unsigned long slen = strlen(ssid), plen = strlen(password);

    if (slen == 0 || slen > 32 || (plen && (plen < 8 || plen > 63)))
        return APX_ERR_INVAL;
    drv_log("apx: command: connect to \"%s\" (%s)\n", ssid, plen ? "password" : "open");
    leave();
    memcpy(g_ssid, ssid, slen + 1);
    memcpy(g_password, password, plen + 1);
    memcpy(g_st->ssid, g_ssid, sizeof(g_st->ssid));
    g_hold = 0;
    g_handshake_failures = 0;
    g_st->link_error = APX_FAIL_NONE;
    if (!g_power) {
        g_power = 1;                    /* the interface comes up at the next step */
        g_st->power = 1;
    } else if (g_up && g_st->link_state != APX_LINK_SCANNING) {
        retry(500);                     /* the disconnect goes out first */
    }
    return 0;
}

static long cmd_disconnect(void)
{
    drv_log("apx: command: disconnect\n");
    leave();
    g_hold = 1;
    g_st->link_error = APX_FAIL_NONE;
    if (g_up)
        set_state(APX_LINK_IDLE, 0);
    return 0;
}

static long cmd_power(unsigned long on)
{
    if (on > 1)
        return APX_ERR_INVAL;
    drv_log("apx: command: AirPort %s\n", on ? "on" : "off");
    g_power = (int)on;
    g_st->power = on;
    if (on) {
        g_hold = 0;                     /* back to the configured network */
        g_handshake_failures = 0;
        g_st->link_error = APX_FAIL_NONE;
    }
    return 0;                           /* the next step brings it up/down */
}

/* Takes the command of an application, if there is one (apx_status.h) */
static void commands(void)
{
    unsigned long seq = g_st->cmd_seq;
    long result;

    if (seq == g_st->cmd_done)
        return;
    APX_BARRIER();
    g_st->cmd_ssid[sizeof(g_st->cmd_ssid) - 1] = '\0';
    g_st->cmd_password[sizeof(g_st->cmd_password) - 1] = '\0';
    switch (g_st->cmd) {
    case APX_CMD_SCAN:
        if (!g_up) {
            result = APX_ERR_NETDOWN;
        } else {
            g_scan_wanted = 1;
            result = 0;
        }
        break;
    case APX_CMD_CONNECT:
        result = cmd_connect(g_st->cmd_ssid, g_st->cmd_password);
        break;
    case APX_CMD_DISCONNECT:
        result = cmd_disconnect();
        break;
    case APX_CMD_POWER:
        result = cmd_power(g_st->cmd_arg);
        break;
    default:
        result = APX_ERR_INVAL;
        break;
    }
    memset(g_st->cmd_password, 0, sizeof(g_st->cmd_password));
    g_st->cmd_result = result;
    APX_BARRIER();
    g_st->cmd_done = seq;
}

/* A scan for the application while nothing else scans: the state machine's
 * own scan (to find the configured network) serves it too */
static void user_scan(void)
{
    int rc;

    if (!g_scan_wanted || g_scan_mine)
        return;
    rc = b43os9_scan(g_b, NULL);
    if (rc == B43OS9_ERR_BUSY)
        return;                         /* mac80211 is scanning: next tick */
    g_scan_wanted = 0;
    if (rc) {
        drv_log("apx: scan failed: %d\n", rc);
    } else {
        g_scan_mine = 1;
        g_scan_mine_ms = drv_time_ms();
        g_scan_start_ms = g_scan_mine_ms;
    }
}

void drv_link_step(int want_up)
{
    int rc;

    commands();
    if (ev_scan_list) {
        ev_scan_list = 0;
        g_scan_mine = 0;
        g_scan_wanted = 0;              /* whoever asked, this scan answers */
        publish_scan();
    } else if (g_scan_mine && drv_time_ms() - g_scan_mine_ms >= SCAN_TIMEOUT_MS) {
        drv_log("apx: scan timeout\n");
        g_scan_mine = 0;
    }

    /* Interface up/down follows the Open Transport clients, and AirPort
     * on/off: off takes the interface down, so the radio is off */
    want_up = want_up && g_power;
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
            leave();
            b43os9_down(g_b);
            g_up = 0;
        }
        set_state(g_power ? APX_LINK_DOWN : APX_LINK_OFF, 0);
        g_scan_wanted = 0;
        g_scan_mine = 0;
        return;
    }

    switch (g_st->link_state) {
    case APX_LINK_DOWN:
    case APX_LINK_OFF:
    case APX_LINK_RETRY:
        /* A scan asked by an application is done now, for both; one of
         * ours still running (started while connected) ends first */
        if (g_scan_mine)
            break;
        if (expired() || g_scan_wanted)
            start_scan();
        break;

    case APX_LINK_IDLE:
        user_scan();
        break;

    case APX_LINK_SCANNING:
        if (ev_scan_done)
            connect_best();
        else if (expired()) {
            drv_log("apx: scan timeout\n");
            fail(APX_FAIL_ERROR, RETRY_FAILED_MS);
        }
        break;

    case APX_LINK_CONNECTING:
        if (ev_connected) {
            if (ev_connect_status != 0) {
                drv_log("apx: connection refused, status %d\n", ev_connect_status);
                g_st->last_reason = (unsigned short)ev_connect_status;
                fail(ev_connect_status < 0 ? APX_FAIL_NO_ANSWER : APX_FAIL_REFUSED,
                     RETRY_FAILED_MS);
            } else if (g_wpa) {
                set_state(APX_LINK_HANDSHAKE, HANDSHAKE_TIMEOUT_MS);
            } else {
                connected();
            }
        } else if (expired()) {
            drv_log("apx: no answer from the access point\n");
            b43os9_disconnect(g_b, 3);
            fail(APX_FAIL_NO_ANSWER, RETRY_FAILED_MS);
        }
        break;

    case APX_LINK_HANDSHAKE:
        if (ev_authorized) {
            connected();
        } else if (ev_disconnected) {
            handshake_failed();         /* the AP gave up on the handshake */
        } else if (expired()) {
            drv_log("apx: WPA handshake not completed: wrong password?\n");
            b43os9_disconnect(g_b, 3);
            handshake_failed();
        }
        break;

    case APX_LINK_CONNECTED:
        if (ev_disconnected) {
            ev_disconnected = 0;
            fail(APX_FAIL_LOST, RETRY_LOST_MS);
        } else {
            update_station();
            user_scan();                /* mac80211 scans between frames */
        }
        break;
    }
}

int drv_link_can_send(void)
{
    return g_up && g_st->link_state == APX_LINK_CONNECTED;
}
