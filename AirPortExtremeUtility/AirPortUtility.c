/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * AirPortUtility.c - "AirPort Extreme", the application to choose the
 * wireless network.
 *
 * One window: the state of the connection with the signal, AirPort on/off,
 * the networks in range (double-click or Join), Other... for a network that
 * is not in the list. Passwords are kept in the Keychain (apx_keychain.c),
 * the remembered networks and the network to join at startup in the
 * driver's preferences file (apx_prefs.c). Everything goes to the driver
 * through its status block (apx_client.c).
 *
 * Built with Retro68 for the classic Toolbox and the Appearance Manager
 * (Mac OS 8.5 and later).
 */
#include <string.h>
#include <stdio.h>
#include <MacTypes.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <MacWindows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Controls.h>
#include <ControlDefinitions.h>
#include <Appearance.h>
#include <Events.h>
#include <ToolUtils.h>
#include <TextUtils.h>
#include <Devices.h>
#include <Sound.h>
#include <LowMem.h>
#include <Icons.h>

#include "apx_client.h"
#include "apx_prefs.h"
#include "apx_keychain.h"
#include "apx_join.h"

#define APP_VERSION "1.0d1"             /* as the 'vers' resource */

/* MacRoman characters */
#define LQ      "\xD2"                  /* left double quote */
#define RQ      "\xD3"                  /* right double quote */
#define ELLIPSIS "\xC9"

/* Menus */
enum { MENU_APPLE = 128, MENU_FILE, MENU_EDIT, MENU_NETWORKS, MENU_FORGET };
enum { ITEM_ABOUT = 1 };
enum { ITEM_QUIT = 1 };
enum { ITEM_REFRESH = 1, ITEM_OTHER, ITEM_DISCONNECT, ITEM_POWER, ITEM_SEP, ITEM_FORGET };

/* Dialogs and alerts (AirPortUtility.r) */
#define DLOG_PASSWORD   200
#define DLOG_OTHER      201
#define ALRT_MESSAGE    128

/* Window layout (local coordinates) */
#define WIN_W           380
#define WIN_H           322
#define ROW_H           18
#define LIST_TOP        92
#define LIST_ROWS       6
#define LIST_BOTTOM     (LIST_TOP + 2 + LIST_ROWS * ROW_H)
#define LIST_LEFT       14
#define LIST_RIGHT      366
#define SCROLL_W        16

static const Rect kStatusRect = { 8, 8, 62, 248 };
static const Rect kListRect = { LIST_TOP, LIST_LEFT, LIST_BOTTOM, LIST_RIGHT };
static const Rect kDetailsRect = { 258, 8, 316, 372 };

static WindowPtr      g_win;
static ControlHandle  g_btn_power, g_btn_refresh, g_btn_other, g_btn_join, g_scroll;
static ControlActionUPP g_scroll_upp;
static MenuHandle     g_menu_networks, g_menu_forget;
static int            g_quit;
static int            g_front = 1;

/* The driver and what the window shows */
static apx_status    *g_st;
static apx_prefs      g_prefs;
static apx_network    g_nets[APX_SCAN_MAX];
static int            g_net_count;
static int            g_sel = -1, g_top;
static unsigned long  g_scan_seq_seen;
static int            g_scanning;
static unsigned long  g_scan_ticks;
static unsigned long  g_last_click, g_last_click_row = 0xFFFF;

/* Snapshot of the status block: the window is redrawn when it changes */
typedef struct view {
    int           driver;
    unsigned long state, error, power, security;
    char          ssid[33];
    unsigned char bssid[6], channel;
    long          signal;
    unsigned long tx_rate, minutes;
    int           scanning;
} view;
static view g_view;
static unsigned long g_prev_error = APX_FAIL_NONE;

/* -------------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------------- */
static void c2p(unsigned char *dst, const char *src)
{
    unsigned long n = strlen(src);

    if (n > 255)
        n = 255;
    dst[0] = (unsigned char)n;
    memcpy(dst + 1, src, n);
}

static void draw_text(short h, short v, const char *s)
{
    Str255 p;

    c2p(p, s);
    MoveTo(h, v);
    DrawString(p);
}

/* Text cut with an ellipsis to fit width */
static void draw_text_fit(short h, short v, const char *s, short width)
{
    Str255 p;

    c2p(p, s);
    TruncString(width, p, truncEnd);
    MoveTo(h, v);
    DrawString(p);
}

static void message(const char *text)
{
    Str255 p;

    c2p(p, text);
    ParamText(p, "\p", "\p", "\p");
    InitCursor();
    (void)NoteAlert(ALRT_MESSAGE, NULL);
}

static void enable(ControlHandle c, int on)
{
    HiliteControl(c, on ? 0 : 255);
}

static void set_title(ControlHandle c, const char *s)
{
    Str255 p, old;

    c2p(p, s);
    GetControlTitle(c, old);
    if (!EqualString(p, old, true, true))
        SetControlTitle(c, p);
}

static void menu_text(MenuHandle m, short item, const char *s)
{
    Str255 p;

    c2p(p, s);
    SetMenuItemText(m, item, p);
}

static void menu_enable(MenuHandle m, short item, int on)
{
    if (on)
        EnableItem(m, item);
    else
        DisableItem(m, item);
}

/* -------------------------------------------------------------------------
 * Drawing
 * ------------------------------------------------------------------------- */
