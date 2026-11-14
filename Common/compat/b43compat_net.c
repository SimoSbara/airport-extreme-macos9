/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * b43compat_net.c - Kernel infrastructure for mac80211/cfg80211 and b43:
 * sk_buff, net_device, notifiers, timers, tasklets, rhashtable, idr, bitops,
 * strings, random numbers, device/class, DMA (absent).
 *
 * Same behavior as the Linux functions, in a single execution context.
 * Received packets (netif_rx) and registered network devices are passed
 * to the Mac OS 9 glue layer through b43compat_net_hooks.
 */
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include "b43compat_os9.h"

struct net init_net;
struct skb_shared_info b43compat_skb_shinfo;
const u8 eth_reserved_addr_base[6] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x00 };
b43compat_net_hooks_t b43compat_net_hooks;
int b43compat_bh_count;

/* =========================================================================
 * sk_buff (net/core/skbuff.c): linear buffers, data right after the head
 * ========================================================================= */
struct sk_buff *alloc_skb(unsigned int size, gfp_t priority)
{
    struct sk_buff *skb;
    u8 *data;

    size = SKB_DATA_ALIGN(size);
    skb = kzalloc(sizeof(*skb), priority);
    if (!skb)
        return NULL;
    data = kmalloc(size, priority);
    if (!data) {
        kfree(skb);
        return NULL;
    }
    skb->head = data;
    skb->data = data;
    skb->tail = data;
    skb->end = data + size;
    skb->truesize = size + sizeof(*skb);
    skb->mac_header = 0xffff;
    skb->users.refs = 1;
    return skb;
}

struct sk_buff *__dev_alloc_skb(unsigned int length, gfp_t gfp_mask)
{
    struct sk_buff *skb = alloc_skb(length + NET_SKB_PAD, gfp_mask);
    if (skb)
        skb_reserve(skb, NET_SKB_PAD);
    return skb;
}

void kfree_skb(struct sk_buff *skb)
{
    if (!skb)
        return;
    if (--skb->users.refs > 0)
        return;
    if (skb->destructor)
        skb->destructor(skb);
    kfree(skb->head);
    kfree(skb);
}

void b43compat_kfree_skb_list(struct sk_buff *segs)
{
    struct sk_buff *next;
    while (segs) {
        next = segs->next;
        kfree_skb(segs);
        segs = next;
    }
}

struct sk_buff *skb_get(struct sk_buff *skb)
{
    skb->users.refs++;
    return skb;
}

static void copy_skb_header(struct sk_buff *n, const struct sk_buff *old)
{
    n->dev = old->dev;
    n->tstamp = old->tstamp;
    memcpy(n->cb, old->cb, sizeof(old->cb));
    n->priority = old->priority;
    n->protocol = old->protocol;
    n->queue_mapping = old->queue_mapping;
    n->pkt_type = old->pkt_type;
    n->ip_summed = old->ip_summed;
    n->mark = old->mark;
    n->hash = old->hash;
    if (old->mac_header != 0xffff)
        n->mac_header = (u16)(old->mac_header - (old->data - old->head) + (n->data - n->head));
    n->network_header = (u16)(old->network_header - (old->data - old->head) + (n->data - n->head));
    n->transport_header = (u16)(old->transport_header - (old->data - old->head) + (n->data - n->head));
}

struct sk_buff *skb_copy_expand(const struct sk_buff *skb, int newheadroom, int newtailroom, gfp_t gfp)
{
    struct sk_buff *n = alloc_skb(newheadroom + skb->len + newtailroom, gfp);
    if (!n)
        return NULL;
    skb_reserve(n, newheadroom);
    skb_put(n, skb->len);
    memcpy(n->data, skb->data, skb->len);
    copy_skb_header(n, skb);
    return n;
}

struct sk_buff *skb_copy(const struct sk_buff *skb, gfp_t gfp)
{
    return skb_copy_expand(skb, (int)skb_headroom(skb), skb_tailroom(skb), gfp);
}

int pskb_expand_head(struct sk_buff *skb, int nhead, int ntail, gfp_t gfp)
{
    unsigned int headroom = skb_headroom(skb);
    unsigned int size = (unsigned int)(skb->end - skb->head) + nhead + ntail;
    u8 *data = kmalloc(SKB_DATA_ALIGN(size), gfp);
    long off;

    if (!data)
        return -ENOMEM;
    memcpy(data + nhead, skb->head, (size_t)(skb->tail - skb->head));
    off = (data + nhead) - skb->head;
    kfree(skb->head);
    skb->head = data;
    skb->data = data + nhead + headroom;
    skb->tail = skb->data + skb->len;
    skb->end = data + size;
    (void)off;                  /* headers are offsets from head: +nhead */
    if (skb->mac_header != 0xffff)
        skb->mac_header = (u16)(skb->mac_header + nhead);
    skb->network_header = (u16)(skb->network_header + nhead);
    skb->transport_header = (u16)(skb->transport_header + nhead);
    return 0;
}

