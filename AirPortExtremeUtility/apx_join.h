/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef APX_JOIN_H_
#define APX_JOIN_H_

/*
 * apx_join.h - Joining a network: the password from the Keychain (or the
 * one just typed), the connect command, and the preferences file updated
 * so that the driver joins the same network at the next startup.
 */
#include "apx_client.h"
#include "apx_prefs.h"

enum {
    APX_JOIN_OK = 0,
    APX_JOIN_NEED_PASSWORD,     /* protected network, no password known */
    APX_JOIN_BAD_PASSWORD,      /* not 8..63 characters */
    APX_JOIN_UNSUPPORTED,       /* WEP */
    APX_JOIN_NO_DRIVER,         /* the driver does not answer */
    APX_JOIN_ERROR              /* the driver refused the command */
};

/* security: APX_SEC_* from the scan list. password: the one typed by the
 * user, or NULL to take it from the Keychain. remember: keep the network
 * in the remembered list and the typed password in the Keychain. */
int apx_join(apx_status *st, apx_prefs *prefs, const char *ssid, unsigned char security,
             const char *password, int remember);

/* A network that is not in the scan list ("Other..."): open without a
 * password, WPA/WPA2 with one */
int apx_join_other(apx_status *st, apx_prefs *prefs, const char *ssid, const char *password,
                   int remember);

/* An old password= line in the preferences file (written by hand before
 * the utility existed) moves to the Keychain and leaves the file, which
 * then holds no password in clear text. 1 = moved. */
int apx_move_password_to_keychain(apx_prefs *prefs);

#endif /* APX_JOIN_H_ */