static void gray(int on)
{
    RGBColor c;

    c.red = c.green = c.blue = on ? 0x9000 : 0x0000;
    RGBForeColor(&c);
}

/* Signal bars: 4 bars of growing height in r, bars filled */
static void draw_bars(const Rect *r, int bars)
{
    short w = (short)((r->right - r->left) / 4), i;

    for (i = 0; i < 4; i++) {
        Rect b;

        b.left = (short)(r->left + i * w);
        b.right = (short)(b.left + w - 2);
        b.bottom = r->bottom;
        b.top = (short)(r->bottom - (r->bottom - r->top) * (i + 1) / 4);
        if (i < bars) {
            gray(0);
            PaintRect(&b);
        } else {
            gray(1);
            FrameRect(&b);
        }
    }
    gray(0);
}

/* Status led colours */
enum { LED_GREEN, LED_ORANGE, LED_RED, LED_GRAY };

/* A round led of the given diameter, top left at (h, v): the colour, a
 * darker rim and a small highlight, as a lit lamp */
static void draw_led(short h, short v, short size, int colour)
{
    static const RGBColor fill[] = {
        { 0x2000, 0xC800, 0x2000 },     /* green */
        { 0xFF00, 0x9000, 0x0000 },     /* orange */
        { 0xE000, 0x1800, 0x1000 },     /* red */
        { 0xA000, 0xA000, 0xA000 },     /* gray */
    };
    static const RGBColor rim[] = {
        { 0x0800, 0x5000, 0x0800 },
        { 0x8000, 0x4000, 0x0000 },
        { 0x6000, 0x0000, 0x0000 },
        { 0x5000, 0x5000, 0x5000 },
    };
    RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF };
    Rect r, shine;

    SetRect(&r, h, v, (short)(h + size), (short)(v + size));
    RGBForeColor(&fill[colour]);
    PaintOval(&r);
    RGBForeColor(&rim[colour]);
    FrameOval(&r);
    SetRect(&shine, (short)(h + size / 4), (short)(v + size / 5),
            (short)(h + size / 4 + size / 3), (short)(v + size / 5 + size / 4));
    RGBForeColor(&white);
    PaintOval(&shine);
    gray(0);
}

/* The led of the connection state */
static int state_led(const view *v)
{
    if (!v->driver || v->state == APX_LINK_OFF || v->state == APX_LINK_DOWN)
        return LED_GRAY;
    if (v->state == APX_LINK_CONNECTED)
        return LED_GREEN;
    if (v->state == APX_LINK_IDLE)
        return LED_RED;
    if (v->error != APX_FAIL_NONE && v->state == APX_LINK_RETRY)
        return LED_RED;                 /* retrying after a failure */
    return LED_ORANGE;                  /* scanning, joining, handshake */
}

/* A small padlock, 9x11, top left at (h, v) */
static void draw_lock(short h, short v)
{
    Rect body, shackle;

    SetRect(&shackle, (short)(h + 1), v, (short)(h + 8), (short)(v + 9));
    FrameRoundRect(&shackle, 6, 6);
    SetRect(&body, h, (short)(v + 5), (short)(h + 9), (short)(v + 11));
    PaintRect(&body);
}

static const char *error_text(unsigned long e)
{
    switch (e) {
    case APX_FAIL_NOT_FOUND:        return "The network is not in range.";
    case APX_FAIL_NEED_PASSWORD:    return "The network needs a password.";
    case APX_FAIL_UNSUPPORTED:      return "Its security (WEP or 802.1X) is not supported.";
    case APX_FAIL_REFUSED:          return "The base station refused the connection.";
    case APX_FAIL_NO_ANSWER:        return "The base station does not answer.";
    case APX_FAIL_HANDSHAKE:        return "The password was not accepted.";
    case APX_FAIL_LOST:             return "The connection was lost.";
    case APX_FAIL_ERROR:            return "Something went wrong (see AirPortStatus).";
    }
    return "";
}

static const char *security_text(unsigned long s)
{
    switch (s) {
    case APX_SEC_OPEN:  return "open";
    case APX_SEC_WEP:   return "WEP";
    case APX_SEC_WPA:   return "WPA";
    case APX_SEC_WPA2:  return "WPA2";
    }
    return "?";
}

