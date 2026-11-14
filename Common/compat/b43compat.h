/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef B43COMPAT_H_
#define B43COMPAT_H_

/*
 * b43compat.h - Linux kernel APIs used by b43, implemented for Mac OS 9.
 *
 * The sources in Common/b43/ are copied from Linux WITHOUT modifications: all
 * of the adaptation lives here and in b43compat.c. System functions (waits,
 * time, memory, logging) go through the b43_env structure supplied by the
 * caller (Tester or driver).
 *
 * The headers in linux/ and net/ in this directory all include this file.
 */

#ifdef __MWERKS__
#pragma cpp_extensions on       /* anonymous unions/structs as in GNU C */
#pragma gcc_extensions on       /* arithmetic on void *, empty structs, etc. */
#endif

#include <stddef.h>
#include <stdarg.h>

#ifdef __MWERKS__
/*
 * CodeWarrior does not know __attribute__ (packed, aligned, ...). The Linux
 * structures describing frames and registers must be packed as with
 * __attribute__((packed)), so all Linux code is compiled with CodeWarrior's
 * 1-byte packing. The interfaces shared with the Tester/driver (b43env.h,
 * b43os9.h, b43os9_wpa.h) switch back to normal alignment with
 * "align=power". The C library headers (above) keep normal alignment.
 */
#define __attribute__(x)
#pragma options align=packed
#endif

/* =========================================================================
 * Kernel configuration (Kconfig) for BCM4306 on an SSB-PCI bus
 * ========================================================================= */
#define CONFIG_B43_SSB          1
#define CONFIG_B43_PHY_G        1
#define CONFIG_B43_PIO          1

/* mac80211/cfg80211 built into the driver (as =y) */
#define CONFIG_CFG80211         1
#define CONFIG_MAC80211         1
/* mac80211's connection messages (beacon loss, probing the AP, state
 * changes): printed with pr_debug, so only when the host enables it */
#define CONFIG_MAC80211_MLME_DEBUG 1
/* CONFIG_B43_DEBUG, CONFIG_B43_BCMA, CONFIG_B43_LEDS, CONFIG_B43_HWRNG,
 * CONFIG_B43_PHY_N/LP/HT/LCN/AC: not defined */

/* drivers/ssb: what Kconfig selects for b43 on a PCI card */
#define CONFIG_SSB              1
#define CONFIG_SSB_SPROM        1
#define CONFIG_SSB_BLOCKIO      1
#define CONFIG_SSB_PCIHOST_POSSIBLE 1
#define CONFIG_SSB_PCIHOST      1
#define CONFIG_SSB_B43_PCI_BRIDGE 1
#define CONFIG_SSB_DRIVER_PCICORE_POSSIBLE 1
#define CONFIG_SSB_DRIVER_PCICORE 1
/* CodeWarrior only: EXTIF (SoC external bus). The driver only acts if it
 * finds the core, which the BCM4306 lacks; without it, ssb.h would use an
 * empty struct, which CodeWarrior does not accept. */
#ifdef __MWERKS__
#define CONFIG_SSB_DRIVER_EXTIF   1
#endif
/* PCMCIA, SDIO, SoC, MIPS, GPIO, EMBEDDED, SERIAL: not defined */

/* =========================================================================
 * Types (linux/types.h)
 * ========================================================================= */
typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned long       u32;
typedef unsigned long long  u64;
typedef signed char         s8;
typedef signed short        s16;
typedef signed long         s32;
typedef signed long long    s64;
typedef u8  __u8;
typedef u16 __u16;
typedef u32 __u32;
typedef u64 __u64;
typedef s8  __s8;
typedef s16 __s16;
typedef s32 __s32;
typedef s64 __s64;
typedef u16 __le16;
typedef u32 __le32;
typedef u64 __le64;
typedef u16 __be16;
typedef u32 __be32;
typedef u64 __be64;
typedef u32 dma_addr_t;
typedef u8  uint8_t;
typedef u16 uint16_t;
typedef u32 uint32_t;
typedef int gfp_t;

#ifndef __cplusplus
typedef _Bool bool;              /* as in linux/types.h */
#ifndef true
#define true  1
#define false 0
#endif
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

/* =========================================================================
 * Compiler
 * ========================================================================= */
#define likely(x)               (x)
#define unlikely(x)             (x)
#define __packed                __attribute__((__packed__))
#define __aligned(x)            __attribute__((aligned(x)))
#define __cold
#define __printf(a, b)
#define __must_check
#define __iomem
#define __init
#define __exit
#define __maybe_unused
#define fallthrough             do { } while (0)
#define noinline
/* Macros that vanish at file scope in Linux (EXPORT_SYMBOL, MODULE_*)
 * become a harmless extern declaration, so the ';' that follows them does not
 * leave an empty declaration, which C does not allow. */
