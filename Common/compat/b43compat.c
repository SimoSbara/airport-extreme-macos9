/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * b43compat.c - Mac OS 9 implementation of the kernel APIs used by b43.
 *
 * Everything that touches the system goes through b43compat_env (see
 * b43env.h), set by b43os9_attach().
 */
#include <linux/types.h>
#include "b43env.h"
#include "b43compat_os9.h"

const b43_env *b43compat_env = NULL;

/* pr_debug() enabled (in Linux: DEBUG or dynamic debug) */
int b43compat_pr_debug = 0;

/* =========================================================================
 * Memory and strings
 * ========================================================================= */
void *b43c_memset(void *s, int c, size_t n)
{
    u8 *d = (u8 *)s;
    while (n--)
        *d++ = (u8)c;
    return s;
}

void *b43c_memmove(void *dst, const void *src, size_t n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;

    if (d == s || n == 0)
        return dst;
    if (d < s || d >= s + n) {
        /* Forward: whole words while both sides are word aligned */
        if ((((unsigned long)d | (unsigned long)s) & 3) == 0) {
            u32 *dw = (u32 *)(void *)d;
            const u32 *sw = (const u32 *)(const void *)s;
            for (; n >= 16; n -= 16) {
                dw[0] = sw[0];
                dw[1] = sw[1];
                dw[2] = sw[2];
                dw[3] = sw[3];
                dw += 4;
                sw += 4;
            }
            for (; n >= 4; n -= 4)
                *dw++ = *sw++;
            d = (u8 *)dw;
            s = (const u8 *)sw;
        }
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

void *b43c_memcpy(void *dst, const void *src, size_t n)
{
    return b43c_memmove(dst, src, n);
}

int b43c_memcmp(const void *a, const void *b, size_t n)
{
    const u8 *x = (const u8 *)a;
    const u8 *y = (const u8 *)b;
    for (; n; n--, x++, y++)
        if (*x != *y)
            return (*x < *y) ? -1 : 1;
    return 0;
}

size_t b43c_strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int b43c_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* lib/kstrtox.c: base 0 (0x/0 prefix), 8, 10 or 16; no sign */
int kstrtoul(const char *s, unsigned int base, unsigned long *res)
{
    unsigned long v = 0;
    int digits = 0;

    if (base == 0 || base == 16) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            s += 2;
            base = 16;
        } else if (base == 0) {
            base = (s[0] == '0') ? 8 : 10;
        }
    }
    for (;; s++) {
        unsigned int d;
        if (*s >= '0' && *s <= '9')
            d = (unsigned int)(*s - '0');
        else if (*s >= 'a' && *s <= 'f')
            d = (unsigned int)(*s - 'a' + 10);
        else if (*s >= 'A' && *s <= 'F')
            d = (unsigned int)(*s - 'A' + 10);
        else
            break;
        if (d >= base)
            return -EINVAL;
        if (v > (~0UL - d) / base)
            return -ERANGE;
        v = v * base + d;
        digits++;
    }
    if (*s == '\n')
        s++;
    if (*s || !digits)
        return -EINVAL;
    *res = v;
    return 0;
}

void *kmalloc(size_t size, gfp_t flags)
{
    (void)flags;
    if (!b43compat_env || !b43compat_env->alloc || size == 0)
        return NULL;
    return b43compat_env->alloc(b43compat_env->ctx, (unsigned long)size);
}

void *kzalloc(size_t size, gfp_t flags)
{
    void *p = kmalloc(size, flags);
    if (p)
        b43c_memset(p, 0, size);
    return p;
}

void *kcalloc(size_t n, size_t size, gfp_t flags)
{
    return kzalloc(n * size, flags);
}

void kfree(const void *p)
{
    if (p && b43compat_env && b43compat_env->free)
        b43compat_env->free(b43compat_env->ctx, (void *)p);
}

/* =========================================================================
 * Time
 * ========================================================================= */
static u64 g_total_us;
static unsigned long g_last_us;
static int g_time_started;

/* 64-bit microsecond counter, derived from the host's 32-bit counter
 * (which overflows every ~71 minutes). */
u64 b43compat_time_us64(void)
{
    unsigned long now = b43compat_env->time_us(b43compat_env->ctx);

    if (!g_time_started) {
        g_time_started = 1;
        g_last_us = now;
    }
    g_total_us += (unsigned long)(now - g_last_us);
    g_last_us = now;
    return g_total_us;
}