static void draw_status(void)
{
    char line1[96], line2[96];
    const view *v = &g_view;
    Rect icon = { 14, 14, 46, 46 };
    int bars = 0;

    line2[0] = '\0';
    if (!v->driver) {
        strcpy(line1, "The driver is not running");
        strcpy(line2, "Use the card in the TCP/IP control panel.");
    } else if (v->state == APX_LINK_OFF) {
        strcpy(line1, "AirPort is off");
    } else if (v->state == APX_LINK_DOWN) {
        strcpy(line1, "AirPort is on");
        strcpy(line2, "Waiting for TCP/IP or AppleTalk to use it.");
    } else if (v->state == APX_LINK_CONNECTED) {
        snprintf(line1, sizeof(line1), "Connected to " LQ "%s" RQ, v->ssid);
        bars = apx_bars(v->signal);
        if (v->tx_rate)
            snprintf(line2, sizeof(line2), "%ld dBm, %lu Mbit/s", v->signal, v->tx_rate / 10);
        else
            snprintf(line2, sizeof(line2), "%ld dBm", v->signal);
    } else if (v->state == APX_LINK_CONNECTING) {
        snprintf(line1, sizeof(line1), "Joining " LQ "%s" RQ ELLIPSIS, v->ssid);
    } else if (v->state == APX_LINK_HANDSHAKE) {
        snprintf(line1, sizeof(line1), "Checking the password" ELLIPSIS);
    } else if (v->state == APX_LINK_SCANNING || v->state == APX_LINK_RETRY) {
        snprintf(line1, sizeof(line1), "Looking for " LQ "%s" RQ ELLIPSIS, v->ssid);
        strcpy(line2, error_text(v->error));
    } else {                            /* idle */
        strcpy(line1, "Not connected");
        if (v->error)
            strcpy(line2, error_text(v->error));
        else
            strcpy(line2, "Choose a network below.");
    }

    EraseRect(&kStatusRect);
    /* The application's icon, the led of the state, the signal */
    (void)PlotIconID(&icon, kAlignNone, kTransformNone, kCustomIconResource);
    draw_led(56, 18, 12, state_led(v));
    UseThemeFont(kThemeSystemFont, smSystemScript);
    draw_text_fit(74, 28, line1, 172);
    UseThemeFont(kThemeSmallSystemFont, smSystemScript);
    if (v->state == APX_LINK_CONNECTED && v->driver) {
        Rect b = { 37, 74, 47, 94 };

        draw_bars(&b, bars);
        draw_text_fit(100, 46, line2, 146);
    } else {
        draw_text_fit(74, 46, line2, 172);
    }
}

static void draw_details(void)
{
    char s[96];
    const view *v = &g_view;
    Rect sep;

    EraseRect(&kDetailsRect);
    SetRect(&sep, 14, 256, 366, 258);
    DrawThemeSeparator(&sep, kThemeStateActive);
    UseThemeFont(kThemeSmallSystemFont, smSystemScript);
    if (!v->driver || v->state != APX_LINK_CONNECTED)
        return;
    snprintf(s, sizeof(s), "Base station  %02X:%02X:%02X:%02X:%02X:%02X, channel %u",
             v->bssid[0], v->bssid[1], v->bssid[2], v->bssid[3], v->bssid[4], v->bssid[5],
             v->channel);
    draw_text(16, 276, s);
    snprintf(s, sizeof(s), "Security  %s", security_text(v->security));
    draw_text(16, 292, s);
    if (v->minutes < 60)
        snprintf(s, sizeof(s), "Connected for  %lu min", v->minutes);
    else
        snprintf(s, sizeof(s), "Connected for  %lu h %lu min", v->minutes / 60, v->minutes % 60);
    draw_text(16, 308, s);
}

static void draw_list(void)
{
    Rect frame = kListRect, inner, row;
    RGBColor white = { 0xFFFF, 0xFFFF, 0xFFFF }, hilite;
    int i;

    UseThemeFont(kThemeSmallSystemFont, smSystemScript);
    SetRect(&row, LIST_LEFT, 74, LIST_RIGHT, 90);
    EraseRect(&row);
    draw_text(LIST_LEFT, 86, g_scanning ? "Networks in range (scanning" ELLIPSIS ")"
                                         : "Networks in range");

    DrawThemeListBoxFrame(&frame, kThemeStateActive);
    inner = frame;
    InsetRect(&inner, 1, 1);
    inner.right -= SCROLL_W - 1;
    RGBForeColor(&white);
    PaintRect(&inner);
    gray(0);

    UseThemeFont(kThemeSystemFont, smSystemScript);
    for (i = 0; i < LIST_ROWS && g_top + i < g_net_count; i++) {
        const apx_network *n = &g_nets[g_top + i];
        Rect bars;
        int current = g_view.state == APX_LINK_CONNECTED && !strcmp(n->ssid, g_view.ssid);

        SetRect(&row, inner.left, (short)(inner.top + i * ROW_H), inner.right,
                (short)(inner.top + (i + 1) * ROW_H));
        if (g_top + i == g_sel) {
            LMGetHiliteRGB(&hilite);
            RGBForeColor(&hilite);
            PaintRect(&row);
            gray(0);
        }
        if (n->security != APX_SEC_OPEN)
            draw_lock((short)(row.left + 6), (short)(row.top + 3));
        if (current) {
            TextFace(bold);
            draw_led((short)(row.right - 52), (short)(row.top + 5), 8, LED_GREEN);
        }
        draw_text_fit((short)(row.left + 22), (short)(row.top + 13), n->ssid, 240);
        TextFace(normal);
        SetRect(&bars, (short)(row.right - 36), (short)(row.top + 4), (short)(row.right - 8),
                (short)(row.bottom - 4));
        draw_bars(&bars, apx_bars(n->signal_dbm));
    }
    if (g_net_count == 0) {
        UseThemeFont(kThemeSmallSystemFont, smSystemScript);
        gray(1);
        draw_text((short)(inner.left + 22), (short)(inner.top + 13),
                  g_st ? "No networks found yet." : "");
        gray(0);
    }
}

static void draw_window(void)
{
    Rect sep;

    draw_status();
    SetRect(&sep, 14, 66, 366, 68);
    DrawThemeSeparator(&sep, kThemeStateActive);
    draw_list();
    draw_details();
    DrawControls(g_win);
}

