/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef B43COMPAT_NET_H_
#define B43COMPAT_NET_H_

/*
 * b43compat_net.h - Kernel infrastructure used by mac80211/cfg80211 and
 * by the b43 data path: Kconfig, RCU, bitops, sk_buff, net_device,
 * timers, ktime, etherdevice.
 *
 * A single execution context (Mac OS 9, no preemption): RCU and locks
 * are empty, the "grace period" is immediate.
 *
 * Included by b43compat.h.
 */

/* =========================================================================
 * linux/kconfig.h: IS_ENABLED()
 * Unlike Linux, __take_second_arg always receives one extra argument
 * (~): this way the variadic part is never empty, as C99 requires.
 * ========================================================================= */
#define __ARG_PLACEHOLDER_1 0,
#define __take_second_arg(__ignored, val, ...) val
#define __is_defined(x)                 ___is_defined(x)
#define ___is_defined(val)              ____is_defined(__ARG_PLACEHOLDER_##val)
#define ____is_defined(arg1_or_junk)    __take_second_arg(arg1_or_junk 1, 0, ~)
#define __or(x, y)                      ___or(x, y)
#define ___or(x, y)                     ____or(__ARG_PLACEHOLDER_##x, y)
#define ____or(arg1_or_junk, y)         __take_second_arg(arg1_or_junk 1, y, ~)
#define IS_BUILTIN(option)              __is_defined(option)
#define IS_MODULE(option)               __is_defined(option##_MODULE)
#define IS_REACHABLE(option)            IS_BUILTIN(option)
#define IS_ENABLED(option)              __or(IS_BUILTIN(option), IS_MODULE(option))

/* =========================================================================
 * sparse and compiler annotations
 * ========================================================================= */
#define __rcu
#define __acquires(x)
#define __releases(x)
#define __acquire(x)            (void)0
#define __release(x)            (void)0
#define __must_hold(x)
#define __bitwise
#define __force
#define __user
#define __percpu
#define __read_mostly
#define __always_inline         inline
#define __ro_after_init
#define __randomize_layout
#define ____cacheline_aligned
#define ____cacheline_aligned_in_smp
#define __deprecated
#define __pure
#define __weak
#define __used
#define __visible
#define __section(s)
#define __nocast
#define __iomem
#define __attribute_const__
#define __noreturn
#define __alias(x)
#define __always_unused
#define __malloc
#define __no_randomize_layout
#define __designated_init

/* =========================================================================
 * RCU (linux/rcupdate.h)
 * ========================================================================= */
struct rcu_head {
    struct rcu_head *next;
    void (*func)(struct rcu_head *head);
};
#define rcu_read_lock()                 do { } while (0)
#define rcu_read_unlock()               do { } while (0)
#define rcu_read_lock_bh()              do { } while (0)
#define rcu_read_unlock_bh()            do { } while (0)
#define rcu_dereference(p)              (p)
#define rcu_dereference_bh(p)           (p)
#define rcu_dereference_raw(p)          (p)
#define rcu_dereference_check(p, c)     (p)
#define rcu_dereference_protected(p, c) (p)
#define rcu_access_pointer(p)           (p)
#define rcu_assign_pointer(p, v)        ((p) = (v))
#define RCU_INIT_POINTER(p, v)          ((p) = (v))
#define RCU_INITIALIZER(v)              (v)
#define rcu_head_init(h)                do { } while (0)
#define synchronize_rcu()               do { } while (0)
#define synchronize_net()               do { } while (0)
#define rcu_read_lock_held()            1
#define lockdep_is_held(l)              1
/*
 * Callbacks and frees are deferred to the end of the current pass of the
 * main loop (b43os9_poll), which is the grace period here: the stack runs
 * one pass at a time and no reader holds a pointer across passes. Freeing at
 * once would let the allocator hand the memory out again while the code
 * that removed the object is still walking it, as Linux allows.
 */
void b43compat_call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *head));
void b43compat_rcu_process(void);
#define call_rcu(head, f)               b43compat_call_rcu((head), (f))
/* As in Linux: the offset of the rcu_head stands in for the callback */
#define kfree_rcu(ptr, field) \
    b43compat_call_rcu(&(ptr)->field, \
        (void (*)(struct rcu_head *))(unsigned long)offsetof(typeof(*(ptr)), field))
#define rcu_barrier()                   b43compat_rcu_process()

/* =========================================================================
 * Bitops (linux/bitops.h, linux/bitmap.h)
 * ========================================================================= */
#define BITS_PER_LONG           32
#define BITS_PER_BYTE           8
#define BIT_ULL(nr)             (1ULL << (nr))
#define BIT_MASK(nr)            (1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)            ((nr) / BITS_PER_LONG)
#define BITS_TO_LONGS(nr)       DIV_ROUND_UP(nr, BITS_PER_LONG)
#define DECLARE_BITMAP(name, bits) unsigned long name[BITS_TO_LONGS(bits)]
#define GENMASK(h, l)           (((~0UL) - (1UL << (l)) + 1) & (~0UL >> (BITS_PER_LONG - 1 - (h))))
#define GENMASK_ULL(h, l)       (((~0ULL) - (1ULL << (l)) + 1) & (~0ULL >> (63 - (h))))

static inline void set_bit(int nr, volatile unsigned long *addr)
{ addr[BIT_WORD(nr)] |= BIT_MASK(nr); }
static inline void clear_bit(int nr, volatile unsigned long *addr)
{ addr[BIT_WORD(nr)] &= ~BIT_MASK(nr); }
static inline int test_bit(int nr, const volatile unsigned long *addr)
{ return (addr[BIT_WORD(nr)] & BIT_MASK(nr)) != 0; }
static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{ int old = test_bit(nr, addr); set_bit(nr, addr); return old; }
static inline int test_and_clear_bit(int nr, volatile unsigned long *addr)
{ int old = test_bit(nr, addr); clear_bit(nr, addr); return old; }
static inline void change_bit(int nr, volatile unsigned long *addr)
{ addr[BIT_WORD(nr)] ^= BIT_MASK(nr); }
#define __set_bit(nr, a)        set_bit(nr, a)
#define __clear_bit(nr, a)      clear_bit(nr, a)
#define __test_and_set_bit(nr, a)   test_and_set_bit(nr, a)
#define __test_and_clear_bit(nr, a) test_and_clear_bit(nr, a)
#define clear_bit_unlock(nr, a) clear_bit(nr, a)
#define test_and_set_bit_lock(nr, a) test_and_set_bit(nr, a)
#define smp_mb__before_atomic() do { } while (0)
#define smp_mb__after_atomic()  do { } while (0)
#define smp_mb()                do { } while (0)
#define smp_store_release(p, v) (*(p) = (v))
#define smp_load_acquire(p)     (*(p))

unsigned long find_next_bit(const unsigned long *addr, unsigned long size, unsigned long offset);
unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size, unsigned long offset);
#define find_first_bit(a, s)        find_next_bit(a, s, 0)
#define find_first_zero_bit(a, s)   find_next_zero_bit(a, s, 0)
#define for_each_set_bit(bit, addr, size) \
    for ((bit) = find_first_bit((addr), (size)); (bit) < (size); \
         (bit) = find_next_bit((addr), (size), (bit) + 1))
#define for_each_clear_bit(bit, addr, size) \
    for ((bit) = find_first_zero_bit((addr), (size)); (bit) < (size); \
         (bit) = find_next_zero_bit((addr), (size), (bit) + 1))
static inline void bitmap_zero(unsigned long *dst, unsigned int nbits)
{ memset(dst, 0, BITS_TO_LONGS(nbits) * sizeof(unsigned long)); }
static inline void bitmap_fill(unsigned long *dst, unsigned int nbits)
{ memset(dst, 0xff, BITS_TO_LONGS(nbits) * sizeof(unsigned long)); }
int  bitmap_empty(const unsigned long *src, unsigned int nbits);

unsigned int hweight8(unsigned int w);
unsigned int hweight16(unsigned int w);
unsigned int hweight32(unsigned int w);
unsigned long hweight64(u64 w);
#define hweight_long(w)         hweight32(w)
int  fls(unsigned int x);
int  ffs(int x);
unsigned long __ffs(unsigned long word);
#define ilog2(n)                (fls(n) - 1)
#define is_power_of_2(n)        ((n) != 0 && (((n) & ((n) - 1)) == 0))

/* linux/bitfield.h, without typeof: the mask is always a constant */
#define __bf_shf(x)             (__ffs(x))
#define FIELD_PREP(_mask, _val) (((u32)(_val) << __bf_shf(_mask)) & (_mask))
#define FIELD_GET(_mask, _reg)  (((_reg) & (_mask)) >> __bf_shf(_mask))
#define u8_get_bits(v, field)   ((u8)FIELD_GET(field, v))
#define u16_get_bits(v, field)  ((u16)FIELD_GET(field, v))
#define u32_get_bits(v, field)  ((u32)FIELD_GET(field, v))
#define le16_get_bits(v, field) ((u16)FIELD_GET(field, le16_to_cpu(v)))
#define le32_get_bits(v, field) ((u32)FIELD_GET(field, le32_to_cpu(v)))
#define u8_encode_bits(v, field)  ((u8)FIELD_PREP(field, v))
#define u16_encode_bits(v, field) ((u16)FIELD_PREP(field, v))
#define u32_encode_bits(v, field) ((u32)FIELD_PREP(field, v))
#define le16_encode_bits(v, field) cpu_to_le16(FIELD_PREP(field, v))
#define le32_encode_bits(v, field) cpu_to_le32(FIELD_PREP(field, v))

/* =========================================================================
 * Atomics (single context: plain operations)
 * ========================================================================= */
#define atomic_inc(v)               ((v)->counter++)
#define atomic_dec(v)               ((v)->counter--)
#define atomic_add(i, v)            ((v)->counter += (i))
#define atomic_sub(i, v)            ((v)->counter -= (i))
#define atomic_inc_return(v)        (++(v)->counter)
#define atomic_dec_return(v)        (--(v)->counter)
#define atomic_add_return(i, v)     ((v)->counter += (i))
#define atomic_sub_return(i, v)     ((v)->counter -= (i))
#define atomic_dec_and_test(v)      (--(v)->counter == 0)
#define atomic_inc_and_test(v)      (++(v)->counter == 0)
#define atomic_sub_and_test(i, v)   (((v)->counter -= (i)) == 0)
#define atomic_xchg(v, n)           b43compat_atomic_xchg(&(v)->counter, n)
#define ATOMIC_INIT(i)              { (i) }
static inline int b43compat_atomic_xchg(int *p, int n) { int o = *p; *p = n; return o; }
typedef struct { long long counter; } atomic64_t;
#define atomic64_read(v)            ((v)->counter)
#define atomic64_set(v, i)          ((v)->counter = (i))
#define atomic64_inc(v)             ((v)->counter++)
#define atomic64_inc_return(v)      (++(v)->counter)
#define atomic64_add(i, v)          ((v)->counter += (i))

