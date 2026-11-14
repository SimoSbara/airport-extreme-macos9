/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
#ifndef AIRPORT_TESTER_H_
#define AIRPORT_TESTER_H_

/*
 * Tester configuration. The actual driver is in Common/: the unmodified
 * Linux 5.15 wireless stack (b43, ssb, mac80211, cfg80211), the
 * compatibility layer (Common/compat) and the Mac OS 9 glue (Common/os9,
 * b43os9.h interface).
 */

/* Network to connect to */
#define TEST_SSID               "Home"
/* Default WPA/WPA2-PSK passphrase (8..63 characters). Better to leave it
 * empty: the Tester asks for it at startup, so it does not end up in the program. */
#define TEST_PASSPHRASE         ""

/* Country for the radio rules (channels and power), like "iw reg set IT".
 * NULL = world domain "00" (channels 1-11 active, 12-13 listen only) */
#define TEST_REGDOM             "IT"

/* 1 = use a random "locally administered" MAC (02:0D:93:xx:xx:xx) instead
 * of the SPROM one: avoids being blacklisted by the AP after too many WPA2
 * handshakes left half-done. */
#define TEST_RANDOM_MAC         1

/* b43 log level (verbose parameter): 0 errors, 1 warnings, 2 info,
 * 3 debug. 1 = Linux default */
#define TEST_B43_VERBOSE        2
/* 1 = also the pr_debug messages of ssb/mac80211/cfg80211 (lots of output) */
#define TEST_PR_DEBUG           0

/* Timeouts (ms) */
#define TEST_SCAN_TIMEOUT_MS    15000
#define TEST_CONNECT_TIMEOUT_MS 10000
/* WPA 4-way handshake + group key */
#define TEST_HANDSHAKE_TIMEOUT_MS 10000

/* wpa_supplicant log: 0 = MSG_EXCESSIVE ... 2 = MSG_DEBUG, 3 = MSG_INFO */
#define TEST_WPA_DEBUG_LEVEL    2

/* After association: how long to listen for data frames */
#define TEST_DATA_LISTEN_MS     5000

/* DHCP Discover attempts and wait for the Offer (open or WPA network) */
#define TEST_DHCP_TRIES         3
#define TEST_DHCP_WAIT_MS       2000

/* Link quality test: ARP requests to the router and the pause between them */
#define TEST_ARP_REQUESTS       200
#define TEST_ARP_INTERVAL_MS    50

/* Load test: pings to the router, their data size and the pause between them */
#define TEST_PING_COUNT         1000
#define TEST_PING_BYTES         1400
#define TEST_PING_INTERVAL_MS   5

/* Register record (diagnostics): entries of 12 bytes each */
#define TEST_IOTRACE_ENTRIES    250000UL

/* 1 = print every received frame */
#define TEST_VERBOSE_RX         0

/* Extra space for the application stack (kernel code uses more of it
 * than a typical Mac program) */
#define TEST_EXTRA_STACK        (256UL * 1024UL)

/* Memory: 1 = the driver's allocator (AirPortExtremeDriver/drv_pool.c) on a
 * block of TEST_POOL_KB, so that the Tester behaves like the driver (freed
 * memory is reused at once and poisoned with 0x6B); 0 = malloc */
#define TEST_DRIVER_POOL        1
#define TEST_POOL_KB            4096

#endif /* AIRPORT_TESTER_H_ */
