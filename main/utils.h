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

#define ALWAYS_INLINE static inline __attribute__((always_inline))

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

typedef struct
{
    uint32_t w[8];
} bitmap256_t;

ALWAYS_INLINE bool bm256_test(const volatile bitmap256_t *bm, uint8_t bit)
{
    uint32_t word = bit >> 5;
    uint32_t offset = bit & 31;
    return (bm->w[word] >> offset) & 1u;
}

ALWAYS_INLINE void bm256_set(volatile bitmap256_t *bm, uint8_t bit)
{
    uint32_t word = bit >> 5;
    uint32_t offset = bit & 31;
    bm->w[word] |= (1u << offset);
}

ALWAYS_INLINE void bm256_clear(volatile bitmap256_t *bm, uint8_t bit)
{
    uint32_t word = bit >> 5;
    uint32_t offset = bit & 31;
    bm->w[word] &= ~(1u << offset);
}

ALWAYS_INLINE void bm256_reset(volatile bitmap256_t *bm)
{
    for (int i = 0; i < ARRAY_SIZE(bm->w); i++)
    {
        bm->w[i] = 0;
    }
}