int skb_cow_head(struct sk_buff *skb, unsigned int headroom)
{
    if (headroom > skb_headroom(skb))
        return pskb_expand_head(skb, (int)(headroom - skb_headroom(skb)), 0, GFP_ATOMIC);
    return 0;
}

void *skb_put(struct sk_buff *skb, unsigned int len)
{
    u8 *tmp = skb->tail;
    skb->tail += len;
    skb->len += len;
    if (skb->tail > skb->end)
        printk("skb_put: past the end of the buffer (%u)\n", len);
    return tmp;
}

void *skb_push(struct sk_buff *skb, unsigned int len)
{
    skb->data -= len;
    skb->len += len;
    if (skb->data < skb->head)
        printk("skb_push: before the start of the buffer (%u)\n", len);
    return skb->data;
}

void *skb_pull(struct sk_buff *skb, unsigned int len)
{
    if (len > skb->len)
        return NULL;
    skb->len -= len;
    skb->data += len;
    return skb->data;
}

void skb_trim(struct sk_buff *skb, unsigned int len)
{
    if (skb->len > len) {
        skb->len = len;
        skb->tail = skb->data + len;
    }
}

int skb_copy_bits(const struct sk_buff *skb, int offset, void *to, int len)
{
    if (offset < 0 || len < 0 || (unsigned int)(offset + len) > skb->len)
        return -EFAULT;
    memcpy(to, skb->data + offset, (size_t)len);
    return 0;
}

void *skb_header_pointer(const struct sk_buff *skb, int offset, int len, void *buffer)
{
    (void)buffer;
    if (offset < 0 || len < 0 || (unsigned int)(offset + len) > skb->len)
        return NULL;
    return skb->data + offset;
}

void skb_add_rx_frag(struct sk_buff *skb, int i, struct page *page, int off, int size, unsigned int truesize)
{
    (void)skb; (void)i; (void)page; (void)off; (void)size; (void)truesize;
    WARN_ON(1);                 /* never used: b43 delivers linear buffers */
}

/* Transmit status for the socket (SO_WIFI_STATUS): there is no socket */
void skb_complete_wifi_ack(struct sk_buff *skb, bool acked)
{
    (void)acked;
    kfree_skb(skb);
}

void __skb_queue_tail(struct sk_buff_head *list, struct sk_buff *newsk)
{
    struct sk_buff *prev = list->prev;
    newsk->next = (struct sk_buff *)list;
    newsk->prev = prev;
    prev->next = newsk;
    list->prev = newsk;
    list->qlen++;
}

void __skb_queue_head(struct sk_buff_head *list, struct sk_buff *newsk)
{
    struct sk_buff *next = list->next;
    newsk->prev = (struct sk_buff *)list;
    newsk->next = next;
    next->prev = newsk;
    list->next = newsk;
    list->qlen++;
}

void __skb_unlink(struct sk_buff *skb, struct sk_buff_head *list)
{
    struct sk_buff *next = skb->next, *prev = skb->prev;
    list->qlen--;
    skb->next = skb->prev = NULL;
    next->prev = prev;
    prev->next = next;
}

struct sk_buff *__skb_dequeue(struct sk_buff_head *list)
{
    struct sk_buff *skb = skb_peek(list);
    if (skb)
        __skb_unlink(skb, list);
    return skb;
}

struct sk_buff *__skb_dequeue_tail(struct sk_buff_head *list)
{
    struct sk_buff *skb = skb_peek_tail(list);
    if (skb)
        __skb_unlink(skb, list);
    return skb;
}

void __skb_queue_purge(struct sk_buff_head *list)
{
    struct sk_buff *skb;
    while ((skb = __skb_dequeue(list)) != NULL)
        kfree_skb(skb);
}

void skb_queue_splice_tail_init(struct sk_buff_head *list, struct sk_buff_head *head)
{
    struct sk_buff *skb;
    while ((skb = __skb_dequeue(list)) != NULL)
        __skb_queue_tail(head, skb);
}

void skb_queue_splice_init(struct sk_buff_head *list, struct sk_buff_head *head)
{
    struct sk_buff *skb;
    while ((skb = __skb_dequeue_tail(list)) != NULL)
        __skb_queue_head(head, skb);
}

/* =========================================================================
 * net_device (net/core/dev.c, net/ethernet/eth.c)
 * ========================================================================= */
static struct notifier_block *g_netdev_notifiers;
static int g_ifindex;

