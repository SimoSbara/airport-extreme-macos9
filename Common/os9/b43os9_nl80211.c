/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * b43os9_nl80211.c - Replaces net/wireless/nl80211.c (and sysfs.c).
 *
 * On Linux cfg80211 and mac80211 notify user space (iw, wpa_supplicant)
 * with netlink messages built by these functions. On Mac OS 9 the same
 * events become calls to the b43os9_events callbacks (b43os9.h); events
 * concerning unused modes (AP, mesh, NAN, TDLS, measurements) have no
 * recipient, as if no program were listening.
 *
 * The signatures are those of net/wireless/nl80211.h and include/net/cfg80211.h.
 */
#include <net/cfg80211.h>
#include "core.h"
#include "b43os9_priv.h"

/* =========================================================================
 * Netlink family and sysfs registration: nothing to register
 * ========================================================================= */
int nl80211_init(void)
{
    return 0;
}

void nl80211_exit(void)
{
}

struct class ieee80211_class = {
    .name = "ieee80211",
};

int wiphy_sysfs_init(void)
{
    return class_register(&ieee80211_class);
}

void wiphy_sysfs_exit(void)
{
    class_unregister(&ieee80211_class);
}

/* nl80211.c: WoWLAN/coalesce rules (only set via netlink) */
void cfg80211_rdev_free_coalesce(struct cfg80211_registered_device *rdev)
{
    (void)rdev;
}

/* pmsr.c: peer measurements (ranging), only via netlink */
void cfg80211_pmsr_free_wk(struct work_struct *work)
{
    (void)work;
}

void cfg80211_pmsr_wdev_down(struct wireless_dev *wdev)
{
    (void)wdev;
}

/* =========================================================================
 * State notifications with no recipient
 * ========================================================================= */
void nl80211_notify_wiphy(struct cfg80211_registered_device *rdev, enum nl80211_commands cmd)
{
    (void)rdev; (void)cmd;
}

void nl80211_notify_iface(struct cfg80211_registered_device *rdev, struct wireless_dev *wdev,
                          enum nl80211_commands cmd)
{
    (void)rdev; (void)wdev; (void)cmd;
}

void nl80211_common_reg_change_event(enum nl80211_commands cmd_id, struct regulatory_request *request)
{
    (void)cmd_id;
    printk("cfg80211: regulatory domain %c%c\n", request->alpha2[0], request->alpha2[1]);
}

void nl80211_send_beacon_hint_event(struct wiphy *wiphy, struct ieee80211_channel *channel_before,
                                    struct ieee80211_channel *channel_after)
{
    (void)wiphy; (void)channel_before; (void)channel_after;
}

void nl80211_send_scan_start(struct cfg80211_registered_device *rdev, struct wireless_dev *wdev)
{
    (void)rdev; (void)wdev;
}

void nl80211_send_sched_scan(struct cfg80211_sched_scan_request *req, u32 cmd)
{
    (void)req; (void)cmd;
}

void nl80211_send_rx_auth(struct cfg80211_registered_device *rdev, struct net_device *netdev,
                          const u8 *buf, size_t len, gfp_t gfp)
{
    (void)rdev; (void)netdev; (void)buf; (void)len; (void)gfp;
}

void nl80211_send_rx_assoc(struct cfg80211_registered_device *rdev, struct net_device *netdev,
                           const u8 *buf, size_t len, gfp_t gfp, int uapsd_queues,
                           const u8 *req_ies, size_t req_ies_len)
{
    (void)rdev; (void)netdev; (void)buf; (void)len; (void)gfp; (void)uapsd_queues;
    (void)req_ies; (void)req_ies_len;
}

void nl80211_send_deauth(struct cfg80211_registered_device *rdev, struct net_device *netdev,
                         const u8 *buf, size_t len, bool reconnect, gfp_t gfp)
{
    (void)rdev; (void)netdev; (void)buf; (void)len; (void)reconnect; (void)gfp;
}

