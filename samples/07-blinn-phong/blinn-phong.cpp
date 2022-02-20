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
#include "camera-controller.h"
#include "check.h"
#include "imgui.h"
#include "logging.h"
#include "nicegraf-util.h"
#include "nicegraf-wrappers.h"
#include "nicemath.h"
#include "sample-interface.h"
#include "shader-loader.h"
#include "mesh-loader.h"

#include <stdio.h>

namespace ngf_samples {

namespace blinn_phong {

struct light_data {
  nm::float4 ambient_light_intensity { 0.01f, 0.02f, 0.03f, 0.0f };
  nm::float4 obj_space_point_light_position { 0.0f, 0.0f, 2.0f, 1.0f };
  nm::float4 point_light_intensity { 0.6f, 0.5f, 0.3f, 1.0f };
  nm::float4 obj_space_directional_light_direction { 0.0f, -1.0f, 0.5f, 0.0f };
  nm::float4 directional_light_intensity { 0.2f, 0.3f, 0.5f, 1.0f };
};

struct material_data {
  nm::float4 diffuse_reflectance { 0.9f, 0.9f, 0.9f, 1.0f};
  nm::float4 specular_coefficient { 1.0f, 1.0f, 1.0f, 1.0f };
  float      shininess = 125.0f;
};

struct uniforms {
  camera_matrices cam_matrices;
  light_data    lights;
  material_data material;
};

struct state {
  ngf::graphics_pipeline             vanilla_pipeline;
  ngf::graphics_pipeline             half_lambert_pipeline;
  mesh                               bunny_mesh;
  light_data                         lights;
  material_data                      material;
  ngf::uniform_multibuffer<uniforms> uniforms_multibuf;
  camera_state                       camera;
  float                              dolly = 3.0f;
  float                              vfov  = 60.0f;
  bool                               enable_half_lambert = true;
};

}  // namespace blinn_phong

void* sample_initialize(
    uint32_t /*width*/,
    uint32_t /*height*/,
    ngf_sample_count main_render_target_sample_count,
    ngf_xfer_encoder xfer_encoder) {
  auto state = new blinn_phong::state {};

  /**
   * Load the shader stages.
   */
  const ngf::shader_stage vertex_shader_stage =
      load_shader_stage("blinn-phong", "VSMain", NGF_STAGE_VERTEX);
  const ngf::shader_stage fragment_shader_stage =
      load_shader_stage("blinn-phong", "PSMain", NGF_STAGE_FRAGMENT);

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
      {/* normal. */
       .location   = 1u,
       .binding    = 0u,
       .offset     = 3u * sizeof(float),
       .type       = NGF_TYPE_FLOAT,
       .size       = 3u,
       .normalized = false},
  };

  /**
   * Note that the displayed model has positions, normals and UV coordinates,
   * however we only use positions and normals in this sample. We still have
   * to account for the UV coordinates when providing the stride for the vertex
   * attribute binding.
   */
  const ngf_vertex_buf_binding_desc vertex_buf_binding_descriptions[] = {{
      .binding    = 0u,
      .stride     = sizeof(float) * (3u + 3u + 2u),
      .input_rate = NGF_INPUT_RATE_VERTEX,
  }};

  pipeline_data.vertex_input_info.nattribs =
      sizeof(vertex_attrib_descriptions) / sizeof(vertex_attrib_descriptions[0]);
  pipeline_data.vertex_input_info.attribs = vertex_attrib_descriptions;
  pipeline_data.vertex_input_info.nvert_buf_bindings =
      sizeof(vertex_buf_binding_descriptions) / sizeof(vertex_buf_binding_descriptions[0]);
  pipeline_data.vertex_input_info.vert_buf_bindings = vertex_buf_binding_descriptions;

  /**
   * Initialize the "vanilla" pipeline object.
   */
  NGF_SAMPLES_CHECK_NGF_ERROR(state->vanilla_pipeline.initialize(pipeline_data.pipeline_info));

  /**
   * Set the appropriate specialization constant and initialize the half-lambert pipeline object.
   */
   const ngf_constant_specialization half_lambert_spec = {
    .constant_id = 0,
    .offset = 0,
    .type = NGF_TYPE_INT32
   };
   int half_lambert_spec_value = 1;
   pipeline_data.spec_info.nspecializations = 1;
   pipeline_data.spec_info.specializations =  &half_lambert_spec;
   pipeline_data.spec_info.value_buffer = &half_lambert_spec_value;
   NGF_SAMPLES_CHECK_NGF_ERROR(state->half_lambert_pipeline.initialize(pipeline_data.pipeline_info));

