/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef APX_STATUS_H_
#define APX_STATUS_H_

/*
 * apx_status.h - Status block shared by the driver and the applications.
 *
 * The driver allocates one apx_status in resident memory and publishes its
 * address in the Name Registry, as the APX_STATUS_PROPERTY property of the
 * card's node ("pci80211"). Mac OS 9 has a single address space, so an
 * application reads the property and then reads the structure directly.
 *
 * Only the driver writes it. The log is a ring buffer: log_head counts every
 * byte ever written, the byte number n is at log_buf[n % log_size].
 *
 * Standard C types only, so that applications can include it.
 */

#define APX_STATUS_PROPERTY "apx-status"
#define APX_STATUS_MAGIC    0x41505853UL        /* 'APXS' */
#define APX_STATUS_VERSION  2

/* Connection state (apx_status.link_state) */
enum {
    APX_LINK_DOWN = 0,          /* interface down: no client bound */
    APX_LINK_IDLE,              /* up, no network configured */
    APX_LINK_SCANNING,
    APX_LINK_CONNECTING,        /* authentication + association */
    APX_LINK_HANDSHAKE,         /* WPA 4-way handshake */
    APX_LINK_CONNECTED,         /* data flows */
    APX_LINK_RETRY              /* waiting before the next attempt */
};

typedef struct apx_status {
    unsigned long magic;            /* APX_STATUS_MAGIC */
    unsigned long version;          /* APX_STATUS_VERSION */

    /* Connection */
    unsigned long link_state;       /* APX_LINK_* */
    char          ssid[33];         /* configured network */
    unsigned char bssid[6];         /* current access point */
    unsigned char channel;
    long          signal_dbm;       /* from the last scan */
    unsigned char mac[6];           /* interface address */
    unsigned short last_reason;     /* last 802.11 status/reason code */
    unsigned long connects;         /* successful connections */

    /* Traffic */
    unsigned long tx_frames, tx_bytes, tx_drops;
    unsigned long rx_frames, rx_bytes, rx_drops;

    /* Memory pool of the Linux stack (bytes) */
    unsigned long mem_size, mem_used, mem_peak, mem_failures, mem_bad_frees;

    /* Private stack the Linux stack runs on (bytes) */
    unsigned long stack_size, stack_peak;

    /* Log ring buffer */
    unsigned long          log_size;
    volatile unsigned long log_head;
    char                  *log_buf;
} apx_status;

#endif /* APX_STATUS_H_ */
