/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * b43os9.c - Startup of the Linux wireless stack on Mac OS 9, and commands.
 *
 * Startup: the module init functions are called in the order in which
 * the kernel runs them (initcall levels and link order: drivers/ before
 * net/):
 *   subsys  ieee80211_init        mac80211 (rate control minstrel_ht)
 *   fs      ssb_modinit           SSB bus, PCI bridge -> ssb_pcihost_probe
 *   fs      cfg80211_init         cfg80211 (class, notifier, regulatory)
 *   module  b43_init              b43 SSB driver -> b43_ssb_probe
 *   late    regulatory_init_db    regulatory database (not included: "00")
 * b43_ssb_probe queues the firmware loading: the work item calls
 * ieee80211_register_hw(), which creates the "wlan0" interface.
 *
 * Commands: they do what the net/wireless/nl80211.c handlers do
 * (nl80211_trigger_scan, nl80211_connect, nl80211_new_key, ...) without
 * going through netlink.
 */
#include "b43os9_priv.h"
#include "core.h"           /* net/wireless (cfg80211, internal) */
#include "rdev-ops.h"

struct b43os9 *b43os9_instance;

/* =========================================================================
 * Hooks into b43compat_net.c
 * ========================================================================= */
static void os9_rx(void *ctx, struct sk_buff *skb)
{
    struct b43os9 *b = (struct b43os9 *)ctx;

    /* eth_type_trans() has stripped the Ethernet header: restart from mac_header */
    if (skb_mac_header_was_set(skb)) {
        u8 *eth = skb_mac_header(skb);
        unsigned long hlen = (unsigned long)(skb->data - eth);
        /* EAPOL to the supplicant (on Linux it reads it from a packet socket) */
        if (skb->protocol == htons(ETH_P_PAE) && b->supp.rx_eapol && hlen >= ETH_HLEN)
            b->supp.rx_eapol(b->supp.ctx, eth + ETH_ALEN, skb->data, skb->len);
        if (b->ev.rx)
            b->ev.rx(b->ev.ctx, eth, hlen + skb->len);
    }
    kfree_skb(skb);
}

static void os9_netdev_registered(void *ctx, struct net_device *dev)
{
    struct b43os9 *b = (struct b43os9 *)ctx;
    if (!b->dev)
        b->dev = dev;
}

static void os9_netdev_unregistered(void *ctx, struct net_device *dev)
{
    struct b43os9 *b = (struct b43os9 *)ctx;
    if (b->dev == dev)
        b->dev = NULL;
}

static struct cfg80211_registered_device *os9_rdev(struct b43os9 *b)
{
    return wiphy_to_rdev(b->dev->ieee80211_ptr->wiphy);
}

/* =========================================================================
 * Startup
 * ========================================================================= */