/* -------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */
static void read_view(view *v)
{
    memset(v, 0, sizeof(*v));
    v->scanning = g_scanning;
    if (!g_st)
        g_st = apx_find();
    if (!g_st)
        return;
    v->driver = 1;
    v->state = g_st->link_state;
    v->error = g_st->link_error;
    v->power = g_st->power;
    v->security = g_st->security;
    memcpy(v->ssid, g_st->ssid, sizeof(v->ssid));
    v->ssid[sizeof(v->ssid) - 1] = '\0';
    memcpy(v->bssid, g_st->bssid, 6);
    v->channel = g_st->channel;
    v->signal = g_st->signal_dbm;
    v->tx_rate = g_st->tx_rate;
    v->minutes = g_st->link_seconds / 60;
}

static void update_controls(void)
{
    const view *v = &g_view;
    int up = v->driver && v->state != APX_LINK_OFF && v->state != APX_LINK_DOWN;

    set_title(g_btn_power, v->driver && v->state == APX_LINK_OFF ? "Turn AirPort On"
                                                                 : "Turn AirPort Off");
    enable(g_btn_power, v->driver);
    enable(g_btn_refresh, up && !g_scanning);
    enable(g_btn_other, v->driver && v->state != APX_LINK_OFF);
    enable(g_btn_join, up && g_sel >= 0);
}

static void set_scroll(void)
{
    int max = g_net_count > LIST_ROWS ? g_net_count - LIST_ROWS : 0;

    if (g_top > max)
        g_top = max;
    SetControlMaximum(g_scroll, (SInt16)max);
    SetControlValue(g_scroll, (SInt16)g_top);
}

static void inval(const Rect *r)
{
    InvalRect(r);
}

static void start_scan(void)
{
    long result;

    if (!g_st || !apx_send(g_st, APX_CMD_SCAN, 0, NULL, NULL, &result) || result)
        return;
    g_scanning = 1;
    g_scan_ticks = TickCount();
    inval(&kListRect);
}

/* New scan results: the list, keeping the selected network selected */
static void reload_networks(void)
{
    char selected[33];

    selected[0] = '\0';
    if (g_sel >= 0 && g_sel < g_net_count)
        strcpy(selected, g_nets[g_sel].ssid);
    g_net_count = (int)apx_networks(g_st, g_nets);
    g_sel = -1;
    if (selected[0]) {
        int i;

        for (i = 0; i < g_net_count; i++)
            if (!strcmp(g_nets[i].ssid, selected))
                g_sel = i;
    }
    set_scroll();
}

/* -------------------------------------------------------------------------
 * Passwords and joining
 * ------------------------------------------------------------------------- */
static ControlHandle dialog_control(DialogPtr d, short item)
{
    short type;
    Handle h;
    Rect r;

    GetDialogItem(d, item, &type, &h, &r);
    return (ControlHandle)h;
}

static void control_text(ControlHandle c, ResType tag, char *out, unsigned long size)
{
    Size actual = 0;

    out[0] = '\0';
    if (GetControlDataSize(c, kControlEntireControl, tag, &actual) != noErr)
        return;
    if ((unsigned long)actual >= size)
        actual = (Size)(size - 1);
    if (GetControlData(c, kControlEntireControl, tag, actual, out, &actual) != noErr)
        actual = 0;
    out[actual] = '\0';
}

/* Runs a dialog with Join (1), Cancel (2) and a "Remember" checkbox.
 * 1 = Join. */
static int run_dialog(DialogPtr d, short remember_item, int *remember, ControlHandle focus)
{
    short item;

    SetDialogDefaultItem(d, 1);
    SetDialogCancelItem(d, 2);
    SetControlValue(dialog_control(d, remember_item), *remember ? 1 : 0);
    ShowWindow(GetDialogWindow(d));
    SetKeyboardFocus(GetDialogWindow(d), focus, kControlFocusNextPart);
    InitCursor();
    for (;;) {
        ModalDialog(NULL, &item);
        if (item == 1 || item == 2)
            break;
        if (item == remember_item) {
            ControlHandle c = dialog_control(d, remember_item);

            SetControlValue(c, (SInt16)!GetControlValue(c));
        }
    }
    *remember = GetControlValue(dialog_control(d, remember_item)) != 0;
    return item == 1;
}

/* Asks the password of ssid. 1 = typed. */
static int ask_password(const char *ssid, const char *why, char *password, int *remember)
{
    char text[200];
    Str255 p;
    DialogPtr d = GetNewDialog(DLOG_PASSWORD, NULL, (WindowPtr)-1);
    int ok;

    password[0] = '\0';
    if (!d)
        return 0;
    snprintf(text, sizeof(text), "%s " LQ "%s" RQ ". Type its WPA password:", why, ssid);
    c2p(p, text);
    ParamText(p, "\p", "\p", "\p");
    ok = run_dialog(d, 5, remember, dialog_control(d, 4));
    if (ok)
        control_text(dialog_control(d, 4), kControlEditTextPasswordTag, password, 64);
    DisposeDialog(d);
    return ok;
}

static void join_result(int rc, const char *ssid)
{
    char text[160];

    switch (rc) {
    case APX_JOIN_OK:
    case APX_JOIN_NEED_PASSWORD:
        break;
    case APX_JOIN_BAD_PASSWORD:
        message("A WPA password has 8 to 63 characters.");
        break;
    case APX_JOIN_UNSUPPORTED:
        snprintf(text, sizeof(text), LQ "%s" RQ " uses WEP, which this driver does not support.",
                 ssid);
        message(text);
        break;
    case APX_JOIN_NO_DRIVER:
        message("The AirPort Extreme driver does not answer.");
        break;
    default:
        message("The driver did not accept the command.");
        break;
    }
}

