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
#include "main.h"
#include "xmit.h"

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

/* =========================================================================
 * Radio setup of Linux 3.2 (experiment)
 *
 * For this card (G PHY rev 2, radio 2050 rev 2) the b43 of Linux 3.2, which
 * works well on the iBook, leaves three values different from 5.15:
 *   radio 0x7C         0x20 (b43_phy_initb6; 5.15 writes radio 0x50 instead)
 *   PHY 0x5D           low 7 bits = 3 (b43_phy_initb6: 3.2 does it for radio
 *                      rev <= 5, 5.15 only for rev 4 and 5)
 *   table ORIGTR[0]    0xC480 (b43_wa_tr_ltov, the T/R switch table; 5.15
 *                      writes 0x7654)
 * This writes the 3.2 values after b43 has initialized the PHY, to find out
 * whether they are what makes the AP hear our frames. b43 itself is not
 * changed; the calibrations it ran during init used the 5.15 values.
 * 0 = already set, 1 = applied now, < 0 = not running or another card.
 * ========================================================================= */
int b43os9_radio_setup_3_2(b43os9 *b)
{
    struct ieee80211_hw *hw;
    struct b43_wl *wl;
    struct b43_wldev *dev;
    struct b43_phy *phy;
    u16 tr, r7c, p5d;

    if (!b->dev)
        return -ENODEV;
    hw = wiphy_to_ieee80211_hw(b->dev->ieee80211_ptr->wiphy);
    wl = hw_to_b43_wl(hw);
    mutex_lock(&wl->mutex);
    dev = wl->current_dev;
    if (!dev || b43_status(dev) < B43_STAT_STARTED) {
        mutex_unlock(&wl->mutex);
        return -ENODEV;
    }
    phy = &dev->phy;
    if (phy->type != B43_PHYTYPE_G || phy->rev != 2 || phy->radio_ver != 0x2050 ||
        phy->radio_rev > 2) {
        mutex_unlock(&wl->mutex);
        return -EINVAL;
    }
    b43_mac_suspend(dev);
    tr = b43_gtab_read(dev, B43_GTAB_ORIGTR, 0);
    r7c = b43_radio_read16(dev, 0x7C);
    p5d = b43_phy_read(dev, 0x5D);
    if (tr == 0xC480 && r7c == 0x20 && (p5d & 0x007F) == 0x0003) {
        b43_mac_enable(dev);
        mutex_unlock(&wl->mutex);
        return 0;
    }
    b43_gtab_write(dev, B43_GTAB_ORIGTR, 0, 0xC480);
    b43_radio_write16(dev, 0x7C, 0x20);
    b43_phy_maskset(dev, 0x5D, 0xFF80, 0x0003);
    printk("b43os9: radio setup of Linux 3.2: ORIGTR %04x -> %04x, radio 7C %04x -> %04x,"
           " PHY 5D %04x -> %04x\n", tr, b43_gtab_read(dev, B43_GTAB_ORIGTR, 0), r7c,
           b43_radio_read16(dev, 0x7C), p5d, b43_phy_read(dev, 0x5D));
    b43_mac_enable(dev);
    mutex_unlock(&wl->mutex);
    return 1;
}

/* =========================================================================
 * Board flags (the SPROM as b43 sees it)
 *
 * b43 decides some of the setup from the board flags of the SPROM, read at
 * every interface-up, and corrects them itself for boards whose SPROM is
 * wrong (b43_sprom_fixup). This sets more flags the same way, before the
 * interface goes up, for what the host needs.
 *
 * B43OS9_BFL_XTAL_NOSLOW ("no slow clock available"): b43 then tells the
 * firmware not to ask for the slow clock when idle (host flag DSCRQ) and
 * keeps the fast clock on. On this card (chipcommon rev 4, PCI core rev 9)
 * the slow clock is the PCI clock, which belongs to the host.
 * ========================================================================= */
int b43os9_set_boardflags(b43os9 *b, unsigned short set)
{
    struct ieee80211_hw *hw;
    struct b43_wl *wl;
    struct b43_wldev *dev;
    u16 old;

    if (!b->dev)
        return -ENODEV;
    hw = wiphy_to_ieee80211_hw(b->dev->ieee80211_ptr->wiphy);
    wl = hw_to_b43_wl(hw);
    mutex_lock(&wl->mutex);
    dev = wl->current_dev;
    if (!dev || !dev->dev->bus_sprom) {
        mutex_unlock(&wl->mutex);
        return -ENODEV;
    }
    old = dev->dev->bus_sprom->boardflags_lo;
    dev->dev->bus_sprom->boardflags_lo = old | set;
    printk("b43os9: board flags %04x -> %04x\n", old, dev->dev->bus_sprom->boardflags_lo);
    mutex_unlock(&wl->mutex);
    return 0;
}

/* =========================================================================
 * Receiver address of the ACK and CTS frames
 *
 * The firmware sends its ACK/CTS from a 16-byte template (template RAM
 * offset 0); the transmit modify engine replaces each 16-bit word of it with
 * a value, in the bits where the word's mask register has ones. The receiver
 * address is words 10, 12 and 14: the firmware loads the values (the sender
 * of the frame it answers), the masks are registers 0x58A, 0x58C and 0x58E.
 * The initial values (b0g0initvals5) set the masks of words 0 to 8 only, and
 * b43's dummy transmission leaves zeros in the address part of the template.
 *
 * On a PowerBook6,1 (BCM4306 rev 3, core rev 5, firmware 784.2) the three
 * masks held a fixed pattern instead of all ones: every ACK went out on time
 * with the receiver address ANDed with that pattern (seen in a capture from
 * a second machine), so the access point never got one, sent every frame
 * again and stayed at 1 Mbit/s, or did not start the WPA handshake at all.
 * With 0xFFFF in the masks the ACKs are addressed correctly.
 *
 * Called after the interface is up: b43_chip_init has loaded the initial
 * values by then.
 * ========================================================================= */
void b43os9_ack_address_masks(b43os9 *b)
{
    struct ieee80211_hw *hw;
    struct b43_wl *wl;
    struct b43_wldev *dev;

    if (!b->dev)
        return;
    hw = wiphy_to_ieee80211_hw(b->dev->ieee80211_ptr->wiphy);
    wl = hw_to_b43_wl(hw);
    mutex_lock(&wl->mutex);
    dev = wl->current_dev;
    if (dev && b43_status(dev) >= B43_STAT_STARTED &&
        dev->phy.type == B43_PHYTYPE_G && dev->dev->core_rev == 5) {
        b43_write16(dev, 0x58A, 0xFFFF);
        b43_write16(dev, 0x58C, 0xFFFF);
        b43_write16(dev, 0x58E, 0xFFFF);
    }
    mutex_unlock(&wl->mutex);
}