struct net_device *alloc_netdev_mqs(int sizeof_priv, const char *name, unsigned char name_assign_type,
                                    void (*setup)(struct net_device *), unsigned int txqs, unsigned int rxqs)
{
    struct net_device *dev;
    size_t alloc = ALIGN(sizeof(*dev), NETDEV_ALIGN) + (size_t)sizeof_priv;

    (void)name_assign_type;
    (void)rxqs;
    dev = kzalloc(alloc, GFP_KERNEL);
    if (!dev)
        return NULL;
    if (sizeof_priv)
        dev->b43c_priv = (u8 *)dev + ALIGN(sizeof(*dev), NETDEV_ALIGN);
    dev->dev_addr = dev->b43c_dev_addr;
    strlcpy(dev->name, name, IFNAMSIZ);
    INIT_LIST_HEAD(&dev->dev_list);
    INIT_LIST_HEAD(&dev->mc.list);
    INIT_LIST_HEAD(&dev->uc.list);
    dev->num_tx_queues = txqs ? txqs : 1;
    dev->real_num_tx_queues = dev->num_tx_queues;
    netif_carrier_off(dev);
    setup(dev);
    return dev;
}

void free_netdev(struct net_device *dev)
{
    kfree(dev);
}

int dev_alloc_name(struct net_device *dev, const char *name)
{
    /* A single device: "wlan%d" -> "wlan0" */
    char buf[IFNAMSIZ];
    const char *p = strchr(name, '%');

    if (!p) {
        strlcpy(dev->name, name, IFNAMSIZ);
        return 0;
    }
    snprintf(buf, sizeof(buf), name, 0);
    strlcpy(dev->name, buf, IFNAMSIZ);
    return 0;
}

int dev_change_name(struct net_device *dev, const char *newname)
{
    strlcpy(dev->name, newname, IFNAMSIZ);
    return 0;
}

int register_netdevice_notifier(struct notifier_block *nb)
{
    nb->next = g_netdev_notifiers;
    g_netdev_notifiers = nb;
    return 0;
}

int unregister_netdevice_notifier(struct notifier_block *nb)
{
    struct notifier_block **pp;
    for (pp = &g_netdev_notifiers; *pp; pp = &(*pp)->next) {
        if (*pp == nb) {
            *pp = nb->next;
            break;
        }
    }
    return 0;
}

int call_netdevice_notifiers(unsigned long val, struct net_device *dev)
{
    struct netdev_notifier_info info;
    struct notifier_block *nb;
    int ret = NOTIFY_DONE;

    info.dev = dev;
    info.extack = NULL;
    for (nb = g_netdev_notifiers; nb; nb = nb->next) {
        ret = nb->notifier_call(nb, val, &info);
        if (ret & NOTIFY_STOP_MASK)
            break;
    }
    return ret;
}

static int notifier_to_errno(int ret)
{
    ret &= ~NOTIFY_STOP_MASK;
    return ret > NOTIFY_OK ? NOTIFY_OK - ret : 0;
}

int register_netdevice(struct net_device *dev)
{
    int ret;

    if (strchr(dev->name, '%'))
        dev_alloc_name(dev, dev->name);
    if (dev->netdev_ops && dev->netdev_ops->ndo_init) {
        ret = dev->netdev_ops->ndo_init(dev);
        if (ret)
            return ret;
    }
    dev->ifindex = ++g_ifindex;
    /* as in net/core/dev.c: first NETDEV_POST_INIT (cfg80211 initializes
     * the wireless_dev here), then NETDEV_REGISTER */
    ret = notifier_to_errno(call_netdevice_notifiers(NETDEV_POST_INIT, dev));
    if (ret) {
        if (dev->netdev_ops && dev->netdev_ops->ndo_uninit)
            dev->netdev_ops->ndo_uninit(dev);
        return ret;
    }
    ret = notifier_to_errno(call_netdevice_notifiers(NETDEV_REGISTER, dev));
    if (ret)
        return ret;
    if (b43compat_net_hooks.netdev_registered)
        b43compat_net_hooks.netdev_registered(b43compat_net_hooks.ctx, dev);
    return 0;
}

void unregister_netdevice(struct net_device *dev)
{
    if (dev->flags & IFF_UP)
        dev_close(dev);
    if (b43compat_net_hooks.netdev_unregistered)
        b43compat_net_hooks.netdev_unregistered(b43compat_net_hooks.ctx, dev);
    call_netdevice_notifiers(NETDEV_UNREGISTER, dev);
    if (dev->netdev_ops && dev->netdev_ops->ndo_uninit)
        dev->netdev_ops->ndo_uninit(dev);
    if (dev->priv_destructor)
        dev->priv_destructor(dev);
    if (dev->needs_free_netdev)
        free_netdev(dev);
}

