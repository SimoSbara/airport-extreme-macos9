/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * drv_screen.c - The log, drawn on the screen (screen_log=1 in the prefs).
 *
 * A freeze takes the log in memory with it: AirPortStatus can no longer be
 * run to read it. What was drawn on the screen stays there, to be
 * photographed. The text is written straight into the frame buffer of the
 * main screen: plain memory, writable at any execution level, no QuickDraw.
 *
 * The text wraps around: after the last row it goes on from the first one.
 * The rows after the newest line are kept blank, so the newest line is the
 * one just above the blank rows (and the time stamps give the order).
 */
#include <MacTypes.h>
#include <Quickdraw.h>

#include "drv.h"
#include "drv_font8x16.h"

#define MAIN_DEVICE     0x08A4          /* low-memory global MainDevice (GDHandle) */
#define TOP_MARGIN      24              /* the menu bar stays visible */
#define MAX_COLS        160

/* The layouts of the system (68K alignment), whatever the compiler options */
_Static_assert(__builtin_offsetof(GDevice, gdPMap) == 22, "GDevice: system layout");
_Static_assert(__builtin_offsetof(PixMap, pixelSize) == 32, "PixMap: system layout");

static unsigned char *g_base;           /* first pixel of the text area */
static unsigned long  g_row_bytes;
static int            g_depth;          /* bits per pixel: 8, 16 or 32; 0 = off */
static int            g_cols, g_rows;
static int            g_col, g_row;

/* Black on white in every depth (8 bits: Mac palettes have white at 0 and
 * black at 255) */
static void put_glyph(int col, int row, unsigned char c)
{
    unsigned char *line = g_base + (unsigned long)row * DRV_FONT_H * g_row_bytes;
    const unsigned char *glyph;
    int x, y;

    if (c < DRV_FONT_FIRST || c > DRV_FONT_LAST)
        c = '?';
    glyph = &drv_font8x16[(c - DRV_FONT_FIRST) * DRV_FONT_H];
    for (y = 0; y < DRV_FONT_H; y++, line += g_row_bytes) {
        unsigned char bits = glyph[y];

        if (g_depth == 8) {
            volatile unsigned char *p = line + col * DRV_FONT_W;
            for (x = 0; x < DRV_FONT_W; x++)
                p[x] = (bits & (0x80 >> x)) ? 0xFF : 0x00;
        } else if (g_depth == 16) {
            volatile unsigned short *p = (volatile unsigned short *)line + col * DRV_FONT_W;
            for (x = 0; x < DRV_FONT_W; x++)
                p[x] = (bits & (0x80 >> x)) ? 0x0000 : 0x7FFF;
        } else {
            volatile unsigned long *p = (volatile unsigned long *)line + col * DRV_FONT_W;
            for (x = 0; x < DRV_FONT_W; x++)
                p[x] = (bits & (0x80 >> x)) ? 0x00000000UL : 0x00FFFFFFUL;
        }
    }
}

static void clear_row(int row)
{
    int col;

    for (col = 0; col < g_cols; col++)
        put_glyph(col, row, ' ');
}

static void new_line(void)
{
    g_col = 0;
    g_row = (g_row + 1) % g_rows;
    clear_row(g_row);
    clear_row((g_row + 1) % g_rows);
}

void drv_screen_putc(char c)
{
    if (!g_depth)
        return;
    if (c == '\n') {
        new_line();
        return;
    }
    if (g_col >= g_cols)
        new_line();                     /* long line: continues below */
    put_glyph(g_col++, g_row, (unsigned char)c);
}

/* Task time. 0 = the log is drawn from now on. */
int drv_screen_init(void)
{
    GDHandle gd = *(GDHandle *)MAIN_DEVICE;
    PixMapHandle pm;
    long width, height;
    int depth;

    if (!gd || !*gd)
        return -1;
    pm = (**gd).gdPMap;
    if (!pm || !*pm || !(**pm).baseAddr)
        return -1;
    depth = (**pm).pixelSize;
    width = (**pm).bounds.right - (**pm).bounds.left;
    height = (**pm).bounds.bottom - (**pm).bounds.top - TOP_MARGIN;
    if ((depth != 8 && depth != 16 && depth != 32) || width < 320 || height < 10 * DRV_FONT_H)
        return -1;
    g_row_bytes = (unsigned long)((**pm).rowBytes & 0x3FFF);
    g_base = (unsigned char *)(**pm).baseAddr + TOP_MARGIN * g_row_bytes;
    g_cols = (int)(width / DRV_FONT_W);
    if (g_cols > MAX_COLS)
        g_cols = MAX_COLS;
    g_rows = (int)(height / DRV_FONT_H);
    g_col = 0;
    g_row = 0;
    g_depth = depth;
    clear_row(0);
    clear_row(1);
    return 0;
}