struct kref { atomic_t refcount; };
#define kref_init(k)                ((k)->refcount.counter = 1)
#define kref_get(k)                 ((k)->refcount.counter++)
static inline int kref_put(struct kref *k, void (*release)(struct kref *k))
{
    if (--k->refcount.counter == 0) {
        release(k);
        return 1;
    }
    return 0;
}
typedef struct { int refs; } refcount_t;

/* =========================================================================
 * Time: ktime (ns), timers (run by b43compat_run_work())
 * ========================================================================= */
typedef s64 ktime_t;
#define NSEC_PER_USEC           1000L
#define NSEC_PER_MSEC           1000000L
#define USEC_PER_MSEC           1000L
#define MSEC_PER_SEC            1000L
#define USEC_PER_SEC            1000000L
#define NSEC_PER_SEC            1000000000LL
ktime_t ktime_get(void);
#define ktime_get_boottime()    ktime_get()
#define ktime_get_real()        ktime_get()
#define ktime_get_ns()          ((u64)ktime_get())
#define ktime_get_boottime_ns() ((u64)ktime_get())
#define ktime_to_ns(t)          ((s64)(t))
#define ktime_to_us(t)          ((s64)(t) / 1000)
#define ktime_to_ms(t)          ((s64)(t) / 1000000)
#define ns_to_ktime(ns)         ((ktime_t)(ns))
#define ktime_sub(a, b)         ((a) - (b))
#define ktime_add_us(t, us)     ((t) + (ktime_t)(us) * 1000)
#define ktime_us_delta(a, b)    ktime_to_us((a) - (b))
#define ktime_ms_delta(a, b)    ktime_to_ms((a) - (b))
#define ktime_before(a, b)      ((a) < (b))
#define ktime_after(a, b)       ((a) > (b))
#define usecs_to_jiffies(us)    (((unsigned long)(us) + (1000000 / HZ) - 1) / (1000000 / HZ))
#define jiffies_to_usecs(j)     ((unsigned long)(j) * (1000000 / HZ))
#define get_jiffies_64()        ((u64)jiffies)
#define jiffies_64              get_jiffies_64()
#define MAX_JIFFY_OFFSET        ((long)(~0UL >> 1) - 1)
#define time_is_before_jiffies(a)   time_after(jiffies, a)
#define time_is_after_jiffies(a)    time_before(jiffies, a)
#define time_is_before_eq_jiffies(a) time_after_eq(jiffies, a)
#define time_in_range(a, b, c)  (time_after_eq(a, b) && time_before_eq(a, c))

struct timer_list {
    unsigned long expires;
    void (*function)(struct timer_list *t);
    unsigned int flags;
    /* Mac OS 9 */
    int b43c_armed;
    struct timer_list *b43c_next;
};
void timer_setup(struct timer_list *t, void (*fn)(struct timer_list *), unsigned int flags);
int  mod_timer(struct timer_list *t, unsigned long expires);
int  del_timer(struct timer_list *t);
#define del_timer_sync(t)       del_timer(t)
#define add_timer(t)            mod_timer(t, (t)->expires)
#define timer_pending(t)        ((t)->b43c_armed)
#define from_timer(var, callback_timer, timer_fieldname) \
    ((void *)((char *)(callback_timer) - __b43_member_off(var, timer_fieldname)))

/* =========================================================================
 * Random numbers, CRC
 * ========================================================================= */
/* Random numbers (b43compat_random.c): get_random_bytes is a CSPRNG (AES-CTR
 * keyed by an entropy pool), as wpa_supplicant uses it for the WPA nonces */
void get_random_bytes(void *buf, int nbytes);
u32  prandom_u32(void);
/* Entropy: every value read from the card goes into a cheap accumulator,
 * folded into the pool at every interrupt poll with the time of the poll */
extern u32 b43compat_entropy_acc;
#define b43compat_entropy_add(v) \
    (b43compat_entropy_acc = ((b43compat_entropy_acc << 7) | (b43compat_entropy_acc >> 25)) + (u32)(v))
void b43compat_entropy_poll(void);
void b43compat_entropy_data(const void *data, unsigned long len);
#define get_random_u32()        prandom_u32()
#define prandom_u32_max(r)      ((u32)(((u64)prandom_u32() * (r)) >> 32))
u32  crc32_le(u32 crc, const unsigned char *p, size_t len);
#define ether_crc_le(len, p)    crc32_le(~0, p, len)

/* =========================================================================
 * sk_buff (linux/skbuff.h): linear buffers only
 * ========================================================================= */
struct net_device;
struct sock;
#include <linux/rbtree_types.h>   /* Linux, unmodified */

typedef unsigned int sk_buff_data_t;

struct sk_buff {
    union {
        struct {
            struct sk_buff *next;
            struct sk_buff *prev;
            union {
                struct net_device *dev;
                unsigned long dev_scratch;
            };
        };
        struct rb_node rbnode;
        struct list_head list;
    };
    struct sock *sk;
    void (*destructor)(struct sk_buff *skb);
    ktime_t tstamp;
    char cb[48] __aligned(8);
    unsigned int len, data_len;
    u16 mac_len, hdr_len;
    u16 queue_mapping;
    u8 cloned:1, nohdr:1, pkt_type:3, ip_summed:2, ooo_okay:1, head_frag:1;
    u8 wifi_acked_valid:1, wifi_acked:1, no_fcs:1, encapsulation:1;
    u32 priority;
    int skb_iif;
    u32 hash;
    u16 vlan_tci;
    u32 mark;
    __be16 protocol;
    u16 transport_header;
    u16 network_header;
    u16 mac_header;
    unsigned char *tail;
    unsigned char *end;
    unsigned char *head, *data;
    unsigned int truesize;
    refcount_t users;
};

struct sk_buff_head {
    struct sk_buff *next;
    struct sk_buff *prev;
    u32 qlen;
    spinlock_t lock;
};

struct page;
typedef struct { struct page *bv_page; unsigned int bv_len; unsigned int bv_offset; } skb_frag_t;
struct skb_shared_info {
    u8 nr_frags;
    skb_frag_t frags[1];
    u8 tx_flags;
    unsigned short gso_size;
    struct sk_buff *frag_list;
};
#define SKBTX_WIFI_STATUS       (1 << 4)
#define skb_has_frag_list(skb)  0
#define skb_get_kcov_handle(skb) 0
#define kcov_remote_start_common(h) do { } while (0)
#define kcov_remote_stop()      do { } while (0)

#define CHECKSUM_NONE           0
#define CHECKSUM_UNNECESSARY    1
#define CHECKSUM_COMPLETE       2
#define CHECKSUM_PARTIAL        3
#define PACKET_HOST             0
#define PACKET_BROADCAST        1
#define PACKET_MULTICAST        2
#define PACKET_OTHERHOST        3
#define NET_SKB_PAD             32
#define SKB_DATA_ALIGN(x)       (((x) + 3) & ~3)

struct sk_buff *alloc_skb(unsigned int size, gfp_t priority);
struct sk_buff *__dev_alloc_skb(unsigned int length, gfp_t gfp_mask);
#define dev_alloc_skb(len)      __dev_alloc_skb(len, GFP_ATOMIC)
#define netdev_alloc_skb(dev, len) __dev_alloc_skb(len, GFP_ATOMIC)
void kfree_skb(struct sk_buff *skb);
#define consume_skb(skb)        kfree_skb(skb)
#define dev_kfree_skb(skb)      kfree_skb(skb)
#define dev_kfree_skb_any(skb)  kfree_skb(skb)
#define dev_kfree_skb_irq(skb)  kfree_skb(skb)
#define dev_consume_skb_any(skb) kfree_skb(skb)
#define kfree_skb_list(skb)     b43compat_kfree_skb_list(skb)
void b43compat_kfree_skb_list(struct sk_buff *segs);
struct sk_buff *skb_copy(const struct sk_buff *skb, gfp_t gfp);
struct sk_buff *skb_copy_expand(const struct sk_buff *skb, int newheadroom, int newtailroom, gfp_t gfp);
#define skb_clone(skb, gfp)     skb_copy(skb, gfp)
#define pskb_copy(skb, gfp)     skb_copy(skb, gfp)
int  pskb_expand_head(struct sk_buff *skb, int nhead, int ntail, gfp_t gfp);
struct sk_buff *skb_get(struct sk_buff *skb);

static inline unsigned char *skb_tail_pointer(const struct sk_buff *skb) { return skb->tail; }
static inline unsigned char *skb_end_pointer(const struct sk_buff *skb) { return skb->end; }
static inline unsigned int skb_headroom(const struct sk_buff *skb) { return (unsigned int)(skb->data - skb->head); }
static inline int skb_tailroom(const struct sk_buff *skb) { return (int)(skb->end - skb->tail); }
static inline unsigned int skb_headlen(const struct sk_buff *skb) { return skb->len - skb->data_len; }
static inline int skb_is_nonlinear(const struct sk_buff *skb) { return skb->data_len != 0; }
static inline void skb_reserve(struct sk_buff *skb, int len) { skb->data += len; skb->tail += len; }
void *skb_put(struct sk_buff *skb, unsigned int len);
void *skb_push(struct sk_buff *skb, unsigned int len);
void *skb_pull(struct sk_buff *skb, unsigned int len);
void  skb_trim(struct sk_buff *skb, unsigned int len);
#define __skb_put(skb, len)     skb_put(skb, len)
#define __skb_push(skb, len)    skb_push(skb, len)
#define __skb_pull(skb, len)    skb_pull(skb, len)
#define __skb_trim(skb, len)    skb_trim(skb, len)
#define pskb_trim(skb, len)     (skb_trim(skb, len), 0)
#define pskb_pull(skb, len)     skb_pull(skb, len)
static inline bool pskb_may_pull(struct sk_buff *skb, unsigned int n) { return n <= skb->len; }
static inline void *skb_put_data(struct sk_buff *skb, const void *data, unsigned int len)
{ void *p = skb_put(skb, len); memcpy(p, data, len); return p; }
#define __skb_put_data(skb, d, l) skb_put_data(skb, d, l)
static inline void *skb_put_zero(struct sk_buff *skb, unsigned int len)
{ void *p = skb_put(skb, len); memset(p, 0, len); return p; }
#define __skb_put_zero(skb, l)  skb_put_zero(skb, l)
static inline void skb_put_u8(struct sk_buff *skb, u8 val) { *(u8 *)skb_put(skb, 1) = val; }
#define skb_linearize(skb)      0
#define skb_cloned(skb)         0
#define skb_header_cloned(skb)  0
#define skb_shared(skb)         0
#define skb_unshare(skb, gfp)   (skb)
#define skb_orphan(skb)         do { } while (0)
#define skb_shinfo(skb)         (&b43compat_skb_shinfo)
extern struct skb_shared_info b43compat_skb_shinfo;
int  skb_cow_head(struct sk_buff *skb, unsigned int headroom);
#define skb_cow(skb, h)         skb_cow_head(skb, h)
#define skb_ensure_writable(skb, len) 0
static inline void skb_set_queue_mapping(struct sk_buff *skb, u16 q) { skb->queue_mapping = q; }
static inline u16 skb_get_queue_mapping(const struct sk_buff *skb) { return skb->queue_mapping; }
static inline unsigned char *skb_mac_header(const struct sk_buff *skb) { return skb->head + skb->mac_header; }
static inline unsigned char *skb_network_header(const struct sk_buff *skb) { return skb->head + skb->network_header; }
static inline unsigned char *skb_transport_header(const struct sk_buff *skb) { return skb->head + skb->transport_header; }
static inline void skb_reset_mac_header(struct sk_buff *skb) { skb->mac_header = (u16)(skb->data - skb->head); }
static inline void skb_reset_network_header(struct sk_buff *skb) { skb->network_header = (u16)(skb->data - skb->head); }
static inline void skb_reset_transport_header(struct sk_buff *skb) { skb->transport_header = (u16)(skb->data - skb->head); }
static inline void skb_set_network_header(struct sk_buff *skb, int off) { skb->network_header = (u16)(skb->data - skb->head + off); }
static inline void skb_set_mac_header(struct sk_buff *skb, int off) { skb->mac_header = (u16)(skb->data - skb->head + off); }
static inline void skb_set_transport_header(struct sk_buff *skb, int off) { skb->transport_header = (u16)(skb->data - skb->head + off); }
static inline int skb_network_offset(const struct sk_buff *skb) { return (int)(skb_network_header(skb) - skb->data); }
static inline int skb_mac_header_was_set(const struct sk_buff *skb) { return skb->mac_header != 0xffff; }
#define skb_checksum_none_assert(skb) do { } while (0)
#define skb_tx_timestamp(skb)   do { } while (0)
#define skb_record_rx_queue(skb, q) do { } while (0)
#define skb_get_hash(skb)       ((skb)->hash)
int  skb_copy_bits(const struct sk_buff *skb, int offset, void *to, int len);
void *skb_header_pointer(const struct sk_buff *skb, int offset, int len, void *buffer);