/* Joins a network of the list, asking the password when the Keychain does
 * not have it (or why is set: the last one was not accepted) */
static void join(const char *ssid, unsigned char security, const char *why)
{
    char password[64];
    int remember = 1, rc;

    if (!g_st)
        return;
    rc = why ? APX_JOIN_NEED_PASSWORD : apx_join(g_st, &g_prefs, ssid, security, NULL, 1);
    while (rc == APX_JOIN_NEED_PASSWORD || rc == APX_JOIN_BAD_PASSWORD) {
        if (rc == APX_JOIN_BAD_PASSWORD)
            join_result(rc, ssid);
        if (!ask_password(ssid, why ? why : "The network", password, &remember))
            return;
        rc = apx_join(g_st, &g_prefs, ssid, security, password, remember);
        memset(password, 0, sizeof(password));
    }
    join_result(rc, ssid);
}

static void join_selected(void)
{
    if (g_sel >= 0 && g_sel < g_net_count)
        join(g_nets[g_sel].ssid, g_nets[g_sel].security, NULL);
}

static void join_other(void)
{
    char ssid[64], password[64];
    int remember = 1, rc;
    DialogPtr d;

    if (!g_st)
        return;
    for (;;) {
        d = GetNewDialog(DLOG_OTHER, NULL, (WindowPtr)-1);
        if (!d)
            return;
        if (!run_dialog(d, 9, &remember, dialog_control(d, 5))) {
            DisposeDialog(d);
            return;
        }
        control_text(dialog_control(d, 5), kControlEditTextTextTag, ssid, 33);
        control_text(dialog_control(d, 7), kControlEditTextPasswordTag, password, 64);
        DisposeDialog(d);
        if (!ssid[0]) {
            message("Type the name of the network.");
            continue;
        }
        rc = apx_join_other(g_st, &g_prefs, ssid, password, remember);
        memset(password, 0, sizeof(password));
        if (rc != APX_JOIN_BAD_PASSWORD)
            break;
        join_result(rc, ssid);
    }
    join_result(rc, ssid);
}

static void set_power(int on)
{
    long result;

    if (!g_st || !apx_send(g_st, APX_CMD_POWER, on ? 1UL : 0UL, NULL, NULL, &result)) {
        message("The AirPort Extreme driver does not answer.");
        return;
    }
    g_prefs.power = on;                 /* the same at the next startup */
    (void)apx_prefs_save(&g_prefs);
    if (!on) {
        g_net_count = 0;
        g_sel = -1;
        set_scroll();
        inval(&kListRect);
    }
}

static void disconnect(void)
{
    long result;

    if (g_st)
        (void)apx_send(g_st, APX_CMD_DISCONNECT, 0, NULL, NULL, &result);
}

static void forget(short item)
{
    char ssid[33];

    if (item < 1 || item > g_prefs.remembered_count)
        return;
    strcpy(ssid, g_prefs.remembered[item - 1]);
    apx_prefs_forget(&g_prefs, ssid);
    (void)apx_kc_delete(ssid);
    if (!strcmp(g_prefs.ssid, ssid))
        g_prefs.ssid[0] = '\0';         /* not joined at startup any more */
    (void)apx_prefs_save(&g_prefs);
}

/* The driver asks for a password the utility did not give it: at startup
 * without one in the Keychain, or after the last one was refused */
static void check_driver_needs(void)
{
    static unsigned long pending;       /* asks when the application is in front */
    static char pending_ssid[33];
    unsigned long error = g_view.error;
    int edge = error != g_prev_error;
    char ssid[33];

    g_prev_error = error;
    if (edge && g_view.driver && g_view.state == APX_LINK_IDLE && g_view.ssid[0] &&
        (error == APX_FAIL_HANDSHAKE || error == APX_FAIL_NEED_PASSWORD)) {
        strcpy(pending_ssid, g_view.ssid);
        pending = error;
        /* A refused password is forgotten at once, in front or not, so
         * that it is not used again (by Join or at startup); the one typed
         * next is stored if the network is remembered */
        if (error == APX_FAIL_HANDSHAKE) {
            (void)apx_kc_delete(pending_ssid);
            apx_prefs_forget(&g_prefs, pending_ssid);
            (void)apx_prefs_save(&g_prefs);
        }
    }
    /* The user chose something else in the meantime */
    if (pending && (g_view.state != APX_LINK_IDLE || strcmp(g_view.ssid, pending_ssid)))
        pending = 0;
    if (!pending || !g_front)
        return;
    error = pending;
    pending = 0;
    strcpy(ssid, pending_ssid);
    if (error == APX_FAIL_HANDSHAKE)
        join(ssid, APX_SEC_WPA2, "The password was not accepted by");
    else
        join(ssid, APX_SEC_WPA2, NULL);
}

/* -------------------------------------------------------------------------
 * Periodic work: follows the driver
 * ------------------------------------------------------------------------- */
