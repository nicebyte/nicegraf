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

#include <stdio.h>
#include <string>

using namespace ngf_misc;

namespace ngf_samples {

namespace compute_verts {

constexpr int nverts_per_side    = 512;
constexpr int ntotal_verts       = nverts_per_side * nverts_per_side;
constexpr int nindices_per_strip = 2 * nverts_per_side + 1u;
constexpr int nstrips            = nverts_per_side - 1;
constexpr int ntotal_indices     = nstrips * nindices_per_strip;

struct render_uniforms {
  camera_matrices cam_matrices;
};

struct compute_uniforms {
  float time;
  float pad[3];
};

struct state {
  ngf::compute_pipeline                      compute_pipeline;
  ngf::graphics_pipeline                     render_pipeline;
  ngf::uniform_multibuffer<render_uniforms>  render_uniforms_multibuf;
  ngf::uniform_multibuffer<compute_uniforms> compute_uniforms_multibuf;
  ngf::buffer                                index_buffer;
  ngf::buffer                                vertex_buffer;
  ngf_buffer_slice                           compute_buffer_slice;
  ngf_compute_encoder                        prev_compute_encoder;
  camera_state                               camera;
  uint32_t                                   frame = 0u;
};

}  // namespace compute_verts

void* sample_initialize(
    uint32_t /*width*/,
    uint32_t /*height*/,
    ngf_sample_count main_render_target_sample_count,
    ngf_xfer_encoder xfer_encoder) {
  auto state = new compute_verts::state {};

  /**
   * Load the shader stages.
   */
  const ngf::shader_stage compute_shader =
      load_shader_stage("compute-vertices", "CSMain", NGF_STAGE_COMPUTE);

  /**
   * Create the compute pipeline.
   */
  ngf_compute_pipeline_info pipeline_info;
  pipeline_info.shader_stage = compute_shader.get();
  pipeline_info.spec_info    = nullptr;
  NGF_MISC_CHECK_NGF_ERROR(state->compute_pipeline.initialize(pipeline_info));

  /**
   * Load shader stages.
   */
  const ngf::shader_stage render_vertex_stage =
      load_shader_stage("render-vertices", "VSMain", NGF_STAGE_VERTEX);
  const ngf::shader_stage render_fragment_stage =
      load_shader_stage("render-vertices", "PSMain", NGF_STAGE_FRAGMENT);

  /**
   * Create pipeline for rendering vertex data.
   */
  const ngf_vertex_attrib_desc position_attrib_desc {
      .location   = 0u,
      .binding    = 0u,
      .offset     = 0u,
      .type       = NGF_TYPE_FLOAT,
      .size       = 4u,
      .normalized = false};
  const ngf_vertex_buf_binding_desc vert_buf_binding_desc {
      .binding    = 0u,
      .stride     = 4u * sizeof(float),
      .input_rate = NGF_INPUT_RATE_VERTEX};
  ngf_util_graphics_pipeline_data render_pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(&render_pipeline_data);
  render_pipeline_data.multisample_info.sample_count = main_render_target_sample_count;
  render_pipeline_data.input_assembly_info.enable_primitive_restart = true;
  render_pipeline_data.input_assembly_info.primitive_topology =
      NGF_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  render_pipeline_data.depth_stencil_info.depth_test        = true;
  render_pipeline_data.depth_stencil_info.depth_write       = true;
  render_pipeline_data.depth_stencil_info.depth_compare     = NGF_COMPARE_OP_LESS;
  render_pipeline_data.rasterization_info.cull_mode         = NGF_CULL_MODE_NONE;
  render_pipeline_data.vertex_input_info.nattribs           = 1u;
  render_pipeline_data.vertex_input_info.attribs            = &position_attrib_desc;
  render_pipeline_data.vertex_input_info.nvert_buf_bindings = 1u;
  render_pipeline_data.vertex_input_info.vert_buf_bindings  = &vert_buf_binding_desc;
  ngf_graphics_pipeline_info& render_pipe_info              = render_pipeline_data.pipeline_info;
  render_pipe_info.nshader_stages                           = 2u;
  render_pipe_info.shader_stages[0]                         = render_vertex_stage.get();
  render_pipe_info.shader_stages[1]                         = render_fragment_stage.get();
  render_pipe_info.compatible_rt_attachment_descs = ngf_default_render_target_attachment_descs();
  NGF_MISC_CHECK_NGF_ERROR(state->render_pipeline.initialize(render_pipe_info));

  /**
   * Initialize the index buffer.
   */
  const ngf_buffer_info staging_index_buffer_info {
      .size         = compute_verts::ntotal_indices * sizeof(uint32_t),
      .storage_type = NGF_BUFFER_STORAGE_HOST_WRITEABLE,
      .buffer_usage = NGF_BUFFER_USAGE_XFER_SRC};
  const ngf_buffer_info index_buffer_info {
      .size         = compute_verts::ntotal_indices * sizeof(uint32_t),
      .storage_type = NGF_BUFFER_STORAGE_PRIVATE,
      .buffer_usage = NGF_BUFFER_USAGE_XFER_DST | NGF_BUFFER_USAGE_INDEX_BUFFER};
  ngf::buffer staging_index_buffer;
  NGF_MISC_CHECK_NGF_ERROR(staging_index_buffer.initialize(staging_index_buffer_info));
  NGF_MISC_CHECK_NGF_ERROR(state->index_buffer.initialize(index_buffer_info));
  auto mapped_staging_index_buffer = (uint32_t*)
      ngf_buffer_map_range(staging_index_buffer.get(), 0u, staging_index_buffer_info.size);
  uint32_t idx = 0u;
  for (uint32_t strip = 0u; strip < compute_verts::nverts_per_side - 1; ++strip) {
    for (uint32_t v = 0u; v < compute_verts::nverts_per_side; ++v) {
      NGF_MISC_ASSERT(idx < compute_verts::ntotal_indices);
      mapped_staging_index_buffer[idx++] = (strip + 1u) * compute_verts::nverts_per_side + v;
      NGF_MISC_ASSERT(idx < compute_verts::ntotal_indices);
      mapped_staging_index_buffer[idx++] = strip * compute_verts::nverts_per_side + v;
    }
    NGF_MISC_ASSERT(idx < compute_verts::ntotal_indices);
    mapped_staging_index_buffer[idx++] = ~0u;
  }
  ngf_buffer_flush_range(staging_index_buffer.get(), 0, staging_index_buffer_info.size);
  ngf_buffer_unmap(staging_index_buffer.get());
  ngf_cmd_copy_buffer(
      xfer_encoder,
      staging_index_buffer.get(),
      state->index_buffer.get(),
      staging_index_buffer_info.size,
      0u,
      0u);

  /**
   * Create the vertex buffer.
   */
  const ngf_buffer_info vertex_buffer_info {
      .size         = compute_verts::ntotal_verts * (4u * sizeof(float)) * 2,
      .storage_type = NGF_BUFFER_STORAGE_PRIVATE,
      .buffer_usage = NGF_BUFFER_USAGE_VERTEX_BUFFER | NGF_BUFFER_USAGE_STORAGE_BUFFER};
  NGF_MISC_CHECK_NGF_ERROR(state->vertex_buffer.initialize(vertex_buffer_info));
  state->compute_buffer_slice.buffer = state->vertex_buffer.get();
  state->compute_buffer_slice.range  = compute_verts::ntotal_verts * (4u * sizeof(float));

  /**
   * Set up some initial viewing parameters.
   */
  state->camera.look_at[1] = 1.0f;

  state->render_uniforms_multibuf.initialize(3);
  state->compute_uniforms_multibuf.initialize(3);

  return static_cast<void*>(state);
}

void sample_draw_frame(
    ngf_render_encoder main_render_pass,
    float /* time_delta */,
    ngf_frame_token /* token */,
    uint32_t w,
    uint32_t h,
    float /* time */,
    void* userdata) {
  auto           state  = reinterpret_cast<compute_verts::state*>(userdata);
  const uint32_t f_prev = (state->frame + 1u) % 2;
  if (state->frame > 0u) {
    compute_verts::render_uniforms render_uniforms;
    render_uniforms.cam_matrices =
        compute_camera_matrices(state->camera, static_cast<float>(w) / static_cast<float>(h));
    state->render_uniforms_multibuf.write(render_uniforms);

    ngf_irect2d onsc_viewport {0, 0, w, h};
    ngf_cmd_bind_gfx_pipeline(main_render_pass, state->render_pipeline);
    ngf::cmd_bind_resources(
        main_render_pass,
        state->render_uniforms_multibuf.bind_op_at_current_offset(0, 0));
    ngf_cmd_viewport(main_render_pass, &onsc_viewport);
    ngf_cmd_scissor(main_render_pass, &onsc_viewport);
    ngf_cmd_bind_index_buffer(main_render_pass, state->index_buffer, 0u, NGF_TYPE_UINT32);
    ngf_cmd_bind_attrib_buffer(
        main_render_pass,
        state->vertex_buffer,
        0u,
        f_prev * sizeof(float) * 4u * compute_verts::ntotal_verts);
    ngf_cmd_draw(main_render_pass, true, 0u, compute_verts::ntotal_indices, 1u);
  }
}

void sample_pre_draw_frame(ngf_cmd_buffer, void*) { }

void sample_post_draw_frame(
    ngf_cmd_buffer     cmd_buffer,
    void*              userdata) {
  static float   time   = 0.f;
  auto           state  = reinterpret_cast<compute_verts::state*>(userdata);
  const uint32_t f_curr = (state->frame) % 2;
  time += 0.01f;
  compute_verts::compute_uniforms compute_uniforms;
  compute_uniforms.time = time;
  state->compute_uniforms_multibuf.write(compute_uniforms);

  ngf_compute_pass_info pass_info {};
  ngf_compute_encoder compute_enc;
  ngf_cmd_begin_compute_pass(cmd_buffer, &pass_info, &compute_enc);
  ngf_cmd_bind_compute_pipeline(compute_enc, state->compute_pipeline);
  ngf::cmd_bind_resources(
      compute_enc,
      state->compute_uniforms_multibuf.bind_op_at_current_offset(1, 1),
      ngf_resource_bind_op {

          .target_set     = 1u,
          .target_binding = 0u,
          .type           = NGF_DESCRIPTOR_STORAGE_BUFFER,
          .info           = {
              .buffer = {
                  .buffer = state->vertex_buffer.get(),
                  .offset = f_curr * 4u * sizeof(float) * compute_verts::ntotal_verts,
                  .range  = compute_verts::ntotal_verts * (4u * sizeof(float))}}});
  ngf_cmd_dispatch(
      compute_enc,
      compute_verts::nverts_per_side / 2,
      compute_verts::nverts_per_side / 2,
      1u);
  ngf_cmd_end_compute_pass(compute_enc);
  state->prev_compute_encoder = compute_enc;
  state->frame += 1u;
}

void sample_post_submit(void*) {
}

void sample_draw_ui(void* userdata) {
  auto data = reinterpret_cast<compute_verts::state*>(userdata);
  ImGui::Begin("Controls");
  camera_ui(data->camera, std::make_pair(-5.f, 5.f), .1f, std::make_pair(1.0f, 10.0f), .1f);
  ImGui::End();
}

void sample_shutdown(void* userdata) {
  delete reinterpret_cast<compute_verts::state*>(userdata);
}

}  // namespace ngf_samples
