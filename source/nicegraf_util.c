/**
Copyright © 2018 nicegraf contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "nicegraf_util.h"
#include "nicegraf_internal.h"

#include <assert.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

void ngf_util_create_default_graphics_pipeline_data(
    const ngf_irect2d *window_size,
    ngf_util_graphics_pipeline_data *result) {
  ngf_blend_info bi = {
    .enable = false,
    .sfactor = NGF_BLEND_FACTOR_ONE,
    .dfactor = NGF_BLEND_FACTOR_ZERO
  };
  result->blend_info = bi;
  ngf_stencil_info default_stencil = {
    .fail_op = NGF_STENCIL_OP_KEEP,
    .pass_op = NGF_STENCIL_OP_KEEP,
    .depth_fail_op = NGF_STENCIL_OP_KEEP,
    .compare_op = NGF_COMPARE_OP_EQUAL,
    .compare_mask = 0,
    .write_mask = 0,
    .reference = 0 
  };
  ngf_depth_stencil_info dsi = {
    .min_depth = 0.0f,
    .max_depth = 1.0f,
    .stencil_test = false,
    .depth_test = false,
    .depth_write = false,
    .depth_compare = NGF_COMPARE_OP_LESS,
    .front_stencil = default_stencil,
    .back_stencil = default_stencil
  };
  result->depth_stencil_info = dsi;
  ngf_vertex_input_info vii = {
    .nattribs = 0,
    .nvert_buf_bindings = 0
  };
  result->vertex_input_info = vii;
  ngf_multisample_info msi = {
    .multisample = false,
    .alpha_to_coverage = false
  };
  result->multisample_info = msi;
  ngf_rasterization_info ri = {
    .cull_mode = NGF_CULL_MODE_NONE,
    .discard = false,
    .front_face = NGF_FRONT_FACE_COUNTER_CLOCKWISE,
    .line_width = 1.0f,
    .polygon_mode = NGF_POLYGON_MODE_FILL
  };
  result->rasterization_info = ri;
  uint32_t dynamic_state_mask = 0u;
  if (window_size != NULL) {
    result->scissor = result->viewport = *window_size;
  } else {
    dynamic_state_mask = NGF_DYNAMIC_STATE_VIEWPORT_AND_SCISSOR;
  }
  ngf_specialization_info spi = {
    .specializations = NULL,
    .nspecializations = 0u,
    .value_buffer = NULL
  };
  result->spec_info = spi;
  ngf_pipeline_layout_info pli = {
    .ndescriptor_set_layouts = 0u,
    .descriptor_set_layouts = NULL
  };
  result->layout_info  = pli;
  ngf_graphics_pipeline_info gpi = {
    .blend = &result->blend_info,
    .depth_stencil = &result->depth_stencil_info,
    .dynamic_state_mask = dynamic_state_mask,
    .input_info = &result->vertex_input_info,
    .primitive_type = NGF_PRIMITIVE_TYPE_TRIANGLE_LIST,
    .multisample = &result->multisample_info,
    .shader_stages = {NULL},
    .nshader_stages = 0u,
    .rasterization = &result->rasterization_info,
    .layout = &result->layout_info,
    .scissor = &result->scissor,
    .viewport = &result->viewport,
    .spec_info = &result->spec_info
  };
  result->pipeline_info = gpi;
}

ngf_error ngf_util_create_simple_layout(const ngf_descriptor_info *desc,
                                        uint32_t ndesc,
                                        ngf_pipeline_layout_info *result) {
  assert(desc);
  assert(result);
  ngf_error err = NGF_ERROR_OK;
  result->ndescriptor_set_layouts = 1u;
  ngf_descriptor_set_layout_info *dsl =
      NGF_ALLOC(ngf_descriptor_set_layout_info);
  ngf_descriptor_info *desc_copy = NGF_ALLOCN(ngf_descriptor_info, ndesc);
  memcpy(desc_copy, desc, ndesc * sizeof(ngf_descriptor_info));
  dsl->descriptors = desc_copy;
  dsl->ndescriptors = ndesc;
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
    const ngf_plmd_layout *layout_metadata,
    ngf_pipeline_layout_info *result) {
  assert(layout_metadata);
  assert(result);

  ngf_error err = NGF_ERROR_OK;
  ngf_descriptor_set_layout_info *descriptor_set_layout_infos = NULL;
  descriptor_set_layout_infos = NGF_ALLOCN(ngf_descriptor_set_layout_info,
                                           layout_metadata->ndescriptor_sets);
  if (descriptor_set_layout_infos == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_util_create_pipeline_layout_from_metadata_cleanup;
  }
  memset(descriptor_set_layout_infos, 0,
         sizeof(ngf_descriptor_set_layout_info) *
           layout_metadata->ndescriptor_sets);

  result->ndescriptor_set_layouts = layout_metadata->ndescriptor_sets;
  result->descriptor_set_layouts = descriptor_set_layout_infos;
  
  if (descriptor_set_layout_infos == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_util_create_pipeline_layout_from_metadata_cleanup;
  }

  for (uint32_t set = 0u; set < layout_metadata->ndescriptor_sets; ++set) {
    ngf_descriptor_set_layout_info *set_layout_info =
       &descriptor_set_layout_infos[set];
    set_layout_info->ndescriptors =
        layout_metadata->set_layouts[set]->ndescriptors;
    ngf_descriptor_info *descriptors =
        NGF_ALLOCN(ngf_descriptor_info, set_layout_info->ndescriptors);
    set_layout_info->descriptors = descriptors;
    if (set_layout_info->descriptors == NULL) {
      err = NGF_ERROR_OUTOFMEM;
      goto ngf_util_create_pipeline_layout_from_metadata_cleanup;
    }
    const ngf_plmd_descriptor_set_layout *descriptor_set_metadata =
        layout_metadata->set_layouts[set];
    for (uint32_t d = 0u; d < set_layout_info->ndescriptors; ++d) {
      const ngf_plmd_descriptor *descriptor_metadata =
          &descriptor_set_metadata->descriptors[d];
      descriptors[d].id = descriptor_metadata->binding;
      descriptors[d].type = _plmd_desc_to_ngf(descriptor_metadata->type);
      descriptors[d].stage_flags =
          _plmd_stage_flags_to_ngf(descriptor_metadata->stage_visibility_mask);
    }
  }
ngf_util_create_pipeline_layout_from_metadata_cleanup:
  return err;
}

const char* ngf_util_get_error_name(const ngf_error err) {
  static const char* ngf_error_names[] = {
    "OK",
    "OUTOFMEM",
    "FAILED_TO_CREATE_PIPELINE",
    "INCOMPLETE_PIPELINE",
    "CANT_POPULATE_IMAGE",
    "IMAGE_CREATION_FAILED",
    "CREATE_SHADER_STAGE_FAILED"
    "INVALID_BINDING",
    "INVALID_INDEX_BUFFER_BINDING",
    "INVALID_VERTEX_BUFFER_BINDING",
    "INCOMPLETE_RENDER_TARGET",
    "INVALID_RESOURCE_SET_BINDING",
    "CONTEXT_CREATION_FAILED",
    "INVALID_CONTEXT",
    "SWAPCHAIN_CREATION_FAILED",
    "INVALID_SURFACE_FORMAT",
    "INITIALIZATION_FAILED",
    "SURFACE_CREATION_FAILED",
    "CANT_SHARE_CONTEXT",
    "BEGIN_FRAME_FAILED",
    "END_FRAME_FAILED",
    "OUT_OF_BOUNDS",
    "CMD_BUFFER_ALREADY_RECORDING",
    "CMD_BUFFER_WAS_NOT_RECORDING",
    "CONTEXT_ALREADY_CURRENT",
    "CALLER_HAS_CURRENT_CONTEXT",
    "CANNOT_SPECIALIZE_SHADER_STAGE_BINARY",
    "SHADER_STAGE_INVALID_BINARY_FORMAT",
    "CANNOT_READ_BACK_SHADER_STAGE_BINARY",
    "NO_DEFAULT_RENDER_TARGET",
    "NO_FRAME",
    "UNIFORM_BUFFER_SIZE_MISMATCH",
    "INVALID_IMAGE_FORMAT",
    "INVALID_DEPTH_FORMAT",
    "INVALID_VERTEX_ATTRIB_FORMAT",
    "INVALID_SAMPLER_ADDRESS_MODE",
  };
  return ngf_error_names[err];
}