static void idle(void)
{
    view v;
    int was_up = g_view.driver && g_view.state != APX_LINK_OFF && g_view.state != APX_LINK_DOWN;
    int up;

    if (g_scanning && TickCount() - g_scan_ticks > 15 * 60)
        g_scanning = 0;
    if (g_st && g_st->scan_seq != g_scan_seq_seen && !(g_st->scan_seq & 1)) {
        g_scan_seq_seen = g_st->scan_seq;
        g_scanning = 0;
        reload_networks();
        inval(&kListRect);
    }
    read_view(&v);
    up = v.driver && v.state != APX_LINK_OFF && v.state != APX_LINK_DOWN;
    if (memcmp(&v, &g_view, sizeof(v))) {
        if (strcmp(v.ssid, g_view.ssid) || v.state != g_view.state)
            inval(&kListRect);          /* the bold name follows the connection */
        g_view = v;
        inval(&kStatusRect);
        inval(&kDetailsRect);
        update_controls();
    }
    if (up && !was_up)
        start_scan();                   /* AirPort just came up */
    check_driver_needs();
}

/* -------------------------------------------------------------------------
 * List: clicks, keys, scrolling
 * ------------------------------------------------------------------------- */
static void select_row(int row)
{
    if (row < -1 || row >= g_net_count)
        return;
    g_sel = row;
    if (g_sel >= 0 && g_sel < g_top)
        g_top = g_sel;
    if (g_sel >= g_top + LIST_ROWS)
        g_top = g_sel - LIST_ROWS + 1;
    set_scroll();
    inval(&kListRect);
    update_controls();
}

static pascal void scroll_action(ControlHandle c, ControlPartCode part)
{
    int delta = 0, max = GetControlMaximum(c);

    switch (part) {
    case kControlUpButtonPart:      delta = -1; break;
    case kControlDownButtonPart:    delta = 1; break;
    case kControlPageUpPart:        delta = -(LIST_ROWS - 1); break;
    case kControlPageDownPart:      delta = LIST_ROWS - 1; break;
    default:                        return;
    }
    g_top += delta;
    if (g_top < 0)
        g_top = 0;
    if (g_top > max)
        g_top = max;
    SetControlValue(c, (SInt16)g_top);
    draw_list();
}

static void click_list(Point pt, unsigned long when)
{
    int row = g_top + (pt.v - (LIST_TOP + 1)) / ROW_H;

    if (row >= g_net_count)
        row = -1;
    select_row(row);
    if (row >= 0 && row == (int)g_last_click_row && when - g_last_click <= GetDblTime())
        join_selected();
    g_last_click = when;
    g_last_click_row = (unsigned long)row;
}

static void click_content(EventRecord *ev)
{
    Point pt = ev->where;
    ControlHandle c;
    ControlPartCode part;
    Rect list = kListRect;

    SetPort(g_win);
    GlobalToLocal(&pt);
    part = FindControl(pt, g_win, &c);
    if (part && c) {
        if (c == g_scroll) {
            if (part == kControlIndicatorPart) {
                if (TrackControl(c, pt, NULL)) {
                    g_top = GetControlValue(c);
                    inval(&kListRect);
                }
            } else {
                (void)TrackControl(c, pt, g_scroll_upp);
            }
            return;
        }
        if (!TrackControl(c, pt, NULL))
            return;
        if (c == g_btn_power)
            set_power(g_view.state == APX_LINK_OFF);
        else if (c == g_btn_refresh)
            start_scan();
        else if (c == g_btn_other)
            join_other();
        else if (c == g_btn_join)
            join_selected();
        return;
    }
    list.right -= SCROLL_W;
    InsetRect(&list, 1, 1);
    if (PtInRect(pt, &list))
        click_list(pt, ev->when);
}

/* -------------------------------------------------------------------------
 * Menus
 * ------------------------------------------------------------------------- */
static MenuHandle add_menu(short id, const char *title)
{
    Str255 p;
    MenuHandle m;

    c2p(p, title);
    m = NewMenu(id, p);
    InsertMenu(m, 0);
    return m;
}

/* AppendMenu interprets ; ( / ^ ! < as commands: the text is set apart */
static void add_item(MenuHandle m, const char *text, char key)
{
    char cmd[4] = "x";

    if (key) {
        cmd[1] = '/';
        cmd[2] = key;
    }
    {
        Str255 p;

        c2p(p, cmd);
        AppendMenu(m, p);
    }
    menu_text(m, (short)CountMenuItems(m), text);
}

static void build_menus(void)
{
    MenuHandle apple, file, edit;

    apple = add_menu(MENU_APPLE, "\x14");
    add_item(apple, "About AirPort Extreme" ELLIPSIS, 0);
    AppendMenu(apple, "\p(-");
    AppendResMenu(apple, 'DRVR');

    file = add_menu(MENU_FILE, "File");
    add_item(file, "Quit", 'Q');

    edit = add_menu(MENU_EDIT, "Edit");
    add_item(edit, "Undo", 'Z');
    AppendMenu(edit, "\p(-");
    add_item(edit, "Cut", 'X');
    add_item(edit, "Copy", 'C');
    add_item(edit, "Paste", 'V');
    add_item(edit, "Clear", 0);

    g_menu_networks = add_menu(MENU_NETWORKS, "Networks");
    add_item(g_menu_networks, "Refresh List", 'R');
    add_item(g_menu_networks, "Join Other Network" ELLIPSIS, 'J');
    add_item(g_menu_networks, "Disconnect", 'D');
    add_item(g_menu_networks, "Turn AirPort Off", 'T');
    AppendMenu(g_menu_networks, "\p(-");
    add_item(g_menu_networks, "Forget Network", 0);

    g_menu_forget = NewMenu(MENU_FORGET, "\pForget");
    InsertMenu(g_menu_forget, hierMenu);
    SetItemCmd(g_menu_networks, ITEM_FORGET, hMenuCmd);
    SetItemMark(g_menu_networks, ITEM_FORGET, MENU_FORGET);
    DrawMenuBar();
}