/* sk_buff queues */
static inline void __skb_queue_head_init(struct sk_buff_head *list)
{ list->prev = list->next = (struct sk_buff *)list; list->qlen = 0; }
#define skb_queue_head_init(l)  __skb_queue_head_init(l)
static inline u32 skb_queue_len(const struct sk_buff_head *list) { return list->qlen; }
#define skb_queue_len_lockless(l) skb_queue_len(l)
static inline int skb_queue_empty(const struct sk_buff_head *list) { return list->next == (const struct sk_buff *)list; }
#define skb_queue_empty_lockless(l) skb_queue_empty(l)
static inline struct sk_buff *skb_peek(const struct sk_buff_head *list)
{ struct sk_buff *skb = list->next; return (skb == (const struct sk_buff *)list) ? NULL : skb; }
static inline struct sk_buff *skb_peek_tail(const struct sk_buff_head *list)
{ struct sk_buff *skb = list->prev; return (skb == (const struct sk_buff *)list) ? NULL : skb; }
static inline int skb_queue_is_last(const struct sk_buff_head *list, const struct sk_buff *skb)
{ return skb->next == (const struct sk_buff *)list; }
void __skb_queue_tail(struct sk_buff_head *list, struct sk_buff *newsk);
void __skb_queue_head(struct sk_buff_head *list, struct sk_buff *newsk);
void __skb_unlink(struct sk_buff *skb, struct sk_buff_head *list);
struct sk_buff *__skb_dequeue(struct sk_buff_head *list);
struct sk_buff *__skb_dequeue_tail(struct sk_buff_head *list);
void __skb_queue_purge(struct sk_buff_head *list);
void skb_queue_splice_tail_init(struct sk_buff_head *list, struct sk_buff_head *head);
void skb_queue_splice_init(struct sk_buff_head *list, struct sk_buff_head *head);
#define skb_queue_tail(l, s)    __skb_queue_tail(l, s)
#define skb_queue_head(l, s)    __skb_queue_head(l, s)
#define skb_unlink(s, l)        __skb_unlink(s, l)
#define skb_dequeue(l)          __skb_dequeue(l)
#define skb_dequeue_tail(l)     __skb_dequeue_tail(l)
#define skb_queue_purge(l)      __skb_queue_purge(l)
#define skb_queue_splice_tail(l, h) skb_queue_splice_tail_init(l, h)
#define __skb_queue_splice_tail_init(l, h) skb_queue_splice_tail_init(l, h)
#define skb_queue_walk(queue, skb) \
    for (skb = (queue)->next; skb != (struct sk_buff *)(queue); skb = skb->next)
#define skb_queue_walk_safe(queue, skb, tmp) \
    for (skb = (queue)->next, tmp = skb->next; skb != (struct sk_buff *)(queue); \
         skb = tmp, tmp = skb->next)
#define skb_list_walk_safe(first, skb, next_skb) \
    for ((skb) = (first), (next_skb) = (skb) ? (skb)->next : NULL; (skb); \
         (skb) = (next_skb), (next_skb) = (skb) ? (skb)->next : NULL)
#define skb_mark_not_on_list(skb) ((skb)->next = NULL)
static inline void skb_list_del_init(struct sk_buff *skb)
{ list_del(&skb->list); skb->next = NULL; }

/* =========================================================================
 * linux/etherdevice.h
 * ========================================================================= */
static inline bool is_zero_ether_addr(const u8 *a)
{ return (a[0] | a[1] | a[2] | a[3] | a[4] | a[5]) == 0; }
static inline bool is_multicast_ether_addr(const u8 *a) { return (a[0] & 0x01) != 0; }
#define is_multicast_ether_addr_64bits(a) is_multicast_ether_addr(a)
static inline bool is_local_ether_addr(const u8 *a) { return (a[0] & 0x02) != 0; }
static inline bool is_broadcast_ether_addr(const u8 *a)
{ return (a[0] & a[1] & a[2] & a[3] & a[4] & a[5]) == 0xff; }
static inline bool is_unicast_ether_addr(const u8 *a) { return !is_multicast_ether_addr(a); }
static inline bool is_valid_ether_addr(const u8 *a)
{ return !is_multicast_ether_addr(a) && !is_zero_ether_addr(a); }
static inline bool ether_addr_equal(const u8 *a, const u8 *b) { return memcmp(a, b, 6) == 0; }
#define ether_addr_equal_64bits(a, b)   ether_addr_equal(a, b)
#define ether_addr_equal_unaligned(a, b) ether_addr_equal(a, b)
static inline bool ether_addr_equal_masked(const u8 *a, const u8 *b, const u8 *m)
{ int i; for (i = 0; i < 6; i++) if ((a[i] ^ b[i]) & m[i]) return false; return true; }
static inline void ether_addr_copy(u8 *dst, const u8 *src) { memcpy(dst, src, 6); }
static inline void eth_zero_addr(u8 *addr) { memset(addr, 0, 6); }
static inline void eth_broadcast_addr(u8 *addr) { memset(addr, 0xff, 6); }
static inline void eth_random_addr(u8 *addr)
{ get_random_bytes(addr, 6); addr[0] &= 0xfe; addr[0] |= 0x02; }
static inline u64 ether_addr_to_u64(const u8 *addr)
{ u64 u = 0; int i; for (i = 0; i < 6; i++) u = u << 8 | addr[i]; return u; }
static inline void u64_to_ether_addr(u64 u, u8 *addr)
{ int i; for (i = 5; i >= 0; i--) { addr[i] = (u8)(u & 0xff); u = u >> 8; } }
extern const u8 eth_reserved_addr_base[6];
static inline bool is_link_local_ether_addr(const u8 *addr)
{ return ((addr[0] ^ 0x01) | (addr[1] ^ 0x80) | (addr[2] ^ 0xc2) |
          (addr[3] ^ 0x00) | (addr[4] ^ 0x00) | ((addr[5] ^ 0x00) & 0xf0)) == 0; }
#define eth_hdr(skb)            ((struct ethhdr *)skb_mac_header(skb))
__be16 eth_type_trans(struct sk_buff *skb, struct net_device *dev);

/* =========================================================================
 * Networking: net_device, namespace, ethtool, netlink (linux/netdevice.h, ...)
 * ========================================================================= */
#define IFNAMSIZ                16
#define ETHTOOL_FWVERS_LEN      32
#define ETHTOOL_BUSINFO_LEN     32
#define NETDEV_TX_OK            0
#define NETDEV_TX_BUSY          0x10
typedef int netdev_tx_t;
#define NET_NAME_UNKNOWN        0
#define NET_NAME_ENUM           1
#define NET_NAME_USER           3
#define LL_MAX_HEADER           128
#define IFF_UP                  0x1
#define IFF_ALLMULTI            0x200
#define IFF_PROMISC             0x100
#define IFF_LIVE_ADDR_CHANGE    0x100000
#define IFF_NO_QUEUE            0x200000
#define IFF_DONT_BRIDGE         0x800
#define IFF_TX_SKB_SHARING      0x10000
#define ARPHRD_ETHER            1
#define ARPHRD_IEEE80211_RADIOTAP 803
#define NETIF_F_SG              (1ULL << 0)
#define NETIF_F_IP_CSUM         (1ULL << 1)
#define NETIF_F_HW_CSUM         (1ULL << 3)
#define NETIF_F_IPV6_CSUM       (1ULL << 4)
#define NETIF_F_HIGHDMA         (1ULL << 5)
#define NETIF_F_NETNS_LOCAL     (1ULL << 13)
#define NETIF_F_GSO_SOFTWARE    (0ULL)
#define NETIF_F_RXCSUM          (1ULL << 32)
#define NETDEV_UP               0x0001
#define NETDEV_DOWN             0x0002
#define NETDEV_REGISTER         0x0005
#define NETDEV_UNREGISTER       0x0006
#define NETDEV_GOING_DOWN       0x0009
#define NETDEV_CHANGENAME       0x000A
#define NETDEV_PRE_UP           0x000D
#define NETDEV_POST_INIT        0x0010

struct net { int dummy; };
extern struct net init_net;
typedef struct { struct net *net; } possible_net_t;
#define read_pnet(pnet)         ((pnet)->net)
#define write_pnet(pnet, n)     ((pnet)->net = (n))
#define net_eq(a, b)            ((a) == (b))
#define get_net(n)              (n)
#define put_net(n)              do { } while (0)
#define dev_net(dev)            (&init_net)
#define dev_net_set(dev, n)     do { } while (0)

struct netlink_ext_ack { const char *_msg; };
#define NL_SET_ERR_MSG(e, m)        do { if (e) (e)->_msg = (m); } while (0)
#define NL_SET_ERR_MSG_MOD(e, m)    NL_SET_ERR_MSG(e, m)
#define NL_SET_ERR_MSG_ATTR(e, a, m) NL_SET_ERR_MSG(e, m)
struct nlattr { u16 nla_len; u16 nla_type; };
struct genl_info;
struct netlink_callback;
struct nla_policy;

