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
 *   - a Driver Services timer schedules an OT deferred task every TICK_MS;
 *     the task runs the stack: b43os9_poll() (chip interrupts are polled,
 *     as in the Tester), the connection manager, the transmit queue;
 *   - wput (any level up to deferred task) queues outbound frames on an
 *     atomic list and sends them at once if the stack is free;
 *   - the stack is never entered twice: a test-and-set lock guards it.
 *   - inbound frames go up with dle_inbound() from inside the stack.
 *   - the Linux code always runs on a private 256 KB stack (on_stack):
 *     deferred tasks borrow a small system stack, and the receive path of
 *     mac80211 is deep.
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

static OTDeferredTaskRef  g_dtask;
static TimerID            g_timer;
static volatile int       g_stopping;
static volatile int       g_want_up;    /* a client is bound (dlehw_start) */
static UInt8              g_lock;       /* stack lock, test-and-set */

static OTLIFO             g_txq;        /* outbound mblks, newest first */
static SInt32             g_txq_len;
static unsigned char      g_txbuf[MAX_FRAME + 2];

/* -------------------------------------------------------------------------
 * Private stack
 * ------------------------------------------------------------------------- */
static char              *g_stack;      /* PoolAllocateResident, painted */
static void             (*g_stack_fn)(void);
static unsigned long      g_ticks;

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

static void tx_drain(void)
{
    OTLink *list = OTLIFOStealList(&g_txq);

    list = OTReverseList(list);         /* oldest first */
    while (list) {
        mblk_t *mp = (mblk_t *)list;
        list = list->fNext;
        mp->b_next = NULL;
        OTAtomicAdd32(-1, &g_txq_len);
        tx_one(mp);
    }
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
 * The stack's clock: Driver Services timer -> OT deferred task
 * ------------------------------------------------------------------------- */
static void arm_timer(unsigned long ms);

static void stack_run(void)
{
    static unsigned long runs;

    b43os9_poll(g_b43);
    drv_link_step(g_want_up);
    tx_drain();
    /* Diagnostics, on the private stack too */
    runs++;
    if (!g_prefs.debug)
        return;
    if (runs % 100 == 0)
        rx_watchdog();
    if (runs % 1000 == 0 && g_want_up)
        chip_snapshot("10 s");
}

static pascal void tick(void *arg)
{
    (void)arg;
    if (g_stopping)
        return;
    if (lock_try()) {
        on_stack(stack_run);
        if (++g_ticks % 100 == 0)
            stack_measure();
        if (g_prefs.debug && g_ticks % 1000 == 0)
            log_summary();
        lock_release();
    }
    arm_timer(TICK_MS);
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
    if (lock_try()) {
        on_stack(tx_drain);
        lock_release();
    }
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
    kOTModIsDriver | kOTModUpperIsDLPI,
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

    drv_log("AirPort Extreme driver 1.0d1 - Linux 5.15 wireless stack on Open Transport\n");
    if (prefs_err)
        drv_log("apx: \"AirPort Extreme Prefs\" not read (%d): no network configured\n", prefs_err);
    else
        drv_log("apx: network \"%s\", %s, country \"%s\", pool %lu KB%s\n",
                g_prefs.ssid, g_prefs.password[0] ? "password set" : "no password",
                g_prefs.country[0] ? g_prefs.country : "00", g_prefs.memory_kb,
                g_prefs.debug ? ", debug log on" : "");

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
    wpa_debug_level = g_prefs.wpa_debug;

    b43_builtin_firmware(&fw);
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
    tx_drain();                         /* drops what is left */
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
    arm_timer(TICK_MS);
    return true;
}

void TerminateStreamModule(void *portInfo)
{
    (void)portInfo;
    g_stopping = 1;
    CancelTimer(g_timer, NULL);
    if (g_dtask) {
        OTDestroyDeferredTask(g_dtask);
        g_dtask = 0;
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
