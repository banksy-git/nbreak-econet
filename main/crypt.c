/*
 * EconetWiFi
 * Copyright (c) 2025 Paul G. Banks <https://paulbanks.org/projects/econet>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * See the LICENSE file in the project root for full license information.
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "esp_random.h"
#include "mbedtls/aes.h"

#include "crypt.h"

static size_t pkcs7_pad(uint8_t *buf, size_t len, size_t cap)
{
    size_t pad = 16 - (len % 16);
    if (len + pad > cap)
        return 0;
    memset(buf + len, (uint8_t)pad, pad);
    return len + pad;
}

static size_t pkcs7_unpad(uint8_t *buf, size_t len)
{
    if (len == 0 || (len % 16) != 0)
        return 0;
    uint8_t pad = buf[len - 1];
    if (pad == 0 || pad > 16)
        return 0;
    for (size_t i = 0; i < pad; i++)
    {
        if (buf[len - 1 - i] != pad)
            return 0;
    }
    return len - pad;
}

void crypt_gen_iv(uint8_t *iv)
{
    esp_fill_random(iv, 16);
}

int crypt_aes256_cbc_encrypt(
    const uint8_t key[32],
    const uint8_t iv_in[16],
    const uint8_t *plaintext, size_t pt_len,
    uint8_t *ciphertext, size_t ct_cap,
    size_t *ct_len_out)
{
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);

    // Copy plaintext into output buffer so we can pad in-place
    if (pt_len > ct_cap)
        return -1;
    memcpy(ciphertext, plaintext, pt_len);

    size_t padded_len = pkcs7_pad(ciphertext, pt_len, ct_cap);
    if (padded_len == 0)
        return -2;

    uint8_t iv[16];
    memcpy(iv, iv_in, 16);

    int rc = mbedtls_aes_setkey_enc(&aes, key, 256);
    if (rc != 0)
    {
        mbedtls_aes_free(&aes);
        return -3;
    }

    rc = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, padded_len, iv, ciphertext, ciphertext);
    mbedtls_aes_free(&aes);
    if (rc != 0)
        return -4;

    *ct_len_out = padded_len;
    return 0;
}

int crypt_aes256_cbc_decrypt(
    const uint8_t key[32],
    const uint8_t iv_in[16],
    const uint8_t *ciphertext, size_t ct_len,
    uint8_t *plaintext, size_t pt_cap,
    size_t *pt_len_out)
{
    if ((ct_len % 16) != 0)
        return -1;
    if (ct_len > pt_cap)
        return -2;

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);

    uint8_t iv[16];
    memcpy(iv, iv_in, 16);

    int rc = mbedtls_aes_setkey_dec(&aes, key, 256);
    if (rc != 0)
    {
        mbedtls_aes_free(&aes);
        return -3;
    }

    rc = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, ct_len, iv, ciphertext, plaintext);
    mbedtls_aes_free(&aes);
    if (rc != 0)
        return -4;

    size_t unpadded = pkcs7_unpad(plaintext, ct_len);
    if (unpadded == 0)
        return -5;

    *pt_len_out = unpadded;
    return 0;
}
