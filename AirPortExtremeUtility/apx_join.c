/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * apx_join.c - Joining a network.
 */
#include <string.h>
#include <MacTypes.h>

#include "apx_join.h"
#include "apx_keychain.h"

static int send_connect(apx_status *st, apx_prefs *prefs, const char *ssid, const char *pw,
                        int remember, int typed)
{
    long result;

    if (!apx_send(st, APX_CMD_CONNECT, 0, ssid, pw, &result))
        return APX_JOIN_NO_DRIVER;
    if (result)
        return APX_JOIN_ERROR;

    /* The typed password goes to the Keychain only for a remembered
     * network; if it is wrong, the driver reports it and the user types it
     * again, which replaces it */
    if (remember) {
        if (typed && pw[0])
            (void)apx_kc_set(ssid, pw);
        apx_prefs_remember(prefs, ssid);
    }
    /* The driver joins this network at the next startup. An old password=
     * line belongs to the network it was written for: it goes away now
     * (apx_prefs_load moved it to the Keychain if it could). */
    strncpy(prefs->ssid, ssid, sizeof(prefs->ssid) - 1);
    prefs->ssid[sizeof(prefs->ssid) - 1] = '\0';
    prefs->power = 1;
    prefs->drop_password = 1;
    (void)apx_prefs_save(prefs);
    return APX_JOIN_OK;
}

static int valid_password(const char *pw)
{
    unsigned long n = strlen(pw);

    return n >= 8 && n <= 63;
}

int apx_join(apx_status *st, apx_prefs *prefs, const char *ssid, unsigned char security,
             const char *password, int remember)
{
    char pw[64];
    int rc;

    if (security == APX_SEC_WEP)
        return APX_JOIN_UNSUPPORTED;
    if (security == APX_SEC_OPEN)
        return send_connect(st, prefs, ssid, "", remember, 0);

    if (password) {
        if (!valid_password(password))
            return APX_JOIN_BAD_PASSWORD;
        return send_connect(st, prefs, ssid, password, remember, 1);
    }
    if (apx_kc_get(ssid, pw, sizeof(pw)) != 0 || !valid_password(pw)) {
        memset(pw, 0, sizeof(pw));
        return APX_JOIN_NEED_PASSWORD;
    }
    rc = send_connect(st, prefs, ssid, pw, remember, 0);
    memset(pw, 0, sizeof(pw));
    return rc;
}

int apx_move_password_to_keychain(apx_prefs *prefs)
{
    char pw[64];

    if (!prefs->password[0] || !prefs->ssid[0])
        return 0;
    /* One already in the Keychain wins: it was typed later */
    if (apx_kc_get(prefs->ssid, pw, sizeof(pw)) != 0 && apx_kc_set(prefs->ssid, prefs->password) != 0) {
        memset(pw, 0, sizeof(pw));
        return 0;                       /* keychain not usable: keep the line */
    }
    memset(pw, 0, sizeof(pw));
    apx_prefs_remember(prefs, prefs->ssid);
    prefs->drop_password = 1;
    (void)apx_prefs_save(prefs);
    memset(prefs->password, 0, sizeof(prefs->password));
    return 1;
}

int apx_join_other(apx_status *st, apx_prefs *prefs, const char *ssid, const char *password,
                   int remember)
{
    if (!ssid[0] || strlen(ssid) > 32)
        return APX_JOIN_ERROR;
    if (!password || !password[0])
        return send_connect(st, prefs, ssid, "", remember, 0);
    if (!valid_password(password))
        return APX_JOIN_BAD_PASSWORD;
    return send_connect(st, prefs, ssid, password, remember, 1);
}
