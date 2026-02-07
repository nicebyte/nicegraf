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

#include "macros.h"

#include <stdio.h>
#include <stdlib.h>

struct {
  size_t          allocated_mem;
  pthread_mutex_t mut;
} ngfi_sys_alloc_stats;

ngf_diagnostic_info ngfi_diag_info = {
    .verbosity = NGF_DIAGNOSTICS_VERBOSITY_DEFAULT,
    .userdata  = NULL,
    .callback  = NULL};

// Default allocation callbacks.
void* ngf_default_alloc(size_t obj_size, size_t nobjs, void* userdata) {
  NGFI_IGNORE_VAR(userdata);
  pthread_mutex_lock(&ngfi_sys_alloc_stats.mut);
  ngfi_sys_alloc_stats.allocated_mem += obj_size * nobjs;
  pthread_mutex_unlock(&ngfi_sys_alloc_stats.mut);
  return malloc(obj_size * nobjs);
}

void ngf_default_free(void* ptr, size_t s, size_t n, void* userdata) {
  NGFI_IGNORE_VAR(s);
  NGFI_IGNORE_VAR(n);
  NGFI_IGNORE_VAR(userdata);
  pthread_mutex_lock(&ngfi_sys_alloc_stats.mut);
  ngfi_sys_alloc_stats.allocated_mem -= s * n;
  pthread_mutex_unlock(&ngfi_sys_alloc_stats.mut);
  free(ptr);
}

const ngf_allocation_callbacks NGF_DEFAULT_ALLOC_CB = {ngf_default_alloc, ngf_default_free, NULL};

const ngf_allocation_callbacks* NGF_ALLOC_CB = &NGF_DEFAULT_ALLOC_CB;

void ngfi_set_allocation_callbacks(const ngf_allocation_callbacks* callbacks) {
  static bool mutex_inited = false;
  if (callbacks == NULL) {
    NGF_ALLOC_CB = &NGF_DEFAULT_ALLOC_CB;
    if (!mutex_inited) {
      pthread_mutex_init(&ngfi_sys_alloc_stats.mut, 0);
      mutex_inited = true;
    }
  } else {
    NGF_ALLOC_CB = callbacks;
  }
}

ngf_sample_count ngfi_get_highest_sample_count(size_t counts_bitmap) {
  size_t res = (size_t)NGF_SAMPLE_COUNT_64;
  while ((res & counts_bitmap) == 0 && res > 1) { res >>= 1; }
  return (ngf_sample_count)res;
}

void ngfi_dump_sys_alloc_dbgstats(FILE* out) {
  fprintf(out, "System allocator debug stats\n");
  fprintf(out, "Requested memory:\t%zu\n", ngfi_sys_alloc_stats.allocated_mem);
}