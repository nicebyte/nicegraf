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

namespace vertex_attribs {

struct uniforms {
  nm::float4x4 world_to_clip;
  float        timestamp;
};

struct state {
  ngf::graphics_pipeline             pipeline;
  ngf::image                         object_texture;
  ngf::sampler                       trilinear_sampler;
  ngf::uniform_multibuffer<uniforms> uniforms_multibuf;
  ngf::buffer                        per_instance_data;
  ngf::buffer                        vertex_attrib_buffer;
  ngf::buffer                        index_buffer;
  float                              dolly = -130.0f;
  float                              vfov  = 60.0f;
};

/**
 * The model instances are arraged in a (slightly perturbed) grid pattern,
 * this constant controls the size of the grid.
 */
constexpr int INSTANCES_GRID_SIZE = 128;
constexpr size_t INSTANCE_DATA_SIZE = sizeof(float) * 4u * INSTANCES_GRID_SIZE * INSTANCES_GRID_SIZE;

/**
 * The model's raw vertex data (positions and UVs).
 * A dodecahedron.
 */
float vertex_data[] = {
    //clang-format off
    0.577350f,  0.577350f,  -0.577350f, 0.727805f,  0.749509f,  0.356822f,  0.000000f,  -0.934172f,
    0.727805f,  0.868727f,  0.000000f,  0.417775f,  -0.675973f, 0.645760f,  0.809118f,  0.000000f,
    0.934172f,  -0.356822f, 0.614422f,  0.712668f,  0.934172f,  0.356822f,  0.000000f,  0.797880f,
    0.653059f,  0.675973f,  0.000000f,  -0.417775f, 0.829219f,  0.749509f,  0.934172f,  -0.356822f,
    0.000000f,  0.911264f,  0.689899f,  -0.577350f, -0.577350f, 0.577350f,  0.223582f,  0.285757f,
    -0.934172f, -0.356822f, 0.000000f,  0.336965f,  0.248917f,  -0.417775f, -0.675974f, 0.000000f,
    0.305627f,  0.345366f,  0.577350f,  -0.577350f, -0.577350f, 0.911264f,  0.809118f,  -0.577350f,
    0.577350f,  0.577350f,  0.544347f,  0.497000f,  -0.356822f, 0.000000f,  0.934172f,  0.614422f,
    0.400550f,  0.000000f,  0.417775f,  0.675973f,  0.645760f,  0.497000f,  -0.356822f, 0.000000f,
    -0.934172f, 0.614422f,  0.905567f,  -0.356822f, 0.000000f,  -0.934172f, 0.520423f,  0.308526f,
    -0.577350f, -0.577350f, -0.577350f, 0.407040f,  0.345366f,  -0.675974f, 0.000000f,  -0.417775f,
    0.438378f,  0.248917f,  0.356822f,  0.000000f,  -0.934172f, 0.797880f,  0.845958f,  0.356822f,
    0.000000f,  -0.934172f, 0.520423f,  0.501425f,  0.577350f,  -0.577350f, -0.577350f, 0.407040f,
    0.538266f,  0.000000f,  -0.417775f, -0.675974f, 0.438378f,  0.441816f,  -0.577350f, 0.577350f,
    -0.577350f, 0.544347f,  0.809118f,  0.417775f,  0.675973f,  0.000000f,  0.696467f,  0.653059f,
    -0.356822f, 0.000000f,  0.934172f,  0.153507f,  0.189308f,  -0.675974f, 0.000000f,  0.417775f,
    0.254920f,  0.189308f,  0.577350f,  -0.577350f, 0.577350f,  0.153507f,  0.501425f,  0.000000f,
    -0.934172f, 0.356822f,  0.223582f,  0.404976f,  0.417775f,  -0.675973f, 0.000000f,  0.254920f,
    0.501425f,  -0.577350f, 0.577350f,  0.577350f,  0.501039f,  0.556609f,  0.000000f,  0.934172f,
    0.356822f,  0.614422f,  0.593450f,  -0.417775f, 0.675974f,  0.000000f,  0.532377f,  0.653059f,
    0.000000f,  -0.417775f, 0.675974f,  0.141537f,  0.345366f,  0.577350f,  0.577350f,  0.577350f,
    0.727805f,  0.556609f,  0.675974f,  0.000000f,  0.417775f,  0.829219f,  0.556609f,  -0.934172f,
    0.356822f,  0.000000f,  0.430964f,  0.653059f,  -0.577350f, 0.577350f,  -0.577350f, 0.501039f,
    0.749508f,  0.000000f,  -0.934172f, -0.356822f, 0.336965f,  0.441816f,  0.356822f,  0.000000f,
    0.934172f,  0.727805f,  0.437391f,  -0.934172f, 0.356822f,  0.000000f,  0.407040f,  0.152467f,
    -0.577350f, 0.577350f,  -0.577350f, 0.520423f,  0.189308f,  -0.356822f, 0.000000f,  -0.934172f,
    0.520423f,  0.382207f,  -0.577350f, 0.577350f,  0.577350f,  0.223582f,  0.092858f,  -0.934172f,
    0.356822f,  0.000000f,  0.336965f,  0.129698f,  0.577350f,  -0.577350f, -0.577350f, 0.336965f,
    0.561035f,  0.934172f,  -0.356822f, 0.000000f,  0.223582f,  0.597875f,  0.577350f,  -0.577350f,
    0.577350f,  0.110198f,  0.441816f,  0.356822f,  0.000000f,  0.934172f,  0.040124f,  0.345366f,
    -0.356822f, 0.000000f,  0.934172f,  0.110198f,  0.248917f,  0.356822f,  0.000000f,  0.934172f,
    0.797880f,  0.460159f,  0.577350f,  -0.577350f, 0.577350f,  0.911264f,  0.497000f,  0.934172f,
    -0.356822f, 0.000000f,  0.911264f,  0.616218f
    //clang-format on
};

/**
 * The model's index buffer.
 */
uint32_t index_data[] = {
    //clang-format off
    0,  1,  2,  3,  0,  2,  0,  4,  5,  4,  6,  5,  7,  8,  9,  6,  10, 5,  11, 12, 13, 1,  14,
    2,  15, 16, 17, 18, 0,  5,  19, 20, 21, 22, 3,  2,  0,  3,  23, 14, 22, 2,  7,  24, 25, 10,
    18, 5,  26, 27, 28, 29, 30, 31, 7,  27, 32, 30, 3,  31, 4,  33, 34, 35, 29, 31, 3,  36, 31,
    36, 35, 31, 8,  16, 9,  16, 37, 9,  37, 27, 9,  27, 7,  9,  12, 38, 13, 38, 33, 13, 33, 30,
    13, 30, 11, 13, 16, 8,  17, 8,  39, 17, 39, 40, 17, 40, 15, 17, 20, 37, 21, 37, 16, 21, 16,
    41, 21, 41, 19, 21, 3,  30, 23, 30, 33, 23, 33, 4,  23, 4,  0,  23, 24, 42, 25, 42, 43, 25,
    43, 8,  25, 8,  7,  25, 27, 37, 28, 37, 44, 28, 44, 45, 28, 45, 26, 28, 27, 46, 32, 46, 47,
    32, 47, 48, 32, 48, 7,  32, 33, 49, 34, 49, 50, 34, 50, 51, 34, 51, 4,  34,
    //clang-format on
};

}  // namespace vertex_attribs

