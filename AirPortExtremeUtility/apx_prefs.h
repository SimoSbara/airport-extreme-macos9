/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef APX_PREFS_H_
#define APX_PREFS_H_

/*
 * apx_prefs.h - The "AirPort Extreme Prefs" file seen from the utility.
 *
 * The driver reads the file at startup (drv_prefs.c). The utility manages
 * only these lines and keeps every other one as it is (country, debug...):
 *
 *     ssid=Home               the network the driver joins at startup
 *     power=1                 0 = AirPort off at startup
 *     remember=Home           remembered networks, preferred first; the
 *     remember=Office         passwords are in the Keychain
 *
 * An old "password=" line (written by hand) is read, so that the utility
 * can move it to the Keychain, and then left out when drop_password is set.
 */

#define APX_REMEMBER_MAX    16

typedef struct apx_prefs {
    char ssid[33];
    char password[64];                  /* old password= line, "" = none */
    int  power;
    int  remembered_count;
    char remembered[APX_REMEMBER_MAX][33];
    int  drop_password;                 /* save without the password= line */
} apx_prefs;

/* 0 = read (or no file yet: defaults), otherwise a File Manager error */
int  apx_prefs_load(apx_prefs *p);
int  apx_prefs_save(const apx_prefs *p);

/* Remembered networks: remember moves the network to the top of the list */
int  apx_prefs_is_remembered(const apx_prefs *p, const char *ssid);
void apx_prefs_remember(apx_prefs *p, const char *ssid);
void apx_prefs_forget(apx_prefs *p, const char *ssid);

#endif /* APX_PREFS_H_ */