#define EXPORT_SYMBOL(x) extern int b43compat_decl_dummy
#define __stringify_1(x)        #x
#define __stringify(x)          __stringify_1(x)
#define READ_ONCE(x)            (x)
#define WRITE_ONCE(x, v)        ((x) = (v))

#define BIT(nr)                 (1UL << (nr))
#define ARRAY_SIZE(x)           (sizeof(x) / sizeof((x)[0]))
#define BUILD_BUG_ON(c)         ((void)sizeof(char[1 - 2 * !!(c)]))
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* min/max without typeof: the arguments may be evaluated twice; in the b43
 * code they are always expressions without side effects. */
#define min(a, b)               ((a) < (b) ? (a) : (b))
#define max(a, b)               ((a) > (b) ? (a) : (b))
#define min_t(type, a, b)       ((type)(a) < (type)(b) ? (type)(a) : (type)(b))
#define max_t(type, a, b)       ((type)(a) > (type)(b) ? (type)(a) : (type)(b))
#define clamp(v, lo, hi)        min(max(v, lo), hi)
#define clamp_val(v, lo, hi)    clamp(v, lo, hi)
#define clamp_t(type, v, lo, hi) min_t(type, max_t(type, v, lo), hi)
#define abs(x)                  ((x) < 0 ? -(x) : (x))
#define DIV_ROUND_UP(n, d)      (((n) + (d) - 1) / (d))
#define roundup(x, y)           ((((x) + ((y) - 1)) / (y)) * (y))

/* =========================================================================
 * Endianness (PowerPC = big endian)
 * ========================================================================= */
#define swab16(x) ((u16)((((u16)(x) & 0x00FFU) << 8) | (((u16)(x) & 0xFF00U) >> 8)))
#define swab32(x) ((u32)((((u32)(x) & 0x000000FFUL) << 24) | \
                         (((u32)(x) & 0x0000FF00UL) << 8)  | \
                         (((u32)(x) & 0x00FF0000UL) >> 8)  | \
                         (((u32)(x) & 0xFF000000UL) >> 24)))
#define cpu_to_le16(x)          swab16(x)
#define le16_to_cpu(x)          swab16(x)
#define cpu_to_le32(x)          swab32(x)
#define le32_to_cpu(x)          swab32(x)
#define cpu_to_be16(x)          ((u16)(x))
#define be16_to_cpu(x)          ((u16)(x))
#define cpu_to_be32(x)          ((u32)(x))
#define be32_to_cpu(x)          ((u32)(x))

/* =========================================================================
 * Errors
 * ========================================================================= */
#define EPERM       1
#define ENOENT      2
#define EIO         5
#define ENOMEM      12
#define EBUSY       16
#define ENODEV      19
#define EINVAL      22
#define ENOSPC      28
#define ETIMEDOUT   110
#define EOPNOTSUPP  95
#define ESRCH       3
#define EPROTO      71
#define ERESTARTSYS 512
#define EINTR       4
#define EFAULT      14
#define EAGAIN      11
#define ENOTSUPP    524
#define ERANGE      34
#define ENOSYS      38
#define EEXIST      17

/* =========================================================================
 * Diagnostics and logging
 * ========================================================================= */
#define KERN_EMERG      ""
#define KERN_ERR        ""
#define KERN_WARNING    ""
#define KERN_NOTICE     ""
#define KERN_INFO       ""
#define KERN_DEBUG      ""
#define KERN_CONT       ""
int  printk(const char *fmt, ...);

/* pr_*(): as in Linux, the prefix is pr_fmt() (ssb defines it as "ssb: "
 * before including the headers). b43compat_pr_printk receives the prefix as
 * a separate string because pr_fmt(fmt) cannot be pasted onto the format
 * here without ##__VA_ARGS__. */
