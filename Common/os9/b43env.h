/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef B43ENV_H_
#define B43ENV_H_

#ifdef __MWERKS__
#pragma options align=power  /* same as the Tester/driver */
#endif

/*
 * b43env.h - What the port requires from the host system (Tester or driver).
 *
 * Standard C types only: this header can be included together with either
 * the Mac OS headers or the Linux headers.
 */
typedef struct b43_env {
    void *ctx;                                              /* passed to every callback */

    /* Time */
    void          (*delay_us)(void *ctx, unsigned long us); /* busy wait */
    unsigned long (*time_us)(void *ctx);                    /* free-running counter, wrap ok */

    /* Log: already formatted text, newline included */
    void          (*log)(void *ctx, const char *text);

    /* Memory (NewPtrClear in the Tester, PoolAllocateResident in the driver) */
    void         *(*alloc)(void *ctx, unsigned long size);
    void          (*free)(void *ctx, void *p);

    /* PCI configuration space of the card (ExpMgrConfigRead/WriteLong) */
    unsigned long (*pci_cfg_read32)(void *ctx, unsigned short offset);
    void          (*pci_cfg_write32)(void *ctx, unsigned short offset, unsigned long value);
} b43_env;

#ifdef __MWERKS__
#pragma options align=reset
#endif

#endif /* B43ENV_H_ */