void nl80211_send_disassoc(struct cfg80211_registered_device *rdev, struct net_device *netdev,
                           const u8 *buf, size_t len, bool reconnect, gfp_t gfp)
{
    (void)rdev; (void)netdev; (void)buf; (void)len; (void)reconnect; (void)gfp;
}

void nl80211_send_auth_timeout(struct cfg80211_registered_device *rdev, struct net_device *netdev,
                               const u8 *addr, gfp_t gfp)
{
    (void)rdev; (void)netdev; (void)gfp;
    printk("cfg80211: no authentication response from %pM\n", addr);
}

void nl80211_send_assoc_timeout(struct cfg80211_registered_device *rdev, struct net_device *netdev,
                                const u8 *addr, gfp_t gfp)
{
    (void)rdev; (void)netdev; (void)gfp;
    printk("cfg80211: no association response from %pM\n", addr);
}

void nl80211_send_ibss_bssid(struct cfg80211_registered_device *rdev, struct net_device *netdev,
                             const u8 *bssid, gfp_t gfp)
{
    (void)rdev; (void)netdev; (void)bssid; (void)gfp;
}

void nl80211_send_ap_stopped(struct wireless_dev *wdev)
{
    (void)wdev;
}

void nl80211_radar_notify(struct cfg80211_registered_device *rdev,
                          const struct cfg80211_chan_def *chandef,
                          enum nl80211_radar_event event, struct net_device *netdev, gfp_t gfp)
{
    (void)rdev; (void)chandef; (void)event; (void)netdev; (void)gfp;
}

/* Management frames user space asked to receive: none.
 * -ENOENT = nobody listening (cfg80211_rx_mgmt returns false). */
int nl80211_send_mgmt(struct cfg80211_registered_device *rdev, struct wireless_dev *wdev,
                      u32 nlpid, int freq, int sig_dbm, const u8 *buf, size_t len,
                      u32 flags, gfp_t gfp)
{
    (void)rdev; (void)wdev; (void)nlpid; (void)freq; (void)sig_dbm;
    (void)buf; (void)len; (void)flags; (void)gfp;
    return -ENOENT;
}

/* =========================================================================
 * Events to Mac OS 9
 * ========================================================================= */

/* The scan-done message: __cfg80211_scan_done "builds" it and
 * nl80211_send_scan_msg sends it. Here the message carries only the outcome. */
struct sk_buff *nl80211_build_scan_msg(struct cfg80211_registered_device *rdev,
                                       struct wireless_dev *wdev, bool aborted)
{
    struct sk_buff *msg = alloc_skb(16, GFP_KERNEL);
    (void)rdev; (void)wdev;
    if (msg)
        msg->cb[0] = aborted ? 1 : 0;
    return msg;
}

void nl80211_send_scan_msg(struct cfg80211_registered_device *rdev, struct sk_buff *msg)
{
    const b43os9_events *ev = b43os9_ev();
    (void)rdev;
    if (!msg)
        return;
    if (ev && ev->scan_done)
        ev->scan_done(ev->ctx, msg->cb[0]);
    kfree_skb(msg);
}

void nl80211_send_connect_result(struct cfg80211_registered_device *rdev, struct net_device *netdev,
                                 struct cfg80211_connect_resp_params *params, gfp_t gfp)
{
    const b43os9_events *ev = b43os9_ev();
    const b43os9_supp_ops *supp = b43os9_supp();
    (void)rdev; (void)netdev; (void)gfp;
    /* wpa_supplicant's events.c: EVENT_ASSOC */
    if (supp && supp->assoc && params->status == WLAN_STATUS_SUCCESS && params->bssid)
        supp->assoc(supp->ctx, params->bssid);
    if (ev && ev->connect_result)
        ev->connect_result(ev->ctx, params->status, params->bssid);
}

