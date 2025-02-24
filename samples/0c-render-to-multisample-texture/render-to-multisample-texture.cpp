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

#include "check.h"
#include "imgui.h"
#include "nicegraf-util.h"
#include "nicegraf-wrappers.h"
#include "sample-interface.h"
#include "shader-loader.h"

#include <stdio.h>

using namespace ngf_misc;

namespace ngf_samples {

struct render_to_multisample_texture_data {
  ngf::render_target     default_rt;
  ngf::render_target     offscreen_rt;
  ngf::render_target     offscreen_multisample_rt;
  ngf::graphics_pipeline blit_pipeline;
  ngf::graphics_pipeline offscreen_pipeline;
  ngf::graphics_pipeline offscreen_multisample_pipeline;
  ngf::image             rt_texture;
  ngf::image             resolve_texture;
  ngf::sampler           sampler;
  bool                   is_multisample = true;
};

void* sample_initialize(
    uint32_t,
    uint32_t,
    ngf_sample_count main_render_target_sample_count,
    ngf_xfer_encoder /*xfer_encoder*/) {
  auto state = new render_to_multisample_texture_data {};

  /* Create the image to render to. */
  const ngf_extent3d   img_size {512u, 512u, 1u};
  const ngf_image_info img_info {
      NGF_IMAGE_TYPE_IMAGE_2D,
      img_size,
      1u,
      1u,
      NGF_IMAGE_FORMAT_BGRA8_SRGB,
      main_render_target_sample_count,
      NGF_IMAGE_USAGE_ATTACHMENT};
  NGF_MISC_CHECK_NGF_ERROR(state->rt_texture.initialize(img_info));
  const ngf_image_info resolve_img_info {
      NGF_IMAGE_TYPE_IMAGE_2D,
      img_size,
      1u,
      1u,
      NGF_IMAGE_FORMAT_BGRA8_SRGB,
      NGF_SAMPLE_COUNT_1,
      NGF_IMAGE_USAGE_SAMPLE_FROM | NGF_IMAGE_USAGE_ATTACHMENT};
  NGF_MISC_CHECK_NGF_ERROR(state->resolve_texture.initialize(resolve_img_info));

  const ngf_attachment_description offscreen_attachments = {
      .type         = NGF_ATTACHMENT_COLOR,
      .format       = NGF_IMAGE_FORMAT_BGRA8_SRGB,
      .sample_count = NGF_SAMPLE_COUNT_1,
      .is_resolve   = false};

  const ngf_image_ref offscreen_img_ref = {
      .image        = state->resolve_texture.get(),
      .mip_level    = 0u,
      .layer        = 0u,
      .cubemap_face = NGF_CUBEMAP_FACE_COUNT};

  const ngf_attachment_descriptions offscreen_attachments_list = {
      .descs  = &offscreen_attachments,
      .ndescs = 1u,
  };

  ngf_render_target_info rt_info {&offscreen_attachments_list, &offscreen_img_ref};
  NGF_MISC_CHECK_NGF_ERROR(state->offscreen_rt.initialize(rt_info));

  const ngf_attachment_description offscreen_multisample_attachments[2] = {
      {.type         = NGF_ATTACHMENT_COLOR,
       .format       = NGF_IMAGE_FORMAT_BGRA8_SRGB,
       .sample_count = main_render_target_sample_count,
       .is_resolve   = false},
      {.type         = NGF_ATTACHMENT_COLOR,
       .format       = NGF_IMAGE_FORMAT_BGRA8_SRGB,
       .sample_count = NGF_SAMPLE_COUNT_1,
       .is_resolve   = true}};

  const ngf_attachment_descriptions offscreen_multisample_attachments_list = {
      .descs  = offscreen_multisample_attachments,
      .ndescs = 2u,
  };

  const ngf_image_ref offscreen_multisample_img_refs[2] = {
      {.image        = state->rt_texture.get(),
       .mip_level    = 0u,
       .layer        = 0u,
       .cubemap_face = NGF_CUBEMAP_FACE_COUNT},
      {.image        = state->resolve_texture.get(),
       .mip_level    = 0u,
       .layer        = 0u,
       .cubemap_face = NGF_CUBEMAP_FACE_COUNT}};

  ngf_render_target_info multisample_rt_info {
      &offscreen_multisample_attachments_list,
      offscreen_multisample_img_refs};
  NGF_MISC_CHECK_NGF_ERROR(state->offscreen_multisample_rt.initialize(multisample_rt_info));

  /**
   * Load shader stages.
   */
  const ngf::shader_stage blit_vertex_stage =
      load_shader_stage("simple-texture", "VSMain", NGF_STAGE_VERTEX);
  const ngf::shader_stage blit_fragment_stage =
      load_shader_stage("simple-texture", "PSMain", NGF_STAGE_FRAGMENT);
  const ngf::shader_stage offscreen_vertex_stage =
      load_shader_stage("small-triangle", "VSMain", NGF_STAGE_VERTEX);
  const ngf::shader_stage offscreen_fragment_stage =
      load_shader_stage("small-triangle", "PSMain", NGF_STAGE_FRAGMENT);

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
   * Create pipeline for offscreen pass.
   */
  ngf_util_graphics_pipeline_data offscreen_pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(&offscreen_pipeline_data);
  ngf_graphics_pipeline_info& offscreen_pipe_info    = offscreen_pipeline_data.pipeline_info;
  offscreen_pipe_info.nshader_stages                 = 2u;
  offscreen_pipe_info.shader_stages[0]               = offscreen_vertex_stage.get();
  offscreen_pipe_info.shader_stages[1]               = offscreen_fragment_stage.get();
  offscreen_pipe_info.compatible_rt_attachment_descs = &offscreen_attachments_list;
  NGF_MISC_CHECK_NGF_ERROR(state->offscreen_pipeline.initialize(offscreen_pipe_info));

  /**
   * Create pipeline for multisample offscreen pass.
   */
  ngf_util_graphics_pipeline_data offscreen_multisample_pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(&offscreen_multisample_pipeline_data);
  offscreen_multisample_pipeline_data.multisample_info.sample_count =
      main_render_target_sample_count;
  ngf_graphics_pipeline_info& offscreen_multisample_pipe_info =
      offscreen_multisample_pipeline_data.pipeline_info;
  offscreen_multisample_pipe_info.nshader_stages   = 2u;
  offscreen_multisample_pipe_info.shader_stages[0] = offscreen_vertex_stage.get();
  offscreen_multisample_pipe_info.shader_stages[1] = offscreen_fragment_stage.get();
  offscreen_multisample_pipe_info.compatible_rt_attachment_descs =
      &offscreen_multisample_attachments_list;
  NGF_MISC_CHECK_NGF_ERROR(
      state->offscreen_multisample_pipeline.initialize(offscreen_multisample_pipe_info));

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

  return static_cast<void*>(state);
}

void sample_draw_frame(
    ngf_render_encoder main_render_pass,
    float /* time_delta */,
    ngf_frame_token frame_token,
    uint32_t        w,
    uint32_t        h,
    float,
    void* userdata) {
  auto                state = reinterpret_cast<render_to_multisample_texture_data*>(userdata);
  ngf_irect2d         offsc_viewport {0, 0, 512, 512};
  ngf_irect2d         onsc_viewport {0, 0, w, h};
  ngf_cmd_buffer      offscr_cmd_buf = nullptr;
  ngf_cmd_buffer_info cmd_info       = {};
  ngf_create_cmd_buffer(&cmd_info, &offscr_cmd_buf);
  ngf_start_cmd_buffer(offscr_cmd_buf, frame_token);
  {
    ngf::render_encoder renc {
        offscr_cmd_buf,
        state->is_multisample ? state->offscreen_multisample_rt : state->offscreen_rt,
        .0f,
        0.0f,
        0.0f,
        0.0f,
        1.0,
        0u};
    ngf_cmd_bind_gfx_pipeline(
        renc,
        state->is_multisample ? state->offscreen_multisample_pipeline : state->offscreen_pipeline);
    ngf_cmd_viewport(renc, &offsc_viewport);
    ngf_cmd_scissor(renc, &offsc_viewport);
    ngf_cmd_draw(renc, false, 0u, 3u, 1u);
  }
  ngf_submit_cmd_buffers(1, &offscr_cmd_buf);
  ngf_destroy_cmd_buffer(offscr_cmd_buf);

  ngf_cmd_bind_gfx_pipeline(main_render_pass, state->blit_pipeline);
  ngf_cmd_viewport(main_render_pass, &onsc_viewport);
  ngf_cmd_scissor(main_render_pass, &onsc_viewport);
  ngf::cmd_bind_resources(
      main_render_pass,
      ngf::descriptor_set<0>::binding<1>::texture(state->resolve_texture.get()),
      ngf::descriptor_set<0>::binding<2>::sampler(state->sampler.get()));
  ngf_cmd_draw(main_render_pass, false, 0u, 3u, 1u);
}

void sample_pre_draw_frame(ngf_cmd_buffer, void*) {
}

void sample_post_draw_frame(ngf_cmd_buffer, void*) {
}

void sample_draw_ui(void* userdata) {
  auto data = reinterpret_cast<render_to_multisample_texture_data*>(userdata);
  ImGui::Begin("Multisampling");
  ImGui::Checkbox("On/Off", &data->is_multisample);
  ImGui::End();
}

void sample_post_submit(void*) {
}

void sample_shutdown(void* userdata) {
  auto data = static_cast<render_to_multisample_texture_data*>(userdata);
  delete data;
  printf("shutting down\n");
}

}  // namespace ngf_samples
