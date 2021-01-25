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

#include "nicegraf.h"

#include <assert.h>
#if defined(_WIN32) || defined(_WIN64)
#define NGFI_THREADLOCAL __declspec(thread)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// emulate pthread mutexes and condvars
typedef CRITICAL_SECTION   pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;
#define pthread_mutex_lock(m)    (EnterCriticalSection(m), 0)
#define pthread_mutex_unlock(m)  (LeaveCriticalSection(m), 0)
#define pthread_mutex_init(m, a) (InitializeCriticalSection(m), 0)
#define pthread_mutex_destroy(m) (DeleteCriticalSection(m), 0)
#define pthread_cond_init(c, a)  (InitializeConditionVariable(c))
#define pthread_cond_wait(c, m)  (SleepConditionVariableCS(c, m, INFINITE))
#define pthread_cond_signal(c)   (WakeConditionVariable(c))
#define ngfi_cur_thread_id()     (GetCurrentThreadId())
#define pthread_cond_destroy(c)
#else
#define NGFI_THREADLOCAL __thread
#include <pthread.h>
#if defined(__APPLE__)
#define ngfi_cur_thread_id() pthread_mach_thread_np(pthread_self())
#else
#include <sys/syscall.h>
#include <unistd.h>
#define ngfi_cur_thread_id() syscall(SYS_gettid)
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
#define NGFI_IGNORE_VAR(name) { (void)name; }

// MSVC warnings that are safe to ignore.
#pragma warning(disable : 4201)
#pragma warning(disable : 4200)
#pragma warning(disable : 4204)
#pragma warning(disable : 4221)

// Info about a native resource binding.
typedef struct {
  uint32_t ngf_binding_id;     // Nicegraf binding id.
  uint32_t native_binding_id;  // Actual backend api-specific binding id.
  uint32_t ncis_bindings;      // Number of associated combined image/sampler
                               // bindings.
  uint32_t* cis_bindings;      // Associated combined image/sampler bindings.
} ngfi_native_binding;

// Mapping from (set, binding) to (native binding).
typedef ngfi_native_binding** ngfi_native_binding_map;

// Generates a (set, binding) to (native binding) map from the given pipeline
// layout and combined image/sampler maps.
ngf_error ngfi_create_native_binding_map(
    const ngf_pipeline_layout_info* layout,
    const ngf_plmd_cis_map*         images_to_cis,
    const ngf_plmd_cis_map*         samplers_to_cis,
    ngfi_native_binding_map*        result);

void ngfi_destroy_binding_map(ngfi_native_binding_map map);

const ngfi_native_binding*
ngfi_binding_map_lookup(const ngfi_native_binding_map map, uint32_t set, uint32_t binding);

typedef enum {
  NGFI_CMD_BUFFER_READY,
  NGFI_CMD_BUFFER_RECORDING,
  NGFI_CMD_BUFFER_AWAITING_SUBMIT,
  NGFI_CMD_BUFFER_SUBMITTED
} ngfi_cmd_buffer_state;

#define NGFI_CMD_BUF_RECORDABLE(s) \
  (s == NGFI_CMD_BUFFER_READY || s == NGFI_CMD_BUFFER_AWAITING_SUBMIT)

ngf_error ngfi_transition_cmd_buf(
    ngfi_cmd_buffer_state* cur_state,
    bool                   has_active_renderpass,
    ngfi_cmd_buffer_state  new_state);

#define NGFI_TRANSITION_CMD_BUF(b, new_state)                                                    \
  if (ngfi_transition_cmd_buf(&(b)->state, (b)->renderpass_active, new_state) != NGF_ERROR_OK) { \
    return NGF_ERROR_INVALID_OPERATION;                                                          \
  }

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

// Access to device capabilities global structure.
void                           ngfi_device_caps_create(void);
ngf_device_capabilities*       ngfi_device_caps_lock(void);
void                           ngfi_device_caps_unlock(ngf_device_capabilities*);
const ngf_device_capabilities* ngfi_device_caps_read(void);

extern ngf_diagnostic_info ngfi_diag_info;

#ifdef __cplusplus
}
#endif
