/**
Copyright (c) 2019 nicegraf contributors
Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include "stack_alloc.h"
#include "nicegraf_internal.h"

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

ngfi_sa* ngfi_sa_create(size_t capacity) {
  ngfi_sa* result = malloc(capacity + sizeof(ngfi_sa));
  if (result) {
    result->capacity = capacity;
    result->ptr      = result->data;
  }
  return result;
}

void* ngfi_sa_alloc(ngfi_sa *allocator, size_t nbytes) {
  assert(allocator);
  void           *result             = NULL;
  const ptrdiff_t consumed_capacity  = allocator->ptr - allocator->data;
  const ptrdiff_t available_capacity = (ptrdiff_t)allocator->capacity - consumed_capacity;
  if (available_capacity >= (ptrdiff_t)nbytes) {
    result = allocator->ptr;
    allocator->ptr += nbytes;
  } 
  return result;
}

void ngfi_sa_reset(ngfi_sa *allocator) {
  assert(allocator);
  allocator->ptr = allocator->data;
}

void ngfi_sa_destroy(ngfi_sa *allocator) {
  assert(allocator);
  free(allocator);
}

ngfi_sa* ngfi_tmp_store() {
  static NGFI_THREADLOCAL ngfi_sa *temp_storage = NULL;
  if (temp_storage == NULL) {
    const size_t sa_capacity = 1024 * 100; // 100K
    temp_storage = ngfi_sa_create(sa_capacity);
  }
  return temp_storage;
}

