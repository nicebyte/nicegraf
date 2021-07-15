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

/* Provides a mapping from nicegraf's (set, binding) to the backend platform's actual native binding. */
typedef struct ngfi_native_binding_map ngfi_native_binding_map;

/**
 * Finds the first instance of a serialized native binding map within a given character buffer and
 * returns a pointer to it within the buffer. Returns NULL if not found.
 */
const char* ngfi_find_serialized_native_binding_map(const char* input);

/**
 * Parses a native binding map out of the provided buffer. Returns NULL if parsing fails.
 */
ngfi_native_binding_map* ngfi_parse_serialized_native_binding_map(const char* serialized_native_binding_map);

/**
 * Looks up a binding from the given native binding map.
 */
uint32_t ngfi_native_binding_map_lookup(const ngfi_native_binding_map* map, uint32_t set, uint32_t binding);

/**
 * Deallocates any resources associated with the given native binding map.
 */
void ngfi_destroy_native_binding_map(ngfi_native_binding_map* map);

#ifdef __cplusplus
}
#endif
