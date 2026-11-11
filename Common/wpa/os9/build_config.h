/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * wpa_supplicant - Build configuration for Mac OS 9
 *
 * Replaces src/utils/build_config.h: it is the file wpa_supplicant provides
 * for defining the Makefile options when building from an IDE (here
 * CodeWarrior). No wpa_supplicant source file is modified.
 *
 * Only the RSN supplicant (WPA/WPA2-PSK, src/rsn_supp) is used, with the
 * internal crypto; the rest of wpa_supplicant (configuration, EAP,
 * ctrl_iface, nl80211 driver) is not needed: the glue is os9/wpas_os9.c.
 */
#ifndef BUILD_CONFIG_H
#define BUILD_CONFIG_H

#ifdef __MWERKS__
#pragma gcc_extensions on
/* The system headers used by includes.h, with normal alignment;
 * then packed structures as with GCC's __attribute__((packed))
 * (STRUCT_PACKED in common.h is empty outside GCC) */
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#include <sys/time.h>
#pragma options align=packed
#endif

/* Options (equivalent to the CONFIG_... lines of .config) */
#define CONFIG_CRYPTO_INTERNAL          /* sha1/md5/aes/rc4 in src/crypto */
#define CONFIG_NO_RANDOM_POOL           /* os_get_random() directly */
#define CONFIG_NO_CONFIG_BLOBS
#define CONFIG_NO_CONFIG_WRITE
#define CONFIG_NO_RADIUS
#define CONFIG_NO_VLAN
#define CONFIG_NO_ACCOUNTING
#define CONFIG_SHA256                   /* AKM PSK-SHA256 / 802.11w */
#ifdef __MWERKS__
/* wpa_dbg() is defined with a GNU "args..." macro that CodeWarrior expands
 * incorrectly: without wpa_msg() (needed by the control interface) the call
 * disappears. Debug messages remain active through wpa_printf(). */
#define CONFIG_NO_WPA_MSG
#endif

/* os_*() implemented in os9/os_os9.c (no libc in the driver) */
#define OS_NO_C_LIB_DEFINES

/*
 * Console output. utils/wpa_debug.c prints with printf()/vprintf() and
 * sizes its messages with vsnprintf(): all of it goes to the log of the
 * port instead (os9/os_os9.c and the formatter of Common/compat), which
 * also works in the Open Transport driver, where there is no stdio.
 * <stdio.h> comes first so that the libc prototypes keep their names, and
 * the macros take arguments so that PRINTF_FORMAT's "format (printf, ...)"
 * is left alone.
 */
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
int wpas_os9_printf(const char *fmt, ...);
int wpas_os9_vprintf(const char *fmt, va_list ap);
int b43c_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int b43c_snprintf(char *buf, size_t size, const char *fmt, ...);
#define printf(...)                 wpas_os9_printf(__VA_ARGS__)
#define vprintf(fmt, ap)            wpas_os9_vprintf(fmt, ap)
#define vsnprintf(b, s, fmt, ap)    b43c_vsnprintf(b, s, fmt, ap)
#define snprintf(...)               b43c_snprintf(__VA_ARGS__)
#define setvbuf(f, b, m, s)         ((void)0)

/* PowerPC: big-endian (utils/common.h does not recognize Mac OS 9) */
#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321
#define __BYTE_ORDER    __BIG_ENDIAN
#define bswap_16(a) ((unsigned short)((((a) & 0xFFU) << 8) | (((a) >> 8) & 0xFFU)))
#define bswap_32(a) ((((a) & 0xFFUL) << 24) | (((a) & 0xFF00UL) << 8) | \
                     (((a) >> 8) & 0xFF00UL) | (((a) >> 24) & 0xFFUL))
#define bswap_64(a) (((unsigned long long)bswap_32((unsigned long)(a)) << 32) | \
                     bswap_32((unsigned long)((a) >> 32)))

/*
 * Names already used by the Linux stack that wpa_supplicant is linked
 * with (lib/crypto/aes.c, lib/crypto/arc4.c): they are renamed here, for
 * all wpa_supplicant files, so the two do not clash.
 */
#define aes_encrypt             wpas_aes_encrypt
#define aes_decrypt             wpas_aes_decrypt
#define aes_encrypt_init        wpas_aes_encrypt_init
#define aes_decrypt_init        wpas_aes_decrypt_init
#define aes_encrypt_deinit      wpas_aes_encrypt_deinit
#define aes_decrypt_deinit      wpas_aes_decrypt_deinit
#define rc4_skip                wpas_rc4_skip

#endif /* BUILD_CONFIG_H */
