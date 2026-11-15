/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef APX_CLIENT_H_
#define APX_CLIENT_H_

/*
 * apx_client.h - The application side of the driver's status block
 * (apx_status.h): finding it, sending commands, reading the scan list.
 * Task time only (Name Registry, TickCount).
 */
#include "apx_status.h"

/* The driver's status block, or NULL if the driver is not loaded (Open
 * Transport loads it while a TCP/IP or AppleTalk configuration uses the
 * card) or is another version. */
apx_status *apx_find(void);

/* Sends a command and waits until the driver took it (up to 1 s).
 * 1 = taken, *result = 0 or APX_ERR_*; 0 = the driver does not answer. */
int apx_send(apx_status *st, unsigned long cmd, unsigned long arg, const char *ssid,
             const char *password, long *result);

/* Consistent copy of the scan list; returns the number of entries */
unsigned long apx_copy_scan(apx_status *st, apx_network *out);

/* Scan list sorted for display: one entry per network name (the strongest
 * access point), hidden networks left out, strongest first */
unsigned long apx_networks(apx_status *st, apx_network *out);

/* Signal in bars, 0..4 (as the Mac OS 9 AirPort software shows it) */
int apx_bars(long signal_dbm);

#endif /* APX_CLIENT_H_ */
