/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * b43compat_dma.c - linux/dma-mapping.h on Mac OS 9.
 *
 * The host gives the PCI bus address of any byte of the memory it hands to
 * kmalloc (b43_env.dma_phys: in the driver, a table made once with
 * PrepareMemoryForIO over the resident pool).
 *
 * Mac OS 9 does not promise that PCI DMA snoops the processor caches: its
 * own drivers rely on PrepareMemoryForIO/CheckpointIO around every transfer
 * to flush them. So this file works as Linux does on machines without
 * snooping (kernel/dma/direct.c + arch/powerpc/mm/dma-noncoherent.c):
 *
 *   dma_alloc_coherent  an uncached page from the host (b43_env.dma_page_
 *                       alloc): b43 asks for 4 KB descriptor rings, 4 KB
 *                       aligned.
 *   dma_map_single      the buffer's own bus address when its pages are
 *                       physically contiguous, otherwise a bounce page (as
 *                       kernel/dma/swiotlb.c). The buffer's cache lines are
 *                       written back and dropped (dcbf), so the chip reads
 *                       what the CPU wrote.
 *   dma_sync_for_cpu,   for buffers the device writes: the lines are
 *   dma_unmap_single    dropped again, so the CPU reads what the chip wrote
 *                       and not an old copy; bounce pages are copied back.
 *
 * Bounce copies follow the coherent model: the device sees what the CPU
 * wrote before a map or a sync_for_device, the CPU sees what the device
 * wrote after a sync_for_cpu or an unmap (b43 poisons RX buffers from the
 * CPU and checks them again, which needs exactly this).
 *
 * The flushes need the CPU address, while unmap/sync only get the bus
 * address: mappings the device writes to (the RX ring) are remembered.
 */
#include <linux/types.h>
#include <linux/skbuff.h>
#include "b43compat_os9.h"

#define B43C_DMA_PAGE       4096UL
#define B43C_BOUNCE_PAGES   32
#define B43C_RX_MAPS        512         /* b43's RX ring: 256 slots (B43_RXRING_SLOTS) */
#define B43C_CACHE_LINE     32UL        /* G3/G4; a larger line is still covered */

struct b43compat_dma_stats b43compat_dma_stats;

typedef struct {
    u8           *page;         /* NULL: not allocated */
    dma_addr_t    bus;
    void         *orig;         /* NULL: free */
    size_t        size;
} b43c_bounce;

/* A live mapping the device writes to: bus -> CPU address */
typedef struct {
    dma_addr_t    bus;
    void         *cpu;          /* NULL: free entry */
    size_t        size;
} b43c_rxmap;

#define B43C_COHERENT_MAX   16

static b43c_bounce g_bounce[B43C_BOUNCE_PAGES];
static b43c_rxmap  g_rxmap[B43C_RX_MAPS];
static void       *g_coherent[B43C_COHERENT_MAX];   /* descriptor pages */
static void *g_bounce_mem;
static u64 g_mask;

/* Writes back and invalidates the data cache lines of a range (dcbf is
 * allowed in user mode) */
static void b43c_cache_flush(const void *p, size_t size)
{
#if defined(__GNUC__) && (defined(__PPC__) || defined(__powerpc__))
    unsigned long a = (unsigned long)p & ~(B43C_CACHE_LINE - 1);
    unsigned long end = (unsigned long)p + size;

    for (; a < end; a += B43C_CACHE_LINE)
        __asm__ __volatile__("dcbf 0,%0" : : "r"(a) : "memory");
    __asm__ __volatile__("sync" : : : "memory");
#else
    (void)p;
    (void)size;
#endif
}

/* Bus address of [p, p+size) if it is physically contiguous */
static dma_addr_t b43c_dma_contig(const void *p, size_t size)
{
    unsigned long a = (unsigned long)p, last = a + size - 1, pg, phys;

    if (!b43compat_env->dma_phys || size == 0)
        return DMA_MAPPING_ERROR;
    phys = b43compat_env->dma_phys(b43compat_env->ctx, p);
    if (phys == ~0UL)
        return DMA_MAPPING_ERROR;
    for (pg = (a & ~(B43C_DMA_PAGE - 1)) + B43C_DMA_PAGE; pg <= last && pg > a;
         pg += B43C_DMA_PAGE) {
        if (b43compat_env->dma_phys(b43compat_env->ctx, (const void *)pg) != phys + (pg - a))
            return DMA_MAPPING_ERROR;
    }
    if ((u64)phys + size - 1 > g_mask)
        return DMA_MAPPING_ERROR;
    return (dma_addr_t)phys;
}