int dev_open(struct net_device *dev, struct netlink_ext_ack *extack)
{
    int ret;

    (void)extack;
    if (dev->flags & IFF_UP)
        return 0;
    ret = notifier_to_errno(call_netdevice_notifiers(NETDEV_PRE_UP, dev));
    if (ret)
        return ret;
    if (dev->netdev_ops && dev->netdev_ops->ndo_open) {
        ret = dev->netdev_ops->ndo_open(dev);
        if (ret)
            return ret;
    }
    dev->flags |= IFF_UP;
    if (dev->netdev_ops && dev->netdev_ops->ndo_set_rx_mode)
        dev->netdev_ops->ndo_set_rx_mode(dev);
    call_netdevice_notifiers(NETDEV_UP, dev);
    return 0;
}

void dev_close(struct net_device *dev)
{
    if (!(dev->flags & IFF_UP))
        return;
    call_netdevice_notifiers(NETDEV_GOING_DOWN, dev);
    if (dev->netdev_ops && dev->netdev_ops->ndo_stop)
        dev->netdev_ops->ndo_stop(dev);
    dev->flags &= ~IFF_UP;
    call_netdevice_notifiers(NETDEV_DOWN, dev);
}

void b43compat_netif_tx_all(struct net_device *dev, int stop)
{
    unsigned int i;
    for (i = 0; i < dev->num_tx_queues && i < 16; i++)
        dev->b43c_txq[i].stopped = stop;
}

/* Transmission from the stack: queue selection and ndo_start_xmit */
int dev_queue_xmit(struct sk_buff *skb)
{
    struct net_device *dev = skb->dev;
    u16 q = 0;

    if (!dev || !(dev->flags & IFF_UP)) {
        kfree_skb(skb);
        return -ENETDOWN;
    }
    if (dev->netdev_ops->ndo_select_queue)
        q = dev->netdev_ops->ndo_select_queue(dev, skb, NULL);
    skb_set_queue_mapping(skb, q);
    if (dev->b43c_txq[q].stopped) {
        dev->stats.tx_dropped++;
        kfree_skb(skb);
        return NETDEV_TX_BUSY;
    }
    return dev->netdev_ops->ndo_start_xmit(skb, dev);
}

/* Reception into the stack: handed to the Mac OS 9 glue layer (which takes
 * ownership) or, if there is none, dropped */
int netif_rx(struct sk_buff *skb)
{
    if (b43compat_net_hooks.rx) {
        b43compat_net_hooks.rx(b43compat_net_hooks.ctx, skb);
        return 0;
    }
    kfree_skb(skb);
    return 1;
}

/* net/core/dev.c: delivers a list of packets to the stack (mac80211 uses
 * it for all received data frames, in ieee80211_rx_napi) */
void netif_receive_skb_list(struct list_head *head)
{
    struct sk_buff *skb, *next;

    list_for_each_entry_safe(skb, next, head, list) {
        skb_list_del_init(skb);
        netif_rx(skb);
    }
}

__be16 eth_type_trans(struct sk_buff *skb, struct net_device *dev)
{
    const struct ethhdr *eth;

    skb->dev = dev;
    skb_reset_mac_header(skb);
    eth = (const struct ethhdr *)skb->data;
    skb_pull(skb, ETH_HLEN);
    if (is_multicast_ether_addr(eth->h_dest)) {
        if (is_broadcast_ether_addr(eth->h_dest))
            skb->pkt_type = PACKET_BROADCAST;
        else
            skb->pkt_type = PACKET_MULTICAST;
    } else if (!ether_addr_equal(eth->h_dest, dev->dev_addr)) {
        skb->pkt_type = PACKET_OTHERHOST;
    }
    if (ntohs(eth->h_proto) >= ETH_P_802_3_MIN)
        return eth->h_proto;
    return htons(ETH_P_802_2);
}

void ether_setup(struct net_device *dev)
{
    dev->type = ARPHRD_ETHER;
    dev->hard_header_len = ETH_HLEN;
    dev->min_mtu = ETH_MIN_MTU;
    dev->max_mtu = ETH_DATA_LEN;
    dev->mtu = ETH_DATA_LEN;
    dev->addr_len = ETH_ALEN;
    dev->tx_queue_len = 1000;
    dev->flags = 0x2 | 0x1000;          /* IFF_BROADCAST | IFF_MULTICAST */
    memset(dev->broadcast, 0xFF, ETH_ALEN);
}

int b43compat_eth_mac_addr(struct net_device *dev, void *p)
{
    struct sockaddr *addr = (struct sockaddr *)p;
    if (!is_valid_ether_addr((const u8 *)addr->sa_data))
        return -EADDRNOTAVAIL;
    memcpy(dev->dev_addr, addr->sa_data, ETH_ALEN);
    return 0;
}

u32 ethtool_op_get_link(struct net_device *dev)
{
    return netif_carrier_ok(dev) ? 1 : 0;
}

/* =========================================================================
 * Timer (kernel/time/timer.c): list of armed timers, run by
 * b43compat_run_work()
 * ========================================================================= */
static struct timer_list *g_timers;