struct ethtool_ops;
struct ethtool_stats;
struct ethtool_drvinfo;
struct ethtool_ringparam;
struct ethtool_wolinfo;
struct ethtool_regs;
struct ethtool_eeprom;
struct ethtool_cmd;
struct ethtool_link_ksettings;

struct net_device_stats {
    unsigned long rx_packets, tx_packets, rx_bytes, tx_bytes;
    unsigned long rx_errors, tx_errors, rx_dropped, tx_dropped, multicast;
};
struct net_device_ops;
struct netdev_hw_addr {
    struct list_head list;
    unsigned char addr[32];
};
struct netdev_hw_addr_list {
    struct list_head list;
    int count;
};
#define netdev_hw_addr_list_count(l)        ((l)->count)
#define netdev_hw_addr_list_for_each(ha, l) list_for_each_entry(ha, &(l)->list, list)
#define netdev_for_each_mc_addr(ha, dev)    netdev_hw_addr_list_for_each(ha, &(dev)->mc)
#define netdev_mc_count(dev)                netdev_hw_addr_list_count(&(dev)->mc)

typedef u64 netdev_features_t;
#define NETDEV_ALIGN            32
struct napi_struct { int dummy; };
/* Bottom half: there is no preemption, but as in Linux we keep count of
 * local_bh_disable() calls, because mac80211 checks that it is running "in
 * softirq" (softirq_count) when it receives packets from ieee80211_rx_ni() */
extern int b43compat_bh_count;
#define local_bh_disable()      (b43compat_bh_count++)
#define local_bh_enable()       (b43compat_bh_count--)
#define in_interrupt()          (b43compat_bh_count != 0)
#define in_softirq()            (b43compat_bh_count != 0)
struct wireless_dev;
struct netdev_queue { int stopped; };

struct net_device {
    char name[IFNAMSIZ];
    unsigned long state;
    struct list_head dev_list;
    unsigned int flags;
    unsigned int priv_flags;
    const struct net_device_ops *netdev_ops;
    const struct ethtool_ops *ethtool_ops;
    unsigned short type;
    unsigned short hard_header_len;
    unsigned short needed_headroom;
    unsigned short needed_tailroom;
    unsigned int mtu, min_mtu, max_mtu;
    unsigned char perm_addr[32];
    unsigned char addr_len;
    unsigned char *dev_addr;
    unsigned char b43c_dev_addr[32];
    unsigned char broadcast[32];
    struct netdev_hw_addr_list mc;
    struct netdev_hw_addr_list uc;
    int ifindex;
    struct net_device_stats stats;
    struct wireless_dev *ieee80211_ptr;
    struct device dev;
    unsigned int num_tx_queues;
    unsigned int real_num_tx_queues;
    struct netdev_queue b43c_txq[16];
    netdev_features_t features, hw_features;
    struct pcpu_sw_netstats *tstats;
    unsigned long tx_queue_len;
    void (*priv_destructor)(struct net_device *dev);
    bool needs_free_netdev;
    u8 upper_level, lower_level;
    void *ml_priv;
    /* Mac OS 9: private area (netdev_priv) immediately after */
    void *b43c_priv;
};
#define netdev_priv(dev)        ((dev)->b43c_priv)
#define SET_NETDEV_DEV(net, pdev) ((net)->dev.parent = (pdev))
#define netdev_name(dev)        ((dev)->name)
#define netif_running(dev)      (((dev)->flags & IFF_UP) != 0)
#define netif_carrier_ok(dev)   (!test_bit(0, &(dev)->state))
#define netif_carrier_on(dev)   clear_bit(0, &(dev)->state)
#define netif_carrier_off(dev)  set_bit(0, &(dev)->state)
#define netif_device_present(dev) 1
#define netif_tx_start_all_queues(dev)  b43compat_netif_tx_all(dev, 0)
#define netif_tx_stop_all_queues(dev)   b43compat_netif_tx_all(dev, 1)
#define netif_tx_wake_all_queues(dev)   b43compat_netif_tx_all(dev, 0)
#define netif_tx_disable(dev)           b43compat_netif_tx_all(dev, 1)
#define netif_start_queue(dev)          b43compat_netif_tx_all(dev, 0)
#define netif_stop_queue(dev)           b43compat_netif_tx_all(dev, 1)
#define netif_wake_queue(dev)           b43compat_netif_tx_all(dev, 0)
#define netif_queue_stopped(dev)        ((dev)->b43c_txq[0].stopped)
#define netif_stop_subqueue(dev, q)     ((dev)->b43c_txq[q].stopped = 1)
#define netif_wake_subqueue(dev, q)     ((dev)->b43c_txq[q].stopped = 0)
#define netif_start_subqueue(dev, q)    ((dev)->b43c_txq[q].stopped = 0)
#define __netif_subqueue_stopped(dev, q) ((dev)->b43c_txq[q].stopped)
#define netif_tx_lock_bh(dev)           do { } while (0)
#define netif_tx_unlock_bh(dev)         do { } while (0)
#define netif_addr_lock_bh(dev)         do { } while (0)
#define netif_addr_unlock_bh(dev)       do { } while (0)
#define netif_set_real_num_tx_queues(d, n) ((d)->real_num_tx_queues = (n), 0)
#define netdev_get_tx_queue(dev, q)     (&(dev)->b43c_txq[q])
#define netif_tx_queue_stopped(txq)     ((txq)->stopped)
#define netdev_tx_sent_queue(q, b)      do { } while (0)
#define dev_hold(dev)                   do { } while (0)
#define dev_put(dev)                    do { } while (0)
#define dev_sw_netstats_rx_add(d, l)    do { } while (0)
#define dev_sw_netstats_tx_add(d, p, l) do { } while (0)
void b43compat_netif_tx_all(struct net_device *dev, int stop);
int  netif_rx(struct sk_buff *skb);
#define netif_rx_ni(skb)                netif_rx(skb)
#define netif_receive_skb(skb)          netif_rx(skb)
#define napi_gro_receive(n, skb)        netif_rx(skb)
void netif_receive_skb_list(struct list_head *head);
int  dev_queue_xmit(struct sk_buff *skb);
#define rtnl_lock()                     do { } while (0)
#define rtnl_unlock()                   do { } while (0)
#define rtnl_trylock()                  1
#define ASSERT_RTNL()                   do { } while (0)
#define rtnl_dereference(p)             (p)
#define lockdep_rtnl_is_held()          1


/* =========================================================================
 * Errors and limits (uapi/asm-generic/errno.h, linux/limits.h)
 * ========================================================================= */
#define ENXIO           6
#define E2BIG           7
#define ENOEXEC         8
#define ECHILD          10
#define EACCES          13
#define EDOM            33
#define ENODATA         61
#define ETIME           62
#define ENOLINK         67
#define EBADMSG         74
#define EOVERFLOW       75
#define EMSGSIZE        90
#define ENETDOWN        100
#define ENETUNREACH     101
#define ENOBUFS         105
#define EISCONN         106
#define ENOTCONN        107
#define ECONNREFUSED    111
#define EHOSTUNREACH    113
#define EALREADY        114
#define EINPROGRESS     115
#define ECANCELED       125
#define ENOKEY          126
#define EKEYREJECTED    129

#define U8_MAX          ((u8)~0U)
#define S8_MAX          ((s8)(U8_MAX >> 1))
#define S8_MIN          ((s8)(-S8_MAX - 1))
#define U16_MAX         ((u16)~0U)
#define S16_MAX         ((s16)(U16_MAX >> 1))
#define S16_MIN         ((s16)(-S16_MAX - 1))
#define U32_MAX         ((u32)~0UL)
#define S32_MAX         ((s32)(U32_MAX >> 1))
#define S32_MIN         ((s32)(-S32_MAX - 1))
#define U64_MAX         ((u64)~0ULL)
#define S64_MAX         ((s64)(U64_MAX >> 1))
#define INT_MAX         ((int)(~0U >> 1))
#define INT_MIN         (-INT_MAX - 1)
#define UINT_MAX        (~0U)
#define LONG_MAX        ((long)(~0UL >> 1))
#define LONG_MIN        (-LONG_MAX - 1)
#define ULONG_MAX       (~0UL)

/* =========================================================================
 * Miscellanea from linux/kernel.h, linux/err.h, linux/string.h, linux/math64.h
 * ========================================================================= */
#define MAX_ERRNO               4095
#define IS_ERR_VALUE(x)         ((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)
static inline void *ERR_PTR(long error) { return (void *)error; }
static inline long PTR_ERR(const void *ptr) { return (long)ptr; }
static inline bool IS_ERR(const void *ptr) { return IS_ERR_VALUE((unsigned long)ptr); }
static inline bool IS_ERR_OR_NULL(const void *ptr) { return !ptr || IS_ERR_VALUE((unsigned long)ptr); }
static inline void *ERR_CAST(const void *ptr) { return (void *)ptr; }
static inline int PTR_ERR_OR_ZERO(const void *ptr) { return IS_ERR(ptr) ? (int)PTR_ERR(ptr) : 0; }

#define WARN_ONCE(cond, ...)    WARN(cond, __VA_ARGS__)
#define BUILD_BUG_ON_NOT_POWER_OF_2(n) do { } while (0)
#define BUILD_BUG_ON_MSG(c, m)  BUILD_BUG_ON(c)
#define BUILD_BUG()             do { } while (0)
#define typecheck(type, x)      1
#define barrier()               do { } while (0)
#define offsetofend(TYPE, MEMBER) (offsetof(TYPE, MEMBER) + sizeof(((TYPE *)0)->MEMBER))
#define sizeof_field(TYPE, MEMBER) sizeof((((TYPE *)0)->MEMBER))
#define struct_size(p, member, count) (sizeof(*(p)) + sizeof(*(p)->member) * (count))
#define flex_array_size(p, member, count) (sizeof(*(p)->member) * (count))
#define ALIGN(x, a)             (((x) + ((a) - 1)) & ~((a) - 1))
#define IS_ALIGNED(x, a)        (((x) & ((a) - 1)) == 0)
#define PTR_ALIGN(p, a)         ((void *)ALIGN((unsigned long)(p), (a)))
#define round_up(x, y)          ((((x) - 1) | ((y) - 1)) + 1)
#define round_down(x, y)        ((x) & ~((y) - 1))
#define DIV_ROUND_CLOSEST(x, d) (((x) + ((d) / 2)) / (d))
#define DIV_ROUND_UP_ULL(ll, d) ((u64)(((u64)(ll) + (d) - 1) / (d)))
#define swap(a, b)              do { char __b43c_t[sizeof(a)]; memcpy(__b43c_t, &(a), sizeof(a)); \
                                     memcpy(&(a), &(b), sizeof(a)); memcpy(&(b), __b43c_t, sizeof(a)); } while (0)
