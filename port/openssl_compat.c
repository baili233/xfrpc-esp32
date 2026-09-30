// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: OpenSSL crypto compatibility layer over mbedtls.
 *
 * Implements the EVP subset xfrpc uses (see port/include/openssl/evp.h):
 *   - AES-128-CFB128 streaming (in-place) via mbedtls_aes_crypt_cfb128
 *   - AES-256-GCM AEAD (wire v2) via mbedtls incremental GCM API
 *   - MD5/SHA digests, one-shot HMAC, PBKDF2-HMAC-SHA1, RAND_bytes
 */

#include <string.h>
#include <stdlib.h>

#include <mbedtls/aes.h>
#include <mbedtls/base64.h>
#include <mbedtls/constant_time.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/pkcs5.h>
#include <esp_random.h>

#include "openssl/evp.h"
#include "openssl/rand.h"
#include "openssl/hmac.h"
#include "openssl/err.h"
#include "openssl/ssl.h"
#include "openssl/md5.h"

/* ------------------------------------------------------------------ */
/* cipher / digest selectors                                           */
/* ------------------------------------------------------------------ */

static const EVP_CIPHER mini_aes_128_cfb = { .id = MINI_EVP_AES_128_CFB };
static const EVP_CIPHER mini_aes_256_gcm = { .id = MINI_EVP_AES_256_GCM };

const EVP_CIPHER *EVP_aes_128_cfb(void)   { return &mini_aes_128_cfb; }
const EVP_CIPHER *EVP_aes_128_cfb128(void){ return &mini_aes_128_cfb; }
const EVP_CIPHER *EVP_aes_256_gcm(void)   { return &mini_aes_256_gcm; }

static const EVP_MD mini_sha1   = { .id = MBEDTLS_MD_SHA1 };
static const EVP_MD mini_sha256 = { .id = MBEDTLS_MD_SHA256 };
static const EVP_MD mini_md5    = { .id = MBEDTLS_MD_MD5 };

const EVP_MD *EVP_sha1(void)   { return &mini_sha1; }
const EVP_MD *EVP_sha256(void) { return &mini_sha256; }
const EVP_MD *EVP_md5(void)    { return &mini_md5; }

/* ------------------------------------------------------------------ */
/* RAND / MD5 / ERR / SSL stubs                                        */
/* ------------------------------------------------------------------ */

int RAND_bytes(unsigned char *buf, int num)
{
    if (!buf || num <= 0)
        return 0;
    esp_fill_random(buf, (size_t)num);
    return 1;
}

unsigned char *MD5(const unsigned char *d, size_t n, unsigned char *md)
{
    if (!d || !md)
        return NULL;
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    if (mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_MD5), 0) != 0 ||
        mbedtls_md_starts(&ctx) != 0 ||
        mbedtls_md_update(&ctx, d, n) != 0 ||
        mbedtls_md_finish(&ctx, md) != 0) {
        mbedtls_md_free(&ctx);
        return NULL;
    }
    mbedtls_md_free(&ctx);
    return md;
}

unsigned long ERR_get_error(void)              { return 0; }
void ERR_error_string_n(unsigned long e, char *buf, size_t len)
{
    if (buf && len)
        buf[0] = '\0';
    (void)e;
}
void ERR_clear_error(void)                     {}

const SSL_METHOD *TLS_client_method(void)      { return (const SSL_METHOD *)1; }
SSL_CTX *SSL_CTX_new(const SSL_METHOD *m)      { (void)m; return NULL; }
void SSL_CTX_free(SSL_CTX *c)                  { (void)c; }

/* ------------------------------------------------------------------ */
/* HMAC one-shot                                                       */
/* ------------------------------------------------------------------ */

unsigned char *HMAC(const EVP_MD *evp_md, const void *key, int key_len,
                    const unsigned char *data, size_t n,
                    unsigned char *md, unsigned int *md_len)
{
    if (!evp_md || !key || key_len < 0 || (!data && n > 0) || !md)
        return NULL;
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(
        (mbedtls_md_type_t)evp_md->id);
    if (!info)
        return NULL;
    if (mbedtls_md_hmac(info, (const unsigned char *)key, (size_t)key_len,
                        data, n, md) != 0)
        return NULL;
    if (md_len)
        *md_len = (unsigned int)mbedtls_md_get_size(info);
    return md;
}

