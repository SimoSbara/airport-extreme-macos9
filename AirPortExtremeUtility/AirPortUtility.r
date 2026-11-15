/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * AirPortUtility.r - Resources of the "AirPort Extreme" application: the
 * dialogs (Appearance: theme background, controls), version, memory, the
 * Finder bundle. The menus, the main window and the About box are built in
 * code (AirPortUtility.c).
 *
 * MacRoman characters: \0xC9 ellipsis, \0xD2 \0xD3 curly double quotes.
 */
#include "Processes.r"
#include "Dialogs.r"
#include "Controls.r"
#include "MacTypes.r"
#include "Finder.r"

/* Bundle: the Finder takes the icon (family 128, icon_app.r) from here */
type 'APXu' as 'STR ';
resource 'APXu' (0) { "AirPort Extreme 1.0d1" };
resource 'FREF' (128) { 'APPL', 0, "" };
resource 'BNDL' (128) {
    'APXu', 0,
    {
        'ICN#', { 0, 128 };
        'FREF', { 0, 128 };
    }
};

#define kDialogFlags    0x0F    /* theme background, control hierarchy,
                                   movable modal, theme controls */
#define kEditTextProc       272     /* kControlEditTextProc */
#define kEditTextPassword   274     /* kControlEditTextPasswordProc */

resource 'vers' (1) {
    0x01, 0x00, development, 0x01, 0,
    "1.0d1",
    "1.0d1, AirPort Extreme utility - GPL-2.0"
};

resource 'SIZE' (-1) {
    reserved,
    acceptSuspendResumeEvents,
    reserved,
    canBackground,
    doesActivateOnFGSwitch,
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
    1024 * 1024,            /* preferred */
    512 * 1024              /* minimum */
};

/* --- Password for a network in the list (200) --------------------------- */
resource 'DLOG' (200) {
    {0, 0, 156, 380}, movableDBoxProc, invisible, noGoAway, 0x0, 200, "",
    alertPositionParentWindowScreen
};
resource 'dlgx' (200) { versionZero { kDialogFlags } };
resource 'DITL' (200) {
    {
        {122, 290, 142, 366}, Button { enabled, "Join" };
        {122, 200, 142, 276}, Button { enabled, "Cancel" };
        {12, 20, 46, 366}, StaticText { disabled, "^0" };
        {58, 22, 74, 364}, Control { enabled, 200 };
        {88, 20, 106, 366}, CheckBox { enabled, "Remember this network" };
    }
};
resource 'CNTL' (200) {
    {58, 22, 74, 364}, 0, visible, 0, 0, kEditTextPassword, 0, ""
};

/* --- Join a network that is not in the list (201) ----------------------- */
resource 'DLOG' (201) {
    {0, 0, 206, 380}, movableDBoxProc, invisible, noGoAway, 0x0, 201, "",
    alertPositionParentWindowScreen
};
resource 'dlgx' (201) { versionZero { kDialogFlags } };
resource 'DITL' (201) {
    {
        {172, 290, 192, 366}, Button { enabled, "Join" };
        {172, 200, 192, 276}, Button { enabled, "Cancel" };
        {12, 20, 44, 366}, StaticText { disabled,
            "Type the name of the network and, if it is protected, its WPA password." };
        {58, 20, 74, 110}, StaticText { disabled, "Network name:" };
        {58, 118, 74, 364}, Control { enabled, 201 };
        {90, 20, 106, 110}, StaticText { disabled, "Password:" };
        {90, 118, 106, 364}, Control { enabled, 202 };
        {112, 118, 128, 366}, StaticText { disabled, "Leave it empty for an open network." };
        {138, 118, 156, 366}, CheckBox { enabled, "Remember this network" };
    }
};
resource 'CNTL' (201) {
    {58, 118, 74, 364}, 0, visible, 0, 0, kEditTextProc, 0, ""
};
resource 'CNTL' (202) {
    {90, 118, 106, 364}, 0, visible, 0, 0, kEditTextPassword, 0, ""
};

/* --- Messages: one OK button, the text in ^0 (128) ----------------------- */
resource 'ALRT' (128) {
    {0, 0, 120, 380}, 128,
    { OK, visible, sound1; OK, visible, sound1; OK, visible, sound1; OK, visible, sound1 },
    alertPositionParentWindowScreen
};
resource 'DITL' (128) {
    {
        {86, 290, 106, 366}, Button { enabled, "OK" };
        {12, 74, 76, 366}, StaticText { disabled, "^0" };
    }
};
resource 'alrx' (128) { versionOne { 0x0F, 0, kUseThemeWindow, "" } };

/* The About box is drawn in code (AirPortUtility.c) */