void nl80211_send_roamed(struct cfg80211_registered_device *rdev, struct net_device *netdev,
                         struct cfg80211_roam_info *info, gfp_t gfp)
{
    const b43os9_events *ev = b43os9_ev();
    (void)rdev; (void)netdev; (void)gfp;
    if (ev && ev->connect_result)
        ev->connect_result(ev->ctx, 0, info->bssid ? info->bssid : info->bss->bssid);
}

void nl80211_send_port_authorized(struct cfg80211_registered_device *rdev, struct net_device *netdev,
                                  const u8 *bssid)
{
    (void)rdev; (void)netdev;
    printk("cfg80211: port authorized to %pM\n", bssid);
}

void nl80211_send_disconnected(struct cfg80211_registered_device *rdev, struct net_device *netdev,
                               u16 reason, const u8 *ie, size_t ie_len, bool from_ap)
{
    const b43os9_events *ev = b43os9_ev();
    const b43os9_supp_ops *supp = b43os9_supp();
    (void)rdev; (void)netdev; (void)ie; (void)ie_len;
    if (supp && supp->disassoc)
        supp->disassoc(supp->ctx);
    if (ev && ev->disconnected)
        ev->disconnected(ev->ctx, reason, from_ap ? 1 : 0);
}

void nl80211_michael_mic_failure(struct cfg80211_registered_device *rdev, struct net_device *netdev,
                                 const u8 *addr, enum nl80211_key_type key_type, int key_id,
                                 const u8 *tsc, gfp_t gfp)
{
    const b43os9_events *ev = b43os9_ev();
    (void)rdev; (void)netdev; (void)key_id; (void)tsc; (void)gfp;
    if (ev && ev->mic_failure)
        ev->mic_failure(ev->ctx, addr, key_type == NL80211_KEYTYPE_GROUP);
}

/* =========================================================================
 * Public cfg80211 functions defined in nl80211.c
 * ========================================================================= */

/* EAPOL via nl80211 (control_port_over_nl80211): not requested, EAPOL
 * frames arrive as normal data (netif_rx) */
bool cfg80211_rx_control_port(struct net_device *dev, struct sk_buff *skb, bool unencrypted)
{
    (void)dev; (void)skb; (void)unencrypted;
    return false;
}

void cfg80211_control_port_tx_status(struct wireless_dev *wdev, u64 cookie,
                                     const u8 *buf, size_t len, bool ack, gfp_t gfp)
{
    (void)wdev; (void)cookie; (void)buf; (void)len; (void)ack; (void)gfp;
}

void cfg80211_mgmt_tx_status(struct wireless_dev *wdev, u64 cookie,
                             const u8 *buf, size_t len, bool ack, gfp_t gfp)
{
    (void)wdev; (void)cookie; (void)buf; (void)len; (void)ack; (void)gfp;
}

void cfg80211_new_sta(struct net_device *dev, const u8 *mac_addr,
                      struct station_info *sinfo, gfp_t gfp)
{
    (void)dev; (void)mac_addr; (void)sinfo; (void)gfp;
}

void cfg80211_del_sta_sinfo(struct net_device *dev, const u8 *mac_addr,
                            struct station_info *sinfo, gfp_t gfp)
{
    (void)dev; (void)mac_addr; (void)sinfo; (void)gfp;
}

void cfg80211_ready_on_channel(struct wireless_dev *wdev, u64 cookie,
                               struct ieee80211_channel *chan, unsigned int duration, gfp_t gfp)
{
    (void)wdev; (void)cookie; (void)chan; (void)duration; (void)gfp;
}

void cfg80211_remain_on_channel_expired(struct wireless_dev *wdev, u64 cookie,
                                        struct ieee80211_channel *chan, gfp_t gfp)
{
    (void)wdev; (void)cookie; (void)chan; (void)gfp;
}

void cfg80211_tx_mgmt_expired(struct wireless_dev *wdev, u64 cookie,
                              struct ieee80211_channel *chan, gfp_t gfp)
{
    (void)wdev; (void)cookie; (void)chan; (void)gfp;
}

