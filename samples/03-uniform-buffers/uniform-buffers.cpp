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

#include "nicegraf-exception.h"
#include "nicegraf-util.h"
#include "nicegraf-wrappers.h"
#include "sample-interface.h"
#include "shader-loader.h"

#include <imgui.h>
#include <stdio.h>

namespace ngf_samples {

struct shader_uniform_values {
  float scale_a = 0.0f;
  float scale_b = 0.5f;
  float time    = 0.0f;
  float aspect  = 1.0f;
  float theta   = 0.0f;
};

struct uniform_buffers_data {
  ngf::graphics_pipeline polygon_pipeline;
  ngf::uniform_buffer    uniform_buffer;
  size_t                 uniform_buffer_offset     = 0u;
  size_t                 aligned_uniform_data_size = 0u;
  shader_uniform_values  uniform_values;
  int                    n            = 6;
  float                  growth_speed = 1.f;
  bool                   growing      = true;
};

static float theta_for_n(int n) {
  return 2.0f * 3.1415926f / static_cast<float>(n);
}

static float min_scale_for_ngon(int n) {
  float a = theta_for_n(n);
  return (1.0f - sinf(a) * tanf(a / 2.0f));
}

void* sample_initialize(
    uint32_t,
    uint32_t,
    ngf_sample_count main_render_target_sample_count,
    ngf_xfer_encoder /*xfer_encoder*/) {
  auto state = new uniform_buffers_data {};

  /**
   * Pre-initialize some uniform variables.
   */
  state->uniform_values.scale_a = state->uniform_values.scale_b * min_scale_for_ngon(state->n);
  state->uniform_values.theta   = theta_for_n(state->n);

  /**
   * Load shader stages.
   */
  const ngf::shader_stage polygon_vertex_stage =
      load_shader_stage("polygon", "VSMain", NGF_STAGE_VERTEX);
  const ngf::shader_stage polygon_fragment_stage =
      load_shader_stage("polygon", "PSMain", NGF_STAGE_FRAGMENT);

  /**
   * Create pipeline.
   */
  ngf_util_graphics_pipeline_data polygon_pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(&polygon_pipeline_data);
  polygon_pipeline_data.multisample_info.sample_count = main_render_target_sample_count;
  ngf_graphics_pipeline_info& polygon_pipe_info       = polygon_pipeline_data.pipeline_info;
  polygon_pipe_info.nshader_stages                    = 2u;
  polygon_pipe_info.shader_stages[0]                  = polygon_vertex_stage.get();
  polygon_pipe_info.shader_stages[1]                  = polygon_fragment_stage.get();
  polygon_pipe_info.compatible_rt_attachment_descs = ngf_default_render_target_attachment_descs();
  polygon_pipe_info.primitive_type                 = NGF_PRIMITIVE_TYPE_TRIANGLE_FAN;
  NGF_SAMPLES_CHECK(state->polygon_pipeline.initialize(polygon_pipe_info));

  /**
   * Create the uniform buffer.
   * We need to write to the buffer every frame from the CPU. However, as we're preparing the
   * data for the next frame, the GPU might still be rendering the current frame. Modifying the
   * buffer at that time would lead to a data race. To avoid it, we employ a triple bufferinga
   * strategy:
   *  - assume we need N bytes for the uniform buffer
   *  - allocate 3*N bytes for the buffer;
   *  - ensure that while GPU reads data at offset i*N, the CPU writes at ((i + 1) mod 3) * N.
   * This ensures that, as long as the CPU is no more than 2 frames ahead of the GPU, no
   * data races will happen.
   *
   * Note that the offset at which we read/write must have an alignment that is specific to the
   * GPU. That alignment can be obtained from ngf_get_device_capabilities().
   */
  const size_t uniform_buffer_offset_alignment =
      ngf_get_device_capabilities()->uniform_buffer_offset_alignment;
  const size_t requested_data_size = sizeof(shader_uniform_values);
  state->aligned_uniform_data_size =
      requested_data_size +
      (uniform_buffer_offset_alignment - requested_data_size % uniform_buffer_offset_alignment);
  const size_t          uniform_buffer_size = 3 * state->aligned_uniform_data_size;
  const ngf_buffer_info uniform_buffer_info = {
      .size         = uniform_buffer_size,
      .storage_type = NGF_BUFFER_STORAGE_HOST_WRITEABLE,
      .buffer_usage = 0u};
  NGF_SAMPLES_CHECK(state->uniform_buffer.initialize(uniform_buffer_info));

  return static_cast<void*>(state);
}

void sample_draw_frame(
    ngf_render_encoder main_render_pass,
    float              time_delta,
    ngf_frame_token /*frame_token*/,
    uint32_t w,
    uint32_t h,
    float,
    void* userdata) {
  auto state = reinterpret_cast<uniform_buffers_data*>(userdata);

  /**
   * Update the values for the uniform buffer.
   */
  shader_uniform_values& uniforms  = state->uniform_values;
  const float            max_scale = uniforms.scale_b;
  float                  min_scale = max_scale * min_scale_for_ngon(state->n);
  const bool             growing   = state->growing;
  uniforms.aspect                  = static_cast<float>(w) / static_cast<float>(h);
  uniforms.time += time_delta;
  uniforms.scale_a += (growing ? 1.0f : -1.0f) * time_delta * (state->growth_speed);
  const bool evolve_ngon =
      (growing && uniforms.scale_a >= max_scale) || (!growing && uniforms.scale_a <= min_scale);
  constexpr int max_ngon_sides = 96;
  constexpr int min_ngon_sides = 6;
  const bool    switch_phase   = evolve_ngon && ((growing && state->n == max_ngon_sides) ||
                                            (!growing && state->n == min_ngon_sides));
  if (switch_phase) {
    state->growing = !state->growing;
  } else if (evolve_ngon) {
    state->n       = growing ? (state->n << 1) : (state->n >> 1);
    uniforms.theta = theta_for_n(state->n);
    state->growth_speed *= (growing ? 0.5f : 2.0f);
    uniforms.scale_a = growing ? max_scale * min_scale_for_ngon(state->n) : max_scale;
  }

  /**
   * Write the updated values to the uniform buffer at current offset.
   * Map the range, write the data using memcpy, then flush and unmap.
   */
  void* mapped_uniform_buffer_offset = ngf_uniform_buffer_map_range(
      state->uniform_buffer,
      state->uniform_buffer_offset,
      state->aligned_uniform_data_size,
      NGF_BUFFER_MAP_WRITE_BIT);
  memcpy(mapped_uniform_buffer_offset, &state->uniform_values, sizeof(state->uniform_values));
  ngf_uniform_buffer_flush_range(state->uniform_buffer, 0, state->aligned_uniform_data_size);
  ngf_uniform_buffer_unmap(state->uniform_buffer);

  /**
   * Record the rendering commands.
   */
  ngf_irect2d viewport {0, 0, w, h};
  ngf_cmd_bind_gfx_pipeline(main_render_pass, state->polygon_pipeline);
  ngf_cmd_viewport(main_render_pass, &viewport);
  ngf_cmd_scissor(main_render_pass, &viewport);
  ngf::cmd_bind_resources(
      main_render_pass,
      ngf_resource_bind_op {
          .target_set     = 0,
          .target_binding = 0,
          .type           = NGF_DESCRIPTOR_UNIFORM_BUFFER,
          .info           = {
              .uniform_buffer = {
                  .buffer = state->uniform_buffer,
                  .offset = state->uniform_buffer_offset,
                  .range  = state->aligned_uniform_data_size}}});
  ngf_cmd_draw(main_render_pass, false, 0u, (state->n) + 2u, 1u);

  /**
   * Update the uniform buffer offset so we write there on the next frame.
   */
  state->uniform_buffer_offset = (state->uniform_buffer_offset + state->aligned_uniform_data_size) %
                                 (3 * state->aligned_uniform_data_size);
}

void sample_draw_ui(void*) {
}

void sample_shutdown(void* userdata) {
  auto data = static_cast<uniform_buffers_data*>(userdata);
  delete data;
  printf("shutting down\n");
}

}  // namespace ngf_samples