void timer_setup(struct timer_list *t, void (*fn)(struct timer_list *), unsigned int flags)
{
    t->function = fn;
    t->flags = flags;
    t->b43c_armed = 0;
    t->b43c_next = NULL;
}

int del_timer(struct timer_list *t)
{
    struct timer_list **pp;

    if (!t->b43c_armed)
        return 0;
    for (pp = &g_timers; *pp; pp = &(*pp)->b43c_next) {
        if (*pp == t) {
            *pp = t->b43c_next;
            break;
        }
    }
    t->b43c_armed = 0;
    return 1;
}

int mod_timer(struct timer_list *t, unsigned long expires)
{
    int was = del_timer(t);
    t->expires = expires;
    t->b43c_armed = 1;
    t->b43c_next = g_timers;
    g_timers = t;
    return was;
}

int b43compat_run_timers(unsigned long now)
{
    static unsigned long pass;
    struct timer_list *t;
    int ran = 0, again = 1;

    /* As in Linux, a timer runs once per pass: one that its handler arms
     * again for a time already reached waits for the next pass (there it
     * would wait for the next tick) instead of running here forever */
    pass++;
    while (again) {
        again = 0;
        for (t = g_timers; t; t = t->b43c_next) {
            if (t->b43c_pass != pass && time_after_eq(now, t->expires)) {
                t->b43c_pass = pass;
                if (b43compat_pr_debug >= 2)
                    printk("b43compat: timer %08lx (expires %lu, now %lu)\n",
                           *(const unsigned long *)(const void *)t->function,
                           t->expires, now);
                del_timer(t);
                t->function(t);
                ran++;
                again = 1;      /* the list may have changed */
                break;
            }
        }
    }
    return ran;
}

/* =========================================================================
 * Tasklet: a work_struct at the start of the structure
 * ========================================================================= */
static void tasklet_work_fn(struct work_struct *w)
{
    struct tasklet_struct *t = (struct tasklet_struct *)w;

    if (t->disabled) {
        b43compat_queue_work(w);        /* retry on the next pass */
        return;
    }
    if (t->use_callback)
        t->callback(t);
    else
        t->func(t->data);
}

void tasklet_init(struct tasklet_struct *t, void (*func)(unsigned long), unsigned long data)
{
    memset(t, 0, sizeof(*t));
    INIT_WORK(&t->b43c_work, tasklet_work_fn);
    t->func = func;
    t->data = data;
}

void tasklet_setup(struct tasklet_struct *t, void (*callback)(struct tasklet_struct *))
{
    memset(t, 0, sizeof(*t));
    INIT_WORK(&t->b43c_work, tasklet_work_fn);
    t->callback = callback;
    t->use_callback = true;
}

void tasklet_schedule(struct tasklet_struct *t)
{
    b43compat_queue_work(&t->b43c_work);
}

void tasklet_kill(struct tasklet_struct *t)
{
    b43compat_cancel_work(&t->b43c_work);
}

/* =========================================================================
 * Workqueue
 * ========================================================================= */
static struct workqueue_struct g_system_wq = { "events" };
struct workqueue_struct *system_wq = &g_system_wq;
struct workqueue_struct *system_freezable_wq = &g_system_wq;
struct workqueue_struct *system_power_efficient_wq = &g_system_wq;
struct workqueue_struct *system_unbound_wq = &g_system_wq;

struct workqueue_struct *b43compat_alloc_workqueue(const char *name)
{
    struct workqueue_struct *wq = kzalloc(sizeof(*wq), GFP_KERNEL);
    if (wq)
        wq->name = name;
    return wq;
}

/* =========================================================================
 * rhashtable/rhltable: simple list. The object contains the key at
 * key_offset (key_len bytes) and the node at head_offset.
 * ========================================================================= */
#define RHT_OBJ(ht, he)     ((void *)((char *)(he) - (ht)->p.head_offset))
#define RHT_KEY(ht, obj)    ((const u8 *)(obj) + (ht)->p.key_offset)

int rhashtable_init(struct rhashtable *ht, const struct rhashtable_params *params)
{
    memset(ht, 0, sizeof(*ht));
    ht->p = *params;
    return 0;
}

int rhltable_init(struct rhltable *hlt, const struct rhashtable_params *params)
{
    int err = rhashtable_init(&hlt->ht, params);
    hlt->ht.rhlist = true;
    return err;
}

void rhashtable_destroy(struct rhashtable *ht)
{
    ht->b43c_first = NULL;
}

void rhashtable_free_and_destroy(struct rhashtable *ht, void (*free_fn)(void *ptr, void *arg), void *arg)
{
    struct rhash_head *he = ht->b43c_first, *next;

    while (he) {
        next = he->next;
        if (ht->rhlist) {
            struct rhlist_head *l = (struct rhlist_head *)he, *ln;
            while (l) {
                ln = l->next;
                free_fn(RHT_OBJ(ht, l), arg);
                l = ln;
            }
        } else {
            free_fn(RHT_OBJ(ht, he), arg);
        }
        he = next;
    }
    ht->b43c_first = NULL;
}

