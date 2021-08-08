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

#include "nicegraf-util.h"
#include "nicegraf-wrappers.h"
#include "sample-interface.h"
#include "shader-loader.h"

#include <stdio.h>

namespace ngf_samples {

struct texture_sampling_uniforms {
  float matrix[4][4];
};

struct texture_sampling_data {
  ngf::graphics_pipeline pipeline;
  ngf::image texture;
  ngf::sampler nearest_sampler;
  ngf::sampler bilinear_sampler;
  ngf::sampler trilinear_sampler;
  ngf::sampler aniso_sampler;
  float tilt = 0.0f;
  float zoom = 0.0f;
  float pan  = 0.0f;

};

void* sample_initialize(
    uint32_t /*width*/,
    uint32_t /*height*/,
    ngf_sample_count main_render_target_sample_count,
    ngf_xfer_encoder /*xfer_encoder*/) {
  auto data = new texture_sampling_data {};

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
  data->pipeline.initialize(pipeline_data.pipeline_info);

  return static_cast<void*>(data);
}

void sample_draw_frame(
    ngf_render_encoder main_render_pass,
    float /*time_delta*/,
    ngf_frame_token /*token*/,
    uint32_t w,
    uint32_t h,
    float /*time*/,
    void* userdata) {
}

void sample_draw_ui(void*) {
}

void sample_shutdown(void* userdata) {
  printf("shutting down\n");
}

}  // namespace ngf_samples