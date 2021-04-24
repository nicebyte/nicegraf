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
#include <stdio.h>

namespace ngf_samples {

struct fullscreen_triangle_data {
  ngf::graphics_pipeline pipeline;
  ngf::cmd_buffer        cmdbuf;
};

void* sample_initialize(uint32_t , uint32_t ) {
  auto data = new fullscreen_triangle_data{};

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
  pipeline_data.pipeline_info.nshader_stages = 2;
  pipeline_data.pipeline_info.shader_stages[0] = vertex_shader_stage.get();
  pipeline_data.pipeline_info.shader_stages[1] = fragment_shader_stage.get();

  /**
   * Set multisampling state.
   */
  pipeline_data.multisample_info.sample_count = NGF_SAMPLE_COUNT_8;

  /**
   * Set the compatible render target description.
   */
  pipeline_data.pipeline_info.compatible_rt_attachment_descs =
      ngf_default_render_target_attachment_descs();

  /**
   * Initialize the pipeline object.
   */
  data->pipeline.initialize(pipeline_data.pipeline_info);

  /**
   * Initialize the command buffer object.
   */
  const ngf_cmd_buffer_info cmd_buf_info {};
  data->cmdbuf.initialize(cmd_buf_info);

  return static_cast<void*>(data);
}

void sample_draw_frame(
    ngf_frame_token token,
    uint32_t        w,
    uint32_t        h,
    float           /*time*/,
    void*           userdata) {
    auto data = static_cast<fullscreen_triangle_data*>(userdata);

    /**
     * Obtain the raw cmd buffer handle to pass to C functions.
     */
    ngf_cmd_buffer cmdbuf = data->cmdbuf.get();

    /**
     * Start recording a new batch of commands into the command buffer.
     * This operation requires a frame token, which is returned by `ngf_begin_frame`.
     * The token specifies the frame that this series of commands is intended to be
     * executed within.
     */
    ngf_start_cmd_buffer(cmdbuf, token);

    /**
     * Start a new render encoder.
     */
    constexpr ngf_attachment_load_op load_ops[2] = { NGF_LOAD_OP_CLEAR, NGF_LOAD_OP_CLEAR };
    constexpr ngf_attachment_store_op store_ops[2] = { NGF_STORE_OP_STORE, NGF_STORE_OP_DONTCARE }; 
    constexpr ngf_clear clears[2] = {
      ngf_clear { .clear_color = {.0f} },
      ngf_clear {.clear_depth_stencil = { 1.0f } }
    };

    /**
     * Begin a new render pass, drawing to the default render target.
     */
    ngf_pass_info pass = {
      .render_target = ngf_default_render_target(),
      .load_ops = load_ops,
      .store_ops = store_ops,
      .clears = clears,
    };
    ngf_render_encoder renc;
    ngf_cmd_buffer_start_render(cmdbuf, &pass, &renc);
    ngf_cmd_bind_gfx_pipeline(renc, data->pipeline.get());
    const ngf_irect2d viewport {0, 0, w, h};
    ngf_cmd_viewport(renc, &viewport);
    ngf_cmd_scissor(renc, &viewport);

    /**
     * Make a drawcall.
     */
    ngf_cmd_draw(renc, false, 0, 3, 1);

    /**
     * Finish the render pass. 
     */
    ngf_render_encoder_end(renc);
    ngf_submit_cmd_buffers(1, &cmdbuf);
}

void sample_draw_ui(void*) {
}

void sample_shutdown(void* userdata) {
  auto data = static_cast<fullscreen_triangle_data*>(userdata);
  delete data;
  printf("shutting down\n");
}

}
