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

#include <stddef.h>
#include <stdint.h>

typedef uintptr_t           ngfi_dict_key;
typedef struct ngfi_dict_t* ngfi_dict;
typedef void*               ngfi_dict_iter;

#define NGFI_DICT_INVALID_KEY (~((size_t)0u))

ngfi_dict      ngfi_dict_create(size_t nslots, size_t val_size);
void           ngfi_dict_destroy(ngfi_dict dict);
void*          ngfi_dict_get(ngfi_dict* dict, ngfi_dict_key key, void* default_val);
void           ngfi_dict_clear(ngfi_dict dict);
ngfi_dict_iter ngfi_dict_itstart(ngfi_dict dict);
ngfi_dict_iter ngfi_dict_itnext(ngfi_dict dict, ngfi_dict_iter iter);
void*          ngfi_dict_itval(ngfi_dict dict, ngfi_dict_iter iter);
size_t         ngfi_dict_count(ngfi_dict dict);
size_t         ngfi_dict_nslots(ngfi_dict dict);
float          ngfi_dict_max_load_factor(ngfi_dict dict);
void           ngfi_dict_set_max_load_factor(ngfi_dict dict, float a);

#define NGFI_DICT_FOREACH(dict, itname)                                 \
  for (ngfi_dict_iter itname = ngfi_dict_itstart(dict); itname != NULL; \
       itname                = ngfi_dict_itnext(dict, itname))

#if defined(NGFI_DICT_TEST_MODE)
extern void (*ngfi_dict_hashfn)(uintptr_t, uint32_t, void*);
extern uint32_t ngfi_dict_hashseed;
#endif
