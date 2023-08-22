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

namespace compute_demo {

struct state {
  ngf::image             image;
  ngf::compute_pipeline  compute_pipeline;
  ngf::graphics_pipeline blit_pipeline;
  ngf::sampler           sampler;
  ngf_compute_encoder    prev_compute_enc;
  ngf_image_ref          image_ref;
  uint32_t               frame;
};

}  // namespace compute_demo

void* sample_initialize(
    uint32_t /*width*/,
    uint32_t /*height*/,
    ngf_sample_count main_render_target_sample_count,
    ngf_xfer_encoder /* xfer_encoder*/) {
  auto state = new compute_demo::state {};

  /**
   * Load the shader stages.
   */
  const ngf::shader_stage compute_shader =
      load_shader_stage("compute-demo", "CSMain", NGF_STAGE_COMPUTE);

  /**
   * Create the compute pipeline.
   */
  ngf_compute_pipeline_info pipeline_info;
  pipeline_info.shader_stage = compute_shader.get();
  pipeline_info.spec_info    = nullptr;
  NGF_MISC_CHECK_NGF_ERROR(state->compute_pipeline.initialize(pipeline_info));

  /**
   * Load shader stages.
   */
  const ngf::shader_stage blit_vertex_stage =
      load_shader_stage("simple-texture", "VSMain", NGF_STAGE_VERTEX);
  const ngf::shader_stage blit_fragment_stage =
      load_shader_stage("simple-texture", "PSMain", NGF_STAGE_FRAGMENT);

  /**
   * Create pipeline for blit.
   */
  ngf_util_graphics_pipeline_data blit_pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(&blit_pipeline_data);
  blit_pipeline_data.multisample_info.sample_count = main_render_target_sample_count;
  ngf_graphics_pipeline_info& blit_pipe_info       = blit_pipeline_data.pipeline_info;
  blit_pipe_info.nshader_stages                    = 2u;
  blit_pipe_info.shader_stages[0]                  = blit_vertex_stage.get();
  blit_pipe_info.shader_stages[1]                  = blit_fragment_stage.get();
  blit_pipe_info.compatible_rt_attachment_descs    = ngf_default_render_target_attachment_descs();
  NGF_MISC_CHECK_NGF_ERROR(state->blit_pipeline.initialize(blit_pipe_info));

  /**
   * Initialize the image.
   */
  ngf_image_info image_info;
  image_info.format        = NGF_IMAGE_FORMAT_RGBA8;
  image_info.extent.depth  = 1;
  image_info.extent.width  = 4 * 128;
  image_info.extent.height = 4 * 128;
  image_info.nlayers       = 1u;
  image_info.nmips         = 1u;
  image_info.sample_count  = NGF_SAMPLE_COUNT_1;
  image_info.type          = NGF_IMAGE_TYPE_IMAGE_2D;
  image_info.usage_hint    = NGF_IMAGE_USAGE_STORAGE | NGF_IMAGE_USAGE_SAMPLE_FROM;
  NGF_MISC_CHECK_NGF_ERROR(state->image.initialize(image_info));
  state->image_ref.image     = state->image;
  state->image_ref.layer     = 0u;
  state->image_ref.mip_level = 0u;

  /* Create sampler.*/
  const ngf_sampler_info samp_info {
      NGF_FILTER_LINEAR,
      NGF_FILTER_LINEAR,
      NGF_FILTER_NEAREST,
      NGF_WRAP_MODE_CLAMP_TO_EDGE,
      NGF_WRAP_MODE_CLAMP_TO_EDGE,
      NGF_WRAP_MODE_CLAMP_TO_EDGE,
      0.0f,
      0.0f,
      0.0f,
      1.0f,
      false};
  NGF_MISC_CHECK_NGF_ERROR(state->sampler.initialize(samp_info));

  state->frame = 0u;

  return static_cast<void*>(state);
}

void sample_draw_frame(
    ngf_render_encoder main_render_pass,
    float /*time_delta*/,
    ngf_frame_token /* token*/,
    uint32_t w,
    uint32_t h,
    float /*time*/,
    void* userdata) {
  auto state = reinterpret_cast<compute_demo::state*>(userdata);
  if (state->frame > 0u) {
    ngf_irect2d onsc_viewport {0, 0, w, h};
    ngf_cmd_bind_gfx_pipeline(main_render_pass, state->blit_pipeline);
    ngf_cmd_viewport(main_render_pass, &onsc_viewport);
    ngf_cmd_scissor(main_render_pass, &onsc_viewport);
    ngf::cmd_bind_resources(
        main_render_pass,
        ngf::descriptor_set<0>::binding<1>::texture(state->image.get()),
        ngf::descriptor_set<0>::binding<2>::sampler(state->sampler.get()));
    ngf_cmd_draw(main_render_pass, false, 0u, 3u, 1u);
  }
}

void sample_pre_draw_frame(ngf_cmd_buffer, main_render_pass_sync_info* sync_info, void* userdata) {
  auto state = reinterpret_cast<compute_demo::state*>(userdata);
  if (state->frame > 0u) {
    sync_info->nsync_compute_resources = 1u;
    sync_info->sync_compute_resources->encoder        = state->prev_compute_enc;
    sync_info->sync_compute_resources->resource.resource.image_ref = state->image_ref;
    sync_info->sync_compute_resources->resource.sync_resource_type = NGF_SYNC_RESOURCE_IMAGE;
  }
}

void sample_post_draw_frame(
    ngf_cmd_buffer     cmd_buffer,
    ngf_render_encoder prev_render_encoder,
    void*              userdata) {
  auto              state = reinterpret_cast<compute_demo::state*>(userdata);
  const ngf_sync_render_resource sync_render_resource = {
      .encoder = prev_render_encoder,
      .resource =
          {.sync_resource_type = NGF_SYNC_RESOURCE_IMAGE,
           .resource           = {.image_ref = state->image_ref}},
  };
      
  const ngf_compute_pass_info pass_info {
      .sync_render_resources = {
          .nsync_resources = 1u,
          .sync_resources  = &sync_render_resource}};

  ngf_compute_encoder compute_enc;
  NGF_MISC_CHECK_NGF_ERROR(
      ngf_cmd_begin_compute_pass(cmd_buffer, &pass_info, &compute_enc));
  ngf_resource_bind_op bind_op;
  bind_op.info.image_sampler.image = state->image;
  bind_op.target_set               = 0;
  bind_op.target_binding           = 0;
  bind_op.type                     = NGF_DESCRIPTOR_STORAGE_IMAGE;
  ngf_cmd_bind_compute_pipeline(compute_enc, state->compute_pipeline.get());
  ngf_cmd_bind_compute_resources(compute_enc, &bind_op, 1);
  ngf_cmd_dispatch(compute_enc, 128, 128, 1);
  ngf_cmd_end_compute_pass(compute_enc);
  state->prev_compute_enc = compute_enc;
  state->frame++;
}

void sample_post_submit(void*) {
}

void sample_draw_ui(void* /*userdata*/) {
}

void sample_shutdown(void* userdata) {
  delete reinterpret_cast<compute_demo::state*>(userdata);
}

}  // namespace ngf_samples