/* jiffies: a variable as in Linux, updated by b43compat_jiffies().
 *
 * It starts at INITIAL_JIFFIES (linux/jiffies.h: five minutes before the
 * 32-bit wrap), not at 0. Linux code relies on it: a zeroed timestamp must
 * look "long ago", e.g. b43's lo_measure_setup() measures the TX control
 * values only if txctl_measured_time (0 at start) is older than
 * jiffies - B43_LO_TXCTL_EXPIRE. Counting from 0 made that test false for
 * the first three minutes. */
#define B43C_INITIAL_JIFFIES ((unsigned long)(unsigned int)(-300 * HZ))

unsigned long jiffies = B43C_INITIAL_JIFFIES;

unsigned long b43compat_jiffies(void)
{
    jiffies = B43C_INITIAL_JIFFIES +
              (unsigned long)(b43compat_time_us64() / (1000000 / HZ));
    return jiffies;
}

void udelay(unsigned long us)
{
    b43compat_env->delay_us(b43compat_env->ctx, us);
    b43compat_jiffies();
}

void mdelay(unsigned long ms)
{
    udelay(ms * 1000UL);
}

/* msleep: in Linux the scheduler runs other things while sleeping; here
 * pending work and expired timers run meanwhile, as other threads would */
/* Diagnostics: work items run from an msleep() of code that is not itself a
 * work item. In Linux such a work item would wait if it needed a mutex the
 * sleeper holds; here mutexes do not block, so it runs in between. */
unsigned long b43compat_msleep_works;

void msleep(unsigned int ms)
{
    u64 end = b43compat_time_us64() + (u64)ms * 1000;

    while (b43compat_time_us64() < end) {
        b43compat_msleep_works += (unsigned long)b43compat_run_work();
        udelay(100);
    }
}

void usleep_range(unsigned long min_us, unsigned long max_us)
{
    (void)max_us;
    udelay(min_us);
}

/* =========================================================================
 * Work queues: a single context, work items run in b43compat_run_work()
 * (called by the polling loop, by msleep() and by wait_event())
 * ========================================================================= */
#define B43COMPAT_MAX_WORK 64

static struct work_struct *g_work[B43COMPAT_MAX_WORK];
static struct delayed_work *g_dwork[B43COMPAT_MAX_WORK];

int b43compat_queue_work(struct work_struct *w)
{
    int i;

    if (w->pending)
        return 0;
    for (i = 0; i < B43COMPAT_MAX_WORK; i++) {
        if (!g_work[i]) {
            g_work[i] = w;
            w->pending = 1;
            return 1;
        }
    }
    printk("b43compat: work queue full\n");
    return 0;
}

int b43compat_queue_delayed_work(struct delayed_work *d, unsigned long delay)
{
    int i;

    if (d->armed || d->work.pending)
        return 0;
    if (delay == 0)
        return b43compat_queue_work(&d->work);
    for (i = 0; i < B43COMPAT_MAX_WORK; i++) {
        if (!g_dwork[i]) {
            g_dwork[i] = d;
            d->due = b43compat_jiffies() + delay;
            d->armed = 1;
            return 1;
        }
    }
    printk("b43compat: delayed work queue full\n");
    return 0;
}

void b43compat_cancel_work(struct work_struct *w)
{
    int i;
    for (i = 0; i < B43COMPAT_MAX_WORK; i++)
        if (g_work[i] == w)
            g_work[i] = NULL;
    w->pending = 0;
}

void b43compat_cancel_delayed_work(struct delayed_work *d)
{
    int i;
    for (i = 0; i < B43COMPAT_MAX_WORK; i++)
        if (g_dwork[i] == d)
            g_dwork[i] = NULL;
    d->armed = 0;
    b43compat_cancel_work(&d->work);
}

/* Prevents a work item that calls msleep() from re-running the work items
 * (in Linux a worker does not preempt itself) */
static int g_in_run_work;