void* sample_initialize(
    uint32_t /*width*/,
    uint32_t /*height*/,
    ngf_sample_count main_render_target_sample_count,
    ngf_xfer_encoder xfer_encoder) {
  auto state = new vertex_attribs::state {};

  /**
   * Load the shader stages.
   */
  const ngf::shader_stage vertex_shader_stage =
      load_shader_stage("instancing", "VSMain", NGF_STAGE_VERTEX);
  const ngf::shader_stage fragment_shader_stage =
      load_shader_stage("instancing", "PSMain", NGF_STAGE_FRAGMENT);

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
   * Enable depth testing and depth write.
   */
  pipeline_data.depth_stencil_info.depth_test  = true;
  pipeline_data.depth_stencil_info.depth_write = true;

  /**
   * Set the compatible render target description.
   */
  pipeline_data.pipeline_info.compatible_rt_attachment_descs =
      ngf_default_render_target_attachment_descs();

  /**
   * Set up vertex attributes.
   */

  /* attribute descriptions indicate the location and format of individual vertex attributes. */
  const ngf_vertex_attrib_desc vertex_attrib_descriptions[] = {
      {/* position. */
       .location   = 0u,
       .binding    = 0u,
       .offset     = 0u,
       .type       = NGF_TYPE_FLOAT,
       .size       = 3u,
       .normalized = false},
      {/* UV coordinate. */
       .location   = 1u,
       .binding    = 0u,
       .offset     = 3u * sizeof(float),
       .type       = NGF_TYPE_FLOAT,
       .size       = 2u,
       .normalized = false}};

  /* buffer binding descriptions indicate _how_ the attributes are fetched from a buffer. */
  const ngf_vertex_buf_binding_desc vertex_buf_binding_descriptions[] = {{
      .binding    = 0u,
      .stride     = sizeof(float) * (3u + 2u),
      .input_rate = NGF_INPUT_RATE_VERTEX,
  }};

  pipeline_data.vertex_input_info.nattribs =
      sizeof(vertex_attrib_descriptions) / sizeof(vertex_attrib_descriptions[0]);
  pipeline_data.vertex_input_info.attribs = vertex_attrib_descriptions;
  pipeline_data.vertex_input_info.nvert_buf_bindings =
      sizeof(vertex_buf_binding_descriptions) / sizeof(vertex_buf_binding_descriptions[0]);
  pipeline_data.vertex_input_info.vert_buf_bindings = vertex_buf_binding_descriptions;

  /**
   * Initialize the pipeline object.
   */
  NGF_SAMPLES_CHECK_NGF_ERROR(state->pipeline.initialize(pipeline_data.pipeline_info));

  /**
   * Create and populate the vertex and index buffers.
   */
  const ngf_buffer_info vertex_buffer_info = {
      .size         = sizeof(vertex_attribs::vertex_data),
      .storage_type = NGF_BUFFER_STORAGE_PRIVATE,
      .buffer_usage = NGF_BUFFER_USAGE_VERTEX_BUFFER | NGF_BUFFER_USAGE_XFER_DST,
  };
  const ngf_buffer_info index_buffer_info = {
      .size         = sizeof(vertex_attribs::index_data),
      .storage_type = NGF_BUFFER_STORAGE_PRIVATE,
      .buffer_usage = NGF_BUFFER_USAGE_INDEX_BUFFER | NGF_BUFFER_USAGE_XFER_DST,
  };
  const ngf_buffer_info vertex_staging_buffer_info = {
      .size         = vertex_buffer_info.size,
      .storage_type = NGF_BUFFER_STORAGE_HOST_WRITEABLE,
      .buffer_usage = NGF_BUFFER_USAGE_XFER_SRC,
  };
  const ngf_buffer_info index_staging_buffer_info = {
      .size         = index_buffer_info.size,
      .storage_type = NGF_BUFFER_STORAGE_HOST_WRITEABLE,
      .buffer_usage = NGF_BUFFER_USAGE_XFER_SRC,
  };
  ngf::buffer vertex_staging_buffer;
  NGF_SAMPLES_CHECK_NGF_ERROR(vertex_staging_buffer.initialize(vertex_staging_buffer_info));
  ngf::buffer index_staging_buffer;
  NGF_SAMPLES_CHECK_NGF_ERROR(index_staging_buffer.initialize(index_staging_buffer_info));
  NGF_SAMPLES_CHECK_NGF_ERROR(state->vertex_attrib_buffer.initialize(vertex_buffer_info));
  NGF_SAMPLES_CHECK_NGF_ERROR(state->index_buffer.initialize(index_buffer_info));
  void* mapped_vertex_buffer =
      ngf_buffer_map_range(vertex_staging_buffer.get(), 0u, vertex_staging_buffer_info.size);
  void* mapped_index_buffer =
      ngf_buffer_map_range(index_staging_buffer.get(), 0u, index_staging_buffer_info.size);
  memcpy(mapped_vertex_buffer, vertex_attribs::vertex_data, vertex_staging_buffer_info.size);
  memcpy(mapped_index_buffer, vertex_attribs::index_data, index_staging_buffer_info.size);
  ngf_buffer_flush_range(vertex_staging_buffer.get(), 0, vertex_staging_buffer_info.size);
  ngf_buffer_flush_range(index_staging_buffer.get(), 0, index_staging_buffer_info.size);
  ngf_buffer_unmap(vertex_staging_buffer.get());
  ngf_buffer_unmap(index_staging_buffer.get());
  ngf_cmd_copy_buffer(
      xfer_encoder,
      vertex_staging_buffer.get(),
      state->vertex_attrib_buffer.get(),
      vertex_buffer_info.size,
      0,
      0);
  ngf_cmd_copy_buffer(
      xfer_encoder,
      index_staging_buffer.get(),
      state->index_buffer.get(),
      index_buffer_info.size,
      0,
      0);

  /**
   * Create and populate per-instance data.
   */
  const ngf_buffer_info instance_data_buffer_info = {
      .size         = vertex_attribs::INSTANCE_DATA_SIZE,
      .storage_type = NGF_BUFFER_STORAGE_PRIVATE,
      .buffer_usage = NGF_BUFFER_USAGE_TEXEL_BUFFER | NGF_BUFFER_USAGE_XFER_DST,
  };
  const ngf_buffer_info instance_data_staging_buffer_info = {
      .size         = instance_data_buffer_info.size,
      .storage_type = NGF_BUFFER_STORAGE_HOST_WRITEABLE,
      .buffer_usage = NGF_BUFFER_USAGE_XFER_SRC};
  ngf::buffer instance_data_staging_buffer;
  NGF_SAMPLES_CHECK_NGF_ERROR(instance_data_staging_buffer.initialize(instance_data_staging_buffer_info));
  NGF_SAMPLES_CHECK_NGF_ERROR(state->per_instance_data.initialize(instance_data_buffer_info));
  auto mapped_per_instance_staging_buffer = (float*)ngf_buffer_map_range(
      instance_data_staging_buffer.get(),
      0,
      instance_data_staging_buffer_info.size);
  for (uint32_t r = 0; r < vertex_attribs::INSTANCES_GRID_SIZE; ++r) {
    for (uint32_t c = 0; c < vertex_attribs::INSTANCES_GRID_SIZE; ++c) {
      const uint32_t idx = r * (vertex_attribs::INSTANCES_GRID_SIZE) + c;
      assert(idx < instance_data_staging_buffer_info.size);
      float*          p            = &mapped_per_instance_staging_buffer[4 * idx];
      constexpr float grid_offset  = -static_cast<float>(vertex_attribs::INSTANCES_GRID_SIZE >> 1);
      constexpr float grid_spacing = 4.0f;
      p[0]                         = grid_offset * grid_spacing + grid_spacing * c +
             0.75f * (2.0f * rand() / static_cast<float>(RAND_MAX) - 1.0f);
      p[2] = grid_offset * grid_spacing + grid_spacing * r +
             0.75f * (2.0f * rand() / static_cast<float>(RAND_MAX) - 1.0f);
      p[1] = (2.0f * rand() / static_cast<float>(RAND_MAX) - 1.0f);
    }
  }
  ngf_buffer_flush_range(
      instance_data_staging_buffer.get(),
      0,
      instance_data_staging_buffer_info.size);
  ngf_buffer_unmap(instance_data_staging_buffer.get());
  ngf_cmd_copy_buffer(
      xfer_encoder,
      instance_data_staging_buffer,
      state->per_instance_data,
      instance_data_buffer_info.size,
      0,
      0);

  /**
   * Create the uniform buffer.
   */
  NGF_SAMPLES_CHECK_NGF_ERROR(state->uniforms_multibuf.initialize(3));

  /* Load contents of the model's texture into a staging buffer. */
  char              file_name[] = "assets/dodecahedron.tga";
  ngf::buffer       staging_buffer;
  std::vector<char> cubemap_face_tga_data = load_file(file_name);
  uint32_t          texture_width, texture_height;
  load_targa(
      cubemap_face_tga_data.data(),
      cubemap_face_tga_data.size(),
      nullptr,
      0,
      &texture_width,
      &texture_height);
  const uint32_t staging_buffer_size = 4u * texture_width * texture_height;
  staging_buffer.initialize(ngf_buffer_info {
      .size         = staging_buffer_size,
      .storage_type = NGF_BUFFER_STORAGE_HOST_WRITEABLE,
      .buffer_usage = NGF_BUFFER_USAGE_XFER_SRC});
  auto mapped_staging_buffer =
      (char*)ngf_buffer_map_range(staging_buffer.get(), 0, staging_buffer_size);
  std::vector<char> texture_rgba_data;
  texture_rgba_data.resize(staging_buffer_size);
  load_targa(
      cubemap_face_tga_data.data(),
      cubemap_face_tga_data.size(),
      texture_rgba_data.data(),
      texture_rgba_data.size(),
      &texture_width,
      &texture_height);
  memcpy(mapped_staging_buffer, texture_rgba_data.data(), staging_buffer_size);

  /* Flush and unmap the staging buffer. */
  ngf_buffer_flush_range(staging_buffer.get(), 0, staging_buffer_size);
  ngf_buffer_unmap(staging_buffer.get());

  /* Create the texture. */
  const uint32_t nmips =
      1 + static_cast<uint32_t>(std::floor(std::log2(std::max(texture_width, texture_height))));
  NGF_SAMPLES_CHECK_NGF_ERROR(state->object_texture.initialize(ngf_image_info {
      .type         = NGF_IMAGE_TYPE_IMAGE_2D,
      .extent       = ngf_extent3d {.width = texture_width, .height = texture_height, .depth = 1},
      .nmips        = nmips,
      .format       = NGF_IMAGE_FORMAT_SRGBA8,
      .sample_count = NGF_SAMPLE_COUNT_1,
      .usage_hint   = NGF_IMAGE_USAGE_SAMPLE_FROM | NGF_IMAGE_USAGE_XFER_DST |
                    NGF_IMAGE_USAGE_MIPMAP_GENERATION}));

  /* Populate the texture. */
  ngf_cmd_write_image(
      xfer_encoder,
      staging_buffer.get(),
      0,
      ngf_image_ref {state->object_texture, 0, 0, NGF_CUBEMAP_FACE_COUNT},
      ngf_offset3d {},
      ngf_extent3d {texture_width, texture_height, 1});
  ngf_cmd_generate_mipmaps(xfer_encoder, state->object_texture);

  /* Create the image sampler. */
  NGF_SAMPLES_CHECK_NGF_ERROR(state->trilinear_sampler.initialize(ngf_sampler_info {
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

  return static_cast<void*>(state);
}

void sample_draw_frame(
    ngf_render_encoder main_render_pass,
    float              time_delta,
    ngf_frame_token /*token*/,
    uint32_t w,
    uint32_t h,
    float /*time*/,
    void* userdata) {
  static float t = .0f;
  t += time_delta;
  auto state = reinterpret_cast<vertex_attribs::state*>(userdata);

  ngf_irect2d viewport {0, 0, w, h};
  ngf_cmd_bind_gfx_pipeline(main_render_pass, state->pipeline);
  ngf_cmd_viewport(main_render_pass, &viewport);
  ngf_cmd_scissor(main_render_pass, &viewport);
  ngf_cmd_bind_attrib_buffer(main_render_pass, state->vertex_attrib_buffer, 0, 0);
  ngf_cmd_bind_index_buffer(main_render_pass, state->index_buffer, NGF_TYPE_UINT32);
  state->uniforms_multibuf.write(
      {nm::perspective(
           nm::deg2rad(state->vfov),
           static_cast<float>(w) / static_cast<float>(h),
           0.01f,
           1000.0f) *
           nm::look_at(
               nm::float3 {0.0f, 50.0f, state->dolly},
               nm::float3 {.0f, .0f, .0f},
               nm::float3 {.0f, 1.0f, .0f}),
       t});
  ngf::cmd_bind_resources(
      main_render_pass,
      state->uniforms_multibuf.bind_op_at_current_offset(0, 0),
      ngf::descriptor_set<0>::binding<1>::texel_buffer(
          state->per_instance_data,
          0,
          vertex_attribs::INSTANCE_DATA_SIZE,
          NGF_IMAGE_FORMAT_RGBA32F),
      ngf::descriptor_set<0>::binding<2>::texture(state->object_texture.get()),
      ngf::descriptor_set<0>::binding<3>::sampler(state->trilinear_sampler.get()));
  ngf_cmd_draw(
      main_render_pass,
      true,
      0u,
      sizeof(vertex_attribs::index_data) / sizeof(vertex_attribs::index_data[0]),
      128 * 128);
}

void sample_draw_ui(void* userdata) {
  auto data = reinterpret_cast<vertex_attribs::state*>(userdata);
  ImGui::Begin("Camera control");
  ImGui::DragFloat("dolly", &data->dolly, 0.01f, -500.0f, 1.0f);
  ImGui::DragFloat("fov", &data->vfov, 0.08f, 25.0f, 90.0f);
  ImGui::End();
}

void sample_shutdown(void* userdata) {
  delete reinterpret_cast<vertex_attribs::state*>(userdata);
}

}  // namespace ngf_samples