int b43os9_attach(b43os9 **out, volatile unsigned char *mmio, const b43_env *env,
                  const b43os9_firmware *fw, const b43os9_options *opt,
                  const b43os9_events *events)
{
    struct b43os9 *b;
    unsigned long until;
    int err;

    *out = NULL;
    if (!env || !env->delay_us || !env->time_us || !env->alloc || !env->free ||
        !env->pci_cfg_read32 || !env->pci_cfg_write32 || !mmio || !fw)
        return -EINVAL;
    b43compat_env = env;
    b = kzalloc(sizeof(*b), GFP_KERNEL);
    if (!b)
        return -ENOMEM;
    b->env = env;
    b->fw = *fw;
    if (events)
        b->ev = *events;
    b43os9_instance = b;

    b43compat_net_hooks.ctx = b;
    b43compat_net_hooks.rx = os9_rx;
    b43compat_net_hooks.netdev_registered = os9_netdev_registered;
    b43compat_net_hooks.netdev_unregistered = os9_netdev_unregistered;

    /* Module parameters, as in "modprobe b43 pio=1 ...": DMA has not yet
     * been ported to Mac OS 9 */
    *(int *)b43compat_param_pio = 1;
    if (opt) {
        *(int *)b43compat_param_verbose = opt->verbose;
        *(int *)b43compat_param_nohwcrypt = opt->hwcrypt ? 0 : 1;
        *(int *)b43compat_param_qos = opt->qos;
        if (opt->regdom)
            *(const char **)b43compat_param_ieee80211_regdom = opt->regdom;
        b43compat_pr_debug = opt->pr_debug;
    }

    /* The card's PCI device, as the kernel sees it */
    b43compat_pci_host_init((void __iomem *)mmio, "pci80211");

    err = b43compat_initcall_ieee80211_init();
    if (err) {
        printk("b43os9: ieee80211_init: %d\n", err);
        return err;
    }
    err = b43compat_initcall_ssb_modinit();
    if (err) {
        printk("b43os9: ssb_modinit: %d\n", err);
        return err;
    }
    err = b43compat_initcall_cfg80211_init();
    if (err) {
        printk("b43os9: cfg80211_init: %d\n", err);
        return err;
    }
    err = b43compat_initcall_b43_init();
    if (err) {
        printk("b43os9: b43_init: %d\n", err);
        return err;
    }
    b43compat_initcall_regulatory_init_db();

    /* The firmware is loaded in a work item: wait for the interface (5 s) */
    until = jiffies + msecs_to_jiffies(5000);
    while (!b->dev && time_before(b43compat_jiffies(), until)) {
        b43compat_run_work();
        udelay(1000);
    }
    if (!b->dev) {
        printk("b43os9: interface not created (firmware or hardware)\n");
        return -ENODEV;
    }
    printk("b43os9: interface %s ready, MAC %pM\n", b->dev->name, b->dev->dev_addr);
    *out = b;
    return 0;
}

void b43os9_poll(b43os9 *b)
{
    b43compat_poll_irq();
    b43compat_run_work();
    if (b->supp.poll)
        b->supp.poll(b->supp.ctx);
}

/* =========================================================================
 * wpa_supplicant (b43os9_wpa.h)
 * ========================================================================= */
void b43os9_set_supplicant(b43os9 *b, const b43os9_supp_ops *ops)
{
    if (ops)
        b->supp = *ops;
    else
        memset(&b->supp, 0, sizeof(b->supp));
}

void b43os9_supp_authorized(b43os9 *b, const unsigned char *bssid)
{
    if (b->ev.authorized)
        b->ev.authorized(b->ev.ctx, bssid);
}

const b43_env *b43os9_supp_env(void)
{
    return b43compat_env;
}

int b43os9_up(b43os9 *b, const unsigned char *mac)
{
    int err;

    if (!b->dev)
        return -ENODEV;
    rtnl_lock();
    if (mac && b->dev->netdev_ops->ndo_set_mac_address) {
        struct sockaddr sa;
        sa.sa_family = ARPHRD_ETHER;
        memcpy(sa.sa_data, mac, ETH_ALEN);
        err = b->dev->netdev_ops->ndo_set_mac_address(b->dev, &sa);
        if (err) {
            rtnl_unlock();
            return err;
        }
    }
    err = dev_open(b->dev, NULL);
    rtnl_unlock();
    return err;
}

void b43os9_down(b43os9 *b)
{
    if (!b->dev)
        return;
    rtnl_lock();
    dev_close(b->dev);
    rtnl_unlock();
}

void b43os9_get_mac(b43os9 *b, unsigned char *mac)
{
    if (b->dev)
        memcpy(mac, b->dev->dev_addr, ETH_ALEN);
}

/* =========================================================================
 * Scanning (nl80211_trigger_scan, nl80211_dump_scan)
 * ========================================================================= */
