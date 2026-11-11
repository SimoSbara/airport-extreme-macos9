// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2026 SimoSbara */
/*
 * b43compat_random.c - Random numbers (linux/random.h).
 *
 * wpa_supplicant takes its WPA nonces (SNonce) from get_random_bytes()
 * through os_get_random(), so this must be a cryptographic generator.
 * Mac OS 9 has none, so it is built here, the way Linux's random.c does in
 * spirit:
 *
 *   entropy  the exact time (microseconds, absolute) of every interrupt
 *            poll, every value read from the card's registers (all received
 *            frames pass through them in PIO mode, beacons with their TSF
 *            included, as do the radio's noise and RSSI readings) and the
 *            frames delivered to the system;
 *   pool     256 bits, stirred with an add-rotate-xor mix;
 *   output   AES-256 in counter mode, keyed with the pool (Linux's own
 *            AES, lib/crypto/aes.c); after every request the generator
 *            re-keys itself from its own output, so earlier outputs cannot
 *            be recomputed from a later state.
 */
#include <linux/types.h>
#include <crypto/aes.h>
#include "b43compat.h"
#include "b43env.h"

extern const b43_env *b43compat_env;

u32 b43compat_entropy_acc;

static u32  g_pool[8];
static u32  g_pool_pos;
static u32  g_counter[4];
static u32  g_xs;               /* prandom_u32 state */

static u32 raw_time_us(void)
{
    if (!b43compat_env || !b43compat_env->time_us)
        return 0;
    return (u32)b43compat_env->time_us(b43compat_env->ctx);
}

static u32 rol32_(u32 v, unsigned int s)
{
    return (v << s) | (v >> (32 - s));
}

/* Adds one word to the pool and stirs it */
static void pool_mix(u32 w)
{
    u32 i = g_pool_pos++ & 7;
    u32 a = g_pool[i] ^ w, b = g_pool[(i + 3) & 7];

    a += rol32_(b, 11);
    b ^= rol32_(a, 7);
    a += b;
    g_pool[i] = a;
    g_pool[(i + 3) & 7] = b ^ rol32_(a, 19);
}

void b43compat_entropy_poll(void)
{
    pool_mix(raw_time_us());
    pool_mix(b43compat_entropy_acc);
}

void b43compat_entropy_data(const void *data, unsigned long len)
{
    const u8 *p = (const u8 *)data;
    u32 w = raw_time_us();

    while (len--)
        w = rol32_(w, 5) + *p++;
    pool_mix(w);
}

void get_random_bytes(void *buf, int nbytes)
{
    struct crypto_aes_ctx ctx;
    u8 *out = (u8 *)buf;
    u8 key[32], block[16];
    int i;

    b43compat_entropy_poll();
    for (i = 0; i < 8; i++) {
        key[i * 4 + 0] = (u8)(g_pool[i] >> 24);
        key[i * 4 + 1] = (u8)(g_pool[i] >> 16);
        key[i * 4 + 2] = (u8)(g_pool[i] >> 8);
        key[i * 4 + 3] = (u8)g_pool[i];
    }
    aes_expandkey(&ctx, key, sizeof(key));

    while (nbytes > 0) {
        int n = nbytes < 16 ? nbytes : 16;
        if (++g_counter[3] == 0 && ++g_counter[2] == 0)
            ++g_counter[1];
        aes_encrypt(&ctx, block, (const u8 *)g_counter);
        memcpy(out, block, (size_t)n);
        out += n;
        nbytes -= n;
    }

    /* Re-key: two more blocks replace the pool */
    for (i = 0; i < 2; i++) {
        int j;
        ++g_counter[3];
        aes_encrypt(&ctx, block, (const u8 *)g_counter);
        for (j = 0; j < 4; j++)
            g_pool[i * 4 + j] ^= ((u32)block[j * 4] << 24) | ((u32)block[j * 4 + 1] << 16) |
                                 ((u32)block[j * 4 + 2] << 8) | block[j * 4 + 3];
    }
    memzero_explicit(&ctx, sizeof(ctx));
    memzero_explicit(key, sizeof(key));
    memzero_explicit(block, sizeof(block));
}

/* Non-cryptographic (sequence numbers, jitter): xorshift, seeded from the
 * generator above */
u32 prandom_u32(void)
{
    if (!g_xs) {
        get_random_bytes(&g_xs, sizeof(g_xs));
        g_xs |= 1;
    }
    g_xs ^= g_xs << 13;
    g_xs ^= g_xs >> 17;
    g_xs ^= g_xs << 5;
    return g_xs;
}
