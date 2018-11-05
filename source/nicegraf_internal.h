/**
Copyright © 2018 nicegraf contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the “Software”), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "nicegraf.h"

#if defined(_WIN32) || defined(_WIN64)
#define NGF_THREADLOCAL __declspec(thread)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// emulate pthread mutexes lol
typedef CRITICAL_SECTION pthread_mutex_t;
#define pthread_mutex_lock(m) (EnterCriticalSection(m),0)
#define pthread_mutex_unlock(m) (LeaveCriticalSection(m),0)
#define pthread_mutex_init(m, a) (InitializeCriticalSection(m),0)
#define pthread_mutex_destroy(m) (DeleteCriticalSection(m),0)
#define pthread_getthreadid_np() (GetCurrentThreadId())
#else
#define NGF_THREADLOCAL __thread
#include <pthread.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Custom allocation callbacks.
extern const ngf_allocation_callbacks *NGF_ALLOC_CB;

// Convenience macros for invoking custom memory allocation callbacks.
#define NGF_ALLOC(type) ((type*) NGF_ALLOC_CB->allocate(sizeof(type), 1))
#define NGF_ALLOCN(type, n) ((type*) NGF_ALLOC_CB->allocate(sizeof(type), n))
#define NGF_FREE(ptr) (NGF_ALLOC_CB->free((void*)(ptr), sizeof(*ptr), 1))
#define NGF_FREEN(ptr, n) (NGF_ALLOC_CB->free((void*)(ptr), sizeof(*ptr), n))

// Macro for determining size of arrays.
#if defined(_MSC_VER)
#include <stdlib.h>
#define NGF_ARRAYSIZE(arr) _countof(arr)
#else
#define NGF_ARRAYSIZE(arr) (sizeof(arr)/sizeof(arr[0]))
#endif

// For when you don't feel like comparing structs field-by-field.
#define NGF_STRUCT_EQ(s1, s2) (sizeof(s1) == sizeof(s2) && \
                               memcmp((void*)&s1, (void*)&s2, sizeof(s1)) == 0)

// It is $CURRENT_YEAR and C does not have a standard thing for this.
#define NGF_MAX(a, b) (a > b ? a : b)

// A fast fixed-size block allocator.
typedef struct _ngf_block_allocator _ngf_block_allocator;

// Creates a new block allocator with a given fixed `block_size` and a given
// initial capacity of `nblocks`.
_ngf_block_allocator* _ngf_blkalloc_create(uint32_t block_size, uint32_t nblocks);

// Destroys the given block allocator. All unfreed pointers obtained from the
// destroyed allocator become invalid.
void _ngf_blkalloc_destroy(_ngf_block_allocator *alloc);

// Allocates the next free block from the allocator. Returns NULL on error.
void* _ngf_blkalloc_alloc(_ngf_block_allocator *alloc);

typedef enum {
  _NGF_BLK_NO_ERROR,
  _NGF_BLK_DOUBLE_FREE,
  _NGF_BLK_WRONG_ALLOCATOR
} _ngf_blkalloc_error;

// Returns the given block to the allocator.
// Freeing a NULL pointer does nothing.
_ngf_blkalloc_error _ngf_blkalloc_free(_ngf_block_allocator *alloc, void *ptr);

#ifdef __cplusplus
}
#endif