int b43os9_scan(b43os9 *b, const char *ssid)
{
    struct cfg80211_registered_device *rdev;
    struct wireless_dev *wdev;
    struct cfg80211_scan_request *request;
    struct ieee80211_supported_band *sband;
    unsigned int n_channels = 0, i;
    int n_ssids, band, j, err;
    size_t ssid_len = ssid ? strlen(ssid) : 0;

    if (!b->dev || !netif_running(b->dev))
        return -ENETDOWN;
    if (ssid_len > IEEE80211_MAX_SSID_LEN)
        return -EINVAL;
    wdev = b->dev->ieee80211_ptr;
    rdev = os9_rdev(b);
    wiphy_lock(&rdev->wiphy);
    if (rdev->scan_req || rdev->scan_msg) {
        wiphy_unlock(&rdev->wiphy);
        return -EBUSY;
    }

    for (band = 0; band < NUM_NL80211_BANDS; band++) {
        sband = rdev->wiphy.bands[band];
        if (sband)
            n_channels += (unsigned int)sband->n_channels;
    }
    /* SSIDs: the requested one (active) and the wildcard one (empty) */
    n_ssids = ssid_len ? 2 : 1;
    request = kzalloc(struct_size(request, channels, n_channels) +
                      sizeof(*request->ssids) * n_ssids, GFP_KERNEL);
    if (!request) {
        wiphy_unlock(&rdev->wiphy);
        return -ENOMEM;
    }
    request->ssids = (void *)&request->channels[n_channels];
    request->n_ssids = n_ssids;
    if (ssid_len) {
        memcpy(request->ssids[0].ssid, ssid, ssid_len);
        request->ssids[0].ssid_len = (u8)ssid_len;
    }

    i = 0;
    for (band = 0; band < NUM_NL80211_BANDS; band++) {
        sband = rdev->wiphy.bands[band];
        if (!sband)
            continue;
        for (j = 0; j < sband->n_channels; j++) {
            struct ieee80211_channel *chan = &sband->channels[j];
            if (chan->flags & IEEE80211_CHAN_DISABLED)
                continue;
            request->channels[i++] = chan;
        }
        request->rates[band] = (1 << sband->n_bitrates) - 1;
    }
    if (!i) {
        kfree(request);
        wiphy_unlock(&rdev->wiphy);
        return -EINVAL;
    }
    request->n_channels = i;
    request->scan_width = NL80211_BSS_CHAN_WIDTH_20;
    eth_broadcast_addr(request->bssid);
    request->wdev = wdev;
    request->wiphy = &rdev->wiphy;
    request->scan_start = jiffies;

    rdev->scan_req = request;
    err = cfg80211_scan(rdev);
    if (err) {
        rdev->scan_req = NULL;
        kfree(request);
    }
    wiphy_unlock(&rdev->wiphy);
    return err;
}

int b43os9_scan_result(b43os9 *b, unsigned int idx, b43os9_bss *out)
{
    struct cfg80211_registered_device *rdev;
    struct cfg80211_internal_bss *bss;
    unsigned int n = 0;
    int found = 0;

    if (!b->dev)
        return 0;
    rdev = os9_rdev(b);
    spin_lock_bh(&rdev->bss_lock);
    list_for_each_entry(bss, &rdev->bss_list, list) {
        const struct cfg80211_bss_ies *ies;
        const u8 *ssid;

        if (n++ != idx)
            continue;
        memset(out, 0, sizeof(*out));
        memcpy(out->bssid, bss->pub.bssid, ETH_ALEN);
        out->channel = (unsigned char)ieee80211_frequency_to_channel(bss->pub.channel->center_freq);
        if (rdev->wiphy.signal_type == CFG80211_SIGNAL_TYPE_MBM)
            out->signal_dbm = bss->pub.signal / 100;
        else
            out->signal_dbm = bss->pub.signal;
        out->capability = bss->pub.capability;
        out->beacon_interval = bss->pub.beacon_interval;
        ssid = ieee80211_bss_get_ie(&bss->pub, WLAN_EID_SSID);
        if (ssid && ssid[1] <= 32) {
            memcpy(out->ssid, ssid + 2, ssid[1]);
            out->ssid_len = ssid[1];
        }
        ies = rcu_dereference(bss->pub.ies);
        if (ies) {
            size_t len = ies->len < sizeof(out->ies) ? ies->len : sizeof(out->ies);
            memcpy(out->ies, ies->data, len);
            out->ies_len = (unsigned short)len;
        }
        found = 1;
        break;
    }
    spin_unlock_bh(&rdev->bss_lock);
    return found;
}