#define net_ratelimit()         1
#define printk_ratelimit()      1
#define net_info_ratelimited(...)  pr_info(__VA_ARGS__)
#define net_dbg_ratelimited(...)   pr_debug(__VA_ARGS__)
#define net_warn_ratelimited(...)  pr_warn(__VA_ARGS__)
#define pr_err_ratelimited(...)    pr_err(__VA_ARGS__)
#define pr_debug_ratelimited(...)  pr_debug(__VA_ARGS__)
#define pr_warn_once(...)          pr_warn(__VA_ARGS__)
#define pr_err_once(...)           pr_err(__VA_ARGS__)
#define dev_printk(level, dev, ...) b43compat_dev_printk(level, dev, __VA_ARGS__)
#define dev_warn_once(dev, ...)    dev_warn(dev, __VA_ARGS__)
#define dev_err_ratelimited(dev, ...) dev_err(dev, __VA_ARGS__)
#define fips_enabled            0
#define mutex_destroy(m)        do { } while (0)
#define lockdep_assert_not_held(l) do { } while (0)
#define array_index_nospec(index, size) (index)

/* do_div(n, base): n (u64) becomes the quotient, returns the remainder */
static inline u32 b43compat_do_div(u64 *n, u32 base)
{ u32 rem = (u32)(*n % base); *n = *n / base; return rem; }
#define do_div(n, base)         b43compat_do_div(&(n), base)
#define div_u64(a, b)           ((u64)(a) / (u32)(b))
#define div64_u64(a, b)         ((u64)(a) / (u64)(b))
#define div_s64(a, b)           ((s64)(a) / (s32)(b))
#define mult_frac(x, n, d)      (((x) / (d)) * (n) + (((x) % (d)) * (n)) / (d))
static inline u32 rol32(u32 w, unsigned int s) { return (w << (s & 31)) | (w >> ((-s) & 31)); }
static inline u32 ror32(u32 w, unsigned int s) { return (w >> (s & 31)) | (w << ((-s) & 31)); }
static inline u16 ror16(u16 w, unsigned int s) { return (u16)((w >> (s & 15)) | (w << ((-s) & 15))); }
static inline u16 rol16(u16 w, unsigned int s) { return (u16)((w << (s & 15)) | (w >> ((-s) & 15))); }
#define reciprocal_scale(val, ep_ro) ((u32)(((u64)(val) * (ep_ro)) >> 32))
unsigned long gcd(unsigned long a, unsigned long b);
unsigned long int_sqrt(unsigned long x);

/* Byte order (in addition to b43compat.h) */
#define swab64(x) ((u64)swab32((u32)(x)) << 32 | swab32((u32)((u64)(x) >> 32)))
#define cpu_to_le64(x)          swab64(x)
#define le64_to_cpu(x)          swab64(x)
#define cpu_to_be64(x)          ((u64)(x))
#define be64_to_cpu(x)          ((u64)(x))
#define __cpu_to_le16(x)        cpu_to_le16(x)
#define __le16_to_cpu(x)        le16_to_cpu(x)
#define __cpu_to_le32(x)        cpu_to_le32(x)
#define __le32_to_cpu(x)        le32_to_cpu(x)
#define __cpu_to_be16(x)        cpu_to_be16(x)
#define __be16_to_cpu(x)        be16_to_cpu(x)
#define le16_to_cpup(p)         le16_to_cpu(*(p))
#define le32_to_cpup(p)         le32_to_cpu(*(p))
#define be16_to_cpup(p)         be16_to_cpu(*(p))
#define be32_to_cpup(p)         be32_to_cpu(*(p))
#define le16_add_cpu(var, val)  (*(var) = cpu_to_le16(le16_to_cpu(*(var)) + (val)))
#define htons(x)                cpu_to_be16(x)
#define ntohs(x)                be16_to_cpu(x)
#define htonl(x)                cpu_to_be32(x)
#define ntohl(x)                be32_to_cpu(x)
/* get/put_unaligned in CPU byte order (big-endian) without typeof */
#define get_unaligned(ptr) \
    (sizeof(*(ptr)) == 1 ? (u64)*(const u8 *)(ptr) : \
     sizeof(*(ptr)) == 2 ? (u64)get_unaligned_be16(ptr) : \
     sizeof(*(ptr)) == 4 ? (u64)get_unaligned_be32(ptr) : get_unaligned_be64(ptr))
#define put_unaligned(val, ptr) do {                                      \
        switch (sizeof(*(ptr))) {                                         \
        case 1: *(u8 *)(ptr) = (u8)(val); break;                          \
        case 2: put_unaligned_be16((u16)(val), ptr); break;               \
        case 4: put_unaligned_be32((u32)(val), ptr); break;               \
        default: put_unaligned_be32((u32)((u64)(val) >> 32), ptr);        \
                 put_unaligned_be32((u32)(val), (u8 *)(ptr) + 4); break;  \
        }                                                                 \
    } while (0)

/* Strings and memory */
#define strlcpy                 b43c_strlcpy
#define strscpy(d, s, n)        ((int)b43c_strlcpy(d, s, n))
#define strncmp                 b43c_strncmp
#define strchr                  b43c_strchr
#define strnlen                 b43c_strnlen
#define strcpy                  b43c_strcpy
size_t b43c_strlcpy(char *dst, const char *src, size_t size);
int    b43c_strncmp(const char *a, const char *b, size_t n);
char  *b43c_strchr(const char *s, int c);
size_t b43c_strnlen(const char *s, size_t maxlen);
char  *b43c_strcpy(char *d, const char *s);
void  *kmemdup(const void *src, size_t len, gfp_t gfp);
char  *kstrdup(const char *s, gfp_t gfp);
void  *krealloc(const void *p, size_t new_size, gfp_t flags);
#define kmalloc_array(n, size, flags)   kmalloc((n) * (size), flags)
#define kvfree(p)               kfree(p)
#define kvzalloc(s, f)          kzalloc(s, f)
#define vzalloc(s)              kzalloc(s, GFP_KERNEL)
#define vmalloc(s)              kmalloc(s, GFP_KERNEL)
#define kfree_sensitive(p)      kfree(p)
#define memzero_explicit(p, n)  memset(p, 0, n)
#define crypto_memneq(a, b, n)  (memcmp(a, b, n) != 0)
#define GFP_NOWAIT              GFP_ATOMIC
#define __GFP_NOWARN            0
#define __GFP_ZERO              0x100

/* =========================================================================
 * Lists (in addition to b43compat.h), RCU variants included. No typeof.
 * ========================================================================= */
#define __b43c_entry(ptr, pos, member) \
    ((void *)((char *)(ptr) - __b43_member_off(pos, member)))
#define list_first_entry(ptr, type, member)  list_entry((ptr)->next, type, member)
#define list_last_entry(ptr, type, member)   list_entry((ptr)->prev, type, member)
#define list_first_entry_or_null(ptr, type, member) \
    (!list_empty(ptr) ? list_first_entry(ptr, type, member) : NULL)
#define list_next_entry(pos, member)  __b43c_entry((pos)->member.next, pos, member)
#define list_prev_entry(pos, member)  __b43c_entry((pos)->member.prev, pos, member)
#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)
#define list_for_each_safe(pos, n, head) \
    for (pos = (head)->next, n = pos->next; pos != (head); pos = n, n = pos->next)
#define list_for_each_entry_reverse(pos, head, member)                           \
    for (pos = NULL, pos = __b43c_entry((head)->prev, pos, member);              \
         &(pos)->member != (head);                                               \
         pos = __b43c_entry((pos)->member.prev, pos, member))
#define list_for_each_entry_continue(pos, head, member)                          \
    for (pos = __b43c_entry((pos)->member.next, pos, member);                    \
         &(pos)->member != (head);                                               \
         pos = __b43c_entry((pos)->member.next, pos, member))
#define list_for_each_entry_continue_reverse(pos, head, member)                  \
    for (pos = __b43c_entry((pos)->member.prev, pos, member);                    \
         &(pos)->member != (head);                                               \
         pos = __b43c_entry((pos)->member.prev, pos, member))
#define list_for_each_entry_from(pos, head, member)                              \
    for (; &(pos)->member != (head); pos = __b43c_entry((pos)->member.next, pos, member))
#define list_for_each_entry_rcu(pos, head, member, ...) list_for_each_entry(pos, head, member)
#define list_for_each_entry_safe_reverse(pos, n, head, member)                   \
    for (pos = NULL, pos = __b43c_entry((head)->prev, pos, member),              \
         n = __b43c_entry((pos)->member.prev, pos, member);                      \
         &(pos)->member != (head);                                               \
         pos = n, n = __b43c_entry((n)->member.prev, n, member))
static inline void list_del_init(struct list_head *e) { list_del(e); INIT_LIST_HEAD(e); }
static inline void list_splice_tail(const struct list_head *list, struct list_head *head)
{
    if (!list_empty(list)) {
        struct list_head *first = list->next, *last = list->prev, *at = head->prev;
        first->prev = at; at->next = first; last->next = head; head->prev = last;
    }
}
static inline void list_move(struct list_head *e, struct list_head *head)
{ e->next->prev = e->prev; e->prev->next = e->next; list_add(e, head); }
static inline int list_is_last(const struct list_head *l, const struct list_head *head) { return l->next == head; }
static inline int list_is_first(const struct list_head *l, const struct list_head *head) { return l->prev == head; }
static inline int list_is_singular(const struct list_head *h) { return !list_empty(h) && h->next == h->prev; }
static inline void list_replace(struct list_head *old, struct list_head *n)
{ n->next = old->next; n->next->prev = n; n->prev = old->prev; n->prev->next = n; }
static inline void list_splice_tail_init(struct list_head *list, struct list_head *head)
{
    if (!list_empty(list)) {
        struct list_head *first = list->next, *last = list->prev, *at = head->prev;
        first->prev = at; at->next = first; last->next = head; head->prev = last;
        INIT_LIST_HEAD(list);
    }
}
static inline void list_splice_init(struct list_head *list, struct list_head *head)
{
    if (!list_empty(list)) {
        struct list_head *first = list->next, *last = list->prev, *at = head->next;
        first->prev = head; head->next = first; last->next = at; at->prev = last;
        INIT_LIST_HEAD(list);
    }
}
#define list_add_rcu(n, h)      list_add(n, h)
#define list_add_tail_rcu(n, h) list_add_tail(n, h)
#define list_del_rcu(e)         list_del(e)
#define list_replace_rcu(o, n)  list_replace(o, n)
#define list_first_or_null_rcu(ptr, type, member) list_first_entry_or_null(ptr, type, member)
#define list_entry_rcu(ptr, type, member) list_entry(ptr, type, member)

struct hlist_node { struct hlist_node *next, **pprev; };
struct hlist_head { struct hlist_node *first; };
#define INIT_HLIST_HEAD(h)      ((h)->first = NULL)

/* =========================================================================
 * Tasklets (run by b43compat_run_work() like work items)
 * ========================================================================= */
struct tasklet_struct {
    struct work_struct b43c_work;       /* must remain the first field */
    void (*func)(unsigned long data);
    void (*callback)(struct tasklet_struct *t);
    bool use_callback;
    unsigned long data;
    int disabled;
};
void tasklet_init(struct tasklet_struct *t, void (*func)(unsigned long), unsigned long data);
void tasklet_setup(struct tasklet_struct *t, void (*callback)(struct tasklet_struct *));
void tasklet_schedule(struct tasklet_struct *t);
#define tasklet_hi_schedule(t)  tasklet_schedule(t)
void tasklet_kill(struct tasklet_struct *t);
#define tasklet_disable(t)      ((t)->disabled++)
#define tasklet_enable(t)       ((t)->disabled--)
#define from_tasklet(var, callback_tasklet, tasklet_fieldname) \
    __b43c_entry(callback_tasklet, var, tasklet_fieldname)