/* The bounce pages: one page each, so contiguous by construction */
static int b43c_bounce_init(void)
{
    unsigned long base;
    int i;

    if (g_bounce_mem)
        return 0;
    g_bounce_mem = kmalloc(B43C_BOUNCE_PAGES * B43C_DMA_PAGE + B43C_DMA_PAGE, GFP_KERNEL);
    if (!g_bounce_mem)
        return -ENOMEM;
    base = ((unsigned long)g_bounce_mem + B43C_DMA_PAGE - 1) & ~(B43C_DMA_PAGE - 1);
    for (i = 0; i < B43C_BOUNCE_PAGES; i++) {
        g_bounce[i].page = (u8 *)(base + (unsigned long)i * B43C_DMA_PAGE);
        g_bounce[i].bus = b43c_dma_contig(g_bounce[i].page, B43C_DMA_PAGE);
        g_bounce[i].orig = NULL;
    }
    return 0;
}

static b43c_bounce *b43c_bounce_find(dma_addr_t addr)
{
    int i;

    for (i = 0; i < B43C_BOUNCE_PAGES; i++)
        if (g_bounce[i].orig && g_bounce[i].bus == addr)
            return &g_bounce[i];
    return NULL;
}

static b43c_rxmap *b43c_rxmap_find(dma_addr_t addr)
{
    int i;

    for (i = 0; i < B43C_RX_MAPS; i++)
        if (g_rxmap[i].cpu && g_rxmap[i].bus == addr)
            return &g_rxmap[i];
    return NULL;
}

static int b43c_rxmap_add(dma_addr_t bus, void *cpu, size_t size)
{
    int i;

    for (i = 0; i < B43C_RX_MAPS; i++) {
        if (!g_rxmap[i].cpu) {
            g_rxmap[i].bus = bus;
            g_rxmap[i].cpu = cpu;
            g_rxmap[i].size = size;
            return 0;
        }
    }
    {
        static int told;
        if (!told++)
            printk("b43compat: more than %d DMA mappings for the device to write\n",
                   B43C_RX_MAPS);
    }
    return -ENOMEM;
}

int dma_set_mask_and_coherent(struct device *dev, u64 mask)
{
    (void)dev;
    /* b43 asks for 30, 32 or 64 bits and checks every address against
     * the engine's own limit; Mac OS 9 memory is below 4 GB */
    if (!b43compat_env->dma_phys || !b43compat_env->dma_page_alloc || mask < 0x3FFFFFFFULL)
        return -EIO;
    g_mask = mask;
    if (b43c_bounce_init())
        return -ENOMEM;
    return 0;
}

void *dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t flag)
{
    unsigned long bus = 0;
    u32 *p;
    size_t i;

    (void)dev;
    (void)flag;
    *dma_handle = DMA_MAPPING_ERROR;
    if (size > B43C_DMA_PAGE || !b43compat_env->dma_page_alloc) {
        printk("b43compat: dma_alloc_coherent(%lu): not available\n", (unsigned long)size);
        return NULL;
    }
    p = (u32 *)b43compat_env->dma_page_alloc(b43compat_env->ctx, &bus);
    if (!p) {
        printk("b43compat: dma_alloc_coherent: no uncached page left\n");
        return NULL;
    }
    /* Uncached memory: plain word stores (dcbz would fault) */
    for (i = 0; i < B43C_DMA_PAGE / 4; i++)
        ((volatile u32 *)p)[i] = 0;
    for (i = 0; i < B43C_COHERENT_MAX; i++) {
        if (!g_coherent[i]) {
            g_coherent[i] = p;
            break;
        }
    }
    *dma_handle = (dma_addr_t)bus;
    b43compat_dma_stats.coherent++;
    return p;
}

void dma_free_coherent(struct device *dev, size_t size, void *cpu_addr, dma_addr_t dma_handle)
{
    int i;

    (void)dev;
    (void)size;
    (void)dma_handle;
    if (!cpu_addr)
        return;
    for (i = 0; i < B43C_COHERENT_MAX; i++)
        if (g_coherent[i] == cpu_addr)
            g_coherent[i] = NULL;
    b43compat_env->dma_page_free(b43compat_env->ctx, cpu_addr);
    b43compat_dma_stats.coherent--;
}

/*
 * Called by iowrite16/32 before every register write. When the host could
 * not make the descriptor pages uncached, the descriptors are flushed to
 * memory before a write to the DMA engines' registers (0x200-0x2FF of the
 * 802.11 core: ring address, control, index): that write is what makes the
 * chip read them.
 */