int b43compat_run_work(void)
{
    unsigned long now = b43compat_jiffies();
    struct work_struct *w;
    struct delayed_work *d;
    int i, ran = 0;

    if (g_in_run_work)
        return 0;
    g_in_run_work = 1;

    ran += b43compat_run_timers(now);

    /* Expired delayed work items become normal work items */
    for (i = 0; i < B43COMPAT_MAX_WORK; i++) {
        d = g_dwork[i];
        if (d && time_after_eq(now, d->due)) {
            g_dwork[i] = NULL;
            d->armed = 0;
            b43compat_queue_work(&d->work);
        }
    }
    for (i = 0; i < B43COMPAT_MAX_WORK; i++) {
        w = g_work[i];
        if (w) {
            u64 t0 = b43compat_time_us64(), dt;

            g_work[i] = NULL;
            w->pending = 0;
            if (b43compat_pr_debug >= 2)
                printk("b43compat: work %08lx (run_work at %08lx)\n",
                       *(const unsigned long *)(const void *)w->func,
                       *(const unsigned long *)(const void *)b43compat_run_work);
            w->func(w);
            if (b43compat_pr_debug >= 2)
                printk("b43compat: work done\n");
            ran++;
            /* Diagnostics: long work items are the calibrations of b43
             * (periodic work, TX power adjustment) and the scans. The code
             * addresses are resolved with the link map; this function's
             * address gives the load offset. */
            dt = b43compat_time_us64() - t0;
            if (dt >= 20000)
                printk("b43compat: work %08lx took %lu ms (run_work at %08lx)\n",
                       *(const unsigned long *)(const void *)w->func,
                       (unsigned long)(dt / 1000),
                       *(const unsigned long *)(const void *)b43compat_run_work);
        }
    }
    g_in_run_work = 0;
    return ran;
}

/* =========================================================================
 * Interrupts: the handler registered by b43 (request_threaded_irq) is
 * called by b43compat_poll_irq(): from the polling loop in the Tester, from
 * the Mac OS interrupt handler in the driver.
 * ========================================================================= */
irq_handler_t b43compat_irq_handler;
irq_handler_t b43compat_irq_thread;
void *b43compat_irq_dev;

int b43compat_request_irq(unsigned int irq, irq_handler_t h, irq_handler_t th, void *dev_id)
{
    (void)irq;
    b43compat_irq_handler = h;
    b43compat_irq_thread = th;
    b43compat_irq_dev = dev_id;
    return 0;
}

void b43compat_free_irq(void *dev_id)
{
    if (b43compat_irq_dev == dev_id) {
        b43compat_irq_handler = NULL;
        b43compat_irq_thread = NULL;
        b43compat_irq_dev = NULL;
    }
}

/* Statistics for the host's diagnostics */
unsigned long b43compat_irq_polls;      /* polls with a handler installed */
unsigned long b43compat_irq_hard;       /* the hard handler claimed the IRQ */
unsigned long b43compat_irq_threads;    /* the threaded handler ran */

int b43compat_poll_irq(void)
{
    irqreturn_t ret;

    if (!b43compat_irq_handler)
        return 0;
    b43compat_irq_polls++;
    ret = b43compat_irq_handler(0, b43compat_irq_dev);
    if (ret != IRQ_NONE)
        b43compat_irq_hard++;
    if (ret == IRQ_WAKE_THREAD && b43compat_irq_thread) {
        b43compat_irq_threads++;
        ret = b43compat_irq_thread(0, b43compat_irq_dev);
    }
    if (ret != IRQ_NONE)
        b43compat_entropy_poll();       /* time of the interrupt */
    return ret != IRQ_NONE;
}

/* =========================================================================
 * Formatting: %d %i %u %x %X %c %s %p %pM %%, flags '-' '0', width,
 * precision for integers (%.2i), modifiers h l ll z.
 * ========================================================================= */
typedef struct { char *buf; size_t size; size_t pos; } fmt_out;

static void out_c(fmt_out *o, char c)
{
    if (o->pos + 1 < o->size)
        o->buf[o->pos] = c;
    o->pos++;
}

static void out_num(fmt_out *o, u64 val, unsigned base, int upper, int neg,
                    int width, int prec, int zero, int left)
{
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[24];
    int n = 0, pad, zeros;

    do {
        tmp[n++] = dig[val % base];
        val /= base;
    } while (val && n < (int)sizeof(tmp));
    zeros = (prec > n) ? prec - n : 0;
    if (prec >= 0)
        zero = 0;
    pad = width - n - zeros - (neg ? 1 : 0);
    if (left)
        zero = 0;
    if (!left && !zero)
        while (pad-- > 0) out_c(o, ' ');
    if (neg)
        out_c(o, '-');
    if (!left && zero)
        while (pad-- > 0) out_c(o, '0');
    while (zeros-- > 0)
        out_c(o, '0');
    while (n > 0)
        out_c(o, tmp[--n]);
    if (left)
        while (pad-- > 0) out_c(o, ' ');
}