/* Before a menu opens: items that follow the state */
static void adjust_menus(void)
{
    const view *v = &g_view;
    int up = v->driver && v->state != APX_LINK_OFF && v->state != APX_LINK_DOWN;
    int i;

    menu_enable(g_menu_networks, ITEM_REFRESH, up && !g_scanning);
    menu_enable(g_menu_networks, ITEM_OTHER, v->driver && v->state != APX_LINK_OFF);
    menu_enable(g_menu_networks, ITEM_DISCONNECT,
                v->driver && v->state >= APX_LINK_SCANNING && v->state <= APX_LINK_RETRY);
    menu_enable(g_menu_networks, ITEM_POWER, v->driver);
    menu_text(g_menu_networks, ITEM_POWER,
              v->state == APX_LINK_OFF ? "Turn AirPort On" : "Turn AirPort Off");

    while (CountMenuItems(g_menu_forget) > 0)
        DeleteMenuItem(g_menu_forget, 1);
    for (i = 0; i < g_prefs.remembered_count; i++)
        add_item(g_menu_forget, g_prefs.remembered[i], 0);
    menu_enable(g_menu_networks, ITEM_FORGET, g_prefs.remembered_count > 0);
}

static void update(WindowPtr w);

/* About box: drawn in code, so that it can show the icon and two fonts */
#define ABOUT_W     380
#define ABOUT_H     262

static void draw_centered(short v, const char *s)
{
    Str255 p;

    c2p(p, s);
    MoveTo((short)((ABOUT_W - StringWidth(p)) / 2), v);
    DrawString(p);
}

