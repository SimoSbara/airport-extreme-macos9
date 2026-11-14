/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * AirPortDriver.c - Open Transport driver for the AirPort Extreme card.
 *
 * A PCI network driver ('ndrv' in the Extensions folder, service category
 * 'otan'): the Driver Loader matches it to the "pci80211" node, Open
 * Transport's PCI scanner registers the port, and TCP/IP and AppleTalk see
 * an Ethernet interface. Underneath is the Linux 5.15 wireless stack
 * through Common/os9/b43os9.h, which already speaks Ethernet II frames.
 *
 * DLPI is done by DLPISupportLib (Mentat's DLPI template, part of Open
 * Transport, see dle.h): this file only moves frames, as in the Ethernet
 * driver of Apple's PCCardNetworkSample.
 *
 * Execution:
 *   - the stack runs in OT deferred tasks: b43os9_poll() (b43's interrupt
 *     handlers, timers, work items), the connection manager, the transmit
 *     queue. Three things schedule it:
 *       the chip's PCI interrupt: the hardware handler only masks the
 *         interrupt source and schedules the task, which unmasks it when
 *         the stack has served the chip (Linux's hard handler + thread);
 *       wput (any level up to deferred task): queues outbound frames on an
 *         atomic list, the task hands them to mac80211 and runs b43's
 *         transmit work right away;
 *       a Driver Services timer every TICK_MS: Linux timers and delayed
 *         work, and the chip too when interrupts are off (irq=0);
 *   - the stack is never entered twice: a test-and-set lock guards it.
 *   - inbound frames go up with dle_inbound() from inside the stack.
 *   - the Linux code always runs on a private 256 KB stack (on_stack):
 *     deferred tasks borrow a small system stack, and the receive path of
 *     mac80211 is deep.
 *   - a Time Manager task watches over it: when a run does not end, it logs
 *     where the code is (see "Watchdog").
 */
/* Open Transport defines dev_t, uid_t and gid_t as UInt32 (STREAMS), and
 * Apple's CGBase.h pulls in newlib's sys/types.h, which would define them
 * as shorts: keep Open Transport's. */
#define _DEV_T_DECLARED
#define _UID_T_DECLARED
#define _GID_T_DECLARED

/* Our headers first: OpenTransportKernel.h redefines free, bcopy...
 * For the same reason no libc header here (OTMemcpy/OTMemzero instead). */
#include "drv.h"
#include "b43fw.h"

#include <OpenTransportKernel.h>
#include <OpenTransportProviders.h>
#include <DriverServices.h>
#include <DriverFamilyMatching.h>
#include <Timer.h>
#include <NameRegistry.h>

#include "dle.h"

#define DRIVER_NAME     "pci80211"      /* Open Firmware name of the card */
#define TICK_MS         10
#define TXQ_MAX         64              /* queued outbound frames */
#define MAX_FRAME       1514
#define MIN_POOL_KB     1024
#define STACK_SIZE      (256UL * 1024)  /* private stack of the Linux stack */
#define STACK_PAINT     0xA5

/* wpa_supplicant log level (src/utils/wpa_debug.c) */
extern int wpa_debug_level;

/* -------------------------------------------------------------------------
 * Native driver description: Open Transport, Ethernet, DLPI
 * ------------------------------------------------------------------------- */
#define APX_VERSION { 0x01, 0x00, developStage, 0x01 }     /* 1.0d1 */

DriverDescription TheDriverDescription = {
    kTheDescriptionSignature,
    kInitialDriverDescriptor,
    { "\p" DRIVER_NAME, APX_VERSION },
    { kDriverIsUnderExpertControl, "\p" DRIVER_NAME },
    { 1, { { kServiceCategoryOpenTransport,
             OTPCIServiceType(kOTEthernetDevice,
                              kOTFramingEthernet | kOTFramingEthernetIPX |
                              kOTFraming8022, 0, 1),
             APX_VERSION } } }
};

/* -------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */
typedef struct board {
    dle_t dle;                          /* must be first (dle_to_hw) */
} board;

/* Per-stream data (q->q_ptr) */
typedef struct apx_stream {
    dcl_t dcl;                          /* must be first */
} apx_stream;

static board              g_board;
static int                g_dle_ready;
static int                g_attached;   /* the stack survives Terminate/Init */
static b43os9            *g_b43;
static drv_prefs          g_prefs;
static apx_status        *g_st;
static RegEntryID         g_node;

static OTDeferredTaskRef  g_dtask;      /* timer tick */
static OTDeferredTaskRef  g_ktask;      /* kick: chip interrupt, wput */
static TimerID            g_timer;
static volatile int       g_stopping;
static volatile int       g_want_up;    /* a client is bound (dlehw_start) */
static UInt8              g_lock;       /* stack lock, test-and-set */

/* Execution counters (debug summary) */
static unsigned long      g_runs, g_run_max_us, g_irqs, g_kicks, g_kick_busy;
static int                g_irq_on;     /* chip interrupt installed and in use */

/* Common/compat/b43compat_dma.c (same layout) */
struct b43compat_dma_stats {
    unsigned long maps, direct, bounced, failures, bounce_in_use, bounce_peak, coherent;
    unsigned long rx_seen, rx_taken, rx_recycled;
};
extern struct b43compat_dma_stats b43compat_dma_stats;

static OTLIFO             g_txq;        /* outbound mblks, newest first */
static SInt32             g_txq_len;
static unsigned char      g_txbuf[MAX_FRAME + 2];

/* -------------------------------------------------------------------------
 * Private stack
 * ------------------------------------------------------------------------- */
static char              *g_stack;      /* PoolAllocateResident, painted */
static void             (*g_stack_fn)(void);
static unsigned long      g_ticks;
/* drv_time_ms() when the Linux code was entered, 0 = not inside (watchdog) */
static volatile unsigned long g_busy_since;

/* Called by on_stack() with r1 already on the private stack */
void apx_stack_body(void) __attribute__((used, noinline));
void apx_stack_body(void)
{
    g_stack_fn();
}

/*
 * Runs fn on the private stack: switches r1, keeps the old one as the back
 * chain of the first frame (MacsBug still shows the whole calling chain),
 * calls apx_stack_body (same fragment: same TOC, direct call) and switches
 * back. Only with the stack lock held, or before the timer exists.
 */
static void __attribute__((noinline)) on_stack(void (*fn)(void))
{
    char *top;

    if (!g_stack) {
        fn();
        return;
    }
    g_stack_fn = fn;
    top = (char *)(((unsigned long)g_stack + STACK_SIZE) & ~15UL);
    g_busy_since = drv_time_ms() | 1UL;
    __asm__ volatile(
        "mr 12,1\n\t"
        "mr 1,%0\n\t"
        "stwu 12,-64(1)\n\t"
        "bl .apx_stack_body\n\t"
        "nop\n\t"
        "lwz 1,0(1)\n\t"
        :
        : "r"(top)
        : "r0", "r3", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12",
          "ctr", "lr", "cr0", "cr1", "cr5", "cr6", "cr7", "xer", "memory");
    /* The volatile FPRs are not listed: this function is never inlined and
     * keeps no floating point value across the call */
    g_busy_since = 0;
}

static int stack_alloc(void)
{
    unsigned long i;

    g_stack = (char *)PoolAllocateResident(STACK_SIZE, false);
    if (!g_stack)
        return -1;
    for (i = 0; i < STACK_SIZE; i++)
        g_stack[i] = (char)STACK_PAINT;
    return 0;
}

/* Deepest point reached: the first byte no longer painted */
static void stack_measure(void)
{
    unsigned long i;

    if (!g_stack || !g_st)
        return;
    for (i = 0; i < STACK_SIZE && g_stack[i] == (char)STACK_PAINT; i++) {
    }
    g_st->stack_size = STACK_SIZE;
    g_st->stack_peak = STACK_SIZE - i;
    if (i < 4096)
        drv_log("apx: private stack almost full (%lu bytes left)\n", i);
}

static int lock_try(void)
{
    return OTCompareAndSwap8(0, 1, &g_lock);
}

static void lock_release(void)
{
    g_lock = 0;
}

/* -------------------------------------------------------------------------
 * Transmit (lock held)
 * ------------------------------------------------------------------------- */
static void tx_one(mblk_t *mp)
{
    dle_interface_status_t *is = &g_board.dle.dle_istatus;
    unsigned long total = 0;
    mblk_t *m;
    int rc;

    for (m = mp; m; m = m->b_cont) {
        long len = m->b_wptr - m->b_rptr;
        if (len <= 0)
            continue;
        if (total + (unsigned long)len > MAX_FRAME) {
            total = 0;                  /* too long: drop */
            break;
        }
        OTMemcpy(g_txbuf + total, m->b_rptr, (OTByteCount)len);
        total += (unsigned long)len;
    }
    freemsg(mp);

    {
        /* Trace of the first frames from the upper layers (debug) */
        static unsigned long traced;
        if (g_prefs.debug && traced < 60 && total >= 14) {
            traced++;
            drv_log("apx: TX %pM type 0x%02x%02x, %lu bytes%s\n", g_txbuf, g_txbuf[12],
                    g_txbuf[13], total, drv_link_can_send() ? "" : " (dropped: not connected)");
        }
    }
    if (total < 14 || !drv_link_can_send()) {
        is->transmit_discards++;
        g_st->tx_drops++;
        return;
    }
    /* 802.3 frame from the upper layer with the length left to us */
    if (!(g_txbuf[12] | g_txbuf[13])) {
        g_txbuf[12] = (unsigned char)((total - 14) >> 8);
        g_txbuf[13] = (unsigned char)(total - 14);
    }

    is->bytes_sent += total;
    if (g_txbuf[0] & 1) {
        if ((g_txbuf[0] & g_txbuf[1] & g_txbuf[2] & g_txbuf[3] & g_txbuf[4] & g_txbuf[5]) == 0xFF)
            is->broadcast_frames_sent++;
        else
            is->multicast_frames_sent++;
    } else {
        is->unicast_frames_sent++;
    }

    rc = b43os9_tx(g_b43, g_txbuf, total);
    if (rc) {
        is->transmit_errors++;
        g_st->tx_drops++;
    } else {
        g_st->tx_frames++;
        g_st->tx_bytes += total;
    }
}

static int tx_drain(void)
{
    OTLink *list = OTLIFOStealList(&g_txq);
    int n = 0;

    list = OTReverseList(list);         /* oldest first */
    while (list) {
        mblk_t *mp = (mblk_t *)list;
        list = list->fNext;
        mp->b_next = NULL;
        OTAtomicAdd32(-1, &g_txq_len);
        tx_one(mp);
        n++;
    }
    return n;
}

/* -------------------------------------------------------------------------
 * Receive (called by drv_link.c inside the stack)
 * ------------------------------------------------------------------------- */
void drv_deliver(const unsigned char *frame, unsigned long len)
{
    mblk_t *mp;

    if (!g_dle_ready || len < 14)
        return;
    mp = allocb(len, BPRI_HI);
    if (!mp) {
        g_board.dle.dle_istatus.receive_discards++;
        g_st->rx_drops++;
        return;
    }
    OTMemcpy(mp->b_wptr, frame, len);
    mp->b_wptr += len;
    g_st->rx_frames++;
    g_st->rx_bytes += len;
    {
        /* Trace of the first frames given to Open Transport (debug): what
         * DLPISupportLib did with them shows in its counters */
        static unsigned long traced;
        dle_interface_status_t *is = &g_board.dle.dle_istatus;
        UInt32 unknown = is->receive_unknown_protos, disc = is->receive_discards;
        UInt32 bytes = is->bytes_received;

        dle_inbound(&g_board.dle, mp);
        if (g_prefs.debug && traced < 60) {
            traced++;
            drv_log("apx: RX %pM -> %pM type 0x%02x%02x, %lu bytes: %s%s%s\n", frame + 6,
                    frame, frame[12], frame[13], len,
                    is->bytes_received != bytes ? "accepted" : "not counted",
                    is->receive_unknown_protos != unknown ? ", no client for this type" : "",
                    is->receive_discards != disc ? ", discarded" : "");
        }
    }
}

/* Periodic summary (debug), every 10 s */
static void log_summary(void)
{
    const dle_t *d = &g_board.dle;
    const dle_interface_status_t *is = &d->dle_istatus;

    drv_log("apx: [status] link %lu, up %d, streams %lu, binds %lu (any %lu), txq %ld |"
            " TX %lu ok %lu drop | RX %lu ok %lu drop | lib: rx %lu B, unknown %lu,"
            " discards %lu, tx %lu B | mem %lu KB, stack %lu KB\n",
            g_st->link_state, g_want_up, d->dle_refcnt, d->dle_bound_count,
            d->dle_match_any_count, (long)g_txq_len, g_st->tx_frames, g_st->tx_drops,
            g_st->rx_frames, g_st->rx_drops, is->bytes_received, is->receive_unknown_protos,
            is->receive_discards, is->bytes_sent, g_st->mem_used / 1024,
            g_st->stack_peak / 1024);
    drv_log("apx: [run] %lu runs (longest %lu us), %lu interrupts, %lu kicks (%lu busy) |"
            " DMA maps %lu: %lu direct, %lu bounced, %lu failed, bounce peak %lu\n",
            g_runs, g_run_max_us, g_irqs, g_kicks, g_kick_busy, b43compat_dma_stats.maps,
            b43compat_dma_stats.direct, b43compat_dma_stats.bounced,
            b43compat_dma_stats.failures, b43compat_dma_stats.bounce_peak);
    /* RX ring: slots b43 looked at, frames it passed up, buffers it
     * dropped and gave back (empty header, poison, no memory) */
    drv_log("apx: [rx ring] seen %lu, taken %lu, recycled %lu\n", b43compat_dma_stats.rx_seen,
            b43compat_dma_stats.rx_taken, b43compat_dma_stats.rx_recycled);
    g_run_max_us = 0;
}

/* -------------------------------------------------------------------------
 * Chip diagnostics (debug): registers read directly, no side effects
 * ------------------------------------------------------------------------- */
static volatile unsigned char *g_mmio;

extern unsigned long b43compat_irq_polls, b43compat_irq_hard, b43compat_irq_threads;
extern unsigned long b43compat_pio_reads, b43compat_msleep_works;

static UInt32 rd32(unsigned long off)
{
    UInt32 v = *(volatile UInt32 *)(g_mmio + off);
    __asm__ volatile("eieio" : : : "memory");
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | (v >> 24);
}

static UInt16 rd16(unsigned long off)
{
    UInt16 v = *(volatile UInt16 *)(g_mmio + off);
    __asm__ volatile("eieio" : : : "memory");
    return (UInt16)((v << 8) | (v >> 8));
}

/* MAC control, interrupt reason/mask, DMA0 (the RX-done source in PIO
 * mode) reason/mask, PIO RX queue control (FRAMERDY 1, DATARDY 2), the
 * attenuations of b43's TX power control, and the interrupt polls */
static void chip_snapshot(const char *why)
{
    static unsigned long last_polls, last_hard, last_threads, last_pio, last_slept;
    static unsigned long last_ack, last_fcs;
    b43os9_hwinfo hw;

    if (!g_mmio || !g_b43)
        return;
    b43os9_get_hwinfo(g_b43, &hw);
    drv_log("apx: [chip %s] MACCTL %08lx IRQ reason %08lx mask %08lx | DMA0 reason %08lx"
            " mask %08lx | PIO RXCTL %04x | polls +%lu, hard +%lu, thread +%lu |"
            " txpower %d dBm bbatt %d rfatt %d txctl 0x%02x\n", why,
            rd32(0x120), rd32(0x128), rd32(0x12C), rd32(0x20), rd32(0x24), rd16(0x308),
            b43compat_irq_polls - last_polls, b43compat_irq_hard - last_hard,
            b43compat_irq_threads - last_threads, hw.desired_txpower, hw.bbatt, hw.rfatt,
            hw.tx_control);
    /* What the chip handed over (all frames, beacons included, before
     * decryption), what the AP did not acknowledge, bad checksums, and the
     * work items run inside a sleep of non-work code */
    drv_log("apx: [link %s] chip RX reads +%lu, delivered %lu | TX unacked +%lu (total %lu),"
            " RTS failed %lu | RX bad FCS +%lu | works in sleeps +%lu\n", why,
            b43compat_pio_reads - last_pio, g_st->rx_frames,
            hw.tx_ack_failures - last_ack, hw.tx_ack_failures, hw.tx_rts_failures,
            hw.rx_fcs_errors - last_fcs, b43compat_msleep_works - last_slept);
    last_polls = b43compat_irq_polls;
    last_hard = b43compat_irq_hard;
    last_threads = b43compat_irq_threads;
    last_pio = b43compat_pio_reads;
    last_slept = b43compat_msleep_works;
    last_ack = hw.tx_ack_failures;
    last_fcs = hw.rx_fcs_errors;
    {
        /* The AP as mac80211 sees it */
        b43os9_station sta;
        if (b43os9_get_station(g_b43, &sta) == 0)
            drv_log("apx: [station %s] signal %d dBm (avg %d), rate tx %lu.%lu rx %lu.%lu Mbit/s |"
                    " tx %lu, retries %lu, failed %lu | rx %lu, dropped %lu (duplicates %lu) |"
                    " beacons %lu, lost %lu\n", why, sta.signal, sta.signal_avg, sta.tx_rate / 10,
                    sta.tx_rate % 10, sta.rx_rate / 10, sta.rx_rate % 10, sta.tx_packets,
                    sta.tx_retries, sta.tx_failed, sta.rx_packets, sta.rx_dropped,
                    sta.rx_duplicates, sta.rx_beacons, sta.beacon_loss);
    }
    {
        /* Once: what b43 read from the card (board quirks, PA calibration) */
        static int board_logged;
        if (!board_logged && hw.core_id) {
            board_logged = 1;
            drv_log("apx: [board] vendor %04x type %04x rev 0x%02x, SPROM rev %d, boardflags"
                    " %04x/%04x, antennas 0x%x, maxpwr %d, pa0b %04x %04x %04x, itssi %d,"
                    " tx bias %d magn %d\n", hw.board_vendor, hw.board_type, hw.board_rev,
                    hw.sprom_rev, hw.boardflags_lo, hw.boardflags_hi, hw.ant_available_bg,
                    hw.maxpwr_bg, hw.pa0b0, hw.pa0b1, hw.pa0b2, hw.itssi_bg, hw.tx_bias,
                    hw.tx_magn);
        }
    }
}

/* Every second: a connected link with no frame received for 3 s is
 * logged with a register snapshot (again every 5 s while it lasts) */
static void rx_watchdog(void)
{
    static unsigned long last_rx, quiet_s, logged_s;

    if (g_st->link_state != APX_LINK_CONNECTED) {
        quiet_s = 0;
        last_rx = g_st->rx_frames;
        return;
    }
    if (g_st->rx_frames != last_rx) {
        if (quiet_s >= 3)
            drv_log("apx: RX resumed after %lu s\n", quiet_s);
        quiet_s = 0;
        last_rx = g_st->rx_frames;
        return;
    }
    quiet_s++;
    if (quiet_s >= 3 && (quiet_s == 3 || quiet_s - logged_s >= 5)) {
        logged_s = quiet_s;
        drv_log("apx: no frame received for %lu s\n", quiet_s);
        chip_snapshot("RX quiet");
    }
}

/* -------------------------------------------------------------------------
 * Watchdog: where the Linux code is when it does not come back
 *
 * A Time Manager task, once a second. It runs at interrupt level, so it
 * still runs when a deferred task never ends (a frozen Mac, no MacsBug).
 * When the stack has been inside one run for WATCH_STUCK_MS it logs the
 * return addresses found on the private stack: with the link map they give
 * the chain of functions it is stuck in. With screen_log=1 the log is on
 * the screen, the only place where it can still be read.
 *
 * Mac OS takes interrupts on the stack of the interrupted code, so the
 * handler's own frame is on the private stack, just below the frames of the
 * stuck code: the first frame from there whose back chain leads to the top
 * of the private stack is the innermost one. (If the handler runs on another
 * stack, the search starts from the deepest point ever used and the first
 * addresses may belong to an older, deeper call.)
 *
 * Only with watchdog=1 in the prefs. The fragment stays loaded
 * (ValidateHardware), so the task is never removed.
 * ------------------------------------------------------------------------- */
#define WATCH_STUCK_MS  5000
#define WATCH_ADDRS     24

static TMTask       g_watch;
static int          g_watch_on;
static volatile int g_irq_masked;       /* see the interrupt handler below */

/* Does the back chain starting at p reach the first frame of the stack? */
static int watch_chain_ok(unsigned long p, unsigned long lo, unsigned long first)
{
    int n;

    for (n = 0; n < 400; n++) {
        unsigned long next;

        if (p == first)
            return 1;
        if (p < lo || p > first || (p & 3))
            return 0;
        next = *(const unsigned long *)p;
        if (next <= p)
            return 0;
        p = next;
    }
    return 0;
}

static void watch_report(unsigned long stuck_ms, unsigned long irqs_per_s)
{
    unsigned long lo = (unsigned long)g_stack;
    /* The frame on_stack() makes at the top */
    unsigned long first = (((unsigned long)g_stack + STACK_SIZE) & ~15UL) - 64;
    unsigned long sp = (unsigned long)__builtin_frame_address(0), p;
    unsigned long addr[WATCH_ADDRS];
    const char *from = "interrupted stack";
    int n = 0, i;

    if (sp < lo || sp >= first) {
        for (p = lo; p < first && *(const unsigned char *)p == STACK_PAINT; p++) {
        }
        sp = p;
        from = "deepest use";
    }
    for (p = (sp + 3) & ~3UL; p < first; p += 4)
        if (watch_chain_ok(p, lo, first))
            break;
    drv_log("apx: [stuck %lu s] link %lu, runs %lu, interrupts %lu (+%lu/s), MACCTL %08lx,"
            " IRQ reason %08lx mask %08lx | drv_log at %08lX, stack %08lX-%08lX, %s %08lX,"
            " innermost frame %08lX\n", stuck_ms / 1000, g_st ? g_st->link_state : 0UL, g_runs,
            g_irqs, irqs_per_s, g_mmio ? rd32(0x120) : 0UL, g_mmio ? rd32(0x128) : 0UL,
            g_mmio ? rd32(0x12C) : 0UL, *(const unsigned long *)(const void *)drv_log, lo,
            first + 64, from, sp, p);
    /* Each frame holds, at offset 8, the return address its callee saved:
     * an address inside the function the frame belongs to. The first one
     * may be left from an earlier call. */
    while (p < first && n < WATCH_ADDRS) {
        addr[n++] = *(const unsigned long *)(p + 8);
        p = *(const unsigned long *)p;
    }
    for (i = 0; i < n; i += 8) {
        unsigned long a[8];
        int k;

        for (k = 0; k < 8; k++)
            a[k] = i + k < n ? addr[i + k] : 0;
        drv_log("apx: [stuck] return addresses %d-%d: %08lX %08lX %08lX %08lX %08lX %08lX"
                " %08lX %08lX\n", i, i + 7, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    }
}

static pascal void watch_task(TMTaskPtr task)
{
    static const unsigned long when[3] = { WATCH_STUCK_MS, 2 * WATCH_STUCK_MS, 4 * WATCH_STUCK_MS };
    static unsigned long last_since, level, last_irqs, last_ticks, idle_s;
    unsigned long since = g_busy_since, now = drv_time_ms();
    unsigned long irqs = g_irqs - last_irqs;

    last_irqs = g_irqs;
    if (since != last_since) {
        last_since = since;
        level = 0;
    }
    if (since && level < 3 && now - since >= when[level]) {
        level++;
        watch_report(now - since, irqs);
    }
    /* Not inside the stack, and yet the 10 ms tick does not run: something
     * else keeps the deferred tasks from running */
    if (since || g_stopping || !g_dtask || g_ticks != last_ticks) {
        idle_s = 0;
    } else if (++idle_s == WATCH_STUCK_MS / 1000) {
        drv_log("apx: [stalled] no timer tick for %lu s, and the stack is not running:"
                " interrupts %lu (+%lu/s), masked %d\n", idle_s, g_irqs, irqs, g_irq_masked);
    }
    last_ticks = g_ticks;
    PrimeTime((QElemPtr)task, 1000);
}

/* The routine descriptor the Time Manager calls the task through. It lives
 * in the driver's own data: NewTimerUPP() would allocate it in the heap of
 * whatever application is opening the port, and that heap is gone when the
 * application quits, with the timer still firing through it. */
static RoutineDescriptor g_watch_rd = BUILD_ROUTINE_DESCRIPTOR(uppTimerProcInfo, watch_task);

/* Task time, once (watchdog=1 in the prefs) */
static void watch_install(void)
{
    if (g_watch_on)
        return;
    OTMemzero(&g_watch, sizeof(g_watch));
    g_watch.tmAddr = (TimerUPP)&g_watch_rd;
    InsXTime((QElemPtr)&g_watch);
    PrimeTime((QElemPtr)&g_watch, 1000);
    g_watch_on = 1;
}

/* -------------------------------------------------------------------------
 * Running the stack: timer tick, chip interrupt, kicks from wput
 * ------------------------------------------------------------------------- */
static void arm_timer(unsigned long ms);

/* Interrupt source of the card ("driver-ist"), as in Apple's Ethernet
 * sample: our handler, the system's enabler and disabler */
static InterruptSetMember g_ist;
static void              *g_ist_refcon;
static InterruptEnabler   g_ist_enable;
static InterruptDisabler  g_ist_disable;
static volatile int       g_irq_masked;

static void stack_run(void)
{
    static unsigned long last_s;
    unsigned long now_s;

    b43os9_poll(g_b43);
    drv_link_step(g_want_up);
    /* Frames handed to mac80211 end in b43's transmit work: run it now */
    if (tx_drain())
        b43os9_poll(g_b43);

    now_s = drv_time_ms() / 1000;
    if (now_s == last_s)
        return;
    last_s = now_s;

    /* Diagnostics (debug), once a second, on the private stack too */
    if (!g_prefs.debug)
        return;
    rx_watchdog();
    if (now_s % 10 == 0) {
        if (g_want_up)
            chip_snapshot("10 s");
        log_summary();
    }
}

/* Deferred task level. Returns 0 if the stack was busy. */
static int service(void)
{
    static unsigned long last_measure;
    AbsoluteTime t0;
    Duration d;
    unsigned long now, us;

    unsigned long hard = b43compat_irq_hard;
    int from_irq = g_irq_masked;
    static unsigned long idle_irqs;

    if (g_stopping)
        return 1;
    if (!lock_try())
        return 0;
    t0 = UpTime();
    on_stack(stack_run);
    /* An interrupt b43 does not claim, again and again, would come back
     * forever (shared or stuck line): give up on interrupts, keep polling */
    if (from_irq && g_irq_on) {
        if (b43compat_irq_hard != hard) {
            idle_irqs = 0;
        } else if (++idle_irqs >= 200) {
            g_irq_on = 0;
            g_irq_masked = 0;           /* stays disabled */
            drv_log("apx: %lu interrupts in a row with nothing for b43: interrupts off,"
                    " polling every %d ms\n", idle_irqs, TICK_MS);
        }
    }
    /* Duration: positive values are milliseconds, negative microseconds */
    d = AbsoluteDeltaToDuration(UpTime(), t0);
    us = (d < 0) ? (unsigned long)(-d) : (unsigned long)d * 1000UL;
    if (us > g_run_max_us)
        g_run_max_us = us;
    g_runs++;
    now = drv_time_ms();
    if (now - last_measure >= 1000) {
        last_measure = now;
        stack_measure();
    }
    lock_release();
    /* The chip has been served: its interrupt may come again */
    if (g_irq_masked && !g_stopping) {
        g_irq_masked = 0;
        g_ist_enable(g_ist, g_ist_refcon);
    }
    return 1;
}

static pascal void tick(void *arg)
{
    (void)arg;
    if (g_stopping)
        return;
    (void)service();
    g_ticks++;
    arm_timer(TICK_MS);
}

static pascal void kick(void *arg)
{
    (void)arg;
    g_kicks++;
    if (!service())
        g_kick_busy++;              /* the next tick takes it */
}

/* Hardware interrupt level: no chip access here (the stack may be in the
 * middle of an SSB core switch). Mask the source, the task does the rest. */
static InterruptMemberNumber chip_isr(InterruptSetMember member, void *refCon, UInt32 count)
{
    (void)member;
    (void)refCon;
    (void)count;
    g_ist_disable(g_ist, g_ist_refcon);
    g_irq_masked = 1;
    g_irqs++;
    if (!g_stopping && g_ktask) {
        OTEnterInterrupt();
        OTScheduleDeferredTask(g_ktask);
        OTLeaveInterrupt();
    }
    return kIsrIsComplete;
}

/* Task level, once */
static int irq_install(void)
{
    ISTProperty ist;
    RegPropertyValueSize size = sizeof(ist);
    InterruptHandler old = NULL;
    OSStatus err;

    OTMemzero(&ist, sizeof(ist));
    err = RegistryPropertyGet(&g_node, kISTPropertyName, &ist, &size);
    if (err != noErr || size < sizeof(InterruptSetMember)) {
        drv_log("apx: no interrupt source (driver-ist: %ld): polling every %d ms\n",
                (long)err, TICK_MS);
        return -1;
    }
    g_ist = ist[kISTChipInterruptSource];
    err = GetInterruptFunctions(g_ist.setID, g_ist.member, &g_ist_refcon, &old,
                                &g_ist_enable, &g_ist_disable);
    if (err != noErr || !g_ist_enable || !g_ist_disable) {
        drv_log("apx: GetInterruptFunctions: %ld: polling every %d ms\n", (long)err, TICK_MS);
        return -1;
    }
    err = InstallInterruptFunctions(g_ist.setID, g_ist.member, g_ist_refcon, chip_isr,
                                    g_ist_enable, g_ist_disable);
    if (err != noErr) {
        drv_log("apx: InstallInterruptFunctions: %ld: polling every %d ms\n", (long)err, TICK_MS);
        return -1;
    }
    drv_log("apx: chip interrupt: set %p member %ld (previous handler %p)\n",
            (void *)g_ist.setID, (long)g_ist.member, (void *)old);
    return 0;
}

/* Secondary interrupt level */
static OSStatus timer_fired(void *p1, void *p2)
{
    (void)p1;
    (void)p2;
    if (!g_stopping) {
        OTEnterInterrupt();
        OTScheduleDeferredTask(g_dtask);
        OTLeaveInterrupt();
    }
    return noErr;
}

static void arm_timer(unsigned long ms)
{
    AbsoluteTime when = AddDurationToAbsolute((Duration)ms, UpTime());

    SetInterruptTimer(&when, timer_fired, NULL, &g_timer);
}

/* -------------------------------------------------------------------------
 * DLPISupportLib callbacks
 * ------------------------------------------------------------------------- */
static void hw_start(void *hw)
{
    (void)hw;
    if (g_prefs.debug)
        drv_log("apx: DLPI start (first bind)\n");
    g_want_up = 1;                      /* the tick brings the interface up */
}

static void hw_stop(void *hw)
{
    (void)hw;
    if (g_prefs.debug)
        drv_log("apx: DLPI stop (last unbind)\n");
    g_want_up = 0;
}

/* Logs a control message: the DLPI primitive, ioctl command, errors */
static void log_msg(const char *what, const mblk_t *mp)
{
    long len = mp->b_wptr - mp->b_rptr;
    int type = mp->b_datap->db_type;

    if (!g_prefs.debug)
        return;
    if ((type == M_PROTO || type == M_PCPROTO) && len >= 4) {
        const UInt32 *w = (const UInt32 *)mp->b_rptr;
        if (w[0] == DL_ERROR_ACK && len >= 16)
            drv_log("apx: %s DL_ERROR_ACK for primitive 0x%lx: dl_errno %lu, unix %lu\n",
                    what, w[1], w[2], w[3]);
        else if (w[0] == DL_UNITDATA_REQ) {
            static unsigned long unitdata;
            if (++unitdata <= 20 || unitdata % 100 == 0)
                drv_log("apx: %s DL_UNITDATA_REQ #%lu (%ld bytes)\n", what, unitdata, len);
        } else if (w[0] == DL_BIND_REQ && len >= 16)
            drv_log("apx: %s DL_BIND_REQ sap 0x%lx, service mode 0x%lx\n", what, w[1], w[3]);
        else
            drv_log("apx: %s DLPI primitive 0x%lx (%ld bytes)\n", what, w[0], len);
    } else if ((type == M_IOCTL || type == M_IOCACK || type == M_IOCNAK) &&
               len >= (long)sizeof(struct iocblk)) {
        const struct iocblk *ioc = (const struct iocblk *)mp->b_rptr;
        const mblk_t *d = mp->b_cont;
        long dlen = d ? d->b_wptr - d->b_rptr : 0;
        drv_log("apx: %s ioctl type 0x%x cmd 0x%lx count %lu error %ld rval %ld,"
                " data %ld bytes 0x%08lx\n", what, type, (unsigned long)ioc->ioc_cmd,
                (unsigned long)ioc->ioc_count, (long)ioc->ioc_error, (long)ioc->ioc_rval,
                dlen, dlen >= 4 ? *(const UInt32 *)d->b_rptr : 0UL);
    } else if (type == M_FLUSH && len >= 1) {
        drv_log("apx: %s M_FLUSH flags 0x%x%s%s\n", what, mp->b_rptr[0],
                (mp->b_rptr[0] & FLUSHR) ? " R" : "", (mp->b_rptr[0] & FLUSHW) ? " W" : "");
    } else {
        drv_log("apx: %s message type 0x%x (%ld bytes)\n", what, type, len);
    }
}

/* What DLPISupportLib keeps for a stream (debug) */
static void log_dcl(const char *what, queue_t *q)
{
    const dcl_t *dcl = (const dcl_t *)q->q_ptr;

    if (!dcl || !g_prefs.debug)
        return;
    drv_log("apx:   %s stream %p: state %lu, sap 0x%lx, flags 0x%lx, mac %lu, framing 0x%lx,"
            " snap %02x%02x%02x%02x%02x | device binds %lu\n", what, dcl, dcl->dcl_state,
            dcl->dcl_sap, dcl->dcl_flags, dcl->dcl_mac_type, dcl->dcl_framing_type,
            dcl->dcl_snap[0], dcl->dcl_snap[1], dcl->dcl_snap[2], dcl->dcl_snap[3],
            dcl->dcl_snap[4], g_board.dle.dle_bound_count);
}

static void hw_filter(void *hw, dle_addr_t *addr_list, UInt32 addr_count,
                      UInt32 promisc_count, UInt32 multi_promisc_count,
                      UInt32 accept_broadcast, UInt32 accept_errors)
{
    /* mac80211 receives every group frame of the BSS; nothing to program */
    dle_addr_t *a;

    (void)hw;
    if (!g_prefs.debug)
        return;
    drv_log("apx: DLPI address filter: %lu addresses, promisc %lu, multi promisc %lu,"
            " broadcast %lu, errors %lu\n", addr_count, promisc_count, multi_promisc_count,
            accept_broadcast, accept_errors);
    for (a = addr_list; a; a = a->dlea_next)
        drv_log("apx:   address %pM\n", a->dlea_addr);
}

/* -------------------------------------------------------------------------
 * STREAMS entry points
 * ------------------------------------------------------------------------- */
static int apx_open(queue_t *q, dev_t *devp, int flag, int sflag, cred_t *credp)
{
    int rc;

    if (!g_dle_ready) {
        drv_log("apx: open refused: not initialized\n");
        return ENXIO;
    }
    rc = dle_open(&g_board.dle, q, devp, flag, sflag, credp, sizeof(apx_stream));
    if (g_prefs.debug)
        drv_log("apx: open (dev 0x%lx flag 0x%x sflag %d) -> %d, %lu streams\n",
                devp ? (unsigned long)*devp : 0UL, flag, sflag, rc, g_board.dle.dle_refcnt);
    if (rc == 0)
        log_dcl("opened", q);
    return rc;
}

static int apx_close(queue_t *q, int flag, cred_t *credp)
{
    int rc;

    (void)flag;
    (void)credp;
    log_dcl("closing", q);
    rc = dle_close(q);
    if (g_prefs.debug)
        drv_log("apx: close -> %d, %lu streams\n", rc, g_board.dle.dle_refcnt);
    return rc;
}

/* Write side: M_DATA are frames, everything else is DLPI for the library */
static int apx_wput(queue_t *q, mblk_t *mp)
{
    if (mp->b_datap->db_type != M_DATA) {
        log_msg("<-", mp);
        mp = dle_wput(q, mp);
        if (!mp) {
            /* Answered (or consumed) by the library itself */
            log_dcl("after", q);
            return 0;
        }
        if (mp->b_datap->db_type != M_DATA) {
            log_msg("->", mp);
            qreply(q, mp);              /* reply already formatted */
            log_dcl("after", q);
            return 0;
        }
    }
    if (g_txq_len >= TXQ_MAX) {
        freemsg(mp);
        g_board.dle.dle_istatus.transmit_discards++;
        g_st->tx_drops++;
        return 0;
    }
    OTAtomicAdd32(1, &g_txq_len);
    OTLIFOEnqueue(&g_txq, (OTLink *)mp);
    if (g_ktask && !g_stopping)
        OTScheduleDeferredTask(g_ktask);
    return 0;
}

/* Read side service: inbound frames queued by dle_inbound */
static int apx_rsrv(queue_t *q)
{
    mblk_t *mp;

    while ((mp = getq(q)) != NULL) {
        if (mp->b_datap->db_type == M_CTL) {
            dle_rsrv_ctl(q, mp);        /* 802.2 XID/TEST */
        } else if (canputnext(q)) {
            putnext(q, mp);
        } else {
            freemsg(mp);
            flushq(q, FLUSHDATA);
            break;
        }
    }
    return 0;
}

/*
 * module_info as Open Transport reads it. OpenTransportProtocol.h asks for
 * PowerPC alignment for this structure only ("#pragma options align=power"),
 * which GCC ignores: with the header's pack(2), mi_idname would land at
 * offset 2 instead of 4. ARP and IP read the driver's name from here (and
 * the stream head its flow-control marks): with the wrong layout they got
 * garbage, refused the port (ENXIO, EINVAL) or crashed in OTStrLength.
 * Declared here, outside the packed headers, with the natural alignment.
 */
typedef struct apx_module_info {
    unsigned short mi_idnum;
    char          *mi_idname;
    long           mi_minpsz;
    long           mi_maxpsz;
    unsigned long  mi_hiwat;
    unsigned long  mi_lowat;
} apx_module_info;

_Static_assert(__builtin_offsetof(apx_module_info, mi_idname) == 4, "module_info: PowerPC layout");
_Static_assert(sizeof(apx_module_info) == 24, "module_info: PowerPC layout");

static apx_module_info apx_minfo = {
    kEnetModuleID, DRIVER_NAME, 0, MAX_FRAME, 2048, 64
};
static struct qinit apx_rinit = {
    NULL, apx_rsrv, apx_open, apx_close, NULL, (struct module_info *)&apx_minfo, NULL
};
static struct qinit apx_winit = {
    apx_wput, NULL, apx_open, apx_close, NULL, (struct module_info *)&apx_minfo, NULL
};
static struct streamtab apx_streamtab = { &apx_rinit, &apx_winit, NULL, NULL };

static install_info apx_install_info = {
    &apx_streamtab,
    kOTModIsDriver | kOTModUpperIsDLPI | kOTModUsesInterrupts,
    SQLVL_MODULE,
    NULL,
    0,
    0
};

/* -------------------------------------------------------------------------
 * Open Transport module exports
 * ------------------------------------------------------------------------- */
install_info *GetOTInstallInfo(void)
{
    return &apx_install_info;
}

/* Called once at startup, before the port is registered. Staying loaded
 * keeps the data of the fragment (and the Linux stack) between the
 * InitStreamModule/TerminateStreamModule pairs. */
OTResult ValidateHardware(OTPCIInfo *info)
{
    (void)info;
    return kOTPCINoErrorStayLoaded;
}

static void publish_status(void)
{
    OSStatus err = RegistryPropertyCreate(&g_node, APX_STATUS_PROPERTY, &g_st, sizeof(g_st));

    if (err != noErr)
        RegistryPropertySet(&g_node, APX_STATUS_PROPERTY, &g_st, sizeof(g_st));
}

/* Brings up the Linux stack (task time: File Manager, PoolAllocateResident) */
static int attach_stack(void)
{
    volatile unsigned char *mmio = NULL;
    RegPropertyValueSize size = sizeof(mmio);
    b43os9_firmware fw;
    b43os9_options opt;
    OSStatus err;
    int prefs_err, rc;

    g_st = (apx_status *)PoolAllocateResident(sizeof(apx_status), true);
    if (!g_st)
        return -1;
    g_st->magic = APX_STATUS_MAGIC;
    g_st->version = APX_STATUS_VERSION;

    drv_prefs_defaults(&g_prefs);
    prefs_err = drv_prefs_load(&g_prefs);
    if (g_prefs.memory_kb < MIN_POOL_KB)
        g_prefs.memory_kb = MIN_POOL_KB;
    if (drv_env_init(&g_node, g_prefs.memory_kb * 1024UL, g_st))
        return -1;
    publish_status();
    if (g_prefs.screen_log && drv_screen_init())
        g_prefs.screen_log = 0;         /* screen not usable (depth, no device) */
    if (g_prefs.watchdog)
        watch_install();

    drv_log("AirPort Extreme driver 1.0d1 - Linux 5.15 wireless stack on Open Transport\n");
    if (prefs_err)
        drv_log("apx: \"AirPort Extreme Prefs\" not read (%d): no network configured\n", prefs_err);
    else
        drv_log("apx: network \"%s\", %s, country \"%s\", pool %lu KB%s\n",
                g_prefs.ssid, g_prefs.password[0] ? "password set" : "no password",
                g_prefs.country[0] ? g_prefs.country : "00", g_prefs.memory_kb,
                g_prefs.debug ? ", debug log on" : "");
    if (!g_prefs.dma || !g_prefs.irq)
        drv_log("apx: dma=%d irq=%d\n", g_prefs.dma, g_prefs.irq);

    err = RegistryPropertyGet(&g_node, "AAPL,address", (void *)&mmio, &size);
    if (err != noErr || !mmio) {
        drv_log("apx: AAPL,address not available (%ld)\n", (long)err);
        return -1;
    }
    g_mmio = mmio;

    OTMemzero(&opt, sizeof(opt));
    opt.verbose = g_prefs.verbose;
    opt.pr_debug = g_prefs.pr_debug;
    opt.hwcrypt = 1;
    opt.qos = 1;
    opt.regdom = g_prefs.country[0] ? g_prefs.country : NULL;
    opt.dma = g_prefs.dma && drv_env_dma_init(g_prefs.dma_uncached) == 0;
    wpa_debug_level = g_prefs.wpa_debug;

    b43_builtin_firmware(&fw);
    if (!g_prefs.firmware[0])
        drv_log("apx: firmware %s\n", b43_builtin_firmware_name(0));
    else if (b43_builtin_firmware_named(g_prefs.firmware, &fw) == 0)
        drv_log("apx: firmware %s (firmware= in the prefs)\n", g_prefs.firmware);
    else
        drv_log("apx: firmware \"%s\" is not built in: using %s\n", g_prefs.firmware,
                b43_builtin_firmware_name(0));
    rc = b43os9_attach(&g_b43, mmio, drv_env(), &fw, &opt, drv_link_events());
    if (rc != B43OS9_OK) {
        drv_log("apx: b43os9_attach failed: %d\n", rc);
        return -1;
    }
    drv_link_init(g_b43, &g_prefs, g_st);
    return 0;
}

static int g_attach_rc;

static void attach_on_stack(void)
{
    g_attach_rc = attach_stack();
}

static void terminate_on_stack(void)
{
    drv_link_step(0);                   /* interface down */
    (void)tx_drain();                   /* drops what is left */
}

Boolean InitStreamModule(void *portInfo)
{
    OTPCIInfo *info = (OTPCIInfo *)portInfo;
    dle_t *dle = &g_board.dle;

    if (!g_attached) {
        RegistryEntryIDCopy(&info->fTheID, &g_node);
        if (!g_stack && stack_alloc())
            return false;
        on_stack(attach_on_stack);
        stack_measure();
        if (g_attach_rc) {
            /* The pool and the log stay: the log tells what went wrong */
            return false;
        }
        g_attached = 1;
        if (g_prefs.irq)
            g_irq_on = irq_install() == 0;
    } else {
        drv_log("apx: Open Transport reloads the driver\n");
    }

    OTMemzero(dle, sizeof(*dle));
    dle->dle_hw.dlehw_start = hw_start;
    dle->dle_hw.dlehw_stop = hw_stop;
    dle->dle_hw.dlehw_address_filter_reset = hw_filter;
    dle->dle_hw.dlehw_send_error = NULL;
    dle->dle_hw.dlehw_recv_error_flags = 0;
    OTMemcpy(dle->dle_factory_addr, g_st->mac, 6);
    OTMemcpy(dle->dle_current_addr, g_st->mac, 6);
    dle_init(dle, 0);
    dle->dle_istatus.speed = 54000000UL;
    g_dle_ready = 1;
    if (g_prefs.debug)
        drv_log("apx: init: port info %p (config %p, %lu bytes), library: mtu %lu, speed %lu,"
                " min sdu %lu, extra header %lu, factory %pM, current %pM\n", info,
                info ? info->fConfigurationInfo : NULL,
                info ? (unsigned long)info->fConfigurationLength : 0UL,
                dle->dle_istatus.mtu, dle->dle_istatus.speed, dle->dle_min_sdu,
                dle->dle_xtra_hdr_len, dle->dle_factory_addr, dle->dle_current_addr);

    g_want_up = 0;
    g_stopping = 0;
    g_dtask = OTCreateDeferredTask(tick, NULL);
    g_ktask = OTCreateDeferredTask(kick, NULL);
    arm_timer(TICK_MS);
    if (g_irq_on) {
        g_irq_masked = 0;
        g_ist_enable(g_ist, g_ist_refcon);
    }
    return true;
}

void TerminateStreamModule(void *portInfo)
{
    (void)portInfo;
    g_stopping = 1;
    if (g_irq_on)
        g_ist_disable(g_ist, g_ist_refcon);
    CancelTimer(g_timer, NULL);
    if (g_dtask) {
        OTDestroyDeferredTask(g_dtask);
        g_dtask = 0;
    }
    if (g_ktask) {
        OTDestroyDeferredTask(g_ktask);
        g_ktask = 0;
    }

    /* Task level: no deferred task can be holding the lock now */
    while (!lock_try()) {
    }
    g_want_up = 0;
    on_stack(terminate_on_stack);
    lock_release();

    g_dle_ready = 0;
    dle_terminate(&g_board.dle);
    drv_log("apx: driver closed\n");
}