static struct rhash_head *rht_find(struct rhashtable *ht, const void *key)
{
    struct rhash_head *he;
    for (he = ht->b43c_first; he; he = he->next)
        if (!memcmp(RHT_KEY(ht, RHT_OBJ(ht, he)), key, ht->p.key_len))
            return he;
    return NULL;
}

int rhashtable_insert_fast(struct rhashtable *ht, struct rhash_head *obj, const struct rhashtable_params params)
{
    (void)params;
    obj->next = ht->b43c_first;
    ht->b43c_first = obj;
    ht->nelems.counter++;
    return 0;
}

int rhashtable_lookup_insert_fast(struct rhashtable *ht, struct rhash_head *obj, const struct rhashtable_params params)
{
    if (rht_find(ht, RHT_KEY(ht, RHT_OBJ(ht, obj))))
        return -EEXIST;
    return rhashtable_insert_fast(ht, obj, params);
}

int rhashtable_remove_fast(struct rhashtable *ht, struct rhash_head *obj, const struct rhashtable_params params)
{
    struct rhash_head **pp;
    (void)params;
    for (pp = &ht->b43c_first; *pp; pp = &(*pp)->next) {
        if (*pp == obj) {
            *pp = obj->next;
            ht->nelems.counter--;
            return 0;
        }
    }
    return -ENOENT;
}

void *rhashtable_lookup_fast(struct rhashtable *ht, const void *key, const struct rhashtable_params params)
{
    struct rhash_head *he = rht_find(ht, key);
    (void)params;
    return he ? RHT_OBJ(ht, he) : NULL;
}

/* rhltable: objects with the same key are in a list under the first one,
 * linked through rhlist_head.next */
int rhltable_insert(struct rhltable *hlt, struct rhlist_head *list, const struct rhashtable_params params)
{
    struct rhashtable *ht = &hlt->ht;
    struct rhash_head *he = rht_find(ht, RHT_KEY(ht, RHT_OBJ(ht, &list->rhead)));

    (void)params;
    if (he) {
        struct rhlist_head *first = (struct rhlist_head *)he;
        list->next = first->next;
        first->next = list;
    } else {
        list->next = NULL;
        list->rhead.next = ht->b43c_first;
        ht->b43c_first = &list->rhead;
    }
    ht->nelems.counter++;
    return 0;
}

int rhltable_remove(struct rhltable *hlt, struct rhlist_head *list, const struct rhashtable_params params)
{
    struct rhashtable *ht = &hlt->ht;
    struct rhash_head **pp;
    struct rhlist_head **lp, *first;

    (void)params;
    for (pp = &ht->b43c_first; *pp; pp = &(*pp)->next) {
        first = (struct rhlist_head *)*pp;
        if (first == list) {
            /* head of the list: replaced by the next one */
            if (list->next) {
                list->next->rhead.next = list->rhead.next;
                *pp = &list->next->rhead;
            } else {
                *pp = list->rhead.next;
            }
            ht->nelems.counter--;
            return 0;
        }
        for (lp = &first->next; *lp; lp = &(*lp)->next) {
            if (*lp == list) {
                *lp = list->next;
                ht->nelems.counter--;
                return 0;
            }
        }
    }
    return -ENOENT;
}

struct rhlist_head *rhltable_lookup(struct rhltable *hlt, const void *key, const struct rhashtable_params params)
{
    (void)params;
    return (struct rhlist_head *)rht_find(&hlt->ht, key);
}

void rhashtable_walk_enter(struct rhashtable *ht, struct rhashtable_iter *iter)
{
    iter->ht = ht;
    iter->p = NULL;
    iter->list = NULL;
    iter->b43c_started = 0;
}

void rhashtable_walk_exit(struct rhashtable_iter *iter)
{
    (void)iter;
}

void *rhashtable_walk_next(struct rhashtable_iter *iter)
{
    struct rhashtable *ht = iter->ht;

    if (ht->rhlist && iter->list && iter->list->next) {
        iter->list = iter->list->next;
        return RHT_OBJ(ht, iter->list);
    }
    if (!iter->b43c_started) {
        iter->b43c_started = 1;
        iter->p = ht->b43c_first;
    } else if (iter->p) {
        iter->p = iter->p->next;
    }
    if (!iter->p)
        return NULL;
    iter->list = (struct rhlist_head *)iter->p;
    return RHT_OBJ(ht, iter->p);
}

/* =========================================================================
 * IDR
 * ========================================================================= */
int idr_alloc(struct idr *idr, void *ptr, int start, int end, gfp_t gfp)
{
    int i;
    (void)gfp;
    if (end <= 0 || end > B43COMPAT_IDR_MAX)
        end = B43COMPAT_IDR_MAX;
    for (i = start; i < end; i++) {
        if (!idr->ptrs[i]) {
            idr->ptrs[i] = ptr;
            return i;
        }
    }
    return -ENOSPC;
}

