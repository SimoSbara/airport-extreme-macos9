/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 SimoSbara */
/*
 * b43compat_crypto.c - Kernel crypto API (crypto_alloc_aead/shash/...)
 * for the algorithms mac80211 requests, built on the Linux AES
 * (lib/crypto/aes.c, unmodified):
 *
 *   "ccm(aes)"  AEAD   CCMP (WPA2)                    RFC 3610 / crypto/ccm.c
 *   "cmac(aes)" shash  BIP, FILS (802.11w)            RFC 4493 / crypto/cmac.c
 *   "gcm(aes)", "ctr(aes)": not available (-ENOENT): GCMP and FILS are not
 *   supported by the BCM4306, and mac80211 only rejects those keys.
 *
 * Also CRC32 (lib/crc32.c) and crypto_xor (crypto/algapi.c).
 *
 * The BCM4306 encrypts/decrypts CCMP in hardware (b43 nohwcrypt=0): this code
 * is needed when mac80211 works in software (protected management frames,
 * recovery after errors, before the key is in the chip).
 */
#include <linux/types.h>
#include <crypto/aes.h>
#include "b43compat_os9.h"

/* =========================================================================
 * Scatterlist: the entries are contiguous in the array (sg_init_table)
 * ========================================================================= */
void sg_init_table(struct scatterlist *sgl, unsigned int nents)
{
    memset(sgl, 0, sizeof(*sgl) * nents);
}

void sg_set_buf(struct scatterlist *sg, const void *buf, unsigned int buflen)
{
    sg->b43c_buf = (void *)buf;
    sg->length = buflen;
    sg->offset = 0;
}

void sg_init_one(struct scatterlist *sg, const void *buf, unsigned int buflen)
{
    sg_init_table(sg, 1);
    sg_set_buf(sg, buf, buflen);
}

/* Copy len bytes from/to logical position pos of the scatterlist */
static void sg_copy(struct scatterlist *sg, unsigned int pos, u8 *buf,
                    unsigned int len, int to_sg)
{
    while (len) {
        unsigned int n;
        if (pos >= sg->length) {
            pos -= sg->length;
            sg++;
            continue;
        }
        n = sg->length - pos;
        if (n > len)
            n = len;
        if (to_sg)
            memcpy((u8 *)sg->b43c_buf + pos, buf, n);
        else
            memcpy(buf, (u8 *)sg->b43c_buf + pos, n);
        buf += n;
        len -= n;
        pos += n;
    }
}

/* =========================================================================
 * crypto/algapi.c
 * ========================================================================= */
void b43compat_crypto_xor(u8 *dst, const u8 *src, unsigned int len)
{
    while (len--)
        *dst++ ^= *src++;
}

/* =========================================================================
 * AEAD: ccm(aes)
 * ========================================================================= */
struct crypto_aead {
    struct crypto_aes_ctx aes;
    unsigned int authsize;
};

struct crypto_aead *crypto_alloc_aead(const char *alg_name, u32 type, u32 mask)
{
    struct crypto_aead *tfm;

    (void)type;
    (void)mask;
    if (strcmp(alg_name, "ccm(aes)") != 0)
        return (struct crypto_aead *)ERR_PTR(-ENOENT);
    tfm = kzalloc(sizeof(*tfm), GFP_KERNEL);
    if (!tfm)
        return (struct crypto_aead *)ERR_PTR(-ENOMEM);
    tfm->authsize = 16;
    return tfm;
}

void crypto_free_aead(struct crypto_aead *tfm)
{
    if (tfm && !IS_ERR(tfm)) {
        memzero_explicit(tfm, sizeof(*tfm));
        kfree(tfm);
    }
}

int crypto_aead_setkey(struct crypto_aead *tfm, const u8 *key, unsigned int keylen)
{
    return aes_expandkey(&tfm->aes, key, keylen);
}

int crypto_aead_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
    /* crypto/ccm.c: 4, 6, 8, 10, 12, 14, 16 */
    if (authsize < 4 || authsize > 16 || (authsize & 1))
        return -EINVAL;
    tfm->authsize = authsize;
    return 0;
}

unsigned int crypto_aead_reqsize(struct crypto_aead *tfm)
{
    (void)tfm;
    return 0;
}

unsigned int crypto_aead_authsize(struct crypto_aead *tfm)
{
    return tfm->authsize;
}

/*
 * CCM as in crypto/ccm.c: iv[0] = L - 1, iv[1 .. 15 - L] = nonce.
 * Scatterlist: [assoclen bytes of AAD][message][tag]
 * On encryption cryptlen = message length, on decryption
 * message + tag.
 */
static void ccm_cbcmac_block(struct crypto_aead *tfm, u8 *mac, const u8 *in, unsigned int n)
{
    crypto_xor(mac, in, n);
    aes_encrypt(&tfm->aes, mac, mac);
}

