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

#ifdef __cplusplus
extern "C" {
#endif

// Info about a native resource binding.
typedef struct {
  uint32_t ngf_binding_id;     // nicegraf binding id.
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

#ifdef __cplusplus
}
#endif


