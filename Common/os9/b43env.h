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

    /* DMA (optional, NULL = PIO only): the PCI bus address of a byte of
     * memory returned by alloc(), or ~0 if it has none. The memory must stay
     * resident at that address while the stack runs. */
    unsigned long (*dma_phys)(void *ctx, const void *p);

    /* DMA descriptor memory (needed with dma_phys): one page (4096 bytes,
     * page aligned) the CPU does not cache, so the chip and the CPU always
     * see the same descriptors, as Linux's dma_alloc_coherent on machines
     * without cache snooping. NULL if none is left. */
    void         *(*dma_page_alloc)(void *ctx, unsigned long *bus);
    void          (*dma_page_free)(void *ctx, void *page);
    /* 0: the host could not make those pages uncached; the descriptors are
     * then flushed from the cache before every write to a DMA register */
    int             dma_pages_uncached;
} b43_env;

#ifdef __MWERKS__
#pragma options align=reset
#endif

#endif /* B43ENV_H_ */
