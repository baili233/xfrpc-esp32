// SPDX-License-Identifier: GPL-3.0-only
/*
 * ESP32 port: OpenSSL EVP compatibility header backed by mbedtls.
 * Implements exactly the subset xfrpc uses:
 *   - AES-128-CFB (streaming, in-place)
 *   - AES-256-GCM (AEAD, for optional wire v2)
 *   - MD5 / SHA1 / SHA256 digests, PBKDF2
 * See port/openssl_compat.c.
 */

#ifndef MINI_OPENSSL_EVP_H
#define MINI_OPENSSL_EVP_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mini_evp_cipher_ctx EVP_CIPHER_CTX;
typedef struct mini_evp_md_ctx     EVP_MD_CTX;

typedef struct { int id; } EVP_MD;
typedef struct { int id; } EVP_CIPHER;

/* cipher ids (internal to the shim) */
#define MINI_EVP_AES_128_CFB  1
#define MINI_EVP_AES_256_GCM  2

const EVP_CIPHER *EVP_aes_128_cfb(void);
const EVP_CIPHER *EVP_aes_128_cfb128(void); /* same as aes_128_cfb */
const EVP_CIPHER *EVP_aes_256_gcm(void);

const EVP_MD *EVP_sha1(void);
const EVP_MD *EVP_sha256(void);
const EVP_MD *EVP_md5(void);

EVP_CIPHER_CTX *EVP_CIPHER_CTX_new(void);
void EVP_CIPHER_CTX_free(EVP_CIPHER_CTX *);

/* For CFB: may be called once with key+iv. GCM: call with cipher first,
 * then again with (NULL cipher, key, iv) after setting IV len — mirroring
 * the OpenSSL usage pattern in xfrpc. */
int EVP_EncryptInit_ex(EVP_CIPHER_CTX *, const EVP_CIPHER *,
                       void *impl, const unsigned char *key, const unsigned char *iv);
int EVP_DecryptInit_ex(EVP_CIPHER_CTX *, const EVP_CIPHER *,
                       void *impl, const unsigned char *key, const unsigned char *iv);

/* CFB: in-place allowed, *outl == inl. GCM: pass out==NULL for AAD. */
int EVP_EncryptUpdate(EVP_CIPHER_CTX *, unsigned char *out, int *outl,
                      const unsigned char *in, int inl);
int EVP_DecryptUpdate(EVP_CIPHER_CTX *, unsigned char *out, int *outl,
                      const unsigned char *in, int inl);

int EVP_EncryptFinal_ex(EVP_CIPHER_CTX *, unsigned char *out, int *outl);
int EVP_DecryptFinal_ex(EVP_CIPHER_CTX *, unsigned char *out, int *outl);

#define EVP_CTRL_GCM_SET_IVLEN 0x09
#define EVP_CTRL_GCM_GET_TAG   0x10
#define EVP_CTRL_GCM_SET_TAG   0x11

int EVP_CIPHER_CTX_ctrl(EVP_CIPHER_CTX *, int type, int arg, void *ptr);

/* Digest API (MD5/SHA) */
EVP_MD_CTX *EVP_MD_CTX_new(void);
void EVP_MD_CTX_free(EVP_MD_CTX *);
int EVP_DigestInit_ex(EVP_MD_CTX *, const EVP_MD *, void *impl);
int EVP_DigestUpdate(EVP_MD_CTX *, const void *data, size_t len);
int EVP_DigestFinal_ex(EVP_MD_CTX *, unsigned char *md, unsigned int *len);

int PKCS5_PBKDF2_HMAC(const char *pass, int passlen,
                      const unsigned char *salt, int saltlen, int iter,
                      const EVP_MD *digest, int keylen, unsigned char *out);

/* base64 (wire v2 client hello) */
int EVP_EncodeBlock(unsigned char *dst, const unsigned char *src, int srclen);

#ifdef __cplusplus
}
#endif

#endif /* MINI_OPENSSL_EVP_H */
