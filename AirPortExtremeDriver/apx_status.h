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
 * The driver writes everything except the command fields. The log is a ring
 * buffer: log_head counts every byte ever written, the byte number n is at
 * log_buf[n % log_size].
 *
 * Commands (application -> driver), one at a time:
 *   1. wait until cmd_done == cmd_seq (the previous command is finished);
 *   2. fill cmd, cmd_ssid, cmd_password;
 *   3. cmd_seq = cmd_seq + 1, last.
 * The driver takes the command at its next tick (10 ms), wipes
 * cmd_password, sets cmd_result and then cmd_done = cmd_seq. If cmd_done
 * does not follow within a second, the driver is not running: Open Transport
 * loads it only while a TCP/IP or AppleTalk configuration uses the card.
 * The command only starts the work: the progress is in link_state and
 * scan_seq.
 *
 * Scan results: the driver increments scan_seq before and after replacing
 * the list, so it is odd while the list changes. Read scan_seq (even), copy
 * the list, read scan_seq again: if it changed, copy again. The driver runs
 * at deferred task time and can interrupt an application at any point.
 *
 * Standard C types only, so that applications can include it.
 */

#define APX_STATUS_PROPERTY "apx-status"
#define APX_STATUS_MAGIC    0x41505853UL        /* 'APXS' */
#define APX_STATUS_VERSION  4

/* Commands (apx_status.cmd) */
enum {
    APX_CMD_NONE = 0,
    APX_CMD_SCAN,               /* scan all channels: new scan list */
    APX_CMD_CONNECT,            /* join cmd_ssid (cmd_password: "" = open
                                   network), reconnect to it when lost;
                                   turns AirPort on if it is off */
    APX_CMD_DISCONNECT,         /* leave the network, stay idle */
    APX_CMD_POWER               /* cmd_arg 0 = AirPort off (radio off, as
                                   "Turn AirPort Off"), 1 = on */
};

/* apx_status.cmd_result: 0 or a negative errno value, as in b43os9.h */
#define APX_ERR_INVAL       (-22)       /* bad command or argument */
#define APX_ERR_NETDOWN     (-100)      /* interface down (no client bound,
                                           or AirPort off) */

/* Security of a network (apx_network.security) */
enum {
    APX_SEC_OPEN = 0,
    APX_SEC_WEP,                /* not supported by the driver */
    APX_SEC_WPA,                /* WPA (TKIP), PSK */
    APX_SEC_WPA2                /* WPA2 (RSN), PSK */
};

#define APX_SCAN_MAX 32

/* Keeps the compiler from moving memory accesses across it: between filling
 * the fields and incrementing a sequence number, on both sides. Mac OS 9 has
 * one processor, so nothing more is needed. */
#ifdef __GNUC__
#define APX_BARRIER()   __asm__ __volatile__("" ::: "memory")
#else
#define APX_BARRIER()   do { } while (0)
#endif

/* An access point found by the last scan */
typedef struct apx_network {
    char          ssid[33];             /* zero-terminated, "" = hidden */
    unsigned char bssid[6];
    unsigned char channel;
    unsigned char security;             /* APX_SEC_* */
    long          signal_dbm;
} apx_network;

/* Connection state (apx_status.link_state) */
enum {
    APX_LINK_DOWN = 0,          /* interface down: no client bound */
    APX_LINK_IDLE,              /* up, not joining a network (none
                                   configured, disconnected, or failed:
                                   see link_error) */
    APX_LINK_SCANNING,
    APX_LINK_CONNECTING,        /* authentication + association */
    APX_LINK_HANDSHAKE,         /* WPA 4-way handshake */
    APX_LINK_CONNECTED,         /* data flows */
    APX_LINK_RETRY,             /* waiting before the next attempt */
    APX_LINK_OFF                /* AirPort turned off (APX_CMD_POWER) */
};

/* Why the last attempt failed (apx_status.link_error); the driver keeps
 * trying in RETRY, and stops in IDLE for the errors marked "stops" */
enum {
    APX_FAIL_NONE = 0,
    APX_FAIL_NOT_FOUND,         /* the network was not in the scan */
    APX_FAIL_NEED_PASSWORD,     /* protected network, no password (stops) */
    APX_FAIL_UNSUPPORTED,       /* WEP or 802.1X network (stops) */
    APX_FAIL_REFUSED,           /* the AP refused us (last_reason) */
    APX_FAIL_NO_ANSWER,         /* the AP did not answer */
    APX_FAIL_HANDSHAKE,         /* WPA handshake not completed: wrong
                                   password, most of the time */
    APX_FAIL_LOST,              /* connection lost (last_reason) */
    APX_FAIL_ERROR              /* internal error, see the log */
};

/* The driver could not start (apx_status.driver_error): the log says why */
enum {
    APX_DRV_OK = 0,
    APX_DRV_NO_FIRMWARE,        /* the firmware files are missing or bad */
    APX_DRV_FAILED              /* the chip or the stack did not start */
};

/* The card, for problem reports (from b43os9_hwinfo); valid when
 * hw_valid is set, the firmware revision once the interface was up */
typedef struct apx_hw {
    unsigned short chip_id, chip_rev, chip_pkg;
    unsigned short core_rev;
    unsigned short board_vendor, board_type, board_rev;
    unsigned char  phy_type, phy_rev;
    unsigned short radio_manuf, radio_ver, radio_rev;
    unsigned char  sprom_rev;
    unsigned short boardflags_lo, boardflags_hi;
    unsigned short fw_rev, fw_patch;
} apx_hw;

typedef struct apx_status {
    unsigned long magic;            /* APX_STATUS_MAGIC */
    unsigned long version;          /* APX_STATUS_VERSION */
    char          driver_version[16];
    char          country[4];       /* regulatory domain, "00" = world */
    unsigned long driver_error;     /* APX_DRV_* */
    char          firmware[64];     /* where the firmware came from */
    unsigned long hw_valid;
    apx_hw        hw;

    /* Connection */
    unsigned long link_state;       /* APX_LINK_* */
    unsigned long link_error;       /* APX_FAIL_* of the last attempt */
    unsigned long power;            /* 1 = AirPort on */
    char          ssid[33];         /* configured network */
    unsigned char bssid[6];         /* current access point */
    unsigned char channel;
    unsigned char security;         /* APX_SEC_* of the current network */
    unsigned char mac[6];           /* interface address */
    unsigned short last_reason;     /* last 802.11 status/reason code */
    unsigned long connects;         /* successful connections */
    unsigned long link_seconds;     /* how long the connection has lasted */

    /* The access point while connected, updated every second (mac80211's
     * station data); signal from the scan otherwise */
    long          signal_dbm;       /* last frame */
    long          signal_avg_dbm;   /* average */
    unsigned long tx_rate, rx_rate; /* units of 100 kbit/s, 0 = unknown */

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

    /* Command mailbox (written by the application, see above) */
    volatile unsigned long cmd_seq;
    volatile unsigned long cmd_done;    /* written by the driver */
    unsigned long          cmd;         /* APX_CMD_* */
    unsigned long          cmd_arg;     /* APX_CMD_POWER: 0 / 1 */
    long                   cmd_result;  /* written by the driver */
    char                   cmd_ssid[33];
    char                   cmd_password[64];

    /* Last scan (written by the driver, see above) */
    volatile unsigned long scan_seq;
    unsigned long          scan_count;
    apx_network            scan[APX_SCAN_MAX];
} apx_status;

#endif /* APX_STATUS_H_ */
