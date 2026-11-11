/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef APX_DRV_POOL_H_
#define APX_DRV_POOL_H_

/*
 * drv_pool.h - Memory allocator of the driver, for the Linux stack.
 *
 * Splits one block allocated in advance (first fit, freed blocks merged
 * with their neighbours), so it works at deferred task time. Not reentrant.
 *
 * Debugging, as Linux's slab poisoning: freed memory is filled with 0x6B
 * (a use after free shows up as the address 6B6B6B6B) and kept aside for
 * a while (quarantine). When it leaves the quarantine the poison is
 * checked: if something wrote to it after the free, report() is called
 * with the code address that freed it and that allocated it.
 *
 * Also used by the Tester, so that it runs with the same memory behaviour
 * as the driver.
 */
typedef struct drv_pool_stats {
    unsigned long size, used, peak;     /* bytes */
    unsigned long failures;             /* allocations that did not fit */
    unsigned long bad_frees;            /* free of a block not allocated here */
    unsigned long poison_errors;        /* blocks written after their free */
    /* Called for every poison error (may be NULL) */
    void (*report)(void *block, unsigned long size, unsigned long offset,
                   unsigned long alloc_caller, unsigned long free_caller);
} drv_pool_stats;

/* stats->report is kept, the counters are cleared */
void  drv_pool_init(void *mem, unsigned long bytes, drv_pool_stats *stats);
void *drv_pool_alloc(unsigned long size);
void  drv_pool_free(void *p);

#endif /* APX_DRV_POOL_H_ */