void *idr_remove(struct idr *idr, unsigned long id)
{
    void *p;
    if (id >= B43COMPAT_IDR_MAX)
        return NULL;
    p = idr->ptrs[id];
    idr->ptrs[id] = NULL;
    return p;
}

void *idr_find(const struct idr *idr, unsigned long id)
{
    return id < B43COMPAT_IDR_MAX ? idr->ptrs[id] : NULL;
}

void *b43compat_idr_next(const struct idr *idr, int *id)
{
    for (; *id < B43COMPAT_IDR_MAX; (*id)++)
        if (idr->ptrs[*id])
            return idr->ptrs[*id];
    return NULL;
}

int idr_for_each(const struct idr *idr, int (*fn)(int id, void *p, void *data), void *data)
{
    int i, ret;
    for (i = 0; i < B43COMPAT_IDR_MAX; i++) {
        if (idr->ptrs[i]) {
            ret = fn(i, idr->ptrs[i], data);
            if (ret)
                return ret;
        }
    }
    return 0;
}

/* =========================================================================
 * Bitops and arithmetic (lib/)
 * ========================================================================= */
unsigned long find_next_bit(const unsigned long *addr, unsigned long size, unsigned long offset)
{
    for (; offset < size; offset++)
        if (addr[BIT_WORD(offset)] & BIT_MASK(offset))
            return offset;
    return size;
}

unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size, unsigned long offset)
{
    for (; offset < size; offset++)
        if (!(addr[BIT_WORD(offset)] & BIT_MASK(offset)))
            return offset;
    return size;
}

int bitmap_empty(const unsigned long *src, unsigned int nbits)
{
    return find_next_bit(src, nbits, 0) >= nbits;
}

unsigned int hweight32(unsigned int w)
{
    unsigned int n = 0;
    while (w) {
        n += w & 1;
        w >>= 1;
    }
    return n;
}

unsigned int hweight16(unsigned int w) { return hweight32(w & 0xffff); }
unsigned int hweight8(unsigned int w) { return hweight32(w & 0xff); }
unsigned long hweight64(u64 w) { return hweight32((u32)w) + hweight32((u32)(w >> 32)); }

int fls(unsigned int x)
{
    int r = 32;
    if (!x)
        return 0;
    while (!(x & 0x80000000U)) {
        x <<= 1;
        r--;
    }
    return r;
}

int fls64(u64 x)
{
    u32 h = (u32)(x >> 32);
    return h ? fls(h) + 32 : fls((u32)x);
}

int ffs(int x)
{
    int r = 1;
    unsigned int v = (unsigned int)x;
    if (!v)
        return 0;
    while (!(v & 1)) {
        v >>= 1;
        r++;
    }
    return r;
}

unsigned long __ffs(unsigned long word)
{
    return (unsigned long)(ffs((int)word) - 1);
}

unsigned long __ffs64(u64 word)
{
    if ((u32)word)
        return __ffs((u32)word);
    return __ffs((u32)(word >> 32)) + 32;
}

unsigned long gcd(unsigned long a, unsigned long b)
{
    unsigned long r;
    while (b) {
        r = a % b;
        a = b;
        b = r;
    }
    return a;
}

unsigned long int_sqrt(unsigned long x)
{
    unsigned long r = 0, b = 1UL << 30;
    while (b > x)
        b >>= 2;
    while (b) {
        if (x >= r + b) {
            x -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

/* =========================================================================
 * Time, random numbers
 * ========================================================================= */
ktime_t ktime_get(void)
{
    return (ktime_t)b43compat_time_us64() * 1000;
}

time64_t ktime_get_seconds(void)
{
    return (time64_t)(b43compat_time_us64() / 1000000);
}

/* =========================================================================
 * Strings
 * ========================================================================= */
size_t b43c_strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = strlen(src);
    if (size) {
        size_t n = len >= size ? size - 1 : len;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}

int b43c_strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (int)(unsigned char)*a - (int)(unsigned char)*b;
        if (!*a)
            break;
    }
    return 0;
}

char *b43c_strchr(const char *s, int c)
{
    for (; *s; s++)
        if (*s == (char)c)
            return (char *)s;
    return c ? NULL : (char *)s;
}

size_t b43c_strnlen(const char *s, size_t maxlen)
{
    size_t n = 0;
    while (n < maxlen && s[n])
        n++;
    return n;
}

char *b43c_strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++) != '\0')
        ;
    return r;
}

void *kmemdup(const void *src, size_t len, gfp_t gfp)
{
    void *p = kmalloc(len, gfp);
    if (p)
        memcpy(p, src, len);
    return p;
}

