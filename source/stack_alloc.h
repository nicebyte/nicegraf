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

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ngf_sa_t {
  uint8_t *ptr;
  size_t   capacity;
  uint8_t  data[];
} _ngf_sa;

/**
 * creates a new stack allocator with the given capacity.
 */
_ngf_sa* _ngf_sa_create(size_t capacity);

/**
 * allocates a specified amount of bytes from the given stack allocator
 * and returns a pointer to the start of the allocated region.
 * if the allocator has no available capacity to accomodate the request,
 * returns a null pointer.
 */
void* _ngf_sa_alloc(_ngf_sa *allocator, size_t nbytes);


/**
 * resets the state of the given stack allocator. capacity is fully restored,
 * all pointers to memory previously allocated are invalidated.
 */
void _ngf_sa_reset(_ngf_sa *allocator);

/**
 * tear down the given stack allocator.
 */
void _ngf_sa_destroy(_ngf_sa *allocator);

#ifdef __cplusplus
}
#endif