int b43c_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    fmt_out o;

    o.buf = buf;
    o.size = size;
    o.pos = 0;
    while (*fmt) {
        char c = *fmt++;
        int width = 0, prec = -1, zero = 0, left = 0, lng = 0;

        if (c != '%') {
            out_c(&o, c);
            continue;
        }
        for (;;) {
            if (*fmt == '-') { left = 1; fmt++; }
            else if (*fmt == '0') { zero = 1; fmt++; }
            else if (*fmt == '+' || *fmt == ' ' || *fmt == '#') fmt++;
            else break;
        }
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9')
                prec = prec * 10 + (*fmt++ - '0');
        }
        for (;;) {
            if (*fmt == 'l') { lng++; fmt++; }
            else if (*fmt == 'h') fmt++;
            else if (*fmt == 'z') { lng = 1; fmt++; }
            else break;
        }
        c = *fmt;
        if (!c)
            break;
        fmt++;
        switch (c) {
        case 'd':
        case 'i': {
            s64 v;
            if (lng >= 2) v = va_arg(ap, long long);
            else if (lng == 1) v = va_arg(ap, long);
            else v = va_arg(ap, int);
            out_num(&o, (u64)(v < 0 ? -v : v), 10, 0, v < 0, width, prec, zero, left);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            u64 v;
            if (lng >= 2) v = va_arg(ap, unsigned long long);
            else if (lng == 1) v = va_arg(ap, unsigned long);
            else v = va_arg(ap, unsigned int);
            out_num(&o, v, c == 'u' ? 10 : 16, c == 'X', 0, width, prec, zero, left);
            break;
        }
        case 'p': {
            void *p = va_arg(ap, void *);
            if (*fmt == 'M' || *fmt == 'm') {   /* %pM: MAC address */
                const u8 *m = (const u8 *)p;
                int i;
                fmt++;
                for (i = 0; i < 6; i++) {
                    if (i) out_c(&o, ':');
                    out_num(&o, m[i], 16, 0, 0, 2, -1, 1, 0);
                }
            } else if (*fmt == 'V') {           /* %pV: struct va_format */
                const struct va_format *vf = (const struct va_format *)p;
                char tmp[256];
                const char *t = tmp;
                fmt++;
                b43c_vsnprintf(tmp, sizeof(tmp), vf->fmt, *vf->va);
                while (*t)
                    out_c(&o, *t++);
            } else {
                while (*fmt >= 'A' && *fmt <= 'z') fmt++;  /* other %p* ignored */
                out_num(&o, (u64)(unsigned long)p, 16, 0, 0, 8, -1, 1, 0);
            }
            break;
        }
        case 'c':
            out_c(&o, (char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            int len, pad;
            if (!s) s = "(null)";
            len = (int)b43c_strlen(s);
            if (prec >= 0 && len > prec) len = prec;
            pad = width - len;
            if (!left) while (pad-- > 0) out_c(&o, ' ');
            while (len--) out_c(&o, *s++);
            if (left) while (pad-- > 0) out_c(&o, ' ');
            break;
        }
        case '%':
            out_c(&o, '%');
            break;
        default:
            out_c(&o, '%');
            out_c(&o, c);
            break;
        }
    }
    if (size)
        buf[o.pos < size ? o.pos : size - 1] = '\0';
    return (int)o.pos;
}

int b43c_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = b43c_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

void b43compat_log_va(const char *prefix, const char *fmt, va_list ap)
{
    char line[256];
    size_t n = 0;

    if (!b43compat_env || !b43compat_env->log)
        return;
    if (prefix) {
        n = b43c_strlen(prefix);
        b43c_memcpy(line, prefix, n);
    }
    b43c_vsnprintf(line + n, sizeof(line) - n, fmt, ap);
    b43compat_env->log(b43compat_env->ctx, line);
}

int b43c_sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = b43c_vsnprintf(buf, 4096, fmt, ap);    /* sysfs: PAGE_SIZE */
    va_end(ap);
    return n;
}

int printk(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    b43compat_log_va(NULL, fmt, ap);
    va_end(ap);
    return 0;
}

int b43compat_pr_printk(int debug, const char *prefix, const char *fmt, ...)
{
    va_list ap;
    /* in Linux pr_debug() is only enabled with DEBUG/dynamic debug */
    if (debug && !b43compat_pr_debug)
        return 0;
    va_start(ap, fmt);
    b43compat_log_va(prefix, fmt, ap);
    va_end(ap);
    return 0;
}

int b43compat_warn_on(int cond, const char *file, int line)
{
    if (cond)
        printk("[WARN] %s:%d\n", file, line);
    return cond;
}
