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

#include "stack-alloc.h"

#include "macros.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

ngfi_sa* ngfi_sa_create(size_t capacity) {
  ngfi_sa* result = malloc(capacity + sizeof(ngfi_sa));
  if (result) {
    result->capacity = capacity;
    result->ptr      = result->data;
    result->active_block = result;
    result->next_block = NULL;
  }
  return result;
}

void* ngfi_sa_alloc(ngfi_sa* allocator, size_t nbytes) {
  assert(allocator);

  ngfi_sa* alloc_block = allocator->active_block;

  void*           result             = NULL;
  const ptrdiff_t consumed_capacity  = alloc_block->ptr - alloc_block->data;
  const ptrdiff_t available_capacity = (ptrdiff_t)alloc_block->capacity - consumed_capacity;
  if (available_capacity >= (ptrdiff_t)nbytes) {
    result = alloc_block->ptr;
    alloc_block->ptr += nbytes;
  }
  else {
    const size_t new_capacity = NGFI_MAX(alloc_block->capacity, nbytes);

    ngfi_sa* new_block = ngfi_sa_create(new_capacity);
    if(new_block == NULL) { return NULL; }

    alloc_block->next_block = new_block;
    allocator->active_block = new_block;

    result = new_block->ptr;
    new_block->ptr += nbytes;
  }

  return result;
}

void ngfi_sa_reset(ngfi_sa* allocator) {
  assert(allocator);
 
  // free all blocks except for first block
  ngfi_sa* curr_block = allocator->next_block;
  while (curr_block != NULL) {
      ngfi_sa* next = curr_block->next_block;
      free(curr_block);
      curr_block = next;
  }

  allocator->ptr = allocator->data;
  allocator->active_block = allocator;
  allocator->next_block = NULL;
}

void ngfi_sa_destroy(ngfi_sa* allocator) {
  assert(allocator);
  ngfi_sa_reset(allocator);
  free(allocator);
}

ngfi_sa* ngfi_tmp_store(void) {
  static NGFI_THREADLOCAL ngfi_sa* temp_storage = NULL;
  if (temp_storage == NULL) {
    const size_t sa_capacity = 1024 * 100;  // 100K
    temp_storage             = ngfi_sa_create(sa_capacity);
  }
  return temp_storage;
}


ngfi_sa* ngfi_frame_store(void) {
  static NGFI_THREADLOCAL ngfi_sa* frame_storage = NULL;
  if (frame_storage == NULL) {
    const size_t sa_capacity = 1024 * 100;  // 100K
    frame_storage = ngfi_sa_create(sa_capacity);
  }
  return frame_storage;
}