/* =========================================================================
 * Connection (nl80211_connect, nl80211_disconnect)
 * ========================================================================= */

/* Cipher suites from the RSN or WPA element (with nl80211, wpa_supplicant
 * passes them as separate attributes) */
static u32 os9_suite(const u8 *p)
{
    /* WPA1 uses the OUI 00:50:F2, cfg80211 the 00:0F:AC values */
    return 0x000FAC00UL | p[3];
}

static void os9_crypto_from_ie(struct cfg80211_crypto_settings *c, const u8 *ie, unsigned int len)
{
    const u8 *p = NULL, *end;
    unsigned int n, k;

    while (len >= 2 && ie[1] + 2u <= len) {
        if (ie[0] == WLAN_EID_RSN && ie[1] >= 2) {
            c->wpa_versions = NL80211_WPA_VERSION_2;
            p = ie + 4;                         /* after the version */
            end = ie + 2 + ie[1];
            break;
        }
        if (ie[0] == WLAN_EID_VENDOR_SPECIFIC && ie[1] >= 6 &&
            ie[2] == 0x00 && ie[3] == 0x50 && ie[4] == 0xF2 && ie[5] == 1) {
            c->wpa_versions = NL80211_WPA_VERSION_1;
            p = ie + 8;
            end = ie + 2 + ie[1];
            break;
        }
        len -= ie[1] + 2u;
        ie += ie[1] + 2;
    }
    if (!p)
        return;
    if (p + 4 <= end) {
        c->cipher_group = os9_suite(p);
        p += 4;
    }
    if (p + 2 <= end) {
        n = p[0] | (p[1] << 8);
        p += 2;
        for (k = 0; k < n && p + 4 <= end; k++, p += 4)
            if (k < NL80211_MAX_NR_CIPHER_SUITES)
                c->ciphers_pairwise[c->n_ciphers_pairwise++] = os9_suite(p);
    }
    if (p + 2 <= end) {
        n = p[0] | (p[1] << 8);
        p += 2;
        for (k = 0; k < n && p + 4 <= end; k++, p += 4)
            if (k < NL80211_MAX_NR_AKM_SUITES)
                c->akm_suites[c->n_akm_suites++] = os9_suite(p);
    }
    /* 802.1X: EAPOL frames pass even with the port closed */
    c->control_port = true;
    c->control_port_ethertype = cpu_to_be16(ETH_P_PAE);
}

int b43os9_connect(b43os9 *b, const char *ssid, const unsigned char *bssid,
                   const unsigned char *ie, unsigned int ie_len, int privacy)
{
    struct cfg80211_registered_device *rdev;
    struct cfg80211_connect_params connect;
    size_t ssid_len;
    int err;

    if (!b->dev || !netif_running(b->dev))
        return -ENETDOWN;
    ssid_len = ssid ? strlen(ssid) : 0;
    if (!ssid_len || ssid_len > IEEE80211_MAX_SSID_LEN)
        return -EINVAL;
    rdev = os9_rdev(b);

    memset(&connect, 0, sizeof(connect));
    connect.ssid = (const u8 *)ssid;
    connect.ssid_len = ssid_len;
    connect.bssid = bssid;
    connect.ie = ie;
    connect.ie_len = ie ? ie_len : 0;
    connect.privacy = privacy ? true : false;
    connect.auth_type = NL80211_AUTHTYPE_AUTOMATIC;
    connect.mfp = NL80211_MFP_NO;
    connect.bg_scan_period = -1;
    if (ie && ie_len)
        os9_crypto_from_ie(&connect.crypto, ie, ie_len);

    wiphy_lock(&rdev->wiphy);
    wdev_lock(b->dev->ieee80211_ptr);
    err = cfg80211_connect(rdev, b->dev, &connect, NULL, NULL);
    wdev_unlock(b->dev->ieee80211_ptr);
    wiphy_unlock(&rdev->wiphy);
    return err;
}