static void ccm_mac(struct crypto_aead *tfm, struct aead_request *req,
                    const u8 *msg, unsigned int msglen, u8 *mac)
{
    unsigned int L = (unsigned int)req->iv[0] + 1;
    unsigned int assoclen = req->assoclen;
    u8 b[16];
    unsigned int i, n, pos;

    /* B0: flags, nonce, message length */
    memcpy(b, req->iv, 16);
    b[0] |= (u8)(((tfm->authsize - 2) / 2) << 3);
    if (assoclen)
        b[0] |= 0x40;
    for (i = 0; i < L; i++)
        b[15 - i] = (u8)(i < 4 ? msglen >> (8 * i) : 0);
    memset(mac, 0, 16);
    ccm_cbcmac_block(tfm, mac, b, 16);

    /* Associated data: 2 length bytes (< 0xFF00), then the data */
    if (assoclen) {
        memset(b, 0, 16);
        b[0] = (u8)(assoclen >> 8);
        b[1] = (u8)assoclen;
        n = assoclen < 14 ? assoclen : 14;
        sg_copy(req->src, 0, b + 2, n, 0);
        ccm_cbcmac_block(tfm, mac, b, 16);
        for (pos = n; pos < assoclen; pos += 16) {
            n = assoclen - pos < 16 ? assoclen - pos : 16;
            memset(b, 0, 16);
            sg_copy(req->src, pos, b, n, 0);
            ccm_cbcmac_block(tfm, mac, b, 16);
        }
    }

    /* Plaintext message */
    for (pos = 0; pos < msglen; pos += 16) {
        n = msglen - pos < 16 ? msglen - pos : 16;
        memset(b, 0, 16);
        memcpy(b, msg + pos, n);
        ccm_cbcmac_block(tfm, mac, b, 16);
    }
}

static void ccm_ctr(struct crypto_aead *tfm, struct aead_request *req,
                    u8 *msg, unsigned int msglen, u8 *s0)
{
    unsigned int L = (unsigned int)req->iv[0] + 1;
    u8 a[16], s[16];
    u32 ctr = 0;
    unsigned int i, pos, n;

    memcpy(a, req->iv, 16);
    for (i = 0; i < L; i++)
        a[15 - i] = 0;
    aes_encrypt(&tfm->aes, s0, a);
    for (pos = 0; pos < msglen; pos += 16) {
        ctr++;
        for (i = 0; i < L && i < 4; i++)
            a[15 - i] = (u8)(ctr >> (8 * i));
        aes_encrypt(&tfm->aes, s, a);
        n = msglen - pos < 16 ? msglen - pos : 16;
        crypto_xor(msg + pos, s, n);
    }
}

int crypto_aead_encrypt(struct aead_request *req)
{
    struct crypto_aead *tfm = req->tfm;
    unsigned int msglen = req->cryptlen;
    u8 *msg, mac[16], s0[16];

    if (req->iv[0] < 1 || req->iv[0] > 7)
        return -EINVAL;
    msg = kmalloc(msglen ? msglen : 1, GFP_ATOMIC);
    if (!msg)
        return -ENOMEM;
    sg_copy(req->src, req->assoclen, msg, msglen, 0);
    ccm_mac(tfm, req, msg, msglen, mac);
    ccm_ctr(tfm, req, msg, msglen, s0);
    crypto_xor(mac, s0, tfm->authsize);
    sg_copy(req->dst, req->assoclen, msg, msglen, 1);
    sg_copy(req->dst, req->assoclen + msglen, mac, tfm->authsize, 1);
    kfree_sensitive(msg);
    return 0;
}

int crypto_aead_decrypt(struct aead_request *req)
{
    struct crypto_aead *tfm = req->tfm;
    unsigned int msglen, i;
    u8 *msg, mac[16], s0[16], tag[16], diff = 0;

    if (req->iv[0] < 1 || req->iv[0] > 7 || req->cryptlen < tfm->authsize)
        return -EINVAL;
    msglen = req->cryptlen - tfm->authsize;
    msg = kmalloc(msglen ? msglen : 1, GFP_ATOMIC);
    if (!msg)
        return -ENOMEM;
    sg_copy(req->src, req->assoclen, msg, msglen, 0);
    sg_copy(req->src, req->assoclen + msglen, tag, tfm->authsize, 0);
    ccm_ctr(tfm, req, msg, msglen, s0);
    ccm_mac(tfm, req, msg, msglen, mac);
    crypto_xor(mac, s0, tfm->authsize);
    for (i = 0; i < tfm->authsize; i++)
        diff |= (u8)(mac[i] ^ tag[i]);
    if (diff) {
        kfree_sensitive(msg);
        return -EBADMSG;
    }
    sg_copy(req->dst, req->assoclen, msg, msglen, 1);
    kfree_sensitive(msg);
    return 0;
}

/* =========================================================================
 * shash: cmac(aes) (RFC 4493)
 * ========================================================================= */
struct crypto_shash {
    struct crypto_aes_ctx aes;
    u8 k1[16], k2[16];
};

/* Computation state in desc->b43c_ctx: x[16] accumulator, buf[16], len */
struct cmac_state {
    u8 x[16];
    u8 buf[16];
    unsigned int len;
};

struct crypto_shash *crypto_alloc_shash(const char *alg_name, u32 type, u32 mask)
{
    struct crypto_shash *tfm;

