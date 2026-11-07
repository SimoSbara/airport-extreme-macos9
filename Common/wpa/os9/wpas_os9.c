/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * wpas_os9.c - wpa_supplicant on Mac OS 9: glue between the RSN supplicant
 * (src/rsn_supp, unmodified) and the driver (b43os9.h).
 *
 * For a WPA/WPA2-PSK network it does what, in wpa_supplicant, is done by
 * wpa_supplicant/wpas_glue.c (wpa_sm callbacks), events.c (association
 * events) and driver_nl80211.c (keys, port, EAPOL):
 *   - PMK = PBKDF2-SHA1(passphrase, SSID, 4096)         (wpa_passphrase)
 *   - RSN/WPA IE of the association request             (wpa_sm_set_assoc_wpa_ie_default)
 *   - association -> wpa_sm_notify_assoc, EAPOL -> wpa_sm_rx_eapol
 *   - PTK/GTK keys -> b43os9_add_key (NL80211_CMD_NEW_KEY)
 *   - COMPLETED state -> b43os9_authorize (802.1X port open)
 */
#include "includes.h"
#include "common.h"
#include "eloop.h"
#include "crypto/sha1.h"
#include "common/defs.h"
#include "common/eapol_common.h"
#include "common/wpa_common.h"
#include "rsn_supp/wpa.h"
#include "rsn_supp/wpa_ie.h"
#include "b43os9_wpa.h"

void eloop_os9_poll(void);

struct wpas_os9 {
    b43os9 *b;
    struct wpa_sm *sm;
    enum wpa_states state;
    u8 own_addr[ETH_ALEN];
    u8 bssid[ETH_ALEN];
    u8 ssid[SSID_MAX_LEN];
    size_t ssid_len;
    u8 psk[PMK_LEN];
    u8 assoc_ie[80];
    size_t assoc_ie_len;
    u8 ap_ies[512];
    size_t ap_ies_len;
    /* EAPOL received before the association event (like
     * wpa_s->pending_eapol_rx in wpa_supplicant) */
    u8 pending[512];
    size_t pending_len;
    u8 pending_src[ETH_ALEN];
};

static struct wpas_os9 g_wpas;

/* =========================================================================
 * wpa_sm callbacks (wpas_glue.c)
 * ========================================================================= */
static void os9_set_state(void *ctx, enum wpa_states state)
{
    struct wpas_os9 *w = ctx;

    wpa_printf(MSG_DEBUG, "wpas_os9: state %d -> %d", w->state, state);
    w->state = state;
    if (state == WPA_COMPLETED) {
        /* wpa_supplicant_set_state(COMPLETED) -> wpa_drv_set_supp_port(1) */
        b43os9_authorize(w->b, w->bssid);
        b43os9_supp_authorized(w->b, w->bssid);
    }
}

static enum wpa_states os9_get_state(void *ctx)
{
    return ((struct wpas_os9 *)ctx)->state;
}

static void os9_deauthenticate(void *ctx, u16 reason_code)
{
    struct wpas_os9 *w = ctx;
    wpa_printf(MSG_INFO, "wpas_os9: deauthentication, reason %u", reason_code);
    b43os9_disconnect(w->b, reason_code);
}

static void os9_reconnect(void *ctx)
{
    (void)ctx;
}

static unsigned long alg_to_cipher(enum wpa_alg alg, size_t key_len)
{
    switch (alg) {
    case WPA_ALG_WEP:
        return key_len == 5 ? B43OS9_CIPHER_WEP40 : B43OS9_CIPHER_WEP104;
    case WPA_ALG_TKIP:
        return B43OS9_CIPHER_TKIP;
    case WPA_ALG_CCMP:
        return B43OS9_CIPHER_CCMP;
    case WPA_ALG_BIP_CMAC_128:
        return B43OS9_CIPHER_AES_CMAC;
    default:
        return 0;
    }
}

static int os9_set_key(void *ctx, int link_id, enum wpa_alg alg,
                       const u8 *addr, int key_idx, int set_tx,
                       const u8 *seq, size_t seq_len,
                       const u8 *key, size_t key_len, enum key_flag key_flag)
{
    struct wpas_os9 *w = ctx;
    unsigned long cipher = alg_to_cipher(alg, key_len);
    int pairwise = (key_flag & KEY_FLAG_PAIRWISE) ? 1 : 0;
    int ret;

    (void)link_id;
    (void)set_tx;
    if (alg == WPA_ALG_NONE)
        return 0;       /* removal: done by mac80211 on disassociation */
    if (!cipher) {
        wpa_printf(MSG_ERROR, "wpas_os9: cipher %d not supported", alg);
        return -1;
    }
    ret = b43os9_add_key(w->b, key_idx, pairwise, pairwise ? addr : NULL, cipher,
                         key, (unsigned int)key_len, seq, (unsigned int)seq_len);
    wpa_printf(MSG_DEBUG, "wpas_os9: %s key idx %d alg %d len %u -> %d",
               pairwise ? "pairwise" : "group", key_idx, alg,
               (unsigned int)key_len, ret);
    return ret;
}