void b43compat_dma_before_write(const void __iomem *addr)
{
    unsigned long off;
    int i;

    if (!b43compat_dma_stats.coherent || b43compat_env->dma_pages_uncached)
        return;
    off = (unsigned long)addr - (unsigned long)b43compat_pci_host.b43c_bar0;
    if ((off & ~0xFFUL) != 0x200)
        return;
    for (i = 0; i < B43C_COHERENT_MAX; i++)
        if (g_coherent[i])
            b43c_cache_flush(g_coherent[i], B43C_DMA_PAGE);
}

dma_addr_t dma_map_single(struct device *dev, void *ptr, size_t size, enum dma_data_direction dir)
{
    dma_addr_t bus = DMA_MAPPING_ERROR;
    int i;

    (void)dev;
    b43compat_dma_stats.maps++;
    bus = b43c_dma_contig(ptr, size);
    if (bus != DMA_MAPPING_ERROR) {
        if (dir != DMA_TO_DEVICE && b43c_rxmap_add(bus, ptr, size)) {
            b43compat_dma_stats.failures++;
            return DMA_MAPPING_ERROR;
        }
        b43compat_dma_stats.direct++;
        b43c_cache_flush(ptr, size);
        return bus;
    }
    if (size <= B43C_DMA_PAGE) {
        for (i = 0; i < B43C_BOUNCE_PAGES; i++) {
            b43c_bounce *b = &g_bounce[i];
            if (b->page && !b->orig && b->bus != DMA_MAPPING_ERROR) {
                b->orig = ptr;
                b->size = size;
                memcpy(b->page, ptr, size);
                b43compat_dma_stats.bounced++;
                if (++b43compat_dma_stats.bounce_in_use > b43compat_dma_stats.bounce_peak)
                    b43compat_dma_stats.bounce_peak = b43compat_dma_stats.bounce_in_use;
                b43c_cache_flush(b->page, size);
                return b->bus;
            }
        }
    }
    b43compat_dma_stats.failures++;
    return DMA_MAPPING_ERROR;
}

/* The device is done writing: drop the CPU's lines (they may hold what was
 * there before the device wrote), copy back from a bounce page */
static void b43c_dma_to_cpu(dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
    b43c_bounce *b;
    b43c_rxmap *m;

    rmb();
    if (dir == DMA_TO_DEVICE)
        return;
    b = b43c_bounce_find(addr);
    if (b) {
        size_t n = size < b->size ? size : b->size;
        b43c_cache_flush(b->page, n);
        b43c_cache_flush(b->orig, n);
        memcpy(b->orig, b->page, n);
        return;
    }
    m = b43c_rxmap_find(addr);
    if (m)
        b43c_cache_flush(m->cpu, size < m->size ? size : m->size);
}

void dma_unmap_single(struct device *dev, dma_addr_t addr, size_t size, enum dma_data_direction dir)
{
    b43c_bounce *b;
    b43c_rxmap *m;

    (void)dev;
    if (dir == DMA_FROM_DEVICE)
        b43compat_dma_stats.rx_taken++;
    b43c_dma_to_cpu(addr, size, dir);
    m = b43c_rxmap_find(addr);
    if (m)
        m->cpu = NULL;
    b = b43c_bounce_find(addr);
    if (!b)
        return;
    b->orig = NULL;
    b43compat_dma_stats.bounce_in_use--;
}

void dma_sync_single_for_cpu(struct device *dev, dma_addr_t addr, size_t size,
                             enum dma_data_direction dir)
{
    (void)dev;
    if (dir == DMA_FROM_DEVICE)
        b43compat_dma_stats.rx_seen++;
    b43c_dma_to_cpu(addr, size, dir);
}

void dma_sync_single_for_device(struct device *dev, dma_addr_t addr, size_t size,
                                enum dma_data_direction dir)
{
    b43c_bounce *b = b43c_bounce_find(addr);
    b43c_rxmap *m;

    (void)dev;
    if (dir == DMA_FROM_DEVICE)
        b43compat_dma_stats.rx_recycled++;
    if (b) {
        size_t n = size < b->size ? size : b->size;
        memcpy(b->page, b->orig, n);
        b43c_cache_flush(b->page, n);
        return;
    }
    m = b43c_rxmap_find(addr);
    if (m)
        b43c_cache_flush(m->cpu, size < m->size ? size : m->size);
    else
        wmb();
}
