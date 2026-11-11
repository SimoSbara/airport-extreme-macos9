/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * b43os9_hw.c - Diagnostics: hardware data read from the b43 structures.
 *
 * This is the only glue file that uses the b43 headers: it lives in the
 * CodeWarrior B43 library, which has the b43 folder in its search paths (some
 * b43 headers have the same names as mac80211/cfg80211 ones).
 */
#include "b43os9_priv.h"
#include "b43.h"
#include "phy_g.h"
#include "lo.h"

/* =========================================================================
 * Diagnostics
 * ========================================================================= */
void b43os9_get_hwinfo(b43os9 *b, b43os9_hwinfo *info)
{
    struct ieee80211_hw *hw;
    struct b43_wl *wl;
    struct b43_wldev *dev;
    struct b43_bus_dev *bd;
    struct ssb_sprom *sp;
    struct b43_phy *phy;

    memset(info, 0, sizeof(*info));
    if (!b->dev)
        return;
    memcpy(info->mac, b->dev->dev_addr, ETH_ALEN);
    hw = wiphy_to_ieee80211_hw(b->dev->ieee80211_ptr->wiphy);
    wl = hw_to_b43_wl(hw);
    dev = wl->current_dev;
    if (!dev)
        return;
    bd = dev->dev;
    sp = bd->bus_sprom;
    phy = &dev->phy;
    info->chip_id = bd->chip_id;
    info->chip_rev = bd->chip_rev;
    info->chip_pkg = bd->chip_pkg;
    info->core_id = bd->core_id;
    info->core_rev = bd->core_rev;
    info->board_vendor = bd->board_vendor;
    info->board_type = bd->board_type;
    info->board_rev = bd->board_rev;
    info->phy_type = phy->type;
    info->phy_rev = phy->rev;
    info->phy_analog = phy->analog;
    info->radio_manuf = phy->radio_manuf;
    info->radio_ver = phy->radio_ver;
    info->radio_rev = phy->radio_rev;
    info->sprom_rev = sp->revision;
    info->boardflags_lo = sp->boardflags_lo;
    info->boardflags_hi = sp->boardflags_hi;
    info->ant_available_bg = sp->ant_available_bg;
    info->maxpwr_bg = sp->maxpwr_bg;
    info->pa0b0 = sp->pa0b0;
    info->pa0b1 = sp->pa0b1;
    info->pa0b2 = sp->pa0b2;
    info->itssi_bg = sp->itssi_bg;
    info->fw_rev = dev->fw.rev;
    info->fw_patch = dev->fw.patch;
    info->fw_hdr_format = dev->fw.hdr_format;
    info->desired_txpower = phy->desired_txpower;
    info->tx_ack_failures = wl->ieee_stats.dot11ACKFailureCount;
    info->tx_rts_failures = wl->ieee_stats.dot11RTSFailureCount;
    info->rx_fcs_errors = wl->ieee_stats.dot11FCSErrorCount;
    if (phy->type == B43_PHYTYPE_G && phy->g) {
        info->bbatt = phy->g->bbatt.att;
        info->rfatt = phy->g->rfatt.att;
        info->tx_control = phy->g->tx_control;
        if (phy->g->lo_control) {
            info->tx_bias = phy->g->lo_control->tx_bias;
            info->tx_magn = phy->g->lo_control->tx_magn;
        }
    }
}