static void * os9_get_network_ctx(void *ctx)
{
    return ctx;
}

static int os9_get_bssid(void *ctx, u8 *bssid)
{
    os_memcpy(bssid, ((struct wpas_os9 *)ctx)->bssid, ETH_ALEN);
    return 0;
}

/* Outgoing EAPOL: Ethernet II frame to the driver (l2_packet_send) */
static int os9_ether_send(void *ctx, const u8 *dest, u16 proto, const u8 *buf, size_t len)
{
    struct wpas_os9 *w = ctx;
    u8 *frame = os_malloc(len + 14);
    int ret;

    if (!frame)
        return -1;
    os_memcpy(frame, dest, ETH_ALEN);
    os_memcpy(frame + 6, w->own_addr, ETH_ALEN);
    WPA_PUT_BE16(frame + 12, proto);
    os_memcpy(frame + 14, buf, len);
    ret = b43os9_tx(w->b, frame, len + 14);
    os_free(frame);
    return ret < 0 ? -1 : 0;
}

/* AP's WPA/RSN IE from the scan table (wpa_supplicant_get_beacon_ie) */
static const u8 * find_ie(const u8 *ies, size_t len, u8 id, int wpa_vendor)
{
    while (len >= 2 && (size_t)ies[1] + 2 <= len) {
        if (ies[0] == id &&
            (!wpa_vendor || (ies[1] >= 4 && WPA_GET_BE32(ies + 2) == WPA_IE_VENDOR_TYPE)))
            return ies;
        len -= ies[1] + 2;
        ies += ies[1] + 2;
    }
    return NULL;
}

static int load_ap_ies(struct wpas_os9 *w)
{
    static b43os9_bss bss;
    unsigned int i;

    for (i = 0; b43os9_scan_result(w->b, i, &bss); i++) {
        if (os_memcmp(bss.bssid, w->bssid, ETH_ALEN) == 0) {
            os_memcpy(w->ap_ies, bss.ies, bss.ies_len);
            w->ap_ies_len = bss.ies_len;
            return 0;
        }
    }
    return -1;
}

static int os9_get_beacon_ie(void *ctx)
{
    struct wpas_os9 *w = ctx;
    const u8 *rsn, *wpa;

    if (load_ap_ies(w))
        return -1;
    rsn = find_ie(w->ap_ies, w->ap_ies_len, WLAN_EID_RSN, 0);
    wpa = find_ie(w->ap_ies, w->ap_ies_len, WLAN_EID_VENDOR_SPECIFIC, 1);
    wpa_sm_set_ap_rsn_ie(w->sm, rsn, rsn ? (size_t)rsn[1] + 2 : 0);
    wpa_sm_set_ap_wpa_ie(w->sm, wpa, wpa ? (size_t)wpa[1] + 2 : 0);
    return 0;
}

static void os9_cancel_auth_timeout(void *ctx)
{
    (void)ctx;
}

/* 802.1X header + data (wpa_alloc_eapol in wpas_glue.c) */
static u8 * os9_alloc_eapol(void *ctx, u8 type, const void *data, u16 data_len,
                            size_t *msg_len, void **data_pos)
{
    struct ieee802_1x_hdr *hdr;

    (void)ctx;
    *msg_len = sizeof(*hdr) + data_len;
    hdr = os_malloc(*msg_len);
    if (hdr == NULL)
        return NULL;
    hdr->version = 1;       /* eapol_version=1, wpa_supplicant default */
    hdr->type = type;
    hdr->length = host_to_be16(data_len);
    if (data)
        os_memcpy(hdr + 1, data, data_len);
    else
        os_memset(hdr + 1, 0, data_len);
    if (data_pos)
        *data_pos = hdr + 1;
    return (u8 *)hdr;
}

static int os9_add_pmkid(void *ctx, void *network_ctx, const u8 *bssid,
                         const u8 *pmkid, const u8 *fils_cache_id,
                         const u8 *pmk, size_t pmk_len, u32 pmk_lifetime,
                         u8 pmk_reauth_threshold, int akmp)
{
    (void)ctx; (void)network_ctx; (void)bssid; (void)pmkid; (void)fils_cache_id;
    (void)pmk; (void)pmk_len; (void)pmk_lifetime; (void)pmk_reauth_threshold; (void)akmp;
    return 0;
}

