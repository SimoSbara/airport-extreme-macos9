/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef B43COMPAT_OS9_H_
#define B43COMPAT_OS9_H_

/*
 * Internal interface between the compatibility layer (Common/compat) and the
 * Mac OS 9 glue (Common/os9). The Tester and the driver do not use it.
 */
#include <stdarg.h>
#include "b43env.h"

extern const b43_env *b43compat_env;

u64  b43compat_time_us64(void);
int  b43compat_run_work(void);
void b43compat_log_va(const char *prefix, const char *fmt, va_list ap);

/* Link to the Mac OS 9 network stack (b43compat_net.c):
 * rx receives every packet mac80211 hands to the stack (netif_rx) and takes
 * ownership of it; netdev_* report the registered devices. */
struct sk_buff;
struct net_device;
typedef struct {
    void *ctx;
    void (*rx)(void *ctx, struct sk_buff *skb);
    void (*netdev_registered)(void *ctx, struct net_device *dev);
    void (*netdev_unregistered)(void *ctx, struct net_device *dev);
} b43compat_net_hooks_t;
extern b43compat_net_hooks_t b43compat_net_hooks;
extern int b43compat_pr_debug;

/* Sets up b43compat_pci_host (the card's PCI device) by reading the
 * configuration space: vendor/device, subsystem, revision, IRQ.
 * bar0 is the MMIO mapping already set up by Mac OS ("AAPL,address"). */
void b43compat_pci_host_init(void __iomem *bar0, const char *name);

/* Linux module init functions (see fs_initcall/module_init in
 * b43compat_device.h), to be called in kernel order */
int  b43compat_initcall_cfg80211_init(void);
int  b43compat_initcall_ieee80211_init(void);
int  b43compat_initcall_ssb_modinit(void);
int  b43compat_initcall_regulatory_init_db(void);
int  b43compat_initcall_b43_init(void);

#endif /* B43COMPAT_OS9_H_ */