int b43os9_disconnect(b43os9 *b, unsigned short reason)
{
    struct cfg80211_registered_device *rdev;
    int err;

    if (!b->dev)
        return -ENODEV;
    rdev = os9_rdev(b);
    wiphy_lock(&rdev->wiphy);
    wdev_lock(b->dev->ieee80211_ptr);
    err = cfg80211_disconnect(rdev, b->dev, reason, true);
    wdev_unlock(b->dev->ieee80211_ptr);
    wiphy_unlock(&rdev->wiphy);
    return err;
}

/* =========================================================================
 * Keys and port (nl80211_new_key, nl80211_set_key, nl80211_set_station)
 * ========================================================================= */
int b43os9_add_key(b43os9 *b, int key_idx, int pairwise, const unsigned char *mac,
                   unsigned long cipher, const unsigned char *key, unsigned int key_len,
                   const unsigned char *seq, unsigned int seq_len)
{
    struct cfg80211_registered_device *rdev;
    struct key_params params;
    int err;

    if (!b->dev)
        return -ENODEV;
    rdev = os9_rdev(b);
    memset(&params, 0, sizeof(params));
    params.key = key;
    params.key_len = (int)key_len;
    params.seq = seq;
    params.seq_len = seq ? (int)seq_len : 0;
    params.cipher = (u32)cipher;
    params.mode = NL80211_KEY_RX_TX;

    err = cfg80211_validate_key_settings(rdev, &params, key_idx, pairwise ? true : false,
                                         pairwise ? mac : NULL);
    if (err)
        return err;
    wiphy_lock(&rdev->wiphy);
    wdev_lock(b->dev->ieee80211_ptr);
    err = rdev_add_key(rdev, b->dev, (u8)key_idx, pairwise ? true : false,
                       pairwise ? mac : NULL, &params);
    /* WEP keys are also the default transmit keys */
    if (!err && !pairwise &&
        (cipher == B43OS9_CIPHER_WEP40 || cipher == B43OS9_CIPHER_WEP104))
        err = rdev_set_default_key(rdev, b->dev, (u8)key_idx, true, true);
    wdev_unlock(b->dev->ieee80211_ptr);
    wiphy_unlock(&rdev->wiphy);
    return err;
}

int b43os9_authorize(b43os9 *b, const unsigned char *bssid)
{
    struct cfg80211_registered_device *rdev;
    struct station_parameters params;
    u8 mac[ETH_ALEN];
    int err;

    if (!b->dev)
        return -ENODEV;
    rdev = os9_rdev(b);
    memset(&params, 0, sizeof(params));
    params.listen_interval = -1;
    params.support_p2p_ps = -1;
    params.sta_flags_mask = BIT(NL80211_STA_FLAG_AUTHORIZED);
    params.sta_flags_set = BIT(NL80211_STA_FLAG_AUTHORIZED);
    memcpy(mac, bssid, ETH_ALEN);
    wiphy_lock(&rdev->wiphy);
    err = rdev_change_station(rdev, b->dev, mac, &params);
    wiphy_unlock(&rdev->wiphy);
    return err;
}

/* =========================================================================
 * Data
 * ========================================================================= */
int b43os9_tx(b43os9 *b, const unsigned char *frame, unsigned long len)
{
    struct net_device *dev = b->dev;
    struct sk_buff *skb;

    if (!dev || !netif_running(dev))
        return -ENETDOWN;
    if (len < ETH_HLEN || len > ETH_FRAME_LEN)
        return -EINVAL;
    skb = dev_alloc_skb(dev->needed_headroom + len + dev->needed_tailroom);
    if (!skb)
        return -ENOMEM;
    skb_reserve(skb, dev->needed_headroom);
    skb_put_data(skb, frame, len);
    skb->dev = dev;
    skb->protocol = ((const struct ethhdr *)frame)->h_proto;
    skb_reset_mac_header(skb);
    skb_set_network_header(skb, ETH_HLEN);
    return dev_queue_xmit(skb);
}
