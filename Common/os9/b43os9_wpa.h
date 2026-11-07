/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef B43OS9_WPA_H_
#define B43OS9_WPA_H_

#ifdef __MWERKS__
#pragma options align=power  /* same as the Tester/driver */
#endif

/*
 * b43os9_wpa.h - Internal link between the Mac OS 9 glue (b43os9.c,
 * Linux side) and the supplicant (Common/wpa/os9/wpas_os9.c,
 * wpa_supplicant side). Standard C types only: the two sides share no headers.
 *
 * On Linux wpa_supplicant receives events from nl80211 and EAPOL frames from a
 * socket; here it receives them through these callbacks.
 */
#include "b43os9.h"

typedef struct b43os9_supp_ops {
    void *ctx;
    /* Association completed with bssid (nl80211 connect event) */
    void (*assoc)(void *ctx, const unsigned char *bssid);
    /* Association lost (disconnect event) */
    void (*disassoc)(void *ctx);
    /* EAPOL frame received: src = AP address, buf = 802.1X header
     * and data (after the Ethernet header) */
    void (*rx_eapol)(void *ctx, const unsigned char *src,
                     const unsigned char *buf, unsigned long len);
    /* On every b43os9_poll(): eloop timeouts */
    void (*poll)(void *ctx);
} b43os9_supp_ops;

/* Linux side (b43os9.c) */
void b43os9_set_supplicant(b43os9 *b, const b43os9_supp_ops *ops);
void b43os9_supp_authorized(b43os9 *b, const unsigned char *bssid);

/* wpa_supplicant side: time and log, from the b43_env passed to b43os9_attach */
const b43_env *b43os9_supp_env(void);

#ifdef __MWERKS__
#pragma options align=reset
#endif

#endif /* B43OS9_WPA_H_ */
