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

#include "macros.h"

#ifdef __cplusplus
extern "C" {
#endif

// A fast fixed-size block allocator.
typedef struct ngfi_block_allocator ngfi_block_allocator;

// Creates a new block allocator with a given fixed `block_size` and a given
// initial capacity of `nblocks`.
ngfi_block_allocator* ngfi_blkalloc_create(uint32_t block_size, uint32_t nblocks);

// Destroys the given block allocator. All unfreed pointers obtained from the
// destroyed allocator become invalid.
void ngfi_blkalloc_destroy(ngfi_block_allocator* alloc);

// Allocates the next free block from the allocator. Returns NULL on error.
void* ngfi_blkalloc_alloc(ngfi_block_allocator* alloc);

typedef enum {
  NGFI_BLK_NO_ERROR,
  NGFI_BLK_DOUBLE_FREE,
  NGFI_BLK_WRONG_ALLOCATOR
} ngfi_blkalloc_error;

// Returns the given block to the allocator.
// Freeing a NULL pointer does nothing.
ngfi_blkalloc_error ngfi_blkalloc_free(ngfi_block_allocator* alloc, void* ptr);

#ifdef __cplusplus
}
#endif