#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif
int  b43compat_pr_printk(int debug, const char *prefix, const char *fmt, ...);
#define pr_emerg(...)           b43compat_pr_printk(0, pr_fmt(""), __VA_ARGS__)
#define pr_err(...)             b43compat_pr_printk(0, pr_fmt(""), __VA_ARGS__)
#define pr_warn(...)            b43compat_pr_printk(0, pr_fmt(""), __VA_ARGS__)
#define pr_notice(...)          b43compat_pr_printk(0, pr_fmt(""), __VA_ARGS__)
#define pr_info(...)            b43compat_pr_printk(0, pr_fmt(""), __VA_ARGS__)
#define pr_debug(...)           b43compat_pr_printk(1, pr_fmt(""), __VA_ARGS__)
#define pr_cont(...)            printk(__VA_ARGS__)
int  b43compat_warn_on(int cond, const char *file, int line);
#define WARN_ON(x)              b43compat_warn_on((x) != 0, __FILE__, __LINE__)
#define WARN_ON_ONCE(x)         WARN_ON(x)
#define WARN(cond, ...)         b43compat_warn_on((cond) != 0, __FILE__, __LINE__)
#define BUG_ON(x)               ((void)WARN_ON(x))
#define might_sleep()           do { } while (0)
#define might_sleep_if(c)       do { } while (0)
#define cond_resched()          do { } while (0)
#define dump_stack()            do { } while (0)
#define lockdep_assert_held(l)  do { } while (0)

/* Library functions: renamed so they do not clash with the Tester's libc
 * (the driver has no libc). Implemented in b43compat.c. */
#define snprintf    b43c_snprintf
#define vsnprintf   b43c_vsnprintf
#define memset      b43c_memset
#define memcpy      b43c_memcpy
#define memmove     b43c_memmove
#define memcmp      b43c_memcmp
#define strlen      b43c_strlen
#define sprintf     b43c_sprintf
#define scnprintf   b43c_snprintf
#define strcmp      b43c_strcmp
int    b43c_snprintf(char *buf, size_t size, const char *fmt, ...);
int    b43c_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
void  *b43c_memset(void *s, int c, size_t n);
void  *b43c_memcpy(void *d, const void *s, size_t n);
void  *b43c_memmove(void *d, const void *s, size_t n);
int    b43c_memcmp(const void *a, const void *b, size_t n);
size_t b43c_strlen(const char *s);
int    b43c_sprintf(char *buf, const char *fmt, ...);
int    b43c_strcmp(const char *a, const char *b);
int    kstrtoul(const char *s, unsigned int base, unsigned long *res);
#define isspace(c)  ((c) == ' ' || ((c) >= '\t' && (c) <= '\r'))
#define isdigit(c)  ((c) >= '0' && (c) <= '9')
#define isxdigit(c) (isdigit(c) || ((c) >= 'a' && (c) <= 'f') || ((c) >= 'A' && (c) <= 'F'))

/* =========================================================================
 * Memory (linux/slab.h)
 * ========================================================================= */
#define GFP_KERNEL  0
#define GFP_ATOMIC  1
void *kmalloc(size_t size, gfp_t flags);
void *kzalloc(size_t size, gfp_t flags);
void *kcalloc(size_t n, size_t size, gfp_t flags);
void  kfree(const void *p);

/* =========================================================================
 * Time (linux/delay.h, linux/jiffies.h)
 * ========================================================================= */
#define HZ 100
#define PAGE_SIZE 4096
void udelay(unsigned long us);
void mdelay(unsigned long ms);
void msleep(unsigned int ms);
void usleep_range(unsigned long min_us, unsigned long max_us);
/* jiffies is a variable (mac80211 has fields named "jiffies"): it is
 * updated by b43compat_jiffies(), called by the waits, by the polling loop
 * and by ktime_get() */
extern unsigned long jiffies;
unsigned long b43compat_jiffies(void);
/* Rounds up, as Linux does: a timeout shorter than a tick is one tick, not 0 */
#define msecs_to_jiffies(ms)    (((unsigned long)(ms) + (1000 / HZ) - 1) / (1000 / HZ))
#define jiffies_to_msecs(j)     ((unsigned long)(j) * (1000 / HZ))
#define time_after(a, b)        ((long)((b) - (a)) < 0)
#define time_before(a, b)       time_after(b, a)
#define time_after_eq(a, b)     ((long)((a) - (b)) >= 0)
#define time_before_eq(a, b)    time_after_eq(b, a)
#define round_jiffies(j)        (j)
#define round_jiffies_relative(j) (j)

/* =========================================================================
 * Synchronization: a single execution context, everything is a no-op
 * ========================================================================= */
