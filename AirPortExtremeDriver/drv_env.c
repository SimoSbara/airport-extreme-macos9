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
}

static void log_puts(const char *s)
{
    while (*s)
        log_putc(*s++);
}

static void env_log(void *ctx, const char *text)
{
    char stamp[16];

    (void)ctx;
    if (!g_st || !g_st->log_buf)
        return;
    for (; *text; text++) {
        if (g_line_start) {
            /* "[seconds.milliseconds] " since startup */
            unsigned long ms = drv_time_ms();
            b43c_snprintf(stamp, sizeof(stamp), "[%lu.%03lu] ", ms / 1000, ms % 1000);
            log_puts(stamp);
            g_line_start = 0;
        }
        log_putc(*text);
        if (*text == '\n')
            g_line_start = 1;
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

    g_pool_mem = PoolAllocateResident(pool_bytes, false);
    if (!g_pool_mem)
        return -1;
    g_pool.report = pool_report;
    drv_pool_init(g_pool_mem, pool_bytes, &g_pool);
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
