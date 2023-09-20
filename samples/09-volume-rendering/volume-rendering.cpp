/**
 * Copyright (c) 2023 nicegraf contributors
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

#define _CRT_SECURE_NO_WARNINGS
#include "check.h"
#include "imgui.h"
#include "logging.h"
#include "nicegraf-util.h"
#include "nicegraf-wrappers.h"
#include "nicemath.h"
#include "sample-interface.h"
#include "shader-loader.h"

#include <stdio.h>
#include <string>

using namespace ngf_misc;

namespace ngf_samples {

namespace volume_rendering {

struct uniforms {
  nm::float4x4 transform_matrix;
  float        aspect_ratio;
};

struct state {
  ngf::image                         volume;
  ngf::sampler                       sampler;
  ngf::graphics_pipeline             pipeline;
  ngf::uniform_multibuffer<uniforms> uniforms_multibuffer;
  uint16_t                           volume_voxel_dimensions[3];
};

}  // namespace volume_rendering

void* sample_initialize(
    uint32_t /*width*/,
    uint32_t /*height*/,
    ngf_sample_count main_render_target_sample_count,
    ngf_xfer_encoder xfer_encoder) {
  auto state = new volume_rendering::state {};

  /** Open the file containing the volume data and read in the dimensions. */
  FILE* volume_data_file = fopen("assets/stag-beetle-volume.dat", "rb");
  if (volume_data_file == nullptr) {
    loge("failed to open the volume data file.");
    return nullptr;
  }
  fread(state->volume_voxel_dimensions, sizeof(uint16_t), 3, volume_data_file);

  /** Prepare a staging buffer. */
  const size_t staging_buffer_size = sizeof(uint16_t) * state->volume_voxel_dimensions[0] *
                                     state->volume_voxel_dimensions[1] * state->volume_voxel_dimensions[2];
  const ngf_buffer_info staging_buffer_info = {
      .size         = staging_buffer_size,
      .storage_type = NGF_BUFFER_STORAGE_HOST_READABLE_WRITEABLE,
      .buffer_usage = NGF_BUFFER_USAGE_XFER_SRC,
  };
  ngf::buffer staging_buffer;
  NGF_MISC_CHECK_NGF_ERROR(staging_buffer.initialize(staging_buffer_info));

  /** Map the staging buffer and read the volume data directly into the memory. */
  void* mapped_staging_buffer_ptr = ngf_buffer_map_range(staging_buffer, 0, staging_buffer_size);
  const uint64_t read_bytes =
      fread(mapped_staging_buffer_ptr, 1, staging_buffer_size, volume_data_file);
  if (ferror(volume_data_file)) {
    loge("error reading volume data file: %d", errno);
    return nullptr;
  }
  if (read_bytes != staging_buffer_size) {
    loge("failed to read the entire volume data. EOF: %d", feof(volume_data_file));
    return nullptr;
  }
  fclose(volume_data_file);

  /** Flush and unmap the staging buffer to prepare it for the upcoming transfer. */
  ngf_buffer_flush_range(staging_buffer, 0, staging_buffer_size);
  ngf_buffer_unmap(staging_buffer);

  /** Prepare a 3D image. */
  const ngf_image_info img_info = {
      .type = NGF_IMAGE_TYPE_IMAGE_3D,
      .extent =
          {.width  = state->volume_voxel_dimensions[0],
           .height = state->volume_voxel_dimensions[1],
           .depth  = state->volume_voxel_dimensions[2]},
      .nmips        = 1u,
      .nlayers      = 1u,
      .format       = NGF_IMAGE_FORMAT_R16_UNORM,
      .sample_count = NGF_SAMPLE_COUNT_1,
      .usage_hint   = NGF_IMAGE_USAGE_XFER_DST | NGF_IMAGE_USAGE_SAMPLE_FROM,
  };
  NGF_MISC_CHECK_NGF_ERROR(state->volume.initialize(img_info));

  /** Upload the volume data into the image. */
  const ngf_image_write img_write = {
      .src_offset     = 0u,
      .dst_offset     = {0, 0, 0},
      .extent         = img_info.extent,
      .dst_level      = 0u,
      .dst_base_layer = 0u,
      .nlayers        = 1u};
  ngf_cmd_write_image(xfer_encoder, staging_buffer, state->volume.get(), &img_write, 1u);

  /**
   * Initialize the sampler.
   */
  NGF_MISC_CHECK_NGF_ERROR(state->sampler.initialize(ngf_sampler_info {
      .min_filter        = NGF_FILTER_LINEAR,
      .mag_filter        = NGF_FILTER_LINEAR,
      .mip_filter        = NGF_FILTER_NEAREST,
      .wrap_u            = NGF_WRAP_MODE_CLAMP_TO_EDGE,
      .wrap_v            = NGF_WRAP_MODE_CLAMP_TO_EDGE,
      .wrap_w            = NGF_WRAP_MODE_CLAMP_TO_EDGE,
      .lod_max           = 0.0f,
      .lod_min           = 0.0f,
      .lod_bias          = 0.0f,
      .max_anisotropy    = 0.0f,
      .enable_anisotropy = false}));

  /**
   * Load the shader stages.
   */
  const ngf::shader_stage vertex_shader_stage =
      load_shader_stage("volume-renderer", "VSMain", NGF_STAGE_VERTEX);
  const ngf::shader_stage fragment_shader_stage =
      load_shader_stage("volume-renderer", "PSMain", NGF_STAGE_FRAGMENT);

  /**
   * Prepare a template with some default values for pipeline initialization.
   */
  ngf_util_graphics_pipeline_data pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(&pipeline_data);

  /**
   * Set shader stages.
   */
  pipeline_data.pipeline_info.nshader_stages   = 2;
  pipeline_data.pipeline_info.shader_stages[0] = vertex_shader_stage.get();
  pipeline_data.pipeline_info.shader_stages[1] = fragment_shader_stage.get();

  /**
   * Set multisampling state.
   */
  pipeline_data.multisample_info.sample_count = main_render_target_sample_count;

  /**
   * Set the compatible render target description.
   */
  pipeline_data.pipeline_info.compatible_rt_attachment_descs =
      ngf_default_render_target_attachment_descs();

  /**
   * Set up blending.
   */
  ngf_blend_info blend_info;
  blend_info.enable                 = true;
  blend_info.blend_op_color         = NGF_BLEND_OP_ADD;
  blend_info.dst_color_blend_factor = NGF_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend_info.src_color_blend_factor = NGF_BLEND_FACTOR_SRC_ALPHA;
  blend_info.blend_op_alpha         = NGF_BLEND_OP_ADD;
  blend_info.src_alpha_blend_factor = NGF_BLEND_FACTOR_ZERO;
  blend_info.dst_alpha_blend_factor = NGF_BLEND_FACTOR_ONE;
  blend_info.color_write_mask       = NGF_COLOR_MASK_WRITE_BIT_R | NGF_COLOR_MASK_WRITE_BIT_G |
                                NGF_COLOR_MASK_WRITE_BIT_B | NGF_COLOR_MASK_WRITE_BIT_A;
  pipeline_data.pipeline_info.color_attachment_blend_states = &blend_info;

  /**
   * Initialize the pipeline object.
   */
  state->pipeline.initialize(pipeline_data.pipeline_info);

  /**
   * Initialize uniforms multibuffer.
   */
  state->uniforms_multibuffer.initialize(3);

  return static_cast<void*>(state);
}