/* ------------------------------------------------------------------ */
/* PBKDF2                                                              */
/* ------------------------------------------------------------------ */

int PKCS5_PBKDF2_HMAC(const char *pass, int passlen,
                      const unsigned char *salt, int saltlen, int iter,
                      const EVP_MD *digest, int keylen, unsigned char *out)
{
    if (!pass || !salt || !digest || !out || passlen < 0 || saltlen < 0 ||
        keylen <= 0 || iter <= 0)
        return 0;
    int rc = mbedtls_pkcs5_pbkdf2_hmac_ext((mbedtls_md_type_t)digest->id,
                                           (const unsigned char *)pass,
                                           (size_t)passlen,
                                           salt, (size_t)saltlen,
                                           (unsigned int)iter,
                                           (size_t)keylen, out);
    return rc == 0 ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* base64                                                              */
/* ------------------------------------------------------------------ */

int EVP_EncodeBlock(unsigned char *dst, const unsigned char *src, int srclen)
{
    if (!dst || !src || srclen < 0)
        return -1;
    size_t olen = 0;
    if (mbedtls_base64_encode(dst, 4 * ((size_t)srclen + 2) / 3 + 1,
                              &olen, src, (size_t)srclen) != 0)
        return -1;
    return (int)olen;
}

/* ------------------------------------------------------------------ */
/* EVP cipher context                                                  */
/* ------------------------------------------------------------------ */

#define MINI_GCM_TAG_LEN 16
#define MINI_GCM_IV_LEN  12

struct mini_evp_cipher_ctx {
    int cipher_id;      /* 0 = unset */
    int direction;      /* 1 = encrypt, 0 = decrypt */
    int finished;

    /* CFB128 state (streaming) */
    mbedtls_aes_context aes;
    unsigned char       cfb_iv[16];
    size_t              cfb_iv_off;

    /* GCM state (incremental, one message per context) */
    mbedtls_gcm_context gcm;
    unsigned char       gcm_tag[MINI_GCM_TAG_LEN];
    int                 gcm_tag_set;   /* decrypt: expected tag stored */
    int                 gcm_started;
};

EVP_CIPHER_CTX *EVP_CIPHER_CTX_new(void)
{
    EVP_CIPHER_CTX *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return NULL;
    mbedtls_aes_init(&ctx->aes);
    mbedtls_gcm_init(&ctx->gcm);
    return ctx;
}

void EVP_CIPHER_CTX_free(EVP_CIPHER_CTX *ctx)
{
    if (!ctx)
        return;
    mbedtls_aes_free(&ctx->aes);
    mbedtls_gcm_free(&ctx->gcm);
    free(ctx);
}

static int evp_cfb_init(EVP_CIPHER_CTX *ctx, int direction,
                        const unsigned char *key, const unsigned char *iv)
{
    if (!key || !iv)
        return 0;
    if (mbedtls_aes_setkey_enc(&ctx->aes, key, 128) != 0)
        return 0;
    /* CFB uses the forward cipher for both directions */
    memcpy(ctx->cfb_iv, iv, sizeof(ctx->cfb_iv));
    ctx->cfb_iv_off = 0;
    ctx->direction = direction;
    return 1;
}

static int evp_gcm_init(EVP_CIPHER_CTX *ctx, int direction,
                        const unsigned char *key, const unsigned char *iv)
{
    if (key && iv) {
        if (mbedtls_gcm_setkey(&ctx->gcm, MBEDTLS_CIPHER_ID_AES, key, 256) != 0)
            return 0;
        if (mbedtls_gcm_starts(&ctx->gcm,
                               direction ? MBEDTLS_DECRYPT : MBEDTLS_ENCRYPT,
                               iv, MINI_GCM_IV_LEN) != 0)
            return 0;
        ctx->gcm_started = 1;
        ctx->direction = direction;
        return 1;
    }
    if (key || iv)
        return 0; /* partial init not supported */
    return 1;     /* key/iv come in a later call */
}

static int evp_init(EVP_CIPHER_CTX *ctx, int direction, const EVP_CIPHER *cipher,
                    const unsigned char *key, const unsigned char *iv)
{
    if (cipher) {
        if (ctx->cipher_id != 0 && ctx->cipher_id != cipher->id)
            return 0; /* context reuse with a different cipher */
        ctx->cipher_id = cipher->id;
        ctx->finished = 0;
        ctx->gcm_tag_set = 0;
        ctx->gcm_started = 0;
    }
    if (!key && !iv)
        return 1; /* cipher-only call (GCM pattern) */

    switch (ctx->cipher_id) {
    case MINI_EVP_AES_128_CFB:
        return evp_cfb_init(ctx, direction, key, iv);
    case MINI_EVP_AES_256_GCM:
        return evp_gcm_init(ctx, direction, key, iv);
    default:
        return 0;
    }
}

int EVP_EncryptInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
                       void *impl, const unsigned char *key, const unsigned char *iv)
{
    (void)impl;
    return ctx ? evp_init(ctx, 1, cipher, key, iv) : 0;
}

