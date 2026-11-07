/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef B43COMPAT_CW_H_
#define B43COMPAT_CW_H_

/*
 * b43compat_cw.h - Linux headers with an empty struct.
 *
 * When an option is disabled, some Linux headers define an empty struct
 * ("struct x { };", a GCC extension). CodeWarrior considers it incomplete and
 * does not allow it to be used as a member of another struct.
 *
 * This file holds the "option disabled" branch of those headers, identical
 * except for one member that is never used. The original header's include
 * guard is defined, so the original is no longer read.
 * Included by b43compat.h, i.e. before any Linux header.
 */

struct ssb_device;
struct b43_wl;
struct b43_wldev;

/* include/linux/ssb/ssb_driver_mips.h without CONFIG_SSB_DRIVER_MIPS */
#ifndef CONFIG_SSB_DRIVER_MIPS
#define LINUX_SSB_MIPSCORE_H_

struct ssb_mipscore {
    int b43c_unused;
};

static inline void ssb_mipscore_init(struct ssb_mipscore *mcore)
{
    (void)mcore;
}

static inline unsigned int ssb_mips_irq(struct ssb_device *dev)
{
    (void)dev;
    return 0;
}
#endif

/* drivers/net/wireless/broadcom/b43/leds.h without CONFIG_B43_LEDS */
#ifndef CONFIG_B43_LEDS
#define B43_LEDS_H_

struct b43_leds {
    int b43c_unused;
};

static inline void b43_leds_register(struct b43_wldev *dev) { (void)dev; }
static inline void b43_leds_unregister(struct b43_wl *wl) { (void)wl; }
static inline void b43_leds_init(struct b43_wldev *dev) { (void)dev; }
static inline void b43_leds_exit(struct b43_wldev *dev) { (void)dev; }
static inline void b43_leds_stop(struct b43_wldev *dev) { (void)dev; }
#endif

#endif /* B43COMPAT_CW_H_ */