/* =========================================================================
 * Workqueue (in addition to b43compat.h)
 * ========================================================================= */
struct workqueue_struct { const char *name; };
extern struct workqueue_struct *system_wq, *system_freezable_wq,
                               *system_power_efficient_wq, *system_unbound_wq;
struct workqueue_struct *b43compat_alloc_workqueue(const char *name);
#define alloc_ordered_workqueue(fmt, flags, ...) b43compat_alloc_workqueue(fmt)
#define alloc_workqueue(fmt, flags, max, ...)    b43compat_alloc_workqueue(fmt)
#define create_singlethread_workqueue(name)      b43compat_alloc_workqueue(name)
#define destroy_workqueue(wq)           do { } while (0)
#define flush_workqueue(wq)             b43compat_run_work()
#define drain_workqueue(wq)             b43compat_run_work()
#define queue_work(wq, w)               b43compat_queue_work(w)
#define queue_delayed_work(wq, d, j)    b43compat_queue_delayed_work(d, j)
#define mod_delayed_work(wq, d, j)      (b43compat_cancel_delayed_work(d), b43compat_queue_delayed_work(d, j))
#define schedule_delayed_work(d, j)     b43compat_queue_delayed_work(d, j)
#define flush_delayed_work(d)           do { } while (0)
#define work_pending(w)                 ((w)->pending)
#define delayed_work_pending(d)         ((d)->armed)
#define WQ_MEM_RECLAIM                  0
#define WQ_UNBOUND                      0
#define WQ_FREEZABLE                    0
#define WQ_HIGHPRI                      0

/* linux/wait.h: single context, waiting is done by running the work items */
typedef struct { int dummy; } wait_queue_head_t;
#define init_waitqueue_head(w)          do { } while (0)
#define wake_up(w)                      do { } while (0)
#define wake_up_all(w)                  do { } while (0)
#define wait_event(wq, cond)            do { while (!(cond)) b43compat_run_work(); } while (0)

/* linux/time64.h */
typedef s64 time64_t;
time64_t ktime_get_seconds(void);
#define ktime_get_real_seconds()        ktime_get_seconds()
#define ktime_get_boottime_seconds()    ktime_get_seconds()

/* =========================================================================
 * rhashtable: table backed by simple lists (in station mode there are few
 * elements). Same interface as linux/rhashtable.h.
 * ========================================================================= */
struct rhash_head { struct rhash_head *next; };
struct rhlist_head { struct rhash_head rhead; struct rhlist_head *next; };
struct rhashtable_params {
    u16 nelem_hint;
    u16 key_len;
    u16 key_offset;
    u16 head_offset;
    unsigned int max_size;
    u16 min_size;
    bool automatic_shrinking;
    u32 (*hashfn)(const void *data, u32 len, u32 seed);
    u32 (*obj_hashfn)(const void *data, u32 len, u32 seed);
    int (*obj_cmpfn)(void *arg, const void *obj);
};
struct rhashtable {
    struct rhash_head *b43c_first;      /* list of all objects */
    struct rhashtable_params p;
    atomic_t nelems;
    bool rhlist;
};
struct rhltable { struct rhashtable ht; };
struct rhashtable_iter {
    struct rhashtable *ht;
    struct rhash_head *p;
    struct rhlist_head *list;
    int b43c_started;
};
int  rhashtable_init(struct rhashtable *ht, const struct rhashtable_params *params);
void rhashtable_destroy(struct rhashtable *ht);
void rhashtable_free_and_destroy(struct rhashtable *ht, void (*free_fn)(void *ptr, void *arg), void *arg);
int  rhashtable_insert_fast(struct rhashtable *ht, struct rhash_head *obj, const struct rhashtable_params params);
int  rhashtable_remove_fast(struct rhashtable *ht, struct rhash_head *obj, const struct rhashtable_params params);
void *rhashtable_lookup_fast(struct rhashtable *ht, const void *key, const struct rhashtable_params params);
#define rhashtable_lookup(ht, key, params) rhashtable_lookup_fast(ht, key, params)
int  rhashtable_lookup_insert_fast(struct rhashtable *ht, struct rhash_head *obj, const struct rhashtable_params params);
int  rhltable_init(struct rhltable *hlt, const struct rhashtable_params *params);
#define rhltable_destroy(hlt)   rhashtable_destroy(&(hlt)->ht)
int  rhltable_insert(struct rhltable *hlt, struct rhlist_head *list, const struct rhashtable_params params);
int  rhltable_remove(struct rhltable *hlt, struct rhlist_head *list, const struct rhashtable_params params);
struct rhlist_head *rhltable_lookup(struct rhltable *hlt, const void *key, const struct rhashtable_params params);
void rhashtable_walk_enter(struct rhashtable *ht, struct rhashtable_iter *iter);
void rhashtable_walk_exit(struct rhashtable_iter *iter);
#define rhashtable_walk_start(iter)     do { } while (0)
#define rhashtable_walk_stop(iter)      do { } while (0)
void *rhashtable_walk_next(struct rhashtable_iter *iter);
#define rhl_for_each_entry_rcu(tpos, pos, list, member) \
    for (pos = list; pos && ((tpos = __b43c_entry(pos, tpos, member)), 1); pos = pos->next)

/* =========================================================================
 * IDR (linux/idr.h): few identifiers, plain array
 * ========================================================================= */
#define B43COMPAT_IDR_MAX 64
struct idr { void *ptrs[B43COMPAT_IDR_MAX]; };
#define DEFINE_IDR(name)        struct idr name = { { NULL } }
#define idr_init(idr)           memset(idr, 0, sizeof(*(idr)))
#define idr_destroy(idr)        do { } while (0)
int   idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp);
void *idr_remove(struct idr *idr, unsigned long id);
void *idr_find(const struct idr *idr, unsigned long id);
void *b43compat_idr_next(const struct idr *idr, int *id);
#define idr_for_each_entry(idr, entry, id) \
    for (id = 0; ((entry) = b43compat_idr_next(idr, &(id))) != NULL; id++)

/* =========================================================================
 * Notifier (linux/notifier.h, linux/netdevice.h)
 * ========================================================================= */
struct notifier_block {
    int (*notifier_call)(struct notifier_block *nb, unsigned long action, void *data);
    struct notifier_block *next;
    int priority;
};
#define NOTIFY_DONE             0x0000
#define NOTIFY_OK               0x0001
#define NOTIFY_STOP_MASK        0x8000
#define NOTIFY_BAD              (NOTIFY_STOP_MASK | 0x0002)
struct netdev_notifier_info { struct net_device *dev; struct netlink_ext_ack *extack; };
#define netdev_notifier_info_to_dev(info) (((struct netdev_notifier_info *)(info))->dev)
int  register_netdevice_notifier(struct notifier_block *nb);
int  unregister_netdevice_notifier(struct notifier_block *nb);
int  call_netdevice_notifiers(unsigned long val, struct net_device *dev);
#define register_inetaddr_notifier(nb)      0
#define unregister_inetaddr_notifier(nb)    0
#define register_inet6addr_notifier(nb)     0
#define unregister_inet6addr_notifier(nb)   0
#define register_pernet_device(ops)         0
#define unregister_pernet_device(ops)       do { } while (0)

/* =========================================================================
 * net_device: operations, per-cpu statistics, functions
 * ========================================================================= */
struct rtnl_link_stats64 {
    u64 rx_packets, tx_packets, rx_bytes, tx_bytes;
    u64 rx_errors, tx_errors, rx_dropped, tx_dropped, multicast;
};
struct u64_stats_sync { int dummy; };
#define u64_stats_init(s)               do { } while (0)
#define u64_stats_update_begin(s)       do { } while (0)
#define u64_stats_update_end(s)         do { } while (0)
#define u64_stats_fetch_begin(s)        0
#define u64_stats_fetch_retry(s, st)    0
#define u64_stats_fetch_begin_irq(s)    0
#define u64_stats_fetch_retry_irq(s, st) 0
typedef struct { u64 v; } u64_stats_t;
#define u64_stats_read(p)               ((p)->v)
#define u64_stats_add(p, val)           ((p)->v += (val))
#define u64_stats_inc(p)                ((p)->v++)
struct pcpu_sw_netstats {
    u64 rx_packets, rx_bytes, tx_packets, tx_bytes;
    struct u64_stats_sync syncp;
};
/* per-cpu: a single CPU */
#define NR_CPUS                         1
#define for_each_possible_cpu(cpu)      for ((cpu) = 0; (cpu) < 1; (cpu)++)
#define per_cpu_ptr(ptr, cpu)           (ptr)
#define this_cpu_ptr(ptr)               (ptr)
#define get_cpu_ptr(ptr)                (ptr)
#define put_cpu_ptr(ptr)                do { } while (0)
#define alloc_percpu(type)              ((type *)kzalloc(sizeof(type), GFP_KERNEL))
#define alloc_percpu_gfp(type, gfp)     ((type *)kzalloc(sizeof(type), gfp))
#define netdev_alloc_pcpu_stats(type)   alloc_percpu(type)
#define free_percpu(p)                  kfree(p)
#define smp_processor_id()              0
#define raw_smp_processor_id()          0
#define softirq_count()                 b43compat_bh_count

struct net_device_ops {
    int  (*ndo_init)(struct net_device *dev);
    void (*ndo_uninit)(struct net_device *dev);
    int  (*ndo_open)(struct net_device *dev);
    int  (*ndo_stop)(struct net_device *dev);
    netdev_tx_t (*ndo_start_xmit)(struct sk_buff *skb, struct net_device *dev);
    u16  (*ndo_select_queue)(struct net_device *dev, struct sk_buff *skb, struct net_device *sb_dev);
    void (*ndo_set_rx_mode)(struct net_device *dev);
    int  (*ndo_set_mac_address)(struct net_device *dev, void *addr);
    void (*ndo_get_stats64)(struct net_device *dev, struct rtnl_link_stats64 *storage);
};
struct sockaddr { unsigned short sa_family; char sa_data[14]; };

struct net_device *alloc_netdev_mqs(int sizeof_priv, const char *name, unsigned char name_assign_type,
                                    void (*setup)(struct net_device *), unsigned int txqs, unsigned int rxqs);
