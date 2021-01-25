/**
 * Copyright (c) 2021 nicegraf contributors
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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ngfi_handle_pool_t* ngfi_handle_pool;
typedef uint64_t (*ngfi_handle_allocator)(void*);
typedef void (*ngfi_handle_deallocator)(uint64_t, void*);

typedef struct {
  uint32_t                initial_size;
  ngfi_handle_allocator   allocator;
  void*                   allocator_userdata;
  ngfi_handle_deallocator deallocator;
  void*                   deallocator_userdata;
} ngfi_handle_pool_info;

ngfi_handle_pool ngfi_create_handle_pool(const ngfi_handle_pool_info* info);
void             ngfi_destroy_handle_pool(ngfi_handle_pool pool);
uint64_t         ngfi_handle_pool_alloc(ngfi_handle_pool pool);
void             ngfi_handle_pool_free(ngfi_handle_pool pool, uint64_t handle);

#ifdef __cplusplus
}
#endif
