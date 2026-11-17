/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef B43OS9_PRIV_H_
#define B43OS9_PRIV_H_

/*
 * Internal state of the Mac OS 9 glue. Includes the Linux headers:
 * must not be included by the Tester or the driver (they use b43os9.h).
 */
#include <linux/types.h>
#include <linux/firmware.h>
#include <linux/netdevice.h>
#include <net/mac80211.h>
#include <net/cfg80211.h>
#include "b43compat_os9.h"
#include "b43os9.h"
#include "b43os9_wpa.h"

struct b43os9;
extern struct b43os9 *b43os9_instance;

struct b43os9 {
    const b43_env *env;
    b43os9_events ev;
    b43os9_firmware fw;
    struct net_device *dev;         /* "wlan0", created by mac80211 */
    b43os9_supp_ops supp;           /* wpa_supplicant, if in use */
};

static inline const b43os9_supp_ops *b43os9_supp(void)
{
    return (b43os9_instance && b43os9_instance->supp.ctx) ? &b43os9_instance->supp : NULL;
}

/* There is a single glue instance: used by request_firmware() and the events */
extern struct b43os9 *b43os9_instance;

static inline const b43os9_events *b43os9_ev(void)
{
    return b43os9_instance ? &b43os9_instance->ev : NULL;
}

/* Linux module parameters (module_param, see b43compat_net.h) */
extern void *const b43compat_param_pio;             /* b43 */
extern void *const b43compat_param_verbose;         /* b43 */
extern void *const b43compat_param_nohwcrypt;       /* b43 */
extern void *const b43compat_param_qos;             /* b43 */
extern void *const b43compat_param_ieee80211_regdom; /* cfg80211 */

/* b43os9_hw.c: see there */
void b43os9_ack_address_masks(struct b43os9 *b);

#endif /* B43OS9_PRIV_H_ */
