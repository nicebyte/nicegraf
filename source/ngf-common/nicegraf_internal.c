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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "nicegraf_internal.h"

#include "dynamic_array.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

ngf_diagnostic_info ngfi_diag_info = {
    .verbosity = NGF_DIAGNOSTICS_VERBOSITY_DEFAULT,
    .userdata  = NULL,
    .callback  = NULL};

// Default allocation callbacks.
void* ngf_default_alloc(size_t obj_size, size_t nobjs) {
  return malloc(obj_size * nobjs);
}

void ngf_default_free(void* ptr, size_t s, size_t n) {
  NGFI_IGNORE_VAR(s);
  NGFI_IGNORE_VAR(n);
  free(ptr);
}

const ngf_allocation_callbacks NGF_DEFAULT_ALLOC_CB = {ngf_default_alloc, ngf_default_free};

const ngf_allocation_callbacks* NGF_ALLOC_CB = &NGF_DEFAULT_ALLOC_CB;

void ngf_set_allocation_callbacks(const ngf_allocation_callbacks* callbacks) {
  if (callbacks == NULL) {
    NGF_ALLOC_CB = &NGF_DEFAULT_ALLOC_CB;
  } else {
    NGF_ALLOC_CB = callbacks;
  }
}

static struct {
  ngf_device_capabilities caps;
  bool                    initialized;
  pthread_mutex_t         mut;
} ngfi_device_caps;

void ngfi_device_caps_create(void) {
  pthread_mutex_init(&ngfi_device_caps.mut, NULL);
}

const ngf_device_capabilities* ngfi_device_caps_read(void) {
  const ngf_device_capabilities* result = NULL;
  pthread_mutex_lock(&ngfi_device_caps.mut);
  if (ngfi_device_caps.initialized) { result = &ngfi_device_caps.caps; }
  pthread_mutex_unlock(&ngfi_device_caps.mut);
  return result;
}

ngf_device_capabilities* ngfi_device_caps_lock(void) {
  pthread_mutex_lock(&ngfi_device_caps.mut);
  return ngfi_device_caps.initialized ? NULL : &ngfi_device_caps.caps;
}

void ngfi_device_caps_unlock(ngf_device_capabilities* ptr) {
  if (ptr == &ngfi_device_caps.caps) {
    ngfi_device_caps.initialized = true;
    pthread_mutex_unlock(&ngfi_device_caps.mut);
  }
}

extern ngf_diagnostic_info ngfi_diag_info;