struct mutex { int locked; };
#define mutex_init(m)           ((m)->locked = 0)
#define mutex_lock(m)           ((m)->locked = 1)
#define mutex_unlock(m)         ((m)->locked = 0)
#define mutex_is_locked(m)      ((m)->locked)
#define mutex_lock_interruptible(m) (mutex_lock(m), 0)
#define DEFINE_MUTEX(name)      struct mutex name = { 0 }
typedef struct { int dummy; } spinlock_t;
#define spin_lock_init(l)       do { } while (0)
#define spin_lock(l)            do { } while (0)
#define spin_unlock(l)          do { } while (0)
#define spin_lock_irqsave(l, f)      do { (f) = 0; } while (0)
#define spin_unlock_irqrestore(l, f) do { (void)(f); } while (0)
#define spin_lock_bh(l)         do { } while (0)
#define spin_unlock_bh(l)       do { } while (0)
#define DEFINE_SPINLOCK(name)   spinlock_t name = { 0 }
typedef struct { int counter; } atomic_t;
#define atomic_read(v)          ((v)->counter)
#define atomic_set(v, i)        ((v)->counter = (i))
/* Barriers as arch/powerpc/include/asm/barrier.h (32-bit, uniprocessor):
 * mb/rmb/wmb order memory against the device (DMA descriptors, then the
 * register that hands them over) and are "sync"; the smp_ ones only stop
 * the compiler. The G4 has no lwsync: dma_rmb/dma_wmb are "sync" too. */
#if defined(__GNUC__)
#define b43c_hw_sync()          __asm__ __volatile__("sync" : : : "memory")
#define b43c_compiler_barrier() __asm__ __volatile__("" : : : "memory")
#elif defined(__MWERKS__)
#define b43c_hw_sync()          __sync()
#define b43c_compiler_barrier() do { } while (0)
#else
#define b43c_hw_sync()          do { } while (0)
#define b43c_compiler_barrier() do { } while (0)
#endif
#define smp_wmb()               b43c_compiler_barrier()
#define wmb()                   b43c_hw_sync()
#define rmb()                   b43c_hw_sync()
#define smp_rmb()               b43c_compiler_barrier()
#define dma_wmb()               b43c_hw_sync()
#define dma_rmb()               b43c_hw_sync()
#define mmiowb()                do { } while (0)
#define synchronize_irq(i)      do { } while (0)
struct completion { int done; };
#define init_completion(c)      ((c)->done = 0)
#define complete(c)             ((c)->done = 1)

/* =========================================================================
 * Lists (linux/list.h) - without typeof
 * ========================================================================= */
struct list_head { struct list_head *next, *prev; };
#define LIST_HEAD_INIT(name)    { &(name), &(name) }
#define LIST_HEAD(name)         struct list_head name = LIST_HEAD_INIT(name)
static inline void INIT_LIST_HEAD(struct list_head *l) { l->next = l; l->prev = l; }
static inline void __list_add(struct list_head *n, struct list_head *prev, struct list_head *next)
{
    next->prev = n; n->next = next; n->prev = prev; prev->next = n;
}
static inline void list_add(struct list_head *n, struct list_head *head) { __list_add(n, head, head->next); }
static inline void list_add_tail(struct list_head *n, struct list_head *head) { __list_add(n, head->prev, head); }
static inline void list_del(struct list_head *e)
{
    e->next->prev = e->prev; e->prev->next = e->next; e->next = e; e->prev = e;
}
static inline int list_empty(const struct list_head *h) { return h->next == h; }
static inline void list_move_tail(struct list_head *e, struct list_head *head)
{
    e->next->prev = e->prev; e->prev->next = e->next;
    list_add_tail(e, head);
}
#define list_entry(ptr, type, member) container_of(ptr, type, member)
/* Member offset computed from the pointer itself (pos = NULL at the start) */
#define __b43_member_off(pos, member)  ((size_t)((char *)&(pos)->member - (char *)(pos)))
#define list_for_each_entry(pos, head, member)                                   \
    for (pos = NULL, pos = (void *)((char *)((head)->next) - __b43_member_off(pos, member)); \
         &(pos)->member != (head);                                               \
         pos = (void *)((char *)((pos)->member.next) - __b43_member_off(pos, member)))
#define list_for_each_entry_safe(pos, n, head, member)                           \
    for (pos = NULL, pos = (void *)((char *)((head)->next) - __b43_member_off(pos, member)), \
         n = (void *)((char *)((pos)->member.next) - __b43_member_off(pos, member));         \
         &(pos)->member != (head);                                               \
         pos = n, n = (void *)((char *)((n)->member.next) - __b43_member_off(n, member)))

/* =========================================================================
 * Work queues: run by b43compat_run_work() in the main loop
 * ========================================================================= */