char *kstrdup(const char *s, gfp_t gfp)
{
    return s ? (char *)kmemdup(s, strlen(s) + 1, gfp) : NULL;
}

/* =========================================================================
 * RCU callbacks (see b43compat_net.h): queued, run by b43compat_rcu_process()
 * ========================================================================= */
static struct rcu_head *g_rcu_head;
static struct rcu_head **g_rcu_tail = &g_rcu_head;

void b43compat_call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *head))
{
    head->func = func;
    head->next = NULL;
    *g_rcu_tail = head;
    g_rcu_tail = &head->next;
}

void b43compat_rcu_process(void)
{
    /* Callbacks queued by these callbacks wait for the next pass */
    struct rcu_head *list = g_rcu_head;

    g_rcu_head = NULL;
    g_rcu_tail = &g_rcu_head;
    while (list) {
        struct rcu_head *h = list;
        unsigned long off = (unsigned long)h->func;

        list = h->next;
        if (off < 4096)                 /* kfree_rcu: offset of the rcu_head */
            kfree((char *)h - off);
        else
            h->func(h);
    }
}

long simple_strtol(const char *cp, char **endp, unsigned int base)
{
    long v = 0;
    int neg = 0;

    if (*cp == '-') {
        neg = 1;
        cp++;
    }
    if (!base)
        base = 10;
    for (;; cp++) {
        unsigned int d;
        if (*cp >= '0' && *cp <= '9')
            d = (unsigned int)(*cp - '0');
        else if ((*cp | 0x20) >= 'a' && (*cp | 0x20) <= 'f')
            d = (unsigned int)((*cp | 0x20) - 'a' + 10);
        else
            break;
        if (d >= base)
            break;
        v = v * (long)base + (long)d;
    }
    if (endp)
        *endp = (char *)cp;
    return neg ? -v : v;
}

/* sscanf: only the formats used by cfg80211 (%c, %d, %u, %hhx, %x) */
int sscanf(const char *buf, const char *fmt, ...)
{
    va_list ap;
    int n = 0;

    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') {
            if (*buf != *fmt)
                break;
            buf++;
            fmt++;
            continue;
        }
        fmt++;
        {
            int hh = 0;
            if (fmt[0] == 'h' && fmt[1] == 'h') {
                hh = 1;
                fmt += 2;
            }
            if (*fmt == 'c') {
                if (!*buf)
                    break;
                *va_arg(ap, char *) = *buf++;
            } else if (*fmt == 'd' || *fmt == 'u' || *fmt == 'x') {
                char *end;
                long v = simple_strtol(buf, &end, *fmt == 'x' ? 16 : 10);
                if (end == buf)
                    break;
                buf = end;
                if (hh)
                    *va_arg(ap, unsigned char *) = (unsigned char)v;
                else
                    *va_arg(ap, int *) = (int)v;
            } else {
                break;
            }
            fmt++;
            n++;
        }
    }
    va_end(ap);
    return n;
}

void print_hex_dump(const char *level, const char *prefix_str, int prefix_type,
                    int rowsize, int groupsize, const void *buf, size_t len, bool ascii)
{
    const u8 *b = (const u8 *)buf;
    size_t i, j;

    (void)level; (void)prefix_type; (void)groupsize; (void)ascii;
    if (rowsize <= 0)
        rowsize = 16;
    for (i = 0; i < len; i += (size_t)rowsize) {
        char line[3 * 32 + 1];
        size_t pos = 0;
        for (j = i; j < len && j < i + (size_t)rowsize && pos + 4 < sizeof(line); j++)
            pos += (size_t)snprintf(line + pos, sizeof(line) - pos, "%02x ", b[j]);
        line[pos] = '\0';
        printk("%s%04x: %s\n", prefix_str ? prefix_str : "", (unsigned int)i, line);
    }
}

/* =========================================================================
 * Devices, classes, system
 * ========================================================================= */
void device_initialize(struct device *dev)
{
    (void)dev;
}

int device_add(struct device *dev)
{
    if (!dev->b43c_name[0] && dev->init_name)
        dev_set_name(dev, "%s", dev->init_name);
    dev->b43c_registered = 1;
    return 0;
}

void device_del(struct device *dev)
{
    dev->b43c_registered = 0;
}

int class_register(struct class *cls)
{
    (void)cls;
    return 0;
}

void class_unregister(struct class *cls)
{
    (void)cls;
}

struct platform_device *b43compat_platform_device_register_simple(const char *name, int id)
{
    struct platform_device *p = kzalloc(sizeof(*p), GFP_KERNEL);
    if (!p)
        return (struct platform_device *)ERR_PTR(-ENOMEM);
    p->name = name;
    p->id = id;
    dev_set_name(&p->dev, "%s", name);
    return p;
}

struct new_utsname *init_utsname(void)
{
    static struct new_utsname u = { "MacOS", "9", "b43os9", "ppc" };
    return &u;
}
