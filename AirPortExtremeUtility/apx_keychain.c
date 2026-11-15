/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * apx_keychain.c - Network passwords in the Keychain (KeychainLib).
 */
#include <string.h>
#include <MacTypes.h>
#include <KeychainCore.h>

#include "apx_keychain.h"

#define ERR_ITEM_NOT_FOUND  (-25300)    /* errKCItemNotFound */

static unsigned char kService[] = "\pAirPort Extreme";

static void to_pascal(unsigned char *dst, const char *src)
{
    unsigned long n = strlen(src);

    if (n > 255)
        n = 255;
    dst[0] = (unsigned char)n;
    memcpy(dst + 1, src, n);
}

long apx_kc_get(const char *ssid, char *password, unsigned long size)
{
    Str255 account;
    UInt32 len = 0;
    OSStatus err;

    password[0] = '\0';
    to_pascal(account, ssid);
    err = KCFindGenericPassword(kService, account, (UInt32)(size - 1), password, &len, NULL);
    if (err != noErr)
        return err;
    if (len >= size)
        len = (UInt32)(size - 1);
    password[len] = '\0';
    return 0;
}

long apx_kc_set(const char *ssid, const char *password)
{
    Str255 account;
    KCItemRef item = NULL;
    UInt32 len = 0;
    char old[64];
    OSStatus err;

    to_pascal(account, ssid);
    err = KCFindGenericPassword(kService, account, sizeof(old) - 1, old, &len, &item);
    memset(old, 0, sizeof(old));
    if (err == noErr && item) {
        err = KCSetData(item, (UInt32)strlen(password), password);
        if (err == noErr)
            err = KCUpdateItem(item);
        KCReleaseItem(&item);
        return err;
    }
    if (err != ERR_ITEM_NOT_FOUND)
        return err;
    return KCAddGenericPassword(kService, account, (UInt32)strlen(password), password, NULL);
}

long apx_kc_delete(const char *ssid)
{
    Str255 account;
    KCItemRef item = NULL;
    UInt32 len = 0;
    char old[64];
    OSStatus err;

    to_pascal(account, ssid);
    err = KCFindGenericPassword(kService, account, sizeof(old) - 1, old, &len, &item);
    memset(old, 0, sizeof(old));
    if (err == ERR_ITEM_NOT_FOUND)
        return 0;
    if (err != noErr || !item)
        return err;
    err = KCDeleteItem(item);
    KCReleaseItem(&item);
    return err;
}