#define alloc_netdev(sizeof_priv, name, type, setup) alloc_netdev_mqs(sizeof_priv, name, type, setup, 1, 1)
void free_netdev(struct net_device *dev);
int  register_netdevice(struct net_device *dev);
#define register_netdev(dev)            register_netdevice(dev)
void unregister_netdevice(struct net_device *dev);
#define unregister_netdev(dev)          unregister_netdevice(dev)
#define unregister_netdevice_queue(dev, head) unregister_netdevice(dev)
#define unregister_netdevice_many(head) do { } while (0)
int  dev_open(struct net_device *dev, struct netlink_ext_ack *extack);
void dev_close(struct net_device *dev);
int  dev_alloc_name(struct net_device *dev, const char *name);
int  dev_change_name(struct net_device *dev, const char *newname);
void ether_setup(struct net_device *dev);
#define eth_mac_addr(dev, p)            b43compat_eth_mac_addr(dev, p)
int  b43compat_eth_mac_addr(struct net_device *dev, void *p);
#define eth_hw_addr_set(dev, addr)      memcpy((dev)->dev_addr, addr, ETH_ALEN)
#define netdev_set_default_ethtool_ops(d, o) ((d)->ethtool_ops = (o))
#define dev_fetch_sw_netstats(s, n)     do { } while (0)
#define netif_tx_wake_queue(txq)        ((txq)->stopped = 0)
#define netif_tx_stop_queue(txq)        ((txq)->stopped = 1)
#define dev_mc_sync(to, from)           0
#define dev_uc_sync(to, from)           0
#define dev_mc_unsync(to, from)         do { } while (0)
#define dev_uc_unsync(to, from)         do { } while (0)
#define __hw_addr_init(l)               (INIT_LIST_HEAD(&(l)->list), (l)->count = 0)
#define __hw_addr_sync(to, from, len)   0
#define __hw_addr_unsync(to, from, len) do { } while (0)
#define netdev_info(dev, ...)           pr_info(__VA_ARGS__)
#define netdev_err(dev, ...)            pr_err(__VA_ARGS__)
#define netdev_warn(dev, ...)           pr_warn(__VA_ARGS__)
#define netdev_dbg(dev, ...)            pr_debug(__VA_ARGS__)
#define netdev_notice(dev, ...)         pr_notice(__VA_ARGS__)

/* =========================================================================
 * Crypto API (linux/crypto.h, crypto/aead.h, crypto/hash.h). The algorithms
 * required by mac80211 are in b43compat_crypto.c.
 * ========================================================================= */
#define CRYPTO_ALG_ASYNC        0x00000080
struct crypto_tfm;
#define CRYPTO_TFM_REQ_MAY_SLEEP 0x00000200
struct scatterlist { void *b43c_buf; unsigned int length; unsigned int offset; };
void sg_init_table(struct scatterlist *sgl, unsigned int nents);
void sg_init_one(struct scatterlist *sg, const void *buf, unsigned int buflen);
void sg_set_buf(struct scatterlist *sg, const void *buf, unsigned int buflen);
struct crypto_aead;
struct aead_request {
    struct crypto_aead *tfm;
    unsigned int assoclen;
    unsigned int cryptlen;
    u8 *iv;
    struct scatterlist *src;
    struct scatterlist *dst;
};
struct crypto_aead *crypto_alloc_aead(const char *alg_name, u32 type, u32 mask);
void crypto_free_aead(struct crypto_aead *tfm);
int  crypto_aead_setkey(struct crypto_aead *tfm, const u8 *key, unsigned int keylen);
int  crypto_aead_setauthsize(struct crypto_aead *tfm, unsigned int authsize);
unsigned int crypto_aead_reqsize(struct crypto_aead *tfm);
unsigned int crypto_aead_authsize(struct crypto_aead *tfm);
int  crypto_aead_encrypt(struct aead_request *req);
int  crypto_aead_decrypt(struct aead_request *req);
static inline void aead_request_set_tfm(struct aead_request *req, struct crypto_aead *tfm) { req->tfm = tfm; }
static inline void aead_request_set_crypt(struct aead_request *req, struct scatterlist *src,
                                          struct scatterlist *dst, unsigned int cryptlen, u8 *iv)
{ req->src = src; req->dst = dst; req->cryptlen = cryptlen; req->iv = iv; }
static inline void aead_request_set_ad(struct aead_request *req, unsigned int assoclen) { req->assoclen = assoclen; }

struct crypto_shash;
struct shash_desc { struct crypto_shash *tfm; u8 b43c_ctx[64]; };
#define SHASH_DESC_ON_STACK(shash, ctx) struct shash_desc __b43c_##shash; \
    struct shash_desc *shash = &__b43c_##shash
struct crypto_shash *crypto_alloc_shash(const char *alg_name, u32 type, u32 mask);
void crypto_free_shash(struct crypto_shash *tfm);
int  crypto_shash_setkey(struct crypto_shash *tfm, const u8 *key, unsigned int keylen);
int  crypto_shash_init(struct shash_desc *desc);
int  crypto_shash_update(struct shash_desc *desc, const u8 *data, unsigned int len);
int  crypto_shash_final(struct shash_desc *desc, u8 *out);
int  crypto_shash_finup(struct shash_desc *desc, const u8 *data, unsigned int len, u8 *out);
int  crypto_shash_digest(struct shash_desc *desc, const u8 *data, unsigned int len, u8 *out);

struct crypto_cipher;
struct crypto_cipher *crypto_alloc_cipher(const char *alg_name, u32 type, u32 mask);
void crypto_free_cipher(struct crypto_cipher *tfm);
int  crypto_cipher_setkey(struct crypto_cipher *tfm, const u8 *key, unsigned int keylen);
void crypto_cipher_encrypt_one(struct crypto_cipher *tfm, u8 *dst, const u8 *src);

u32  crc32_be(u32 crc, const unsigned char *p, size_t len);

/* =========================================================================
 * Modules, parameters, kobject (cfg80211)
 * ========================================================================= */
/* Module parameters: like "modprobe b43 pio=1", the Mac OS 9 glue layer sets
 * them before startup through b43compat_param_<name> (pointer) */
#define module_param(name, type, perm) void *const b43compat_param_##name = (void *)&(name)
#define module_param_named(name, value, type, perm) void *const b43compat_param_##name = (void *)&(value)
#define module_param_string(name, string, len, perm) void *const b43compat_param_##name = (void *)(string)
#define MODULE_PARM_DESC(var, desc) extern int b43compat_decl_dummy
#define module_put(m)                   do { } while (0)
#define try_module_get(m)               1
#define postcore_initcall(fn)           b43compat_initcall(fn)
#define late_initcall(fn)               b43compat_initcall(fn)
struct kobject { const char *name; };
#define kobject_uevent_env(k, a, e)     0
#define kobject_uevent(k, a)            0


/* =========================================================================
 * mac80211 Kconfig (like a typical distribution configuration)
 * ========================================================================= */
#define CONFIG_MAC80211_RC_MINSTREL         1
#define CONFIG_MAC80211_RC_DEFAULT_MINSTREL 1
#define CONFIG_MAC80211_RC_DEFAULT          "minstrel_ht"
#define CONFIG_MAC80211_STA_HASH_MAX_SIZE   0
#define CONFIG_CFG80211_DEFAULT_PS          1

/* =========================================================================
 * Additional miscellanea
 * ========================================================================= */
#define EILSEQ          84
#define ERFKILL         132
#define ENOTUNIQ        76
#define EADDRNOTAVAIL   99
#define USHRT_MAX       ((unsigned short)~0U)
#define TIMER_DEFERRABLE 0x00080000
#define NLMSG_DEFAULT_SIZE 3776
#define round_jiffies_up(j)     (j)
#define round_jiffies_up_relative(j) (j)
#define timer_shutdown_sync(t)  del_timer(t)
#define spin_lock_irq(l)        do { } while (0)
#define spin_unlock_irq(l)      do { } while (0)
#define atomic_cmpxchg(v, old, n) b43compat_atomic_cmpxchg(&(v)->counter, old, n)
static inline int b43compat_atomic_cmpxchg(int *p, int old, int n)
{ int o = *p; if (o == old) *p = n; return o; }
#define FIELD_MAX(_mask)        ((_mask) >> __bf_shf(_mask))
int  fls64(u64 x);
unsigned long __ffs64(u64 word);
#define kvcalloc(n, size, flags)        kcalloc(n, size, flags)
#define prandom_bytes(buf, n)           get_random_bytes(buf, n)
#define pr_warn_ratelimited(...)        pr_warn(__VA_ARGS__)
#define put_unaligned_be64(v, p)        put_unaligned(v, (u64 *)(p))
#define notifier_from_errno(err)        ((err) ? (NOTIFY_STOP_MASK | (NOTIFY_OK - (err))) : NOTIFY_OK)
#define isascii(c)              (((unsigned char)(c)) <= 0x7f)
#define isalpha(c)              ((((c) | 0x20) >= 'a') && (((c) | 0x20) <= 'z'))
#define isupper(c)              ((c) >= 'A' && (c) <= 'Z')
#define islower(c)              ((c) >= 'a' && (c) <= 'z')
#define toupper(c)              (islower(c) ? (c) - 'a' + 'A' : (c))
#define tolower(c)              (isupper(c) ? (c) - 'A' + 'a' : (c))
int  sscanf(const char *buf, const char *fmt, ...);
#define kernel_param_lock(mod)          do { } while (0)
#define kernel_param_unlock(mod)        do { } while (0)
#define rcu_dereference_rtnl(p)         (p)
/* net/iw_handler.h without CONFIG_WEXT_CORE */
static inline void wireless_nlevent_flush(void) { }
/* linux/cleanup.h: DEFINE_FREE without __attribute__((cleanup)) */
#define DEFINE_FREE(name, type, free)
#define __free(name)
#define crypto_xor(dst, src, n)         b43compat_crypto_xor(dst, src, n)
void b43compat_crypto_xor(u8 *dst, const u8 *src, unsigned int len);

/* static key (linux/jump_label.h): plain variables */
struct static_key_false { int enabled; };
struct static_key_true { int enabled; };
#define DEFINE_STATIC_KEY_FALSE(name)   struct static_key_false name = { 0 }
#define DEFINE_STATIC_KEY_TRUE(name)    struct static_key_true name = { 1 }
#define DECLARE_STATIC_KEY_FALSE(name)  extern struct static_key_false name
#define static_branch_unlikely(k)       ((k)->enabled)
#define static_branch_likely(k)         ((k)->enabled)
#define static_branch_enable(k)         ((k)->enabled = 1)
#define static_branch_disable(k)        ((k)->enabled = 0)

/* Static workqueues/work items */
#define DECLARE_WORK(n, f)              struct work_struct n = { f, 0, NULL }
#define DECLARE_DELAYED_WORK(n, f)      struct delayed_work n = { { f, 0, NULL }, 0, 0 }

