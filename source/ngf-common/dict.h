/**
 * Copyright (c) 2023 nicegraf contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uintptr_t           ngfi_dict_key;
typedef struct ngfi_dict_t* ngfi_dict;
typedef void*               ngfi_dict_iter;

typedef struct ngfi_dict_keyhash {
  ngfi_dict_key key;
  uint64_t      hash;
} ngfi_dict_keyhash;

#define NGFI_DICT_INVALID_KEY (~((size_t)0u))

ngfi_dict      ngfi_dict_create(size_t nslots, size_t val_size);
void           ngfi_dict_destroy(ngfi_dict dict);
void           ngfi_dict_clear(ngfi_dict dict);
ngfi_dict_iter ngfi_dict_itstart(ngfi_dict dict);
ngfi_dict_iter ngfi_dict_itnext(ngfi_dict dict, ngfi_dict_iter iter);
void*          ngfi_dict_itval(ngfi_dict dict, ngfi_dict_iter iter);
size_t         ngfi_dict_count(ngfi_dict dict);
size_t         ngfi_dict_nslots(ngfi_dict dict);
float          ngfi_dict_max_load_factor(ngfi_dict dict);
void           ngfi_dict_set_max_load_factor(ngfi_dict dict, float a);

void* ngfi_dict_get(
    ngfi_dict*         dict,
    ngfi_dict_key      key,
    void*              default_val,
    bool*              new_entry,
    ngfi_dict_keyhash* keyhash_out);
void* ngfi_dict_get_prehashed(
    ngfi_dict*               dict,
    const ngfi_dict_keyhash* keyhash,
    void*                    default_val,
    bool*                    new_entry);

#define NGFI_DICT_FOREACH(dict, itname)                                 \
  for (ngfi_dict_iter itname = ngfi_dict_itstart(dict); itname != NULL; \
       itname                = ngfi_dict_itnext(dict, itname))

#if defined(NGFI_DICT_TEST_MODE)
extern void (*ngfi_dict_hashfn)(uintptr_t, uint32_t, void*);
extern uint32_t ngfi_dict_hashseed;
#endif

/**
 * mumur3 hash function impl below.
 * this is a simplified version for keys 8 bytes in length.
 */

static inline uint64_t rotl64(uint64_t x, int8_t r) {
  return (x << r) | (x >> (64 - r));
}

static inline uint64_t fmix64(uint64_t k) {
  k ^= k >> 33;
  k *= (0xff51afd7ed558ccdLLU);
  k ^= k >> 33;
  k *= (0xc4ceb9fe1a85ec53LLU);
  k ^= k >> 33;
  return k;
}

static inline void ngfi_mmh3_x64_128(uintptr_t key, const uint32_t seed, void* out) {
  const uint8_t* data = (const uint8_t*)&key;

  uint64_t h1 = seed;
  uint64_t h2 = seed;
  uint64_t c1 = (0x87c37b91114253d5LLU);
  uint64_t c2 = (0x4cf5ad432745937fLLU);
  uint64_t k1 = 0;

  k1 ^= (uint64_t)(data[7]) << 56;
  k1 ^= (uint64_t)(data[6]) << 48;
  k1 ^= (uint64_t)(data[5]) << 40;
  k1 ^= (uint64_t)(data[4]) << 32;
  k1 ^= (uint64_t)(data[3]) << 24;
  k1 ^= (uint64_t)(data[2]) << 16;
  k1 ^= (uint64_t)(data[1]) << 8;
  k1 ^= (uint64_t)(data[0]) << 0;
  k1 *= c1;
  k1 = rotl64(k1, 31);
  k1 *= c2;
  h1 ^= k1;

  h1 ^= sizeof(key);
  h2 ^= sizeof(key);

  h1 += h2;
  h2 += h1;

  h1 = fmix64(h1);
  h2 = fmix64(h2);

  h1 += h2;
  h2 += h1;

  ((uint64_t*)out)[0] = h1;
  ((uint64_t*)out)[1] = h2;
}

#ifdef __cplusplus
}
#endif
