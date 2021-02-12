/**
Copyright (c) 2021 nicegraf contributors
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

typedef struct ngfi_sa_t {
  uint8_t* ptr;
  size_t   capacity;
#pragma warning(push)
#pragma warning(disable : 4200)
  uint8_t data[];
#pragma warning(pop)
} ngfi_sa;

/**
 * creates a new stack allocator with the given capacity.
 */
ngfi_sa* ngfi_sa_create(size_t capacity);

/**
 * allocates a specified amount of bytes from the given stack allocator
 * and returns a pointer to the start of the allocated region.
 * if the allocator has no available capacity to accomodate the request,
 * returns a null pointer.
 */
void* ngfi_sa_alloc(ngfi_sa* allocator, size_t nbytes);

/**
 * resets the state of the given stack allocator. capacity is fully restored,
 * all pointers to memory previously allocated are invalidated.
 */
void ngfi_sa_reset(ngfi_sa* allocator);

/**
 * tear down the given stack allocator.
 */
void ngfi_sa_destroy(ngfi_sa* allocator);

/**
 * Per-thread temporary storage based on stack allocator.
 */
ngfi_sa* ngfi_tmp_store(void);

#ifdef __cplusplus
}
#endif
