/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * AirPortTester.r - Tester resources.
 *
 * Memory partition larger than the Retro68 default (1 MB):
 * the Linux stack allocates packet queues, station tables, firmware.
 */
#include "Processes.r"
#include "CodeFragments.r"

/*
 * 512 KB stack: Linux kernel code (mac80211, cfg80211) uses much
 * more stack than a typical Mac program. The "stack size" field of the
 * application's 'cfrg' is what Mac OS uses at launch (0 = default,
 * too small). Replaces the 'cfrg' from RetroPPCAPPL.r.
 */
resource 'cfrg' (0) {
    {
        kPowerPCCFragArch, kIsCompleteCFrag, kNoVersionNum, kNoVersionNum,
        512 * 1024, kNoAppSubFolder,
        kApplicationCFrag, kDataForkCFragLocator, kZeroOffset, kCFragGoesToEOF,
        "AirPortTester"
    }
};

resource 'SIZE' (-1) {
    reserved,
    ignoreSuspendResumeEvents,
    reserved,
    cannotBackground,
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
    16 * 1024 * 1024,       /* preferred */
    8 * 1024 * 1024         /* minimum */
};