/* sk_buff: fragments (never used: buffers are linear) */
#define MAX_SKB_FRAGS           1
#define skb_frag_size(f)        ((f)->bv_len)
#define skb_frag_page(f)        ((f)->bv_page)
#define skb_frag_address(f)     ((void *)NULL)
#define page_address(p)         ((void *)NULL)
#define virt_to_head_page(p)    ((struct page *)NULL)
#define get_page(p)             do { } while (0)
void skb_add_rx_frag(struct sk_buff *skb, int i, struct page *page, int off, int size, unsigned int truesize);
static inline struct sk_buff *__skb_peek(const struct sk_buff_head *list) { return list->next; }
static inline struct sk_buff *skb_queue_next(const struct sk_buff_head *list, const struct sk_buff *skb)
{ (void)list; return skb->next; }
#define __pskb_trim(skb, len)   pskb_trim(skb, len)
#define __pskb_pull(skb, len)   skb_pull(skb, len)
#define skb_set_tail_pointer(skb, off) ((skb)->tail = (skb)->data + (off))
#define skb_copy_queue_mapping(to, from) ((to)->queue_mapping = (from)->queue_mapping)
#define skb_is_gso(skb)         0
#define skb_gso_segment(skb, f) ((struct sk_buff *)ERR_PTR(-EINVAL))
#define skb_checksum_help(skb)  0
#define skb_checksum_start_offset(skb) 0
#define skb_clone_writable(skb, len) 1
#define skb_clone_sk(skb)       skb_copy(skb, GFP_ATOMIC)
void skb_complete_wifi_ack(struct sk_buff *skb, bool acked);
#define sk_pacing_shift_update(sk, v) do { } while (0)
#define qdisc_all_tx_empty(dev) true
#define INET_ECN_set_ce(skb)    0
#define netif_is_bridge_port(dev) false
#define SET_NETDEV_DEVTYPE(net, devtype) do { } while (0)
#define dev_change_net_namespace(dev, net, pat) 0

/* linux/if_vlan.h, linux/ip.h, linux/ipv6.h, linux/mpls.h, net/dsfield.h:
 * only what cfg80211_classify8021d() needs */
#define VLAN_HLEN               4
#define VLAN_ETH_HLEN           18
#define VLAN_PRIO_MASK          0xe000
#define VLAN_PRIO_SHIFT         13
struct vlan_ethhdr {
    unsigned char h_dest[6];
    unsigned char h_source[6];
    __be16 h_vlan_proto;
    __be16 h_vlan_TCI;
    __be16 h_vlan_encapsulated_proto;
};
#define skb_vlan_tag_present(skb)   0
#define skb_vlan_tag_get(skb)       0
struct iphdr { u8 ihl_version; u8 tos; __be16 tot_len; __be16 id; __be16 frag_off;
               u8 ttl; u8 protocol; u16 check; __be32 saddr; __be32 daddr; };
struct ipv6hdr { u8 priority_version; u8 flow_lbl[3]; __be16 payload_len; u8 nexthdr; u8 hop_limit;
                 u8 saddr[16]; u8 daddr[16]; };
static inline u8 ipv4_get_dsfield(const struct iphdr *iph) { return iph->tos; }
static inline u8 ipv6_get_dsfield(const struct ipv6hdr *ipv6h)
{ return (u8)((((const u8 *)ipv6h)[0] << 4) | (((const u8 *)ipv6h)[1] >> 4)); }
struct mpls_label { __be32 entry; };
#define MPLS_LS_TC_MASK         0x00000E00
#define MPLS_LS_TC_SHIFT        9

/* linux/ethtool.h */
#define ETH_GSTRING_LEN         32
#define ETH_SS_STATS            1
struct ethtool_drvinfo {
    u32 cmd;
    char driver[32];
    char version[32];
    char fw_version[ETHTOOL_FWVERS_LEN];
    char bus_info[ETHTOOL_BUSINFO_LEN];
    char erom_version[32];
    char reserved2[12];
    u32 n_priv_flags, n_stats, testinfo_len, eedump_len, regdump_len;
};
struct ethtool_regs { u32 cmd; u32 version; u32 len; u8 data[1]; };
struct ethtool_stats { u32 cmd; u32 n_stats; u64 data[1]; };
struct ethtool_ringparam {
    u32 cmd, rx_max_pending, rx_mini_max_pending, rx_jumbo_max_pending, tx_max_pending;
    u32 rx_pending, rx_mini_pending, rx_jumbo_pending, tx_pending;
};
struct ethtool_ops {
    void (*get_drvinfo)(struct net_device *, struct ethtool_drvinfo *);
    int  (*get_regs_len)(struct net_device *);
    void (*get_regs)(struct net_device *, struct ethtool_regs *, void *);
    u32  (*get_link)(struct net_device *);
    void (*get_ringparam)(struct net_device *, struct ethtool_ringparam *);
    int  (*set_ringparam)(struct net_device *, struct ethtool_ringparam *);
    void (*get_strings)(struct net_device *, u32 stringset, u8 *);
    void (*get_ethtool_stats)(struct net_device *, struct ethtool_stats *, u64 *);
    int  (*get_sset_count)(struct net_device *, int);
};
u32 ethtool_op_get_link(struct net_device *dev);

/* linux/debugfs.h without CONFIG_DEBUG_FS */
struct dentry { struct dentry *d_parent; };
#define debugfs_create_dir(name, parent)        ((struct dentry *)ERR_PTR(-ENODEV))
#define debugfs_remove(d)                       do { } while (0)
#define debugfs_remove_recursive(d)             do { } while (0)
#define debugfs_rename(od, o, nd, n)            ((struct dentry *)ERR_PTR(-ENODEV))

/* linux/device.h: classes and types (cfg80211 registers the ieee80211 class) */
struct device_type {
    const char *name;
    const struct attribute_group **groups;
    void (*release)(struct device *dev);
};
struct class {
    const char *name;
    struct module *owner;
    void (*dev_release)(struct device *dev);
    const struct attribute_group **dev_groups;
    int (*suspend)(struct device *dev, pm_message_t state);
    int (*resume)(struct device *dev);
    const void *(*namespace)(struct device *dev);
    const struct dev_pm_ops *pm;
};
int  class_register(struct class *cls);
void class_unregister(struct class *cls);
void device_initialize(struct device *dev);
int  device_add(struct device *dev);
void device_del(struct device *dev);
#define device_rename(dev, name)                dev_set_name(dev, "%s", name)
#define dev_set_uevent_suppress(dev, v)         do { } while (0)
#define device_enable_async_suspend(dev)        do { } while (0)
#define sysfs_create_link(kobj, target, name)   0
#define sysfs_remove_link(kobj, name)           do { } while (0)
#define platform_device_register_simple(name, id, res, num) \
    b43compat_platform_device_register_simple(name, id)
struct platform_device *b43compat_platform_device_register_simple(const char *name, int id);

/* linux/utsname.h */
struct new_utsname { char sysname[65]; char release[65]; char version[65]; char machine[65]; };
struct new_utsname *init_utsname(void);

/* net/net_namespace.h */
struct pernet_operations {
    void (*exit)(struct net *net);
    void (*pre_exit)(struct net *net);
    int  (*init)(struct net *net);
};
#define __net_exit
#define __net_init
#define __net_initdata

/* skcipher (crypto/skcipher.h): FILS uses ctr(aes) */
struct crypto_skcipher;
struct skcipher_request { struct crypto_skcipher *tfm; struct scatterlist *src, *dst; unsigned int cryptlen; u8 *iv; };
struct crypto_skcipher *crypto_alloc_skcipher(const char *alg_name, u32 type, u32 mask);
void crypto_free_skcipher(struct crypto_skcipher *tfm);
int  crypto_skcipher_setkey(struct crypto_skcipher *tfm, const u8 *key, unsigned int keylen);
struct skcipher_request *skcipher_request_alloc(struct crypto_skcipher *tfm, gfp_t gfp);
#define skcipher_request_free(req)              kfree(req)
static inline void skcipher_request_set_crypt(struct skcipher_request *req, struct scatterlist *src,
                                              struct scatterlist *dst, unsigned int cryptlen, void *iv)
{ req->src = src; req->dst = dst; req->cryptlen = cryptlen; req->iv = (u8 *)iv; }
int  crypto_skcipher_encrypt(struct skcipher_request *req);
int  crypto_skcipher_decrypt(struct skcipher_request *req);

/* idr_for_each */
int idr_for_each(const struct idr *idr, int (*fn)(int id, void *p, void *data), void *data);


/* =========================================================================
 * For b43 (main.c, dma.c, sysfs.c)
 * ========================================================================= */
#define CONFIG_FW_LOADER        1       /* request_firmware(): b43compat_fw.c */
struct va_format { const char *fmt; va_list *va; };
#define DUMP_PREFIX_NONE        0
#define DUMP_PREFIX_ADDRESS     1
#define DUMP_PREFIX_OFFSET      2
void print_hex_dump(const char *level, const char *prefix_str, int prefix_type,
                    int rowsize, int groupsize, const void *buf, size_t len, bool ascii);
#define mb()                    do { } while (0)
#define upper_32_bits(n)        ((u32)(((u64)(n)) >> 32))
#define lower_32_bits(n)        ((u32)(n))
long simple_strtol(const char *cp, char **endp, unsigned int base);
#define wait_for_completion(c)  do { while (!(c)->done) b43compat_run_work(); } while (0)
#define reinit_completion(c)    ((c)->done = 0)
#define complete_all(c)         ((c)->done = 1)
#define CAP_NET_ADMIN           12
#define capable(cap)            1
#define DEVICE_ATTR(_name, _mode, _show, _store)     struct device_attribute dev_attr_##_name = __ATTR(_name, _mode, _show, _store)

/* linux/bcma/bcma.h: only the chip IDs referenced by b43 */
#define BCMA_CHIP_ID_BCM4313    0x4313
#define BCMA_CHIP_ID_BCM43142   43142
#define BCMA_CHIP_ID_BCM43131   43131
#define BCMA_CHIP_ID_BCM43217   43217
#define BCMA_CHIP_ID_BCM43222   43222
#define BCMA_CHIP_ID_BCM43224   43224
#define BCMA_CHIP_ID_BCM43225   43225
#define BCMA_CHIP_ID_BCM43227   43227
#define BCMA_CHIP_ID_BCM43228   43228
#define BCMA_CHIP_ID_BCM4331    0x4331

/* linux/dma-mapping.h. b43 uses PIO (parameter pio=1): these functions
 * exist for linking; dma_set_mask fails and b43 does not use DMA. */
enum dma_data_direction { DMA_BIDIRECTIONAL = 0, DMA_TO_DEVICE = 1, DMA_FROM_DEVICE = 2, DMA_NONE = 3 };
#define GFP_DMA                 0x01
int   dma_set_mask_and_coherent(struct device *dev, u64 mask);
void *dma_alloc_coherent(struct device *dev, size_t size, dma_addr_t *dma_handle, gfp_t flag);
void  dma_free_coherent(struct device *dev, size_t size, void *cpu_addr, dma_addr_t dma_handle);
dma_addr_t dma_map_single(struct device *dev, void *ptr, size_t size, enum dma_data_direction dir);
#define dma_unmap_single(d, a, s, dir)              do { } while (0)
#define dma_sync_single_for_cpu(d, a, s, dir)       do { } while (0)
#define dma_sync_single_for_device(d, a, s, dir)    do { } while (0)
#define dma_mapping_error(d, a)                     ((a) == 0)

#endif /* B43COMPAT_NET_H_ */