    (void)type;
    (void)mask;
    if (strcmp(alg_name, "cmac(aes)") != 0)
        return (struct crypto_shash *)ERR_PTR(-ENOENT);
    tfm = kzalloc(sizeof(*tfm), GFP_KERNEL);
    if (!tfm)
        return (struct crypto_shash *)ERR_PTR(-ENOMEM);
    return tfm;
}

void crypto_free_shash(struct crypto_shash *tfm)
{
    if (tfm && !IS_ERR(tfm)) {
        memzero_explicit(tfm, sizeof(*tfm));
        kfree(tfm);
    }
}

static void cmac_dbl(u8 *out, const u8 *in)
{
    int i;
    u8 carry = (u8)(in[0] & 0x80);
    for (i = 0; i < 15; i++)
        out[i] = (u8)((in[i] << 1) | (in[i + 1] >> 7));
    out[15] = (u8)(in[15] << 1);
    if (carry)
        out[15] ^= 0x87;
}

int crypto_shash_setkey(struct crypto_shash *tfm, const u8 *key, unsigned int keylen)
{
    u8 l[16];
    int err = aes_expandkey(&tfm->aes, key, keylen);
    if (err)
        return err;
    memset(l, 0, 16);
    aes_encrypt(&tfm->aes, l, l);
    cmac_dbl(tfm->k1, l);
    cmac_dbl(tfm->k2, tfm->k1);
    return 0;
}

int crypto_shash_init(struct shash_desc *desc)
{
    memset(desc->b43c_ctx, 0, sizeof(struct cmac_state));
    return 0;
}

int crypto_shash_update(struct shash_desc *desc, const u8 *data, unsigned int len)
{
    struct cmac_state *st = (struct cmac_state *)desc->b43c_ctx;

    while (len) {
        /* the last block stays in the buffer: it is handled in final */
        if (st->len == 16) {
            crypto_xor(st->x, st->buf, 16);
            aes_encrypt(&desc->tfm->aes, st->x, st->x);
            st->len = 0;
        }
        st->buf[st->len++] = *data++;
        len--;
    }
    return 0;
}

int crypto_shash_final(struct shash_desc *desc, u8 *out)
{
    struct cmac_state *st = (struct cmac_state *)desc->b43c_ctx;
    struct crypto_shash *tfm = desc->tfm;

    if (st->len == 16) {
        crypto_xor(st->buf, tfm->k1, 16);
    } else {
        st->buf[st->len] = 0x80;
        memset(st->buf + st->len + 1, 0, 15 - st->len);
        crypto_xor(st->buf, tfm->k2, 16);
    }
    crypto_xor(st->x, st->buf, 16);
    aes_encrypt(&tfm->aes, out, st->x);
    memzero_explicit(st, sizeof(*st));
    return 0;
}

int crypto_shash_finup(struct shash_desc *desc, const u8 *data, unsigned int len, u8 *out)
{
    crypto_shash_update(desc, data, len);
    return crypto_shash_final(desc, out);
}

int crypto_shash_digest(struct shash_desc *desc, const u8 *data, unsigned int len, u8 *out)
{
    crypto_shash_init(desc);
    return crypto_shash_finup(desc, data, len, out);
}

/* =========================================================================
 * skcipher and cipher: not available
 * ========================================================================= */
struct crypto_skcipher { int dummy; };
struct crypto_cipher { int dummy; };

struct crypto_skcipher *crypto_alloc_skcipher(const char *alg_name, u32 type, u32 mask)
{
    (void)alg_name; (void)type; (void)mask;
    return (struct crypto_skcipher *)ERR_PTR(-ENOENT);
}

void crypto_free_skcipher(struct crypto_skcipher *tfm) { (void)tfm; }

int crypto_skcipher_setkey(struct crypto_skcipher *tfm, const u8 *key, unsigned int keylen)
{
    (void)tfm; (void)key; (void)keylen;
    return -ENOENT;
}

struct skcipher_request *skcipher_request_alloc(struct crypto_skcipher *tfm, gfp_t gfp)
{
    (void)tfm; (void)gfp;
    return NULL;
}

int crypto_skcipher_encrypt(struct skcipher_request *req) { (void)req; return -ENOENT; }
int crypto_skcipher_decrypt(struct skcipher_request *req) { (void)req; return -ENOENT; }

/* =========================================================================
 * CRC32 (lib/crc32.c): IEEE 802.3, reflected polynomial 0xEDB88320 for
 * crc32_le (FCS, WEP ICV), 0x04C11DB7 for crc32_be
 * ========================================================================= */
u32 crc32_le(u32 crc, const unsigned char *p, size_t len)
{
    int i;
    while (len--) {
        crc ^= *p++;
        for (i = 0; i < 8; i++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0);
    }
    return crc;
}

u32 crc32_be(u32 crc, const unsigned char *p, size_t len)
{
    int i;
    while (len--) {
        crc ^= (u32)*p++ << 24;
        for (i = 0; i < 8; i++)
            crc = (crc << 1) ^ ((crc & 0x80000000UL) ? 0x04C11DB7UL : 0);
    }
    return crc;
}
