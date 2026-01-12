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

#pragma once

void crypt_gen_iv(uint8_t *iv);

int crypt_aes256_cbc_encrypt(
    const uint8_t key[32],
    const uint8_t iv_in[16],
    const uint8_t *plaintext, size_t pt_len,
    uint8_t *ciphertext, size_t ct_cap,
    size_t *ct_len_out);

int crypt_aes256_cbc_decrypt(
    const uint8_t key[32],
    const uint8_t iv_in[16],
    const uint8_t *ciphertext, size_t ct_len,
    uint8_t *plaintext, size_t pt_cap,
    size_t *pt_len_out);
