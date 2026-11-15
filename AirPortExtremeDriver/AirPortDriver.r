/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * AirPortDriver.r - Resources of the AirPortExtreme extension (besides the
 * 'cfrg' that Retro68 adds): the bundle, so that the Finder shows its icon
 * (family 128, icon_driver.r).
 */
#include "MacTypes.r"
#include "Finder.r"

type 'APXd' as 'STR ';
resource 'APXd' (0) { "AirPort Extreme driver" };
resource 'FREF' (128) { 'ndrv', 0, "" };
resource 'BNDL' (128) {
    'APXd', 0,
    {
        'ICN#', { 0, 128 };
        'FREF', { 0, 128 };
    }
};