static int os9_remove_pmkid(void *ctx, void *network_ctx, const u8 *bssid,
                            const u8 *pmkid, const u8 *fils_cache_id)
{
    (void)ctx; (void)network_ctx; (void)bssid; (void)pmkid; (void)fils_cache_id;
    return 0;
}

static int os9_mlme_setprotection(void *ctx, const u8 *addr, int protection_type,
                                  int key_type)
{
    (void)ctx; (void)addr; (void)protection_type; (void)key_type;
    return 0;
}

/* =========================================================================
 * Events from the driver (events.c)
 * ========================================================================= */
static void supp_rx_eapol(void *ctx, const unsigned char *src,
                          const unsigned char *buf, unsigned long len);

static void supp_assoc(void *ctx, const unsigned char *bssid)
{
    struct wpas_os9 *w = ctx;

    os_memcpy(w->bssid, bssid, ETH_ALEN);
    wpa_printf(MSG_INFO, "wpas_os9: associated with " MACSTR ", WPA handshake", MAC2STR(bssid));
    os9_set_state(w, WPA_ASSOCIATED);
    wpa_sm_notify_assoc(w->sm, bssid);
    if (w->pending_len) {
        size_t len = w->pending_len;
        w->pending_len = 0;
        supp_rx_eapol(w, w->pending_src, w->pending, len);
    }
}

static void supp_disassoc(void *ctx)
{
    struct wpas_os9 *w = ctx;

    if (w->state == WPA_DISCONNECTED)
        return;
    wpa_sm_notify_disassoc(w->sm);
    os9_set_state(w, WPA_DISCONNECTED);
}

static void supp_rx_eapol(void *ctx, const unsigned char *src,
                          const unsigned char *buf, unsigned long len)
{
    struct wpas_os9 *w = ctx;

    if (w->state < WPA_ASSOCIATED) {
        /* wpa_supplicant_rx_eapol: EAPOL before the association event */
        if (len <= sizeof(w->pending)) {
            os_memcpy(w->pending, buf, len);
            w->pending_len = len;
            os_memcpy(w->pending_src, src, ETH_ALEN);
        }
        return;
    }
    if (w->state == WPA_ASSOCIATED)
        os9_set_state(w, WPA_4WAY_HANDSHAKE);
    wpa_sm_rx_eapol(w->sm, src, buf, len, FRAME_ENCRYPTION_UNKNOWN);
}

static void supp_poll(void *ctx)
{
    (void)ctx;
    eloop_os9_poll();
}

/* =========================================================================
 * WPA/WPA2-PSK connection
 * ========================================================================= */
static int wpas_init(struct wpas_os9 *w, b43os9 *b)
{
    struct wpa_sm_ctx *ctx;
    b43os9_supp_ops ops;

    if (w->sm)
        return 0;
    eloop_init();
    w->b = b;
    ctx = os_zalloc(sizeof(*ctx));
    if (!ctx)
        return -1;
    ctx->ctx = w;
    ctx->msg_ctx = w;
    ctx->set_state = os9_set_state;
    ctx->get_state = os9_get_state;
    ctx->deauthenticate = os9_deauthenticate;
    ctx->reconnect = os9_reconnect;
    ctx->set_key = os9_set_key;
    ctx->get_network_ctx = os9_get_network_ctx;
    ctx->get_bssid = os9_get_bssid;
    ctx->ether_send = os9_ether_send;
    ctx->get_beacon_ie = os9_get_beacon_ie;
    ctx->cancel_auth_timeout = os9_cancel_auth_timeout;
    ctx->alloc_eapol = os9_alloc_eapol;
    ctx->add_pmkid = os9_add_pmkid;
    ctx->remove_pmkid = os9_remove_pmkid;
    ctx->mlme_setprotection = os9_mlme_setprotection;
    w->sm = wpa_sm_init(ctx);
    if (!w->sm) {
        os_free(ctx);
        return -1;
    }

    os_memset(&ops, 0, sizeof(ops));
    ops.ctx = w;
    ops.assoc = supp_assoc;
    ops.disassoc = supp_disassoc;
    ops.rx_eapol = supp_rx_eapol;
    ops.poll = supp_poll;
    b43os9_set_supplicant(b, &ops);
    w->state = WPA_DISCONNECTED;
    return 0;
}

