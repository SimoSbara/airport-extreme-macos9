/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * drv_env.c - The b43_env of the Open Transport driver (see Common/os9/b43env.h).
 *
 * Inside Open Transport the stack runs as a deferred task, where the
 * Memory Manager and the File Manager cannot be called. So:
 *
 *   memory  one resident block, allocated at task time when the driver is
 *           loaded, split by drv_pool.c.
 *   time    UpTime() of Driver Services, usable at any execution level.
 *   log     a ring buffer in resident memory, read by the applications
 *           through the status block (apx_status.h).
 *
 * The allocator is not reentrant: the stack is only entered with the stack
 * lock held (AirPortDriver.c), and so are kmalloc and kfree.
 */
#include <MacTypes.h>
#include <DriverServices.h>
#include <NameRegistry.h>
#include <PCI.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#include "drv.h"
#include "b43privacy.h"
#include "drv_pool.h"

/* Formatter of Common/compat (b43compat.c): %pM etc., no libc stdio */
int b43c_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int b43c_snprintf(char *buf, size_t size, const char *fmt, ...);

#define LOG_SIZE        (256UL * 1024)

/* -------------------------------------------------------------------------
 * Memory pool (drv_pool.c)
 * ------------------------------------------------------------------------- */
static void           *g_pool_mem;  /* as returned by PoolAllocateResident */
static drv_pool_stats  g_pool;
static apx_status     *g_st;
static RegEntryID     *g_node;
static b43_env         g_env;
static int             g_line_start = 1;

/* Copies the counters to the status block read by the applications */
static void pool_publish(void)
{
    g_st->mem_size = g_pool.size;
    g_st->mem_used = g_pool.used;
    g_st->mem_peak = g_pool.peak;
    g_st->mem_failures = g_pool.failures;
    g_st->mem_bad_frees = g_pool.bad_frees;
}

/* Block written after its free. drv_log's code address lets the other
 * addresses be turned into offsets in the binary (link map). */
static void pool_report(void *block, unsigned long size, unsigned long offset,
                        unsigned long alloc_caller, unsigned long free_caller)
{
    drv_log("apx: block %p (%lu bytes) written after free at offset %lu: allocated by %08lX,"
            " freed by %08lX, drv_log at %08lX\n", block, size, offset, alloc_caller,
            free_caller, *(const unsigned long *)(const void *)drv_log);
}

static void *pool_alloc(void *ctx, unsigned long size)
{
    void *p = drv_pool_alloc(size);

    (void)ctx;
    pool_publish();
    return p;
}

static void pool_free(void *ctx, void *p)
{
    unsigned long bad = g_pool.bad_frees;

    (void)ctx;
    drv_pool_free(p);
    if (g_pool.bad_frees != bad)
        drv_log("apx: kfree of an invalid block %p\n", p);
    pool_publish();
}

/* -------------------------------------------------------------------------
 * Time
 * ------------------------------------------------------------------------- */
static unsigned long long now_us64(void)
{
    AbsoluteTime t = UpTime();
    Nanoseconds ns = AbsoluteToNanoseconds(t);
    unsigned long long v = ((unsigned long long)ns.hi << 32) | ns.lo;

    return v / 1000ULL;
}

unsigned long drv_time_ms(void)
{
    return (unsigned long)(now_us64() / 1000ULL);
}

static unsigned long env_time_us(void *ctx)
{
    (void)ctx;
    return (unsigned long)now_us64();
}

static void env_delay_us(void *ctx, unsigned long us)
{
    unsigned long long end = now_us64() + us;

    (void)ctx;
    while (now_us64() < end) {
    }
}

/* -------------------------------------------------------------------------
 * Log
 * ------------------------------------------------------------------------- */
static void log_putc(char c)
{
    g_st->log_buf[g_st->log_head % g_st->log_size] = c;
    g_st->log_head++;
    drv_screen_putc(c);                 /* screen_log=1: on the screen too */
}

static void log_puts(const char *s)
{
    while (*s)
        log_putc(*s++);
}

static void env_log(void *ctx, const char *text)
{
    char stamp[16], chunk[512];
    const char *p;
    unsigned long n;

    (void)ctx;
    if (!g_st || !g_st->log_buf)
        return;
    while (*text) {
        /* MAC addresses are cut to the manufacturer's part before they
         * reach the log (it ends up in public problem reports) */
        for (n = 0; text[n] && n < sizeof(chunk) - 1; n++)
            chunk[n] = text[n];
        text += n;
        b43privacy_hide_macs(chunk, n);
        for (p = chunk; p < chunk + n; p++) {
            if (g_line_start) {
                /* "[seconds.milliseconds] " since startup */
                unsigned long ms = drv_time_ms();
                b43c_snprintf(stamp, sizeof(stamp), "[%lu.%03lu] ", ms / 1000, ms % 1000);
                log_puts(stamp);
                g_line_start = 0;
            }
            log_putc(*p);
            if (*p == '\n')
                g_line_start = 1;
        }
    }
}

