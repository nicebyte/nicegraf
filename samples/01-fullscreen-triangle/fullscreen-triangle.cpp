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
#include "nicegraf-util.h"
#include "nicegraf-wrappers.h"
#include "sample-interface.h"
#include "shader-loader.h"

#include <stdio.h>

using namespace ngf_misc;

namespace ngf_samples {

namespace fullscreen_triangle {
struct state {
  ngf::graphics_pipeline pipeline;
};
}  // namespace fullscreen_triangle

void* sample_initialize(
    uint32_t,
    uint32_t,
    ngf_sample_count main_render_target_sample_count,
    ngf_xfer_encoder /*xfer_encoder*/) {
  auto state = new fullscreen_triangle::state {};

  /**
   * Load the shader stages.
   * Note that these are only necessary when creating pipeline objects.
   * After the pipeline objects have been created, the shader stage objects
   * can be safely discarded.
   */
  const ngf::shader_stage vertex_shader_stage =
      load_shader_stage("fullscreen-triangle", "VSMain", NGF_STAGE_VERTEX);
  const ngf::shader_stage fragment_shader_stage =
      load_shader_stage("fullscreen-triangle", "PSMain", NGF_STAGE_FRAGMENT);

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
  NGF_MISC_CHECK_NGF_ERROR(state->pipeline.initialize(pipeline_data.pipeline_info));

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
  auto state = static_cast<fullscreen_triangle::state*>(userdata);

  ngf_cmd_bind_gfx_pipeline(main_render_pass, state->pipeline.get());
  const ngf_irect2d viewport {0, 0, w, h};
  ngf_cmd_viewport(main_render_pass, &viewport);
  ngf_cmd_scissor(main_render_pass, &viewport);

  /**
   * Make a drawcall.
   */
  ngf_cmd_draw(main_render_pass, false, 0, 3, 1);
}

void sample_pre_draw_frame(ngf_cmd_buffer, main_render_pass_sync_info*, void*) {
}

void sample_post_draw_frame(ngf_cmd_buffer, ngf_render_encoder, void*) {
}
void sample_post_submit(void*) {
}

void sample_draw_ui(void*) {
}

void sample_shutdown(void* userdata) {
  auto state = static_cast<fullscreen_triangle::state*>(userdata);
  delete state;
}

}  // namespace ngf_samples
