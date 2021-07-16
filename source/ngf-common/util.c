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

#include "ngf-common/macros.h"
#include "nicegraf-util.h"

#include <assert.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

void ngf_util_create_default_graphics_pipeline_data(ngf_util_graphics_pipeline_data* result) {
  ngf_blend_info bi;
  bi.enable          = false;
  result->blend_info = bi;

  ngf_stencil_info default_stencil = {
      .fail_op       = NGF_STENCIL_OP_KEEP,
      .pass_op       = NGF_STENCIL_OP_KEEP,
      .depth_fail_op = NGF_STENCIL_OP_KEEP,
      .compare_op    = NGF_COMPARE_OP_EQUAL,
      .compare_mask  = 0,
      .write_mask    = 0,
      .reference     = 0};
  ngf_depth_stencil_info dsi = {
      .min_depth     = 0.0f,
      .max_depth     = 1.0f,
      .stencil_test  = false,
      .depth_test    = false,
      .depth_write   = false,
      .depth_compare = NGF_COMPARE_OP_LESS,
      .front_stencil = default_stencil,
      .back_stencil  = default_stencil};
  result->depth_stencil_info = dsi;

  ngf_vertex_input_info vii = {.nattribs = 0, .nvert_buf_bindings = 0};
  result->vertex_input_info = vii;

  ngf_multisample_info msi = {.sample_count = NGF_SAMPLE_COUNT_1, .alpha_to_coverage = false};
  result->multisample_info = msi;

  ngf_rasterization_info ri = {
      .cull_mode    = NGF_CULL_MODE_NONE,
      .discard      = false,
      .front_face   = NGF_FRONT_FACE_COUNTER_CLOCKWISE,
      .polygon_mode = NGF_POLYGON_MODE_FILL};
  result->rasterization_info = ri;

  ngf_specialization_info spi = {
      .specializations  = NULL,
      .nspecializations = 0u,
      .value_buffer     = NULL};
  result->spec_info = spi;

  ngf_graphics_pipeline_info gpi = {
      .blend          = &result->blend_info,
      .depth_stencil  = &result->depth_stencil_info,
      .input_info     = &result->vertex_input_info,
      .primitive_type = NGF_PRIMITIVE_TYPE_TRIANGLE_LIST,
      .multisample    = &result->multisample_info,
      .shader_stages  = {NULL},
      .nshader_stages = 0u,
      .rasterization  = &result->rasterization_info,
      .spec_info      = &result->spec_info};
  result->pipeline_info = gpi;
}

const char* ngf_util_get_error_name(const ngf_error err) {
  static const char* ngf_error_names[] = {
      "NGF_ERROR_OK",
      "NGF_ERROR_OUT_OF_MEM",
      "NGF_ERROR_OBJECT_CREATION_FAILED",
      "NGF_ERROR_OUT_OF_BOUNDS",
      "NGF_ERROR_INVALID_FORMAT",
      "NGF_ERROR_INVALID_SIZE",
      "NGF_ERROR_INVALID_ENUM",
      "NGF_ERROR_INVALID_OPERATION"};
  if (err > NGFI_ARRAYSIZE(ngf_error_names)) { return "invalid error code"; }
  return ngf_error_names[err];
}