void drv_log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    b43c_vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    env_log(NULL, buf);
}

/* -------------------------------------------------------------------------
 * PCI configuration space
 * ------------------------------------------------------------------------- */
static unsigned long env_cfg_read32(void *ctx, unsigned short offset)
{
    UInt32 v = 0;

    (void)ctx;
    ExpMgrConfigReadLong(g_node, (LogicalAddress)(unsigned long)offset, &v);
    return v;
}

static void env_cfg_write32(void *ctx, unsigned short offset, unsigned long value)
{
    (void)ctx;
    ExpMgrConfigWriteLong(g_node, (LogicalAddress)(unsigned long)offset, value);
}

/* -------------------------------------------------------------------------
 * DMA: bus addresses of the pool
 *
 * The pool is resident (held) memory: its pages never move. One
 * PrepareMemoryForIO over the whole pool, at task time, gives the physical
 * address of every page and keeps it prepared until the driver unloads; the
 * stack then looks addresses up in that table at any execution level.
 * ------------------------------------------------------------------------- */
#define PAGE_BYTES      4096UL
#define DMA_LIMIT       0x40000000UL    /* 30-bit DMA engines (BCM4306) */

static unsigned long       g_pool_base;     /* page aligned */
static unsigned long       g_pool_pages;
static PhysicalAddress    *g_phys;          /* one entry per page */
static IOPreparationTable  g_iop;
static int                 g_prepared;

static unsigned long env_dma_phys(void *ctx, const void *p)
{
    unsigned long a = (unsigned long)p, idx;

    (void)ctx;
    if (a < g_pool_base)
        return ~0UL;
    idx = (a - g_pool_base) / PAGE_BYTES;
    if (idx >= g_pool_pages)
        return ~0UL;
    return ((unsigned long)g_phys[idx] & ~(PAGE_BYTES - 1)) + (a & (PAGE_BYTES - 1));
}

/* Descriptor pages: carved from the pool at task time and made uncached
 * there (SetProcessorCacheMode is a task-level call); b43 needs six (one
 * RX and five TX rings) */
#define DMA_PAGES       8

static unsigned char *g_dma_pages;
static unsigned char  g_dma_page_used[DMA_PAGES];

static void *env_dma_page_alloc(void *ctx, unsigned long *bus)
{
    int i;

    (void)ctx;
    for (i = 0; i < DMA_PAGES; i++) {
        if (!g_dma_page_used[i]) {
            void *p = g_dma_pages + (unsigned long)i * PAGE_BYTES;
            g_dma_page_used[i] = 1;
            *bus = env_dma_phys(NULL, p);
            return p;
        }
    }
    return NULL;
}

static void env_dma_page_free(void *ctx, void *page)
{
    unsigned long i = ((unsigned char *)page - g_dma_pages) / PAGE_BYTES;

    (void)ctx;
    if (i < DMA_PAGES)
        g_dma_page_used[i] = 0;
}

static int dma_pages_init(int uncached)
{
    void *raw = drv_pool_alloc((DMA_PAGES + 1) * PAGE_BYTES);
    OSStatus err = noErr;

    if (!raw)
        return -1;
    g_dma_pages = (unsigned char *)(((unsigned long)raw + PAGE_BYTES - 1) & ~(PAGE_BYTES - 1));
    /* By default the pages stay cached and the stack flushes them before
     * every write to a DMA register (b43 only writes descriptors, the chip
     * only reads them). dma_uncached=1 changes their cache mode instead,
     * which touches the system's memory mapping. */
    if (uncached) {
        FlushProcessorCache(kCurrentAddressSpaceID, g_dma_pages, DMA_PAGES * PAGE_BYTES);
        err = SetProcessorCacheMode(kCurrentAddressSpaceID, g_dma_pages,
                                    DMA_PAGES * PAGE_BYTES, kProcessorCacheModeInhibited);
    }
    g_env.dma_pages_uncached = (uncached && err == noErr);
    drv_log("apx: DMA: descriptor pages %p: %s (%ld)\n", g_dma_pages,
            g_env.dma_pages_uncached ? "uncached"
                                     : "cached, flushed before each DMA register write",
            (long)err);
    g_env.dma_page_alloc = env_dma_page_alloc;
    g_env.dma_page_free = env_dma_page_free;
    return 0;
}

