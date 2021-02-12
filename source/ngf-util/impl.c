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

#include "nicegraf_util.h"

#include "ngf-common/nicegraf_internal.h"

#include <assert.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

void ngf_util_create_default_graphics_pipeline_data(
    const ngf_irect2d*               window_size,
    ngf_util_graphics_pipeline_data* result) {
  NGFI_IGNORE_VAR(window_size);
  ngf_blend_info bi;
  bi.enable                        = false;
  result->blend_info               = bi;
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
  ngf_vertex_input_info vii  = {.nattribs = 0, .nvert_buf_bindings = 0};
  result->vertex_input_info  = vii;
  ngf_multisample_info msi   = {.sample_count = NGF_SAMPLE_COUNT_1, .alpha_to_coverage = false};
  result->multisample_info   = msi;
  ngf_rasterization_info ri  = {
      .cull_mode    = NGF_CULL_MODE_NONE,
      .discard      = false,
      .front_face   = NGF_FRONT_FACE_COUNTER_CLOCKWISE,
      .polygon_mode = NGF_POLYGON_MODE_FILL};
  result->rasterization_info  = ri;
  ngf_specialization_info spi = {
      .specializations  = NULL,
      .nspecializations = 0u,
      .value_buffer     = NULL};
  result->spec_info              = spi;
  ngf_pipeline_layout_info pli   = {.ndescriptor_set_layouts = 0u, .descriptor_set_layouts = NULL};
  result->layout_info            = pli;
  ngf_graphics_pipeline_info gpi = {
      .blend          = &result->blend_info,
      .depth_stencil  = &result->depth_stencil_info,
      .input_info     = &result->vertex_input_info,
      .primitive_type = NGF_PRIMITIVE_TYPE_TRIANGLE_LIST,
      .multisample    = &result->multisample_info,
      .shader_stages  = {NULL},
      .nshader_stages = 0u,
      .rasterization  = &result->rasterization_info,
      .layout         = &result->layout_info,
      .spec_info      = &result->spec_info};
  result->pipeline_info = gpi;
}

ngf_error ngf_util_create_simple_layout(
    const ngf_descriptor_info* desc,
    uint32_t                   ndesc,
    ngf_pipeline_layout_info*  result) {
  assert(desc);
  assert(result);
  ngf_error err                             = NGF_ERROR_OK;
  result->ndescriptor_set_layouts           = 1u;
  ngf_descriptor_set_layout_info* dsl       = NGFI_ALLOC(ngf_descriptor_set_layout_info);
  ngf_descriptor_info*            desc_copy = NGFI_ALLOCN(ngf_descriptor_info, ndesc);
  memcpy(desc_copy, desc, ndesc * sizeof(ngf_descriptor_info));
  dsl->descriptors               = desc_copy;
  dsl->ndescriptors              = ndesc;
  result->descriptor_set_layouts = dsl;
  return err;
}

ngf_descriptor_type _plmd_desc_to_ngf(uint32_t plmd_desc_type) {
  switch (plmd_desc_type) {
  case NGF_PLMD_DESC_UNIFORM_BUFFER:
    return NGF_DESCRIPTOR_UNIFORM_BUFFER;
  case NGF_PLMD_DESC_IMAGE:
    return NGF_DESCRIPTOR_TEXTURE;
  case NGF_PLMD_DESC_SAMPLER:
    return NGF_DESCRIPTOR_SAMPLER;
  case NGF_PLMD_DESC_COMBINED_IMAGE_SAMPLER:
    return NGF_DESCRIPTOR_TEXTURE_AND_SAMPLER;
  default:
    assert(false);
    return 0u;
  }
}

uint32_t _plmd_stage_flags_to_ngf(uint32_t plmd_stage_flags) {
  uint32_t result = 0u;
  if (plmd_stage_flags & NGF_PLMD_STAGE_VISIBILITY_VERTEX_BIT)
    result |= NGF_DESCRIPTOR_VERTEX_STAGE_BIT;
  if (plmd_stage_flags & NGF_PLMD_STAGE_VISIBILITY_FRAGMENT_BIT)
    result |= NGF_DESCRIPTOR_FRAGMENT_STAGE_BIT;
  return result;
}

ngf_error ngf_util_create_pipeline_layout_from_metadata(
    const ngf_plmd_layout*    layout_metadata,
    ngf_pipeline_layout_info* result) {
  assert(layout_metadata);
  assert(result);

  ngf_error                       err                         = NGF_ERROR_OK;
  ngf_descriptor_set_layout_info* descriptor_set_layout_infos = NULL;
  descriptor_set_layout_infos =
      NGFI_ALLOCN(ngf_descriptor_set_layout_info, layout_metadata->ndescriptor_sets);
  if (descriptor_set_layout_infos == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_util_create_pipeline_layout_from_metadata_cleanup;
  }
  memset(
      descriptor_set_layout_infos,
      0,
      sizeof(ngf_descriptor_set_layout_info) * layout_metadata->ndescriptor_sets);

  result->ndescriptor_set_layouts = layout_metadata->ndescriptor_sets;
  result->descriptor_set_layouts  = descriptor_set_layout_infos;

  if (descriptor_set_layout_infos == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_util_create_pipeline_layout_from_metadata_cleanup;
  }

  for (uint32_t set = 0u; set < layout_metadata->ndescriptor_sets; ++set) {
    ngf_descriptor_set_layout_info* set_layout_info = &descriptor_set_layout_infos[set];
    set_layout_info->ndescriptors = layout_metadata->set_layouts[set]->ndescriptors;
    ngf_descriptor_info* descriptors =
        NGFI_ALLOCN(ngf_descriptor_info, set_layout_info->ndescriptors);
    set_layout_info->descriptors = descriptors;
    if (set_layout_info->descriptors == NULL) {
      err = NGF_ERROR_OUT_OF_MEM;
      goto ngf_util_create_pipeline_layout_from_metadata_cleanup;
    }
    const ngf_plmd_descriptor_set_layout* descriptor_set_metadata =
        layout_metadata->set_layouts[set];
    for (uint32_t d = 0u; d < set_layout_info->ndescriptors; ++d) {
      const ngf_plmd_descriptor* descriptor_metadata = &descriptor_set_metadata->descriptors[d];
      descriptors[d].id                              = descriptor_metadata->binding;
      descriptors[d].type                            = _plmd_desc_to_ngf(descriptor_metadata->type);
      descriptors[d].stage_flags =
          _plmd_stage_flags_to_ngf(descriptor_metadata->stage_visibility_mask);
    }
  }
ngf_util_create_pipeline_layout_from_metadata_cleanup:
  return err;
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
