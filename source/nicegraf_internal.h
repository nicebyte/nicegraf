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
#else
#define NGF_THREADLOCAL __thread
#include <pthread.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern const ngf_allocation_callbacks *NGF_ALLOC_CB;

#define NGF_ALLOC(type) ((type*) NGF_ALLOC_CB->allocate(sizeof(type), 1))
#define NGF_ALLOCN(type, n) ((type*) NGF_ALLOC_CB->allocate(sizeof(type), n))
#define NGF_FREE(ptr) (NGF_ALLOC_CB->free(ptr, sizeof(*ptr), 1))
#define NGF_FREEN(ptr, n) (NGF_ALLOC_CB->free(ptr, sizeof(*ptr), n))

#if defined(_MSC_VER)
#include <stdlib.h>
#define NGF_ARRAYSIZE(arr) _countof(arr)
#else
#define NGF_ARRAYSIZE(arr) (sizeof(arr)/sizeof(arr[0]))
#endif

#define NGF_STRUCT_EQ(s1, s2) (sizeof(s1) == sizeof(s2) && \
                               memcmp((void*)&s1, (void*)&s2, sizeof(s1)) == 0)

#define NGF_MAX(a, b) (a > b ? a : b)

#ifdef __cplusplus
}
#endif