struct work_struct;
typedef void (*work_func_t)(struct work_struct *work);
struct work_struct {
    work_func_t func;
    int pending;
    struct work_struct *next_pending;
};
struct delayed_work {
    struct work_struct work;
    unsigned long due;      /* jiffies */
    int armed;
};
#define INIT_WORK(w, f)         do { (w)->func = (f); (w)->pending = 0; } while (0)
#define INIT_DELAYED_WORK(d, f) do { INIT_WORK(&(d)->work, f); (d)->armed = 0; } while (0)
#define to_delayed_work(w)      container_of(w, struct delayed_work, work)
int  b43compat_queue_work(struct work_struct *w);
int  b43compat_run_work(void);
int  b43compat_queue_delayed_work(struct delayed_work *d, unsigned long delay);
void b43compat_cancel_work(struct work_struct *w);
void b43compat_cancel_delayed_work(struct delayed_work *d);
#define schedule_work(w)                b43compat_queue_work(w)
#define cancel_work_sync(w)             b43compat_cancel_work(w)
#define cancel_delayed_work(d)          b43compat_cancel_delayed_work(d)
#define cancel_delayed_work_sync(d)     b43compat_cancel_delayed_work(d)
#define flush_work(w)                   do { } while (0)

/* =========================================================================
 * Interrupts (linux/interrupt.h). For now everything works by polling:
 * registering the handler installs nothing.
 * ========================================================================= */
typedef int irqreturn_t;
#define IRQ_NONE            0
#define IRQ_HANDLED         1
#define IRQ_WAKE_THREAD     2
#define IRQF_SHARED         0x80
#define KBUILD_MODNAME      "b43"
typedef irqreturn_t (*irq_handler_t)(int irq, void *dev_id);
int  b43compat_request_irq(unsigned int irq, irq_handler_t h, irq_handler_t th, void *dev_id);
#define request_threaded_irq(irq, h, th, flags, name, dev) b43compat_request_irq(irq, h, th, dev)
void b43compat_free_irq(void *dev_id);
#define free_irq(irq, dev)  b43compat_free_irq(dev)
int  b43compat_poll_irq(void);
int  b43compat_run_timers(unsigned long now);

/* =========================================================================
 * Miscellaneous
 * ========================================================================= */

/* linux/watchdog.h: only for the types in bcm47xx_wdt.h */
struct watchdog_device { unsigned int timeout; unsigned int max_hw_heartbeat_ms; };
#define vfree(p)                kfree(p)

/* linux/unaligned.h */
static inline u32 get_unaligned_be32(const void *p)
{
    const u8 *b = (const u8 *)p;
    return ((u32)b[0] << 24) | ((u32)b[1] << 16) | ((u32)b[2] << 8) | (u32)b[3];
}
static inline u16 get_unaligned_be16(const void *p)
{
    const u8 *b = (const u8 *)p;
    return (u16)(((u16)b[0] << 8) | b[1]);
}

static inline u16 get_unaligned_le16(const void *p)
{
    const u8 *b = (const u8 *)p;
    return (u16)(b[0] | ((u16)b[1] << 8));
}
static inline u32 get_unaligned_le32(const void *p)
{
    const u8 *b = (const u8 *)p;
    return (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24);
}
static inline u64 get_unaligned_le64(const void *p)
{
    const u8 *b = (const u8 *)p;
    return (u64)get_unaligned_le32(b) | ((u64)get_unaligned_le32(b + 4) << 32);
}
static inline void put_unaligned_le16(u16 v, void *p)
{
    u8 *b = (u8 *)p;
    b[0] = (u8)v; b[1] = (u8)(v >> 8);
}
static inline void put_unaligned_le32(u32 v, void *p)
{
    u8 *b = (u8 *)p;
    b[0] = (u8)v; b[1] = (u8)(v >> 8); b[2] = (u8)(v >> 16); b[3] = (u8)(v >> 24);
}
static inline void put_unaligned_le64(u64 v, void *p)
{
    put_unaligned_le32((u32)v, p);
    put_unaligned_le32((u32)(v >> 32), (u8 *)p + 4);
}
static inline void put_unaligned_be16(u16 v, void *p)
{
    u8 *b = (u8 *)p;
    b[0] = (u8)(v >> 8); b[1] = (u8)v;
}
static inline void put_unaligned_be32(u32 v, void *p)
{
    u8 *b = (u8 *)p;
    b[0] = (u8)(v >> 24); b[1] = (u8)(v >> 16); b[2] = (u8)(v >> 8); b[3] = (u8)v;
}
static inline u64 get_unaligned_be64(const void *p)
{
    const u8 *b = (const u8 *)p;
    return ((u64)get_unaligned_be32(b) << 32) | get_unaligned_be32(b + 4);
}

static inline u8 bitrev8(u8 b)
{
    b = (u8)(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
    b = (u8)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
    return (u8)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
}

#include "b43compat_device.h"
#include "b43compat_net.h"
#ifdef __MWERKS__
#include "b43compat_cw.h"     /* empty structs (CodeWarrior only) */
#endif

#endif /* B43COMPAT_H_ */