int drv_env_dma_init(int uncached)
{
    unsigned long i, runs = 1, lo = ~0UL, hi = 0, len = g_pool_pages * PAGE_BYTES;
    OSStatus err;

    if (!g_pool_pages)
        return -1;
    g_phys = (PhysicalAddress *)PoolAllocateResident(g_pool_pages * sizeof(PhysicalAddress), true);
    if (!g_phys)
        return -1;
    memset(&g_iop, 0, sizeof(g_iop));
    g_iop.options = kIOLogicalRanges | kIOIsInput | kIOIsOutput;
    g_iop.addressSpace = kCurrentAddressSpaceID;
    g_iop.mappingEntryCount = g_pool_pages;
    g_iop.physicalMapping = g_phys;
    g_iop.rangeInfo.range.base = (void *)g_pool_base;
    g_iop.rangeInfo.range.length = len;
    err = PrepareMemoryForIO(&g_iop);
    if (err != noErr || !(g_iop.state & kIOStateDone) || g_iop.lengthPrepared != len) {
        drv_log("apx: DMA: PrepareMemoryForIO failed (%ld, state %lu, %lu of %lu bytes)\n",
                (long)err, (unsigned long)g_iop.state, (unsigned long)g_iop.lengthPrepared, len);
        if (err == noErr)
            CheckpointIO(g_iop.preparationID, 0);
        return -1;
    }
    g_prepared = 1;
    for (i = 0; i < g_pool_pages; i++) {
        unsigned long ph = (unsigned long)g_phys[i] & ~(PAGE_BYTES - 1);
        if (ph < lo)
            lo = ph;
        if (ph + PAGE_BYTES > hi)
            hi = ph + PAGE_BYTES;
        if (i && ph != ((unsigned long)g_phys[i - 1] & ~(PAGE_BYTES - 1)) + PAGE_BYTES)
            runs++;
    }
    drv_log("apx: DMA: pool %08lx, %lu pages, bus addresses %08lx-%08lx in %lu contiguous runs\n",
            g_pool_base, g_pool_pages, lo, hi - 1, runs);
    if (hi > DMA_LIMIT) {
        drv_log("apx: DMA: memory above 1 GB, out of reach of the chip: using PIO\n");
        return -1;
    }
    if (dma_pages_init(uncached)) {
        drv_log("apx: DMA: no memory for the descriptor pages: using PIO\n");
        return -1;
    }
    g_env.dma_phys = env_dma_phys;
    return 0;
}

/* -------------------------------------------------------------------------
 * Setup (task time)
 * ------------------------------------------------------------------------- */
int drv_env_init(RegEntryID *node, unsigned long pool_bytes, apx_status *st)
{
    g_st = st;
    g_node = node;

    st->log_buf = (char *)PoolAllocateResident(LOG_SIZE, true);
    if (!st->log_buf)
        return -1;
    st->log_size = LOG_SIZE;
    st->log_head = 0;

    /* Page aligned, whole pages (for the DMA table) */
    pool_bytes &= ~(PAGE_BYTES - 1);
    g_pool_mem = PoolAllocateResident(pool_bytes + PAGE_BYTES, false);
    if (!g_pool_mem)
        return -1;
    g_pool_base = ((unsigned long)g_pool_mem + PAGE_BYTES - 1) & ~(PAGE_BYTES - 1);
    g_pool_pages = pool_bytes / PAGE_BYTES;
    g_pool.report = pool_report;
    drv_pool_init((void *)g_pool_base, pool_bytes, &g_pool);
    pool_publish();

    memset(&g_env, 0, sizeof(g_env));
    g_env.ctx = NULL;
    g_env.delay_us = env_delay_us;
    g_env.time_us = env_time_us;
    g_env.log = env_log;
    g_env.alloc = pool_alloc;
    g_env.free = pool_free;
    g_env.pci_cfg_read32 = env_cfg_read32;
    g_env.pci_cfg_write32 = env_cfg_write32;
    return 0;
}

/* Frees everything the stack allocated, and the log */
void drv_env_free(void)
{
    if (g_prepared) {
        CheckpointIO(g_iop.preparationID, 0);
        g_prepared = 0;
    }
    g_env.dma_phys = NULL;
    if (g_pool_mem) {
        PoolDeallocate(g_pool_mem);
        g_pool_mem = NULL;
    }
    if (g_st && g_st->log_buf) {
        PoolDeallocate(g_st->log_buf);
        g_st->log_buf = NULL;
        g_st->log_size = 0;
    }
}

const b43_env *drv_env(void)
{
    return &g_env;
}