int EVP_DecryptInit_ex(EVP_CIPHER_CTX *ctx, const EVP_CIPHER *cipher,
                       void *impl, const unsigned char *key, const unsigned char *iv)
{
    (void)impl;
    return ctx ? evp_init(ctx, 0, cipher, key, iv) : 0;
}

int EVP_EncryptUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl,
                      const unsigned char *in, int inl)
{
    if (!ctx || !in || inl < 0)
        return 0;
    switch (ctx->cipher_id) {
    case MINI_EVP_AES_128_CFB: {
        if (!out || !outl)
            return 0;
        if (inl == 0) { *outl = 0; return 1; }
        if (mbedtls_aes_crypt_cfb128(&ctx->aes, MBEDTLS_AES_ENCRYPT,
                                     (size_t)inl, &ctx->cfb_iv_off,
                                     ctx->cfb_iv, in, out) != 0)
            return 0;
        *outl = inl;
        return 1;
    }
    case MINI_EVP_AES_256_GCM: {
        if (!out) {
            /* AAD */
            if (inl == 0)
                return 1;
            return mbedtls_gcm_update_ad(&ctx->gcm, in, (size_t)inl) == 0;
        }
        if (!outl)
            return 0;
        if (inl == 0) { *outl = 0; return 1; }
        size_t olen = 0;
        if (mbedtls_gcm_update(&ctx->gcm, in, (size_t)inl,
                               out, (size_t)inl, &olen) != 0)
            return 0;
        *outl = (int)olen;
        return 1;
    }
    default:
        return 0;
    }
}

int EVP_DecryptUpdate(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl,
                      const unsigned char *in, int inl)
{
    if (!ctx || !in || inl < 0)
        return 0;
    switch (ctx->cipher_id) {
    case MINI_EVP_AES_128_CFB: {
        if (!out || !outl)
            return 0;
        if (inl == 0) { *outl = 0; return 1; }
        if (mbedtls_aes_crypt_cfb128(&ctx->aes, MBEDTLS_AES_DECRYPT,
                                     (size_t)inl, &ctx->cfb_iv_off,
                                     ctx->cfb_iv, in, out) != 0)
            return 0;
        *outl = inl;
        return 1;
    }
    case MINI_EVP_AES_256_GCM:
        return EVP_EncryptUpdate(ctx, out, outl, in, inl);
    default:
        return 0;
    }
}

int EVP_EncryptFinal_ex(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl)
{
    if (!ctx || !outl)
        return 0;
    if (outl)
        *outl = 0;
    (void)out;
    if (ctx->cipher_id == MINI_EVP_AES_256_GCM && !ctx->finished) {
        unsigned char tag[MINI_GCM_TAG_LEN];
        size_t olen = 0;
        if (mbedtls_gcm_finish(&ctx->gcm, NULL, 0, &olen,
                               tag, sizeof(tag)) != 0)
            return 0;
        memcpy(ctx->gcm_tag, tag, sizeof(tag));
        ctx->finished = 1;
    }
    return 1;
}

