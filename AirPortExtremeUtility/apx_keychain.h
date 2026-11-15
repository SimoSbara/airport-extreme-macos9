/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef APX_KEYCHAIN_H_
#define APX_KEYCHAIN_H_

/*
 * apx_keychain.h - Network passwords in the Mac OS 9 Keychain: generic
 * passwords with service "AirPort Extreme" and the network name as account.
 * The Keychain Manager asks for the keychain password itself when the
 * keychain is locked.
 */

/* 0 = found (zero-terminated in password), otherwise a Keychain error
 * (errKCItemNotFound -25300 when there is none) */
long apx_kc_get(const char *ssid, char *password, unsigned long size);

/* Adds the password, or replaces the one already there. 0 = done. */
long apx_kc_set(const char *ssid, const char *password);

/* 0 = removed (or there was none) */
long apx_kc_delete(const char *ssid);

#endif /* APX_KEYCHAIN_H_ */