int b43os9_connect_psk(b43os9 *b, const char *ssid, const unsigned char *bssid,
                       const char *passphrase)
{
    struct wpas_os9 *w = &g_wpas;
    struct rsn_supp_config conf;
    struct wpa_ie_data ie;
    const u8 *rsn, *wpa;
    size_t plen;

    if (!ssid || !bssid || !passphrase)
        return -22;
    plen = os_strlen(passphrase);
    w->ssid_len = os_strlen(ssid);
    if (w->ssid_len == 0 || w->ssid_len > SSID_MAX_LEN || plen < 8 || plen > 63)
        return -22;
    if (wpas_init(w, b))
        return -12;

    os_memcpy(w->ssid, ssid, w->ssid_len);
    os_memcpy(w->bssid, bssid, ETH_ALEN);
    b43os9_get_mac(b, w->own_addr);
    w->pending_len = 0;

    /* AP elements: protocol and ciphers (wpa_supplicant_set_suites) */
    if (load_ap_ies(w)) {
        wpa_printf(MSG_ERROR, "wpas_os9: network " MACSTR " not in scan results",
                   MAC2STR(bssid));
        return -19;
    }
    rsn = find_ie(w->ap_ies, w->ap_ies_len, WLAN_EID_RSN, 0);
    wpa = find_ie(w->ap_ies, w->ap_ies_len, WLAN_EID_VENDOR_SPECIFIC, 1);
    os_memset(&ie, 0, sizeof(ie));
    if (rsn && wpa_parse_wpa_ie(rsn, (size_t)rsn[1] + 2, &ie) == 0) {
        ie.proto = WPA_PROTO_RSN;
    } else if (wpa && wpa_parse_wpa_ie(wpa, (size_t)wpa[1] + 2, &ie) == 0) {
        ie.proto = WPA_PROTO_WPA;
    } else {
        wpa_printf(MSG_ERROR, "wpas_os9: the network does not advertise WPA/WPA2");
        return -22;
    }
    if (!(ie.key_mgmt & WPA_KEY_MGMT_PSK)) {
        wpa_printf(MSG_ERROR, "wpas_os9: the network does not accept PSK (key_mgmt 0x%x)",
                   ie.key_mgmt);
        return -22;
    }

    /* PMK from the passphrase (wpa_passphrase / wpa_config_update_psk) */
    wpa_printf(MSG_INFO, "wpas_os9: computing the PSK (PBKDF2-SHA1, 4096 iterations)");
    if (pbkdf2_sha1(passphrase, w->ssid, w->ssid_len, 4096, w->psk, PMK_LEN))
        return -22;

    os_memset(&conf, 0, sizeof(conf));
    conf.network_ctx = w;
    conf.ssid = w->ssid;
    conf.ssid_len = w->ssid_len;
    wpa_sm_set_config(w->sm, &conf);
    wpa_sm_set_own_addr(w->sm, w->own_addr);
    wpa_sm_set_ssid(w->sm, w->ssid, w->ssid_len);

    wpa_sm_set_param(w->sm, WPA_PARAM_PROTO, ie.proto);
    wpa_sm_set_param(w->sm, WPA_PARAM_RSN_ENABLED, ie.proto == WPA_PROTO_RSN);
    wpa_sm_set_param(w->sm, WPA_PARAM_PAIRWISE,
                     (ie.pairwise_cipher & WPA_CIPHER_CCMP) ? WPA_CIPHER_CCMP : WPA_CIPHER_TKIP);
    wpa_sm_set_param(w->sm, WPA_PARAM_GROUP, ie.group_cipher);
    wpa_sm_set_param(w->sm, WPA_PARAM_KEY_MGMT, WPA_KEY_MGMT_PSK);
    wpa_sm_set_param(w->sm, WPA_PARAM_MGMT_GROUP, 0);
    wpa_sm_set_param(w->sm, WPA_PARAM_MFP, NO_MGMT_FRAME_PROTECTION);
    wpa_sm_set_ap_rsn_ie(w->sm, rsn, rsn ? (size_t)rsn[1] + 2 : 0);
    wpa_sm_set_ap_wpa_ie(w->sm, wpa, wpa ? (size_t)wpa[1] + 2 : 0);

    w->assoc_ie_len = sizeof(w->assoc_ie);
    if (wpa_sm_set_assoc_wpa_ie_default(w->sm, w->assoc_ie, &w->assoc_ie_len)) {
        wpa_printf(MSG_ERROR, "wpas_os9: unable to build the WPA/RSN element");
        return -22;
    }
    wpa_sm_set_pmk(w->sm, w->psk, PMK_LEN, NULL, NULL);

    os9_set_state(w, WPA_ASSOCIATING);
    return b43os9_connect(b, ssid, bssid, w->assoc_ie, (unsigned int)w->assoc_ie_len, 1);
}
