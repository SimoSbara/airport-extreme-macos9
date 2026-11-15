/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * AirPortStartup.c - "AirPort Extreme Startup", for the Startup Items
 * folder.
 *
 * The driver joins at startup the network of the preferences file (ssid=),
 * but cannot read the Keychain. This program waits for the driver (Open
 * Transport loads it with TCP/IP), takes the password of that network from
 * the Keychain and gives it to the driver with a connect command, then
 * quits. If the keychain is locked, the Keychain Manager asks for its
 * password. No window, no menus.
 */
#include <string.h>
#include <MacTypes.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <MacWindows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>

#include "apx_client.h"
#include "apx_prefs.h"
#include "apx_keychain.h"

#define WAIT_DRIVER_TICKS   (120 * 60)  /* TCP/IP may load late */

/* Waits without keeping the processor busy */
static void pause_ticks(unsigned long ticks)
{
    EventRecord ev;

    (void)WaitNextEvent(everyEvent, &ev, ticks, NULL);
}

int main(void)
{
    apx_prefs prefs;
    apx_status *st = NULL;
    unsigned long t0;
    char password[64];
    long result;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    if (apx_prefs_load(&prefs) != 0 || !prefs.power || !prefs.ssid[0])
        return 0;

    t0 = TickCount();
    while (!(st = apx_find())) {
        if (TickCount() - t0 > WAIT_DRIVER_TICKS)
            return 0;                   /* TCP/IP not in use: the utility will do */
        pause_ticks(60);
    }

    /* Already connected (open network, or a password= line), or joining
     * another network chosen in the meantime: nothing to do */
    if (st->link_state == APX_LINK_CONNECTED || strcmp(st->ssid, prefs.ssid))
        return 0;
    if (apx_kc_get(prefs.ssid, password, sizeof(password)) != 0)
        return 0;                       /* open network, or never remembered */
    (void)apx_send(st, APX_CMD_CONNECT, 0, prefs.ssid, password, &result);
    memset(password, 0, sizeof(password));
    return 0;
}
