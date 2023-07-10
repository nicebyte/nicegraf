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

#include <stdint.h>

/**
 * @file
 * \defgroup ngf_util Utility Library
 * 
 * This module contains routines and structures that provide auxiliary functionality or help reduce boilerplate.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct ngf_util_graphics_pipeline_data
 * \ingroup ngf_util
 * 
 * Contains all the data describing a graphics pipeline, with the exception
 * of shader stages.
 * 
 * See \ref ngf_util_create_default_graphics_pipeline_data for more details.
 */
typedef struct ngf_util_graphics_pipeline_data {
  ngf_graphics_pipeline_info pipeline_info; /**< Can be used to initialize a new pipeline object. */
  ngf_depth_stencil_info     depth_stencil_info;
  ngf_vertex_input_info      vertex_input_info;
  ngf_multisample_info       multisample_info;
  ngf_rasterization_info     rasterization_info;
  ngf_input_assembly_info    input_assembly_info;
  ngf_specialization_info    spec_info;
} ngf_util_graphics_pipeline_data;

/**
 * \ingroup ngf_util
 * 
 * Creates a configuration for a graphics pipeline object with some pre-set defaults.
 * 
 * The fields of the members of the resulting \ref ngf_util_graphics_pipeline_data are set such that
 * they match OpenGL defaults. They can be adjusted later. The pointer fields of \ref
 * ngf_util_graphics_pipeline_data::pipeline_info are set to point to the corresponding members of
 * \ref ngf_util_graphics_pipeline_data.
 * 
 * The only aspect of configuration that this function does not set are the programmable shader stages. After the application code sets those, \ref
 * ngf_util_graphics_pipeline_data::pipeline_info can be used to create a new pipeline object.
 * 
 * @param result Pipeline configuration data will be stored here.
 */
void ngf_util_create_default_graphics_pipeline_data(ngf_util_graphics_pipeline_data* result);

/**
 * \ingroup ngf_util
 * 
 * Converts a nicegraf error code to a human-readable string.
 * 
 * @param err The error enum to get the string for.
 * @return A human-readable error message.
 */
const char* ngf_util_get_error_name(const ngf_error err);

/**
 * \ingroup ngf_util
 * 
 * Rounds `value` up to the nearest multiple of `alignment`.
 */
static inline size_t ngf_util_align_size(size_t value, size_t alignment) {
    const size_t m = value % alignment;
    return value + (m > 0 ? (alignment - m) : 0u);
}

#ifdef __cplusplus
}
#endif
