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

#include <stdint.h>
#include <stdlib.h>

#define _NGF_DARRAY_OF(type) struct  { \
  type *data = NULL; \
  type *endptr = NULL; \
  uint32_t capacity = 0u; \
}

#if !defined(__cplusplus)
#define decltype(x) void*
#endif

#define _NGF_DARRAY_RESET(a, c) { \
  a.data = (decltype(a.data))malloc(sizeof(a.data[0]) * c); \
  a.endptr = a.data; \
  a.capacity = c; \
}

#define _NGF_DARRAY_DESTROY(a) (free(a.data))

#define _NGF_DARRAY_APPEND(a, v) { \
  ptrdiff_t cur_size = a.endptr - a.data; \
  if (cur_size >= a.capacity) { \
    a.capacity <<= 1u; \
    a.data = (decltype(a.data)) realloc(a.data, sizeof(a.data[0]) * a.capacity); \
    a.endptr = &a.data[cur_size]; \
  } \
  *(a.endptr++) = v; \
}

#define _NGF_DARRAY_CLEAR(a) (a.endptr = a.data)
#define _NGF_DARRAY_SIZE(a) (a.endptr - a.data)
#define _NGF_DARRAY_AT(a, i) (a.data[i])

