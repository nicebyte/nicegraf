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
#pragma once

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define NGFI_DARRAY_OF(type) struct  { \
  type *data; \
  type *endptr; \
  uint32_t capacity; \
}

#if !defined(__cplusplus)
#define decltype(x) void*
#endif

#define NGFI_DARRAY_RESET(a, c) { \
  a.data = (decltype(a.data))malloc(sizeof(a.data[0]) * c); \
  a.endptr = a.data; \
  a.capacity = c; \
}

#define NGFI_DARRAY_RESIZE(a, s) { \
  uint32_t size = (s); \
  if (a.capacity < size) { \
    a.data = (decltype(a.data))realloc(a.data, sizeof(a.data[0]) * size); \
    a.capacity = size; \
  } \
  a.endptr = a.data + size; \
} 

#define NGFI_DARRAY_DESTROY(a) if(a.data != NULL) { \
  free(a.data); \
  a.data = a.endptr = NULL; \
}

#define NGFI_DARRAY_APPEND(a, v) { \
  ptrdiff_t cur_size = a.endptr - a.data; \
  assert(cur_size >= 0);                                 \
  if ((size_t)cur_size >= a.capacity) { \
    a.capacity <<= 1u; \
    decltype(a.data) tmp = (decltype(a.data)) realloc(a.data, sizeof(a.data[0]) * a.capacity); \
    assert(tmp != NULL); \
    a.data = tmp; \
    a.endptr = &a.data[cur_size]; \
  } \
  *(a.endptr++) = v; \
}

#define NGFI_DARRAY_APPEND_EMPTY(a) { \
  ptrdiff_t cur_size = a.endptr - a.data; \
  assert(cur_size >= 0);                                 \
  if ((size_t)cur_size >= a.capacity) { \
    a.capacity <<= 1u; \
    decltype(a.data) tmp = (decltype(a.data)) realloc(a.data, sizeof(a.data[0]) * a.capacity); \
    assert(tmp != NULL); \
    a.data = tmp; \
    a.endptr = &a.data[cur_size]; \
  } \
  a.endptr++; \
}

#define NGFI_DARRAY_CLEAR(a) ((a).endptr = (a).data)
#define NGFI_DARRAY_SIZE(a) ((uint32_t)((a).endptr - (a).data))
#define NGFI_DARRAY_AT(a, i) ((a).data[(i)])

#define NGFI_DARRAY_POP(a) {\
  assert((a).data != (a).endptr); \
  --((a).endptr);                  \
  }

#define NGFI_DARRAY_EMPTY(a) (a.endptr == a.data)

#define NGFI_DARRAY_BACKPTR(a) (a.endptr - 1)

#define NGFI_DARRAY_FOREACH(a, countername) \
for (size_t countername = 0; (countername) < NGFI_DARRAY_SIZE(a); ++(countername))
