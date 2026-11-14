/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * drv_pool.c - First-fit allocator over one pre-allocated block (drv_pool.h).
 */
#include <string.h>

#include "drv_pool.h"

/* Block header: 32 bytes, so that every allocation is 16-byte aligned */
typedef struct blk {
    unsigned long size;             /* whole block, header included */
    struct blk   *next;             /* next free block (free list only) */
    unsigned long magic;
    unsigned long req;              /* size requested */
    unsigned long alloc_caller;     /* code that allocated it */
    unsigned long free_caller;      /* code that freed it */
    unsigned long pad[2];
} blk;

#define BLK_FREE        0x46524545UL    /* 'FREE' */
#define BLK_USED        0x55534544UL    /* 'USED' */
#define BLK_QUAR        0x51554152UL    /* 'QUAR': freed, in quarantine */
#define BLK_MIN         64UL            /* do not leave smaller fragments */
#define POISON_FREE     0x6B            /* as Linux's mm/slab */
#define QUARANTINE      128             /* freed blocks kept aside */

static blk            *g_free;          /* free list, sorted by address */
static drv_pool_stats *g_st;
static blk            *g_quar[QUARANTINE];
static unsigned int    g_quar_pos;

/* Address in the caller of kfree()/kmalloc(): drv_pool_* is called by the
 * env callback, which is called by kmalloc/kfree (or os_malloc/os_free) */
#define CALLER()    ((unsigned long)__builtin_return_address(2))

void drv_pool_init(void *mem, unsigned long bytes, drv_pool_stats *stats)
{
    unsigned long start = ((unsigned long)mem + 15) & ~15UL;
    unsigned long end = ((unsigned long)mem + bytes) & ~15UL;
    void (*report)(void *, unsigned long, unsigned long, unsigned long, unsigned long) =
        stats->report;

    g_st = stats;
    memset(stats, 0, sizeof(*stats));
    stats->report = report;
    memset(g_quar, 0, sizeof(g_quar));
    g_quar_pos = 0;
    g_free = (blk *)start;
    g_free->size = end - start;
    g_free->next = NULL;
    g_free->magic = BLK_FREE;
    stats->size = g_free->size;
}

/*
 * Blocks from PAGE_FIT_MIN bytes to a page never cross a page boundary: a
 * page is physically contiguous even with virtual memory on, so the DMA
 * buffers of the stack (skb data, 1.5 to 2.5 KB) can be given to the chip
 * in place (b43compat_dma.c) instead of through a bounce page.
 */
#define PAGE_BYTES      4096UL
#define PAGE_FIT_MIN    512UL

/* Bytes to skip at the start of free block b so that a block of size
 * bytes does not cross a page; the skipped part stays a free block */
static unsigned long page_fit_pad(const blk *b, unsigned long size)
{
    unsigned long u = (unsigned long)(b + 1), pad;

    if (size < PAGE_FIT_MIN || size > PAGE_BYTES)
        return 0;
    if ((u & ~(PAGE_BYTES - 1)) == ((u + size - 1) & ~(PAGE_BYTES - 1)))
        return 0;
    pad = ((u & ~(PAGE_BYTES - 1)) + PAGE_BYTES) - u;
    if (pad < BLK_MIN)
        pad += PAGE_BYTES;
    return pad;
}

void *drv_pool_alloc(unsigned long size)
{
    unsigned long need = ((size + 15) & ~15UL) + sizeof(blk);
    blk **pp, *b;

    for (pp = &g_free; (b = *pp) != NULL; pp = &b->next) {
        unsigned long pad = page_fit_pad(b, size);

        if (b->size < pad + need)
            continue;
        if (pad) {
            /* The head stays free, the allocation starts on a page */
            blk *nb = (blk *)((char *)b + pad);
            nb->size = b->size - pad;
            nb->next = b->next;
            nb->magic = BLK_FREE;
            b->size = pad;
            b->next = nb;
            pp = &b->next;
            b = nb;
        }
        if (b->size - need >= BLK_MIN) {
            /* Split: the tail stays free */
            blk *rest = (blk *)((char *)b + need);
            rest->size = b->size - need;
            rest->next = b->next;
            rest->magic = BLK_FREE;
            *pp = rest;
            b->size = need;
        } else {
            *pp = b->next;
        }
        b->next = NULL;
        b->magic = BLK_USED;
        b->req = size;
        b->alloc_caller = CALLER();
        b->free_caller = 0;
        g_st->used += b->size;
        if (g_st->used > g_st->peak)
            g_st->peak = g_st->used;
        memset(b + 1, 0, b->size - sizeof(blk));
        return b + 1;
    }
    g_st->failures++;
    return NULL;
}

/* Back to the free list, merged with the neighbours */
static void release(blk *b)
{
    blk **pp, *prev = NULL, *next;

    b->magic = BLK_FREE;
    for (pp = &g_free; (next = *pp) != NULL && next < b; pp = &next->next)
        prev = next;
    b->next = next;
    *pp = b;
    if (next && (char *)b + b->size == (char *)next) {
        b->size += next->size;
        b->next = next->next;
        memset(next, POISON_FREE, sizeof(blk));
    }
    if (prev && (char *)prev + prev->size == (char *)b) {
        prev->size += b->size;
        prev->next = b->next;
        memset(b, POISON_FREE, sizeof(blk));
    }
}

/* The poison of a block leaving the quarantine must be intact */
static void check_poison(blk *b)
{
    const unsigned char *p = (const unsigned char *)(b + 1);
    unsigned long n = b->size - sizeof(blk), i;

    for (i = 0; i < n; i++) {
        if (p[i] != POISON_FREE) {
            g_st->poison_errors++;
            if (g_st->report)
                g_st->report(b + 1, b->req, i, b->alloc_caller, b->free_caller);
            return;
        }
    }
}

void drv_pool_free(void *p)
{
    blk *b, *old;

    if (!p)
        return;
    b = (blk *)p - 1;
    if (((unsigned long)p & 15) || b->magic != BLK_USED) {
        g_st->bad_frees++;
        return;
    }
    g_st->used -= b->size;
    b->magic = BLK_QUAR;
    b->free_caller = CALLER();
    memset(b + 1, POISON_FREE, b->size - sizeof(blk));

    /* Quarantine: the oldest block leaves it and becomes free */
    old = g_quar[g_quar_pos];
    g_quar[g_quar_pos] = b;
    g_quar_pos = (g_quar_pos + 1) % QUARANTINE;
    if (old) {
        check_poison(old);
        release(old);
    }
}
