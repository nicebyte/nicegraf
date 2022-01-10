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

#define _CRT_SECURE_NO_WARNINGS
#include "check.h"
#include "file-utils.h"
#include "imgui.h"
#include "logging.h"
#include "nicegraf-util.h"
#include "nicegraf-wrappers.h"
#include "nicemath.h"
#include "sample-interface.h"
#include "shader-loader.h"
#include "targa-loader.h"

#include <stdio.h>

namespace ngf_samples {

namespace cubemap {

struct uniforms {
  nm::float4x4 rotation;
  float        aspect_ratio;
};

struct state {
  ngf::graphics_pipeline             pipeline;
  ngf::image                         texture;
  ngf::sampler                       sampler;
  ngf::uniform_multibuffer<uniforms> uniforms_multibuf;
  float                              yaw   = 0.0f;
  float                              pitch = 0.0f;
};

}  // namespace cubemap

void* sample_initialize(
    uint32_t /*width*/,
    uint32_t /*height*/,
    ngf_sample_count main_render_target_sample_count,
    ngf_xfer_encoder xfer_encoder) {
  auto state = new cubemap::state {};

  /* Load contents of cubemap faces into a staging buffer. */
  char        file_name[] = "assets/cube0fx.tga";
  uint32_t    face_width = 0, face_height = 0;
  ngf::buffer staging_buffer;
  char*       mapped_staging_buffer = nullptr;
  uint32_t    staging_buffer_size   = 0u;
  uint32_t    bytes_per_face        = 0u;
  for (uint32_t face = NGF_CUBEMAP_FACE_POSITIVE_X; face < NGF_CUBEMAP_FACE_COUNT; face++) {
    sprintf(file_name, "assets/cube0f%d.tga", face);
    std::vector<char> cubemap_face_tga_data = load_file(file_name);
    uint32_t          width, height;
    load_targa(
        cubemap_face_tga_data.data(),
        cubemap_face_tga_data.size(),
        nullptr,
        0,
        &width,
        &height);
    if (face_width == 0 && face_height == 0) {
      face_width          = width;
      face_height         = height;
      bytes_per_face      = face_width * face_height * 4u;
      staging_buffer_size = bytes_per_face * NGF_CUBEMAP_FACE_COUNT;
      staging_buffer.initialize(ngf_buffer_info {
          .size         = staging_buffer_size,
          .storage_type = NGF_BUFFER_STORAGE_HOST_WRITEABLE,
          .buffer_usage = NGF_BUFFER_USAGE_XFER_SRC});
      mapped_staging_buffer =
          (char*)ngf_buffer_map_range(staging_buffer.get(), 0, staging_buffer_size);
    } else if (face_width != width || face_height != height) {
      loge("All faces of the cubemap must have the same dimensions");
      return nullptr;
    }
    std::vector<char> cubemap_face_rgba_data;
    cubemap_face_rgba_data.resize(bytes_per_face);
    load_targa(
        cubemap_face_tga_data.data(),
        cubemap_face_tga_data.size(),
        cubemap_face_rgba_data.data(),
        cubemap_face_rgba_data.size(),
        &width,
        &height);
    memcpy(
        mapped_staging_buffer + face * cubemap_face_rgba_data.size(),
        cubemap_face_rgba_data.data(),
        face_width * face_height * 4u);
  }

  /* Flush and unmap the staging buffer. */
  ngf_buffer_flush_range(staging_buffer.get(), 0, staging_buffer_size);
  ngf_buffer_unmap(staging_buffer.get());

  /* Create the cubemap texture. */
  NGF_SAMPLES_CHECK_NGF_ERROR(state->texture.initialize(ngf_image_info {
      .type         = NGF_IMAGE_TYPE_CUBE,
      .extent       = ngf_extent3d {.width = face_width, .height = face_height, .depth = 1},
      .nmips        = 1u,
      .nlayers      = 1u,
      .format       = NGF_IMAGE_FORMAT_SRGBA8,
      .sample_count = NGF_SAMPLE_COUNT_1,
      .usage_hint   = NGF_IMAGE_USAGE_SAMPLE_FROM | NGF_IMAGE_USAGE_XFER_DST}));

  /* Populate the cubemap texture. */
  for (uint32_t face = NGF_CUBEMAP_FACE_POSITIVE_X; face < NGF_CUBEMAP_FACE_COUNT; face++) {
    ngf_cmd_write_image(
        xfer_encoder,
        staging_buffer.get(),
        face * bytes_per_face,
        ngf_image_ref {state->texture, 0, 0, (ngf_cubemap_face)face},
        ngf_offset3d {},
        ngf_extent3d {face_width, face_height, 1});
  }

  /* Create the image sampler. */

  /* Same comment as above regarding the min/max LOD applies in case of the bilinear sampler. */
  NGF_SAMPLES_CHECK_NGF_ERROR(state->sampler.initialize(ngf_sampler_info {
      .min_filter        = NGF_FILTER_LINEAR,
      .mag_filter        = NGF_FILTER_LINEAR,
      .mip_filter        = NGF_FILTER_NEAREST,
      .wrap_u            = NGF_WRAP_MODE_REPEAT,
      .wrap_v            = NGF_WRAP_MODE_REPEAT,
      .wrap_w            = NGF_WRAP_MODE_REPEAT,
      .lod_max           = 0.0f,
      .lod_min           = 0.0f,
      .lod_bias          = 0.0f,
      .max_anisotropy    = 0.0f,
      .enable_anisotropy = false}));

  /**
   * Load the shader stages.
   */
  const ngf::shader_stage vertex_shader_stage =
      load_shader_stage("cubemap", "VSMain", NGF_STAGE_VERTEX);
  const ngf::shader_stage fragment_shader_stage =
      load_shader_stage("cubemap", "PSMain", NGF_STAGE_FRAGMENT);

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
  NGF_SAMPLES_CHECK_NGF_ERROR(state->pipeline.initialize(pipeline_data.pipeline_info));

  /**
   * Create the uniform buffer.
   */
  NGF_SAMPLES_CHECK_NGF_ERROR(state->uniforms_multibuf.initialize(3));

  return static_cast<void*>(state);
}

void sample_draw_frame(
    ngf_render_encoder main_render_pass,
    float /*time_delta*/,
    ngf_frame_token /*token*/,
    uint32_t w,
    uint32_t h,
    float /*time*/,
    void* userdata) {
  auto state = reinterpret_cast<cubemap::state*>(userdata);

  ngf_irect2d viewport {0, 0, w, h};
  ngf_cmd_bind_gfx_pipeline(main_render_pass, state->pipeline);
  ngf_cmd_viewport(main_render_pass, &viewport);
  ngf_cmd_scissor(main_render_pass, &viewport);
  state->uniforms_multibuf.write(
      {nm::rotation_y(state->yaw) * nm::rotation_x(state->pitch), (float)w / (float)h});
  ngf::cmd_bind_resources(
      main_render_pass,
      state->uniforms_multibuf.bind_op_at_current_offset(0, 0),
      ngf::descriptor_set<0>::binding<1>::texture(state->texture.get()),
      ngf::descriptor_set<0>::binding<2>::sampler(state->sampler.get()));
  ngf_cmd_draw(main_render_pass, false, 0u, 3u, 1u);
}

void sample_draw_ui(void* userdata) {
  auto state = reinterpret_cast<cubemap::state*>(userdata);
  ImGui::Begin("Cubemap", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::SliderFloat("Pitch", &state->pitch, -nm::PI, nm::PI);
  ImGui::SliderFloat("Yaw", &state->yaw, -nm::PI, nm::PI);
  ImGui::Text("This sample uses textures by Emil Persson.\n"
              "Licensed under CC BY 3.0\n"
              "http://humus.name/index.php?page=Textures");
  ImGui::End();
}

void sample_shutdown(void* userdata) {
  delete reinterpret_cast<cubemap::state*>(userdata);
}

}  // namespace ngf_samples