void cfg80211_cqm_rssi_notify(struct net_device *dev,
                              enum nl80211_cqm_rssi_threshold_event rssi_event,
                              s32 rssi_level, gfp_t gfp)
{
    (void)dev; (void)rssi_event; (void)rssi_level; (void)gfp;
}

void cfg80211_cqm_pktloss_notify(struct net_device *dev, const u8 *peer,
                                 u32 num_packets, gfp_t gfp)
{
    (void)dev; (void)gfp;
    printk("mac80211: %u packets lost to %pM\n", (unsigned int)num_packets, peer);
}

void cfg80211_cqm_beacon_loss_notify(struct net_device *dev, gfp_t gfp)
{
    (void)dev; (void)gfp;
}

void cfg80211_gtk_rekey_notify(struct net_device *dev, const u8 *bssid,
                               const u8 *replay_ctr, gfp_t gfp)
{
    (void)dev; (void)bssid; (void)replay_ctr; (void)gfp;
}

void cfg80211_ch_switch_notify(struct net_device *dev, struct cfg80211_chan_def *chandef)
{
    (void)dev;
    printk("cfg80211: channel switch to %u MHz\n", chandef->chan->center_freq);
}

void cfg80211_ch_switch_started_notify(struct net_device *dev, struct cfg80211_chan_def *chandef,
                                       u8 count, bool quiet)
{
    (void)dev; (void)chandef; (void)count; (void)quiet;
}

int cfg80211_bss_color_notify(struct net_device *dev, gfp_t gfp, enum nl80211_commands cmd,
                              u8 count, u64 color_bitmap)
{
    (void)dev; (void)gfp; (void)cmd; (void)count; (void)color_bitmap;
    return 0;
}

void cfg80211_probe_status(struct net_device *dev, const u8 *addr, u64 cookie, bool acked,
                           s32 ack_signal, bool is_valid_ack_signal, gfp_t gfp)
{
    (void)dev; (void)addr; (void)cookie; (void)acked;
    (void)ack_signal; (void)is_valid_ack_signal; (void)gfp;
}

void cfg80211_report_obss_beacon_khz(struct wiphy *wiphy, const u8 *frame, size_t len,
                                     int freq, int sig_dbm)
{
    (void)wiphy; (void)frame; (void)len; (void)freq; (void)sig_dbm;
}

/* Frames from an unknown station (AP/GO only): not handled */
bool cfg80211_rx_spurious_frame(struct net_device *dev, const u8 *addr, gfp_t gfp)
{
    (void)dev; (void)addr; (void)gfp;
    return false;
}

bool cfg80211_rx_unexpected_4addr_frame(struct net_device *dev, const u8 *addr, gfp_t gfp)
{
    (void)dev; (void)addr; (void)gfp;
    return false;
}

void cfg80211_rx_unprot_mlme_mgmt(struct net_device *dev, const u8 *buf, size_t len)
{
    (void)dev; (void)buf; (void)len;
}

void cfg80211_sta_opmode_change_notify(struct net_device *dev, const u8 *mac,
                                       struct sta_opmode_info *sta_opmode, gfp_t gfp)
{
    (void)dev; (void)mac; (void)sta_opmode; (void)gfp;
}

void cfg80211_tdls_oper_request(struct net_device *dev, const u8 *peer,
                                enum nl80211_tdls_operation oper, u16 reason_code, gfp_t gfp)
{
    (void)dev; (void)peer; (void)oper; (void)reason_code; (void)gfp;
}

void cfg80211_nan_match(struct wireless_dev *wdev, struct cfg80211_nan_match_params *match, gfp_t gfp)
{
    (void)wdev; (void)match; (void)gfp;
}

void cfg80211_nan_func_terminated(struct wireless_dev *wdev, u8 inst_id,
                                  enum nl80211_nan_func_term_reason reason, u64 cookie, gfp_t gfp)
{
    (void)wdev; (void)inst_id; (void)reason; (void)cookie; (void)gfp;
}