void sample_draw_frame(
    ngf_render_encoder main_render_pass,
    float time_delta,
    ngf_frame_token /*token*/,
    uint32_t w,
    uint32_t h,
    float /*time*/,
    void* userdata) {
    static float t = 0.0;
    t += time_delta;
  auto                       state = reinterpret_cast<volume_rendering::state*>(userdata);
  volume_rendering::uniforms u {
      nm::rotation_x(-1.620f) * nm::rotation_y(t) *
      nm::translation(nm::float3(0.0, -0.5, 0.0)),
      (float)w / (float)h };
  state->uniforms_multibuffer.write(u);
  const ngf_irect2d viewport {0, 0, w, h};
  ngf_cmd_bind_gfx_pipeline(main_render_pass, state->pipeline);
  ngf_cmd_viewport(main_render_pass, &viewport);
  ngf_cmd_scissor(main_render_pass, &viewport);
  ngf::cmd_bind_resources(
      main_render_pass,
      ngf::descriptor_set<0>::binding<0>::texture(state->volume),
      ngf::descriptor_set<0>::binding<1>::sampler(state->sampler),
      state->uniforms_multibuffer.bind_op_at_current_offset(1, 0, 0, sizeof(volume_rendering::uniforms)));
  ngf_cmd_draw(main_render_pass, false, 0, 6, state->volume_voxel_dimensions[2]);
}

void sample_pre_draw_frame(ngf_cmd_buffer, void*) {
}

void sample_post_draw_frame(ngf_cmd_buffer, void*) {
}

void sample_post_submit(void*) {
}

void sample_draw_ui(void* /*userdata*/) {
}

void sample_shutdown(void* userdata) {
  delete reinterpret_cast<volume_rendering::state*>(userdata);
}

}  // namespace ngf_samples
