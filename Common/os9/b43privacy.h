/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef B43PRIVACY_H_
#define B43PRIVACY_H_

/*
 * b43privacy.h - Hides MAC addresses in log text: logs are attached to
 * public problem reports, and the address of an access point locates it
 * (WiGLE and similar databases). Every "hh:hh:hh:hh:hh:hh" keeps the
 * manufacturer's part and loses the rest: "2c:91:ab:xx:xx:xx". It catches
 * every format (%pM of Linux, MACSTR of wpa_supplicant, our own), upper
 * or lower case. The length does not change, so it works in place.
 *
 * Header only, no C library: used by the driver and the Tester.
 */

static inline int b43privacy_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* In place; len characters of s */
static inline void b43privacy_hide_macs(char *s, unsigned long len)
{
    unsigned long i, j;

    for (i = 0; i + 17 <= len; i++) {
        for (j = 0; j < 17; j++) {
            char c = s[i + j];

            if ((j % 3 == 2) ? c != ':' : !b43privacy_hex(c))
                break;
        }
        if (j < 17)
            continue;
        /* Not part of a longer hex run (e.g. an IPv6 address) */
        if ((i > 0 && (b43privacy_hex(s[i - 1]) || s[i - 1] == ':')) ||
            (i + 17 < len && (b43privacy_hex(s[i + 17]) || s[i + 17] == ':')))
            continue;
        for (j = 9; j < 17; j++)
            s[i + j] = (j % 3 == 2) ? ':' : 'x';
        i += 16;
    }
}

#endif /* B43PRIVACY_H_ */
