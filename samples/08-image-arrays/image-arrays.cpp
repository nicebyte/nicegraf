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
#include "camera-controller.h"
#include "check.h"
#include "imgui.h"
#include "logging.h"
#include "nicegraf-util.h"
#include "nicegraf-wrappers.h"
#include "nicemath.h"
#include "sample-interface.h"
#include "shader-loader.h"
#include "staging-image.h"

#include <stdio.h>
#include <string>

using namespace ngf_misc;

namespace ngf_samples {

namespace image_arrays {

struct img_array_uniforms {
  nm::float4x4 matrix;
  float        image_array_idx = 0.0f;
  nm::float3   _padding;
};

struct cube_array_uniforms {
  nm::float4x4 matrix;
  float        aspect    = 1.0f;
  float        array_idx = 0.0f;
  nm::float2   _padding;
};

struct state {
  ngf::graphics_pipeline                        img_array_pipeline;
  ngf::graphics_pipeline                        cubemap_array_pipeline;
  ngf::image                                    image_array;
  ngf::image                                    cubemap_array;
  ngf::sampler                                  image_sampler;
  ngf::uniform_multibuffer<img_array_uniforms>  img_array_uniforms_multibuf;
  ngf::uniform_multibuffer<cube_array_uniforms> cube_array_uniforms_multibuf;
  float                                         dolly             = -5.0f;
  float                                         image_array_idx   = 0.0f;
  float                                         cubemap_array_idx = 0.0f;
  float                                         yaw               = 0.0f;
  float                                         pitch             = 0.0f;
};

}  // namespace image_arrays

void* sample_initialize(
    uint32_t /*width*/,
    uint32_t /*height*/,
    ngf_sample_count main_render_target_sample_count,
    ngf_xfer_encoder xfer_encoder) {
  auto state = new image_arrays::state {};

  /**
   * Create staging buffers for all the layers in the array.
   */
  constexpr int NUM_IMAGE_LAYERS = 4;
  staging_image staging_images[NUM_IMAGE_LAYERS];
  uint32_t      image_array_width = 0, image_array_height = 0, nmips = 0;
  for (uint32_t i = 0; i < NUM_IMAGE_LAYERS; ++i) {
    const std::string file_name = std::string("assets/imgarr") + std::to_string(i) + ".tga";
    staging_images[i]           = create_staging_image_from_tga(file_name.c_str());
    /** Ensure the dimensions of the image are valid. */
    if (i > 0 && (staging_images[i].width_px != image_array_width ||
                  staging_images[i].height_px != image_array_height)) {
      loge("all images in the array must have the same dimensions");
      return nullptr;
    } else {
      image_array_width  = staging_images[i].width_px;
      image_array_height = staging_images[i].height_px;
      nmips              = staging_images[i].nmax_mip_levels;
    }
  }

  /**
   * Create the image object with several array layers.
   */
  ngf_image_info image_array_info = {
      .type = NGF_IMAGE_TYPE_IMAGE_2D,
      .extent =
          {
              .width  = image_array_width,
              .height = image_array_height,
              .depth  = 1u,
          },
      .nmips        = nmips,
      .nlayers      = NUM_IMAGE_LAYERS,
      .format       = NGF_IMAGE_FORMAT_SRGBA8,
      .sample_count = NGF_SAMPLE_COUNT_1,
      .usage_hint   = NGF_IMAGE_USAGE_MIPMAP_GENERATION | NGF_IMAGE_USAGE_SAMPLE_FROM |
                    NGF_IMAGE_USAGE_XFER_DST};
  NGF_MISC_CHECK_NGF_ERROR(state->image_array.initialize(image_array_info));

  /**
   * Populate the first mip level for each layer.
   */
  for (uint32_t i = 0; i < NUM_IMAGE_LAYERS; ++i) {
    const ngf_image_write img_write = {
        .src_offset = 0u, .dst_offset = {0,0,0}, .extent = {image_array_width, image_array_height, 1u},
        .dst_base_layer = i, .nlayers =1u
    };
    ngf_cmd_write_image(
        xfer_encoder,
        staging_images[i].staging_buffer.get(),
        state->image_array.get(),
        &img_write,
        1u);
  }

  /** Populate the rest of the mip levels automatically. **/
  ngf_cmd_generate_mipmaps(xfer_encoder, state->image_array.get());

  /** Create a cubemap object with several array layers. */
  ngf_image_info cubemap_array_info = {
      .type = NGF_IMAGE_TYPE_CUBE,
      .extent =
          {
              .width  = image_array_width,
              .height = image_array_height,
              .depth  = 1u,
          },
      .nmips        = nmips,
      .nlayers      = NUM_IMAGE_LAYERS,
      .format       = NGF_IMAGE_FORMAT_SRGBA8,
      .sample_count = NGF_SAMPLE_COUNT_1,
      .usage_hint   = NGF_IMAGE_USAGE_MIPMAP_GENERATION | NGF_IMAGE_USAGE_SAMPLE_FROM |
                    NGF_IMAGE_USAGE_XFER_DST};
  NGF_MISC_CHECK_NGF_ERROR(state->cubemap_array.initialize(cubemap_array_info));

  /** Upload the first mip level for each layer on each face. */
  for (uint32_t i = 0; i < NUM_IMAGE_LAYERS; ++i) {
    for (uint32_t face = NGF_CUBEMAP_FACE_POSITIVE_X; face < NGF_CUBEMAP_FACE_COUNT; ++face) {
      const ngf_image_write img_write = {
          .src_offset     = 0u,
          .dst_offset     = {0, 0, 0},
          .extent         = {image_array_width, image_array_height, 1u},
          .dst_level      = 0u,
          .dst_base_layer = 6u * i + face,
          .nlayers        = 1u};
      ngf_cmd_write_image(
          xfer_encoder,
          staging_images[i].staging_buffer.get(),
          state->cubemap_array.get(),
          &img_write,
          1u);
    }
  }
  /** Generate the rest of the mips automatically. */
  ngf_cmd_generate_mipmaps(xfer_encoder, state->cubemap_array.get());

  /** Create an image sampler. */
  NGF_MISC_CHECK_NGF_ERROR(state->image_sampler.initialize(ngf_sampler_info {
      .min_filter        = NGF_FILTER_LINEAR,
      .mag_filter        = NGF_FILTER_LINEAR,
      .mip_filter        = NGF_FILTER_LINEAR,
      .wrap_u            = NGF_WRAP_MODE_REPEAT,
      .wrap_v            = NGF_WRAP_MODE_REPEAT,
      .wrap_w            = NGF_WRAP_MODE_REPEAT,
      .lod_max           = (float)nmips,
      .lod_min           = 0.0f,
      .lod_bias          = 0.0f,
      .max_anisotropy    = 0.0f,
      .enable_anisotropy = false}));

  /**
   * Load the shader stages for the regular image array pipeline.
   */
  const ngf::shader_stage img_array_vertex_shader_stage =
      load_shader_stage("textured-quad-image-array", "VSMain", NGF_STAGE_VERTEX);
  const ngf::shader_stage img_array_fragment_shader_stage =
      load_shader_stage("textured-quad-image-array", "PSMain", NGF_STAGE_FRAGMENT);

  /**
   * Prepare a template with some default values for pipeline initialization.
   */
  ngf_util_graphics_pipeline_data pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(&pipeline_data);

  /**
   * Set shader stages.
   */
  pipeline_data.pipeline_info.nshader_stages   = 2;
  pipeline_data.pipeline_info.shader_stages[0] = img_array_vertex_shader_stage.get();
  pipeline_data.pipeline_info.shader_stages[1] = img_array_fragment_shader_stage.get();

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
   * Initialize the image array pipeline object.
   */
  NGF_MISC_CHECK_NGF_ERROR(state->img_array_pipeline.initialize(pipeline_data.pipeline_info));

  /**
   * Load the shader stages for the cubemap array pipeline.
   */
  const ngf::shader_stage cubemap_vertex_shader_stage =
      load_shader_stage("cubemap-array", "VSMain", NGF_STAGE_VERTEX);
  const ngf::shader_stage cubemap_fragment_shader_stage =
      load_shader_stage("cubemap-array", "PSMain", NGF_STAGE_FRAGMENT);
  /**
   * Set shader stages.
   */
  pipeline_data.pipeline_info.shader_stages[0] = cubemap_vertex_shader_stage.get();
  pipeline_data.pipeline_info.shader_stages[1] = cubemap_fragment_shader_stage.get();

  /**
   * Initialize the cubemap array pipeline object.
   */
  NGF_MISC_CHECK_NGF_ERROR(
      state->cubemap_array_pipeline.initialize(pipeline_data.pipeline_info));

  /**
   * Create the uniform buffers.
   */
  NGF_MISC_CHECK_NGF_ERROR(state->img_array_uniforms_multibuf.initialize(3));
  NGF_MISC_CHECK_NGF_ERROR(state->cube_array_uniforms_multibuf.initialize(3));

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
  auto state = reinterpret_cast<image_arrays::state*>(userdata);

  /* Compute the perspective transform for the current frame. */
  const nm::float4x4 camera_to_clip = nm::perspective(
      nm::deg2rad(72.0f),
      static_cast<float>(w) / static_cast<float>(h),
      0.01f,
      100.0f);
  /* Build the world-to-camera transform for the current frame. */
  nm::float4x4 world_to_camera = nm::translation(nm::float3 {0.0, 0.0f, state->dolly});

  image_arrays::img_array_uniforms img_arr_uniforms;
  img_arr_uniforms.matrix          = camera_to_clip * world_to_camera;
  img_arr_uniforms.image_array_idx = state->image_array_idx;
  state->img_array_uniforms_multibuf.write(img_arr_uniforms);

  image_arrays::cube_array_uniforms cube_arr_uniforms;
  cube_arr_uniforms.aspect    = (float)w / (float)h;
  cube_arr_uniforms.array_idx = state->cubemap_array_idx;
  cube_arr_uniforms.matrix    = nm::rotation_y(state->yaw) * nm::rotation_x(state->pitch);
  state->cube_array_uniforms_multibuf.write(cube_arr_uniforms);

  ngf_irect2d viewport {0, 0, w, h};

  ngf_cmd_bind_gfx_pipeline(main_render_pass, state->cubemap_array_pipeline);
  ngf_cmd_viewport(main_render_pass, &viewport);
  ngf_cmd_scissor(main_render_pass, &viewport);
  ngf::cmd_bind_resources(
      main_render_pass,
      state->cube_array_uniforms_multibuf.bind_op_at_current_offset(0, 0),
      ngf::descriptor_set<0>::binding<1>::texture(state->cubemap_array.get()),
      ngf::descriptor_set<0>::binding<2>::sampler(state->image_sampler.get()));
  ngf_cmd_draw(main_render_pass, false, 0u, 3u, 1u);

  ngf_cmd_bind_gfx_pipeline(main_render_pass, state->img_array_pipeline);
  ngf_cmd_viewport(main_render_pass, &viewport);
  ngf_cmd_scissor(main_render_pass, &viewport);
  ngf::cmd_bind_resources(
      main_render_pass,
      state->img_array_uniforms_multibuf
          .bind_op_at_current_offset(0, 0, 0, sizeof(image_arrays::img_array_uniforms)),
      ngf::descriptor_set<0>::binding<1>::sampler(state->image_sampler),
      ngf::descriptor_set<1>::binding<0>::texture(state->image_array));
  ngf_cmd_draw(main_render_pass, false, 0, 6, 1);
}

void sample_pre_draw_frame(ngf_cmd_buffer, void*) {
}

void sample_post_draw_frame(ngf_cmd_buffer, void*) {
}

void sample_post_submit(void*) {
}

void sample_draw_ui(void* userdata) {
  auto data = reinterpret_cast<image_arrays::state*>(userdata);
  ImGui::Begin("Image Arrays");
  ImGui::DragFloat("dolly", &data->dolly, 0.01f, -70.0f, 0.11f);
  ImGui::DragFloat("image array index", &data->image_array_idx, 0.1f, 0.0f, 3.0f);
  ImGui::DragFloat("cubemap array index", &data->cubemap_array_idx, 0.1f, 0.0f, 3.0f);
  ImGui::SliderFloat("cubemap pitch", &data->pitch, -nm::PI, nm::PI);
  ImGui::SliderFloat("cubemap yaw", &data->yaw, -nm::PI, nm::PI);
  ImGui::End();
}

void sample_shutdown(void* userdata) {
  delete reinterpret_cast<image_arrays::state*>(userdata);
}

}  // namespace ngf_samples
