/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * AirPortStartup.r - Resources of "AirPort Extreme Startup".
 */
#include "Processes.r"
#include "MacTypes.r"
#include "Finder.r"

/* Bundle: the Finder takes the icon (family 128, icon_startup.r) from here */
type 'APXs' as 'STR ';
resource 'APXs' (0) { "AirPort Extreme Startup 1.0d1" };
resource 'FREF' (128) { 'APPL', 0, "" };
resource 'BNDL' (128) {
    'APXs', 0,
    {
        'ICN#', { 0, 128 };
        'FREF', { 0, 128 };
    }
};

resource 'vers' (1) {
    0x01, 0x00, development, 0x01, 0,
    "1.0d1",
    "1.0d1, joins the AirPort Extreme network at startup - GPL-2.0"
};

resource 'SIZE' (-1) {
    reserved,
    ignoreSuspendResumeEvents,
    reserved,
    canBackground,
    needsActivateOnFGSwitch,
    backgroundAndForeground,
    dontGetFrontClicks,
    ignoreChildDiedEvents,
    is32BitCompatible,
    notHighLevelEventAware,
    onlyLocalHLEvents,
    notStationeryAware,
    dontUseTextEditServices,
    reserved,
    reserved,
    reserved,
    256 * 1024,             /* preferred */
    256 * 1024              /* minimum */
};