static void draw_about(ControlHandle ok)
{
    static const char *lines[] = {
        "Wireless networking for the AirPort Extreme card",
        "(Broadcom BCM4306) on Mac OS 9.",
        "",
        "It runs the Linux 5.15 wireless stack (b43, ssb,",
        "mac80211, cfg80211) and wpa_supplicant 2.11,",
        "unchanged, on top of Open Transport.",
        "",
        "\xA9 2026 SimoSbara. Free software: GNU GPL version 2.",
        "Thanks to the authors of b43 and wpa_supplicant.",
    };
    Rect icon = { 16, (ABOUT_W - 32) / 2, 48, (ABOUT_W + 32) / 2 };
    char version[64];
    unsigned int i;

    (void)PlotIconID(&icon, kAlignNone, kTransformNone, kCustomIconResource);
    UseThemeFont(kThemeSystemFont, smSystemScript);
    TextFace(bold);
    draw_centered(70, "AirPort Extreme");
    TextFace(normal);
    UseThemeFont(kThemeSmallSystemFont, smSystemScript);
    snprintf(version, sizeof(version), "Version %s \xD1 driver %s", APP_VERSION,
             g_st && g_st->driver_version[0] ? g_st->driver_version : "not running");
    draw_centered(88, version);
    for (i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
        draw_centered((short)(114 + i * 13), lines[i]);
    Draw1Control(ok);
}

static void about(void)
{
    Rect r;
    WindowPtr w;
    ControlHandle ok;
    EventRecord ev;
    GrafPtr saved;
    int done = 0;
    Boolean yes = true;

    GetPort(&saved);
    SetRect(&r, 0, 0, ABOUT_W, ABOUT_H);
    OffsetRect(&r, (short)((qd.screenBits.bounds.right - ABOUT_W) / 2), 80);
    w = NewCWindow(NULL, &r, "\p", false, dBoxProc, (WindowPtr)-1, false, 0);
    if (!w)
        return;
    SetPort(w);
    SetThemeWindowBackground(w, kThemeBrushDialogBackgroundActive, true);
    SetRect(&r, ABOUT_W - 86, ABOUT_H - 32, ABOUT_W - 14, ABOUT_H - 12);
    ok = NewControl(w, &r, "\pOK", true, 0, 0, 1, kControlPushButtonProc, 0);
    SetControlData(ok, kControlEntireControl, kControlPushButtonDefaultTag, sizeof(yes), &yes);
    ShowWindow(w);
    InitCursor();

    while (!done) {
        if (!WaitNextEvent(everyEvent, &ev, 30, NULL))
            continue;
        switch (ev.what) {
        case updateEvt:
            if ((WindowPtr)ev.message == w) {
                BeginUpdate(w);
                draw_about(ok);
                EndUpdate(w);
            } else {
                update((WindowPtr)ev.message);
                SetPort(w);
            }
            break;
        case mouseDown: {
            Point pt = ev.where;
            ControlHandle c;

            GlobalToLocal(&pt);
            if (FindControl(pt, w, &c) && c == ok && TrackControl(c, pt, NULL))
                done = 1;
            break;
        }
        case keyDown: {
            char key = (char)(ev.message & charCodeMask);
            unsigned long t;

            if (key == '\r' || key == 0x03 || key == 0x1B ||
                (key == '.' && (ev.modifiers & cmdKey))) {
                HiliteControl(ok, kControlButtonPart);
                Delay(8, &t);
                done = 1;
            }
            break;
        }
        }
    }
    DisposeWindow(w);
    SetPort(saved);
}

static void do_menu(long choice)
{
    short menu = HiWord(choice), item = LoWord(choice);

    switch (menu) {
    case MENU_APPLE:
        if (item == ITEM_ABOUT) {
            about();
        } else {
            Str255 name;

            GetMenuItemText(GetMenuHandle(MENU_APPLE), item, name);
            (void)OpenDeskAcc(name);
        }
        break;
    case MENU_FILE:
        if (item == ITEM_QUIT)
            g_quit = 1;
        break;
    case MENU_EDIT:
        (void)SystemEdit((short)(item - 1));
        break;
    case MENU_NETWORKS:
        switch (item) {
        case ITEM_REFRESH:      start_scan(); break;
        case ITEM_OTHER:        join_other(); break;
        case ITEM_DISCONNECT:   disconnect(); break;
        case ITEM_POWER:        set_power(g_view.state == APX_LINK_OFF); break;
        }
        break;
    case MENU_FORGET:
        forget(item);
        break;
    }
    HiliteMenu(0);
}

/* -------------------------------------------------------------------------
 * Events
 * ------------------------------------------------------------------------- */
static void key_down(EventRecord *ev)
{
    char c = (char)(ev->message & charCodeMask);

    if (ev->modifiers & cmdKey) {
        adjust_menus();
        do_menu(MenuKey(c));
        return;
    }
    switch (c) {
    case 0x1E:                          /* up arrow */
        select_row(g_sel > 0 ? g_sel - 1 : 0);
        break;
    case 0x1F:                          /* down arrow */
        select_row(g_sel < g_net_count - 1 ? g_sel + 1 : g_sel);
        break;
    case '\r':
    case 0x03:                          /* Enter */
        join_selected();
        break;
    }
}

static void mouse_down(EventRecord *ev)
{
    WindowPtr w;
    short part = FindWindow(ev->where, &w);

    switch (part) {
    case inMenuBar:
        adjust_menus();
        do_menu(MenuSelect(ev->where));
        break;
    case inSysWindow:
        SystemClick(ev, w);
        break;
    case inDrag:
        DragWindow(w, ev->where, &qd.screenBits.bounds);
        break;
    case inGoAway:
        if (TrackGoAway(w, ev->where))
            g_quit = 1;
        break;
    case inContent:
        if (w != FrontWindow())
            SelectWindow(w);
        else if (w == g_win)
            click_content(ev);
        break;
    }
}

static void update(WindowPtr w)
{
    if (w != g_win)
        return;
    SetPort(g_win);
    BeginUpdate(g_win);
    draw_window();
    EndUpdate(g_win);
}

static void make_window(void)
{
    Rect r;

    SetRect(&r, 0, 0, WIN_W, WIN_H);
    OffsetRect(&r, (short)((qd.screenBits.bounds.right - WIN_W) / 2), 60);
    g_win = NewCWindow(NULL, &r, "\pAirPort Extreme", false, noGrowDocProc, (WindowPtr)-1, true, 0);
    SetPort(g_win);
    SetThemeWindowBackground(g_win, kThemeBrushDialogBackgroundActive, true);

    SetRect(&r, 252, 18, 366, 38);
    g_btn_power = NewControl(g_win, &r, "\pTurn AirPort Off", true, 0, 0, 1,
                             kControlPushButtonProc, 0);
    SetRect(&r, 14, 222, 94, 242);
    g_btn_refresh = NewControl(g_win, &r, "\pRefresh", true, 0, 0, 1, kControlPushButtonProc, 0);
    SetRect(&r, 102, 222, 182, 242);
    g_btn_other = NewControl(g_win, &r, "\pOther\xC9", true, 0, 0, 1, kControlPushButtonProc, 0);
    SetRect(&r, 286, 222, 366, 242);
    g_btn_join = NewControl(g_win, &r, "\pJoin", true, 0, 0, 1, kControlPushButtonProc, 0);
    {
        Boolean yes = true;

        SetControlData(g_btn_join, kControlEntireControl, kControlPushButtonDefaultTag,
                       sizeof(yes), &yes);
    }
    SetRect(&r, LIST_RIGHT - SCROLL_W, LIST_TOP, LIST_RIGHT, LIST_BOTTOM);
    g_scroll = NewControl(g_win, &r, "\p", true, 0, 0, 0, kControlScrollBarProc, 0);
    g_scroll_upp = NewControlActionUPP(scroll_action);
    ShowWindow(g_win);
}

int main(void)
{
    EventRecord ev;
    unsigned long last_idle = 0;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);
    RegisterAppearanceClient();

    (void)apx_prefs_load(&g_prefs);
    (void)apx_move_password_to_keychain(&g_prefs);
    build_menus();
    make_window();

    /* First look at the driver: its last scan list at once, and a new scan
     * if AirPort is up */
    g_view.driver = -1;                 /* differs from anything: first draw */
    idle();

    while (!g_quit) {
        if (WaitNextEvent(everyEvent, &ev, 15, NULL)) {
            switch (ev.what) {
            case mouseDown:
                mouse_down(&ev);
                break;
            case keyDown:
            case autoKey:
                key_down(&ev);
                break;
            case updateEvt:
                update((WindowPtr)ev.message);
                break;
            case osEvt:
                if (((ev.message >> 24) & 0xFF) == suspendResumeMessage)
                    g_front = (ev.message & resumeFlag) != 0;
                break;
            }
        }
        if (TickCount() - last_idle >= 30) {   /* twice a second */
            last_idle = TickCount();
            SetPort(g_win);
            idle();
        }
    }
    return 0;
}
