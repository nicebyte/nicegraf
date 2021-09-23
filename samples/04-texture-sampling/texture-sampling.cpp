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

#include "check.h"
#include "file-utils.h"
#include "imgui.h"
#include "nicegraf-util.h"
#include "nicegraf-wrappers.h"
#include "nicemath.h"
#include "sample-interface.h"
#include "shader-loader.h"
#include "targa-loader.h"

#include <stdio.h>

namespace ngf_samples {

namespace texture_sampling {

struct matrices {
  nm::float4x4 m[4];
};

struct state {
  ngf::graphics_pipeline             pipeline;
  ngf::image                         texture;
  ngf::sampler                       samplers[4];
  ngf::uniform_multibuffer<matrices> uniforms;
  float                              tilt  = 0.0f;
  float                              dolly = -5.0f;
  float                              pan   = 0.0f;
};

}  // namespace texture_sampling

void* sample_initialize(
    uint32_t /*width*/,
    uint32_t /*height*/,
    ngf_sample_count main_render_target_sample_count,
    ngf_xfer_encoder xfer_encoder) {
  auto s = new texture_sampling::state {};

  /* Read in the texture image file.*/
  std::vector<char> texture_tga_data = load_file("assets/tiles.tga");

  /* this call does nothing but quickly get the width & height. */
  uint32_t texture_width, texture_height;
  load_targa(
      texture_tga_data.data(),
      texture_tga_data.size(),
      nullptr,
      0u,
      &texture_width,
      &texture_height);

  /* Create an appropriately sized staging buffer for the texture upload. */
  const size_t      texture_size_bytes = texture_width * texture_height * 4u;
  ngf::buffer staging_buf;
  NGF_SAMPLES_CHECK(staging_buf.initialize(ngf_buffer_info {
      .size         = texture_size_bytes,
      .storage_type = NGF_BUFFER_STORAGE_HOST_READABLE_WRITEABLE,
      .buffer_usage = NGF_BUFFER_USAGE_XFER_SRC}));
  void* mapped_staging_buf = ngf_buffer_map_range(
      staging_buf.get(),
      0,
      texture_size_bytes);

  /* Decode the loaded targa file, writing RGBA values directly into mapped memory. */
  load_targa(
      texture_tga_data.data(),
      texture_tga_data.size(),
      mapped_staging_buf,
      texture_size_bytes,
      &texture_width,
      &texture_height);

  /* Flush and unmap the staging buffer. */
  ngf_buffer_flush_range(staging_buf.get(), 0, texture_size_bytes);
  ngf_buffer_unmap(staging_buf.get());

  /* Count the number of mipmaps we'll have to generate for trilinear filtering.
     Note that we keep generating mip levels until both dimensions are reduced to 1.
   */
  uint32_t nmips =
      1 + static_cast<uint32_t>(std::floor(std::log2(std::max(texture_width, texture_height))));

  /* Create the texture object. */
  ngf_image_info texture_image_info = {
      .type = NGF_IMAGE_TYPE_IMAGE_2D,
      .extent =
          {
              .width  = texture_width,
              .height = texture_height,
              .depth  = 1u,
          },
      .nmips        = nmips,
      .format       = NGF_IMAGE_FORMAT_SRGBA8,
      .sample_count = NGF_SAMPLE_COUNT_1,
      .usage_hint   = NGF_IMAGE_USAGE_MIPMAP_GENERATION | NGF_IMAGE_USAGE_SAMPLE_FROM |
                    NGF_IMAGE_USAGE_XFER_DST};
  NGF_SAMPLES_CHECK(s->texture.initialize(texture_image_info));

  /* Upload the data from the staging buffer into the 0th mip level of the texture. */
  ngf_cmd_write_image(
      xfer_encoder,
      staging_buf.get(),
      0u,
      ngf_image_ref {
          .image        = s->texture.get(),
          .mip_level    = 0u,
          .layer        = 0u,
          .cubemap_face = NGF_CUBEMAP_FACE_COUNT},
      ngf_offset3d {},
      ngf_extent3d {texture_width, texture_height, 1u});

  /* Populate the rest of the mip levels automatically. */
  ngf_cmd_generate_mipmaps(xfer_encoder, s->texture.get());

  /* Create the image sampler objects. */

  /* Note that with the nearest-neighbor sampler, we constrain the min and max LOD to 0,
     in order to limit ourselves to mip level 0 only and demonstrate the effect of sampling
     without mips. */
  NGF_SAMPLES_CHECK(s->samplers[0].initialize(ngf_sampler_info {
      .min_filter        = NGF_FILTER_NEAREST,
      .mag_filter        = NGF_FILTER_NEAREST,
      .mip_filter        = NGF_FILTER_NEAREST,
      .wrap_s            = NGF_WRAP_MODE_REPEAT,
      .wrap_t            = NGF_WRAP_MODE_REPEAT,
      .wrap_r            = NGF_WRAP_MODE_REPEAT,
      .lod_max           = 0.0f,
      .lod_min           = 0.0f,
      .lod_bias          = 0.0f,
      .max_anisotropy    = 0.0f,
      .enable_anisotropy = false}));

  /* Same comment as above regarding the min/max LOD applies in case of the bilinear sampler. */
  NGF_SAMPLES_CHECK(s->samplers[1].initialize(ngf_sampler_info {
      .min_filter        = NGF_FILTER_LINEAR,
      .mag_filter        = NGF_FILTER_LINEAR,
      .mip_filter        = NGF_FILTER_NEAREST,
      .wrap_s            = NGF_WRAP_MODE_REPEAT,
      .wrap_t            = NGF_WRAP_MODE_REPEAT,
      .wrap_r            = NGF_WRAP_MODE_REPEAT,
      .lod_max           = 0.0f,
      .lod_min           = 0.0f,
      .lod_bias          = 0.0f,
      .max_anisotropy    = 0.0f,
      .enable_anisotropy = false}));

  NGF_SAMPLES_CHECK(s->samplers[2].initialize(ngf_sampler_info {
      .min_filter        = NGF_FILTER_LINEAR,
      .mag_filter        = NGF_FILTER_LINEAR,
      .mip_filter        = NGF_FILTER_LINEAR,
      .wrap_s            = NGF_WRAP_MODE_REPEAT,
      .wrap_t            = NGF_WRAP_MODE_REPEAT,
      .wrap_r            = NGF_WRAP_MODE_REPEAT,
      .lod_max           = (float)nmips,
      .lod_min           = 0.0f,
      .lod_bias          = 0.0f,
      .max_anisotropy    = 0.0f,
      .enable_anisotropy = false}));

  /* note that with anisotropic sampling, mipmaps are still needed because the
     specific (hardware-dependent) implementation may access them. */
  NGF_SAMPLES_CHECK(s->samplers[3].initialize(ngf_sampler_info {
      .min_filter        = NGF_FILTER_LINEAR,
      .mag_filter        = NGF_FILTER_LINEAR,
      .mip_filter        = NGF_FILTER_LINEAR,
      .wrap_s            = NGF_WRAP_MODE_REPEAT,
      .wrap_t            = NGF_WRAP_MODE_REPEAT,
      .wrap_r            = NGF_WRAP_MODE_REPEAT,
      .lod_max           = (float)nmips,
      .lod_min           = 0.0f,
      .lod_bias          = 0.0f,
      .max_anisotropy    = 16.0f,
      .enable_anisotropy = true}));

  /**
   * Load the shader stages.
   */
  const ngf::shader_stage vertex_shader_stage =
      load_shader_stage("textured-quad", "VSMain", NGF_STAGE_VERTEX);
  const ngf::shader_stage fragment_shader_stage =
      load_shader_stage("textured-quad", "PSMain", NGF_STAGE_FRAGMENT);

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
   * Initialize the pipeline object.
   */
  s->pipeline.initialize(pipeline_data.pipeline_info);

  /**
   * Create the uniform buffer.
   */
  s->uniforms.initialize(3);

  return static_cast<void*>(s);
}

void sample_draw_frame(
    ngf_render_encoder main_render_pass,
    float /*time_delta*/,
    ngf_frame_token /*token*/,
    uint32_t w,
    uint32_t h,
    float /*time*/,
    void* userdata) {
  auto state = reinterpret_cast<texture_sampling::state*>(userdata);

  /* Compute the perspective transform for the current frame. */
  const nm::float4x4 camera_to_clip = nm::perspective(
      nm::deg2rad(72.0f),
      static_cast<float>(w) / static_cast<float>(h),
      0.01f,
      100.0f);

  /* Build the world-to-camera transform for the current frame. */
  nm::float4x4 world_to_camera =
      nm::translation(nm::float3 {state->pan, 0.0f, state->dolly}) * nm::rotation_x(state->tilt);

  /* Build the final transform matrices for this frame. */
  texture_sampling::matrices uniforms_for_this_frame;
  for (int i = 0; i < sizeof(uniforms_for_this_frame.m) / sizeof(uniforms_for_this_frame.m[0]);
       ++i) {
    const nm::float4x4 object_to_world =
        nm::translation(nm::float3 {-3.0f + i * 2.05f, 0.0f, 0.0f});
    uniforms_for_this_frame.m[i] = camera_to_clip * world_to_camera * object_to_world;
  }
  state->uniforms.write(uniforms_for_this_frame);

  ngf_irect2d viewport {0, 0, w, h};
  ngf_cmd_bind_gfx_pipeline(main_render_pass, state->pipeline);
  ngf_cmd_viewport(main_render_pass, &viewport);
  ngf_cmd_scissor(main_render_pass, &viewport);
  for (uint32_t i = 0; i < sizeof(state->samplers) / sizeof(state->samplers[0]); ++i) {
    ngf::cmd_bind_resources(
        main_render_pass,
        state->uniforms
            .bind_op_at_current_offset(0, 0, sizeof(nm::float4x4) * i, sizeof(nm::float4x4)),
        ngf::descriptor_set<0>::binding<1>::sampler(state->samplers[i]),
        ngf::descriptor_set<1>::binding<0>::texture(state->texture));
    ngf_cmd_draw(main_render_pass, false, 0, 6, 1);
  }
}

void sample_draw_ui(void* userdata) {
  auto data = reinterpret_cast<texture_sampling::state*>(userdata);
  ImGui::Begin("Camera control");
  ImGui::DragFloat("dolly", &data->dolly, 0.01f, -70.0f, 0.11f);
  ImGui::DragFloat("pan", &data->pan, 0.01f, -70.0f, 70.0f);
  ImGui::DragFloat("tilt", &data->tilt, 0.01f, -(nm::PI / 2.0f + 0.01f), nm::PI / 2.0f + 0.01f);
  ImGui::End();
}

void sample_shutdown(void* userdata) {
  delete reinterpret_cast<texture_sampling::state*>(userdata);
}

}  // namespace ngf_samples
