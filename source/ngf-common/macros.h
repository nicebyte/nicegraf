#pragma once

#include "nicegraf.h"

#include <assert.h>
#include <stdlib.h>
#if defined(_WIN32) || defined(_WIN64)
#define NGFI_THREADLOCAL __declspec(thread)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// emulate pthread mutexes
typedef CRITICAL_SECTION pthread_mutex_t;
#define pthread_mutex_lock(m)    (EnterCriticalSection(m), 0)
#define pthread_mutex_unlock(m)  (LeaveCriticalSection(m), 0)
#define pthread_mutex_init(m, a) (InitializeCriticalSection(m), 0)
#define pthread_mutex_destroy(m) (DeleteCriticalSection(m), 0)
// dynamic module loading
#define ModuleHandle HMODULE
#else
#define NGFI_THREADLOCAL __thread
#include <pthread.h>
#if !defined(__APPLE__)
// dynamic module loading (emulate win32 api)
#define LoadLibraryA(name) dlopen(name, RTLD_NOW)
#define GetProcAddress(h, n) dlsym(h, n)
#define ModuleHandle void*
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Custom allocation callbacks.
extern const ngf_allocation_callbacks* NGF_ALLOC_CB;

// Convenience macros for invoking custom memory allocation callbacks.
#define NGFI_ALLOC(type)     ((type*)NGF_ALLOC_CB->allocate(sizeof(type), 1))
#define NGFI_ALLOCN(type, n) ((type*)NGF_ALLOC_CB->allocate(sizeof(type), n))
#define NGFI_FREE(ptr)       (NGF_ALLOC_CB->free((void*)(ptr), sizeof(*ptr), 1))
#define NGFI_FREEN(ptr, n)   (NGF_ALLOC_CB->free((void*)(ptr), sizeof(*ptr), n))

// Macro for determining size of arrays.
#if defined(_MSC_VER)
#include <stdlib.h>
#define NGFI_ARRAYSIZE(arr) _countof(arr)
#else
#define NGFI_ARRAYSIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#endif

// For when you don't feel like comparing structs field-by-field.
#define NGFI_STRUCT_EQ(s1, s2) \
  (sizeof(s1) == sizeof(s2) && memcmp((void*)&s1, (void*)&s2, sizeof(s1)) == 0)

// It is $CURRENT_YEAR and C does not have a standard thing for this.
#define NGFI_MAX(a, b) (a > b ? a : b)
#define NGFI_MIN(a, b) (a < b ? a : b)

// For fixing unreferenced parameter warnings.
#define NGFI_IGNORE_VAR(name) \
  { (void)name; }

// MSVC warnings that are safe to ignore.
#pragma warning(disable : 4201)
#pragma warning(disable : 4200)
#pragma warning(disable : 4204)
#pragma warning(disable : 4221)

extern ngf_diagnostic_info ngfi_diag_info;

// Invoke diagnostic message callback directly.
#define NGFI_DIAG_MSG(level, fmt, ...)                                           \
  if (ngfi_diag_info.callback) {                                                 \
    ngfi_diag_info.callback(level, ngfi_diag_info.userdata, fmt, ##__VA_ARGS__); \
  }
#define NGFI_DIAG_INFO(fmt, ...)    NGFI_DIAG_MSG(NGF_DIAGNOSTIC_INFO, fmt, ##__VA_ARGS__)
#define NGFI_DIAG_WARNING(fmt, ...) NGFI_DIAG_MSG(NGF_DIAGNOSTIC_WARNING, fmt, ##__VA_ARGS__)
#define NGFI_DIAG_ERROR(fmt, ...)   NGFI_DIAG_MSG(NGF_DIAGNOSTIC_ERROR, fmt, ##__VA_ARGS__)

// Convenience macro to invoke diagnostic callback and raise error on unmet precondition.
#define NGFI_CHECK_CONDITION(cond, err_code, err_fmtstring, ...) \
  if (!(cond)) {                                                 \
    NGFI_DIAG_ERROR(err_fmtstring, ##__VA_ARGS__);               \
    return err_code;                                             \
  }

// Convenience macro to immediately die on an unmet precondition.
#define NGFI_CHECK_FATAL(cond, err_fmtstring, ...) \
if (!(cond)) { \
  NGFI_DIAG_ERROR(err_fmtstring, ##__VA_ARGS__); \
  exit(1); \
}

typedef long double ngfi_max_align_t;

#define NGFI_MAX_ALIGNMENT (sizeof(ngfi_max_align_t))

#ifdef __cplusplus
}
#endif