  /**
   * Load the model from a file.
   */
  state->bunny_mesh = load_mesh_from_file("assets/bunny.mesh", xfer_encoder);
  NGF_SAMPLES_ASSERT(state->bunny_mesh.have_normals);
  NGF_SAMPLES_ASSERT(state->bunny_mesh.num_indices > 0u);

  /**
   * Create the uniform buffer.
   */
  NGF_SAMPLES_CHECK_NGF_ERROR(state->uniforms_multibuf.initialize(3));

  /**
   * Set up some initial viewing parameters.
   */
   state->camera.look_at[1] = 1.0f;
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
  auto state = reinterpret_cast<blinn_phong::state*>(userdata);

  ngf_irect2d viewport {0, 0, w, h};
  ngf_cmd_bind_gfx_pipeline(main_render_pass, state->enable_half_lambert ? state->half_lambert_pipeline : state->vanilla_pipeline);
  ngf_cmd_viewport(main_render_pass, &viewport);
  ngf_cmd_scissor(main_render_pass, &viewport);
  ngf_cmd_bind_attrib_buffer(main_render_pass, state->bunny_mesh.vertex_data.get(), 0, 0);
  ngf_cmd_bind_index_buffer(main_render_pass, state->bunny_mesh.index_data.get(), NGF_TYPE_UINT32);
  blinn_phong::uniforms uniforms;
  uniforms.cam_matrices = compute_camera_matrices(state->camera, 
           static_cast<float>(w) / static_cast<float>(h));
  uniforms.material = state->material;
  uniforms.lights = state->lights;
  uniforms.lights.obj_space_point_light_position =
    uniforms.cam_matrices.world_to_view_transform * uniforms.lights.obj_space_point_light_position;
  uniforms.lights.obj_space_directional_light_direction =
    uniforms.cam_matrices.world_to_view_transform * uniforms.lights.obj_space_directional_light_direction;
  state->uniforms_multibuf.write(uniforms);
  ngf::cmd_bind_resources(
      main_render_pass,
      state->uniforms_multibuf.bind_op_at_current_offset(0, 0));
  ngf_cmd_draw(
      main_render_pass,
      true,
      0u,
      (uint32_t)state->bunny_mesh.num_indices,
      1u);
}

void sample_draw_ui(void* userdata) {
  auto data = reinterpret_cast<blinn_phong::state*>(userdata);
  ImGui::Begin("Controls");
  ImGui::Separator();
  ImGui::Checkbox("enable half-lambert trick", &data->enable_half_lambert);
  ImGui::Separator();
  camera_ui(data->camera, std::make_pair(-5.f, 5.f), .1f, std::make_pair(1.0f, 10.0f), .1f);
  ImGui::Separator();
  ImGui::Text("point light");
  ImGui::DragFloat3("position", data->lights.obj_space_point_light_position.data, 0.1f, -2.0f, 2.0f, "%.1f", 0);
  ImGui::ColorEdit3("intensity##0", data->lights.point_light_intensity.data);
  ImGui::Text("directional light");
  ImGui::DragFloat3("direction", data->lights.obj_space_directional_light_direction.data, 0.1f, -2.0f, 2.0f, "%.1f", 0);
  ImGui::ColorEdit3("intensity##1", data->lights.directional_light_intensity.data);
  ImGui::Text("ambient light");
  ImGui::ColorEdit3("intensity##2", data->lights.ambient_light_intensity.data);
  ImGui::Separator();
  ImGui::Text("material");
  ImGui::ColorEdit3("diffuse reflectance", data->material.diffuse_reflectance.data);
  ImGui::ColorEdit3("specular coefficient", data->material.specular_coefficient.data);
  ImGui::SliderFloat("shininess", &data->material.shininess, 0.1f, 1000.0f, "%.1f", 0);
  ImGui::End();
}

void sample_shutdown(void* userdata) {
  delete reinterpret_cast<blinn_phong::state*>(userdata);
}

}  // namespace ngf_samples
