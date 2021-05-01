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

#include "sample-interface.h"
#include "nicegraf-wrappers.h"
#include "nicegraf-util.h"
#include "shader-loader.h"
#include "nicegraf-exception.h"

#include <stdio.h>

namespace ngf_samples {

struct render_to_texture_data {
  ngf::render_target default_rt;
  ngf::render_target offscreen_rt;
  ngf::graphics_pipeline blit_pipeline;
  ngf::graphics_pipeline offscreen_pipeline;
  ngf::image rt_texture;
  ngf::sampler sampler;

};

void* sample_initialize(uint32_t, uint32_t) {
  auto state = new render_to_texture_data{};
  
  /* Create the image to render to. */
  const ngf_extent3d img_size { 512u, 512u, 1u };
  const ngf_image_info img_info {
    NGF_IMAGE_TYPE_IMAGE_2D,
    img_size,
    1u,
    NGF_IMAGE_FORMAT_BGRA8_SRGB,
    NGF_SAMPLE_COUNT_1,
    NGF_IMAGE_USAGE_SAMPLE_FROM | NGF_IMAGE_USAGE_ATTACHMENT
  };
  NGF_SAMPLES_CHECK(state->rt_texture.initialize(img_info));
 
  /* Create the offscreen render target.*/
  const ngf_attachment_description offscreen_color_attachment_description {
    .type = NGF_ATTACHMENT_COLOR,
    .format = NGF_IMAGE_FORMAT_BGRA8_SRGB,
    .sample_count = NGF_SAMPLE_COUNT_1,
    .is_sampled = true
  };
  const ngf_attachment_descriptions attachments_list = {
    .ndescs = 1u,
    .descs = &offscreen_color_attachment_description
  };
  const ngf_image_ref img_ref = {
    .image = state->rt_texture.get(),
    .mip_level = 0u,
    .layer = 0u,
    .cubemap_face = NGF_CUBEMAP_FACE_COUNT
  };
  ngf_render_target_info rt_info {
    &attachments_list,
    &img_ref
  };
  NGF_SAMPLES_CHECK(state->offscreen_rt.initialize(rt_info));

  /**
   * Load shader stages.
   */
  const ngf::shader_stage blit_vertex_stage = 
    load_shader_stage("fullscreen-triangle", "VSMain", NGF_STAGE_VERTEX);
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
  blit_pipeline_data.multisample_info.sample_count = NGF_SAMPLE_COUNT_8;
  ngf_graphics_pipeline_info &blit_pipe_info =
      blit_pipeline_data.pipeline_info;
  blit_pipe_info.nshader_stages = 2u;
  blit_pipe_info.shader_stages[0] = blit_vertex_stage.get();
  blit_pipe_info.shader_stages[1] = blit_fragment_stage.get();
  blit_pipe_info.compatible_rt_attachment_descs = ngf_default_render_target_attachment_descs();
  blit_pipeline_data.layout_info.ndescriptor_set_layouts = 1u;
  const ngf_descriptor_info descriptors[] = {
    {.type = NGF_DESCRIPTOR_TEXTURE,.id = 1u, .stage_flags = NGF_DESCRIPTOR_FRAGMENT_STAGE_BIT},
    {.type = NGF_DESCRIPTOR_SAMPLER, .id = 2u, .stage_flags = NGF_DESCRIPTOR_FRAGMENT_STAGE_BIT },
  };
  const ngf_descriptor_set_layout_info desc_set_layout = {
    .descriptors = descriptors,
    .ndescriptors = 2u
  };
  blit_pipeline_data.layout_info.descriptor_set_layouts = &desc_set_layout;
  NGF_SAMPLES_CHECK(state->blit_pipeline.initialize(blit_pipe_info));

  /**
   * Create pipeline for offscreen pass.
   */
  ngf_util_graphics_pipeline_data offscreen_pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(&offscreen_pipeline_data);
  ngf_graphics_pipeline_info &offscreen_pipe_info =
      offscreen_pipeline_data.pipeline_info;
  offscreen_pipe_info.nshader_stages = 2u;
  offscreen_pipe_info.shader_stages[0] = offscreen_vertex_stage.get();
  offscreen_pipe_info.shader_stages[1] = offscreen_fragment_stage.get();
  offscreen_pipe_info.compatible_rt_attachment_descs = &attachments_list;
  NGF_SAMPLES_CHECK(state->offscreen_pipeline.initialize(offscreen_pipe_info));
 
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
    {0.0f},
    1.0f,
    false
  };
  NGF_SAMPLES_CHECK(state->sampler.initialize(samp_info));

  return static_cast<void*>(state);
}

void sample_draw_frame(
    ngf_frame_token frame_token,
    uint32_t        w,
    uint32_t        h,
    float           ,
    void*           userdata) {
  auto state = reinterpret_cast<render_to_texture_data*>(userdata);

  ngf_irect2d offsc_viewport { 0, 0, 512, 512 };
  ngf_irect2d onsc_viewport {0, 0, w, h };
  ngf_cmd_buffer cmd_buf = nullptr;
  ngf_cmd_buffer_info cmd_info;
  ngf_create_cmd_buffer(&cmd_info, &cmd_buf);
  ngf_start_cmd_buffer(cmd_buf, frame_token);
  {
    ngf::render_encoder renc { cmd_buf, state->offscreen_rt, .0f, 0.0f, 0.0f, 0.0f, 1.0, 0u};
    ngf_cmd_bind_gfx_pipeline(renc, state->offscreen_pipeline);
    ngf_cmd_viewport(renc, &offsc_viewport);
    ngf_cmd_scissor(renc, &offsc_viewport);
    ngf_cmd_draw(renc, false, 0u, 3u, 1u);
  }
  {
    ngf::render_encoder renc {cmd_buf, ngf_default_render_target(), .0, 0.0, 0.0, 0.0, 1.0, 0u};
    ngf_cmd_bind_gfx_pipeline(renc, state->blit_pipeline);
    ngf_cmd_viewport(renc, &onsc_viewport);
    ngf_cmd_scissor(renc, &onsc_viewport);
    ngf::cmd_bind_resources(
        renc,
        ngf::descriptor_set<0>::binding<1>::texture(state->rt_texture.get()),
        ngf::descriptor_set<0>::binding<2>::sampler(state->sampler.get()));
    ngf_cmd_draw(renc, false, 0u, 3u, 1u);
  }
  ngf_submit_cmd_buffers(1u, &cmd_buf);
  ngf_destroy_cmd_buffer(cmd_buf);
}

void sample_draw_ui(void*) {
}

void sample_shutdown(void* userdata) {
  auto data = static_cast<render_to_texture_data*>(userdata);
  delete data;
  printf("shutting down\n");
}

}