int EVP_DecryptFinal_ex(EVP_CIPHER_CTX *ctx, unsigned char *out, int *outl)
{
    if (!ctx || !outl)
        return 0;
    *outl = 0;
    (void)out;
    if (ctx->cipher_id == MINI_EVP_AES_256_GCM && !ctx->finished) {
        unsigned char tag[MINI_GCM_TAG_LEN];
        size_t olen = 0;
        if (mbedtls_gcm_finish(&ctx->gcm, NULL, 0, &olen,
                               tag, sizeof(tag)) != 0)
            return 0;
        ctx->finished = 1;
        if (!ctx->gcm_tag_set ||
            mbedtls_ct_memcmp(tag, ctx->gcm_tag, MINI_GCM_TAG_LEN) != 0)
            return 0;
    }
    return 1;
}

int EVP_CIPHER_CTX_ctrl(EVP_CIPHER_CTX *ctx, int type, int arg, void *ptr)
{
    if (!ctx)
        return 0;
    switch (type) {
    case EVP_CTRL_GCM_SET_IVLEN:
        /* only the default 12-byte IV is supported (what xfrpc uses) */
        return arg == MINI_GCM_IV_LEN ? 1 : 0;
    case EVP_CTRL_GCM_GET_TAG:
        if (!ptr || arg != MINI_GCM_TAG_LEN || !ctx->finished)
            return 0;
        memcpy(ptr, ctx->gcm_tag, MINI_GCM_TAG_LEN);
        return 1;
    case EVP_CTRL_GCM_SET_TAG:
        if (!ptr || arg != MINI_GCM_TAG_LEN)
            return 0;
        memcpy(ctx->gcm_tag, ptr, MINI_GCM_TAG_LEN);
        ctx->gcm_tag_set = 1;
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* digest context                                                      */
/* ------------------------------------------------------------------ */

struct mini_evp_md_ctx {
    mbedtls_md_context_t md;
    int inited;
};

EVP_MD_CTX *EVP_MD_CTX_new(void)
{
    EVP_MD_CTX *ctx = calloc(1, sizeof(*ctx));
    if (ctx)
        mbedtls_md_init(&ctx->md);
    return ctx;
}

void EVP_MD_CTX_free(EVP_MD_CTX *ctx)
{
    if (!ctx)
        return;
    mbedtls_md_free(&ctx->md);
    free(ctx);
}

int EVP_DigestInit_ex(EVP_MD_CTX *ctx, const EVP_MD *md, void *impl)
{
    (void)impl;
    if (!ctx || !md)
        return 0;
    if (ctx->inited)
        mbedtls_md_free(&ctx->md); /* re-init */
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(
        (mbedtls_md_type_t)md->id);
    if (!info)
        return 0;
    if (mbedtls_md_setup(&ctx->md, info, 0) != 0)
        return 0;
    if (mbedtls_md_starts(&ctx->md) != 0)
        return 0;
    ctx->inited = 1;
    return 1;
}

int EVP_DigestUpdate(EVP_MD_CTX *ctx, const void *data, size_t len)
{
    if (!ctx || !ctx->inited || (!data && len > 0))
        return 0;
    return mbedtls_md_update(&ctx->md, data, len) == 0;
}

int EVP_DigestFinal_ex(EVP_MD_CTX *ctx, unsigned char *md, unsigned int *len)
{
    if (!ctx || !ctx->inited || !md)
        return 0;
    if (mbedtls_md_finish(&ctx->md, md) != 0)
        return 0;
    if (len)
        *len = (unsigned int)mbedtls_md_get_size(
            mbedtls_md_info_from_ctx(&ctx->md));
    /* allow immediate reuse like OpenSSL */
    return mbedtls_md_starts(&ctx->md) == 0;
}
