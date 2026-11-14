/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef APX_DRV_H_
#define APX_DRV_H_

/*
 * drv.h - Private interface between the files of the Open Transport driver.
 *
 *   AirPortDriver.c   Open Transport module: STREAMS, DLPI, timer
 *   drv_link.c        connection manager (scan, connect, reconnect)
 *   drv_env.c         b43_env for Common/: memory pool, time, log, PCI
 *   drv_prefs.c       the "AirPort Extreme Prefs" file
 *   drv_screen.c      the log drawn on the screen (diagnostics of freezes)
 */
#include <MacTypes.h>
#include <NameRegistry.h>
#include "b43os9.h"
#include "apx_status.h"

#define APX_DRIVER_VERSION  "1.0d2"

/* Settings read from the preferences file (drv_prefs.c) */
typedef struct drv_prefs {
    char          ssid[33];
    char          password[64];         /* empty: open network */
    int           power;                /* 0 = AirPort off at startup */
    char          country[3];           /* ISO 3166, "" = world ("00") */
    unsigned long memory_kb;            /* memory pool of the stack */
    int           verbose;              /* b43 verbose (0..3) */
    int           wpa_debug;            /* wpa_supplicant debug level */
    int           pr_debug;             /* pr_debug of ssb/mac80211/cfg80211 */
    int           debug;                /* 1 = driver diagnostics in the log */
    int           dma;                  /* 1 = DMA transfers, 0 = PIO */
    int           dma_uncached;         /* 1 = SetProcessorCacheMode on the rings */
    int           irq;                  /* 1 = chip interrupts, 0 = polling only */
    int           screen_log;           /* 1 = the log is also drawn on the screen */
    int           watchdog;             /* 1 = report where a run that never ends is */
    char          firmware[16];         /* built-in firmware version, "" = default */
} drv_prefs;


void drv_prefs_defaults(drv_prefs *p);
int  drv_prefs_load(drv_prefs *p);      /* 0 = read, otherwise File Manager error */

/* Environment (drv_env.c). Created at task time, used at any time. */
int   drv_env_init(RegEntryID *node, unsigned long pool_bytes, apx_status *st);
int   drv_env_dma_init(int uncached);   /* 0 = DMA possible (sets dma_phys) */
void  drv_env_free(void);
const b43_env *drv_env(void);
unsigned long drv_time_ms(void);
void  drv_log(const char *fmt, ...);

/* The log drawn on the main screen (drv_screen.c): it survives a freeze */
int   drv_screen_init(void);            /* task time; 0 = on */
void  drv_screen_putc(char c);          /* any level; nothing if not on */

/* Connection manager (drv_link.c), called with the stack lock held */
void  drv_link_init(b43os9 *b, const drv_prefs *prefs, apx_status *st);
void  drv_link_step(int want_up);
int   drv_link_can_send(void);
int   drv_link_take_network_change(void);
const b43os9_events *drv_link_events(void);

/* AirPortDriver.c: hands a received Ethernet frame to Open Transport */
void  drv_deliver(const unsigned char *frame, unsigned long len);

#endif /* APX_DRV_H_ */
