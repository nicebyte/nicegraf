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
#include "nicemath.h"
#include "imgui.h"
#include "mesh-loader.h"
#include "camera-controller.h"

#include <stdio.h>

namespace ngf_samples {

struct main_render_pass_uniforms {
  nm::float4x4 model_to_clip;
};

struct portal_render_pass_uniforms {
  camera_matrices cam_matrices;
};

struct portal_data {
  ngf::image             portal_texture;
  ngf::image             portal_depth_texture;
  ngf::render_target     portal_render_target;
  ngf::graphics_pipeline textured_quad_pipeline;
  ngf::graphics_pipeline portal_pipeline;
  ngf::graphics_pipeline non_portal_pipeline;
  ngf::sampler           sampler;

  ngf::uniform_multibuffer<main_render_pass_uniforms> main_uniforms;

  mesh bunny_mesh;
  ngf::uniform_multibuffer<portal_render_pass_uniforms> portal_uniforms;

  nm::float3 portal_position;
  nm::float3 camera_position;
  float      camera_rotation_y = 0.f;
  float      portal_rotation_y = 0.f;

  bool show_mesh = false;
};

void* sample_initialize(
    uint32_t,
    uint32_t,
    ngf_sample_count main_render_target_sample_count,
    ngf_xfer_encoder xfer_encoder) {
  auto state = new portal_data {};

  // set up the portal textures.
  ngf_image_info portal_texture_image_info = {
      .type = NGF_IMAGE_TYPE_IMAGE_2D,
      .extent =
          {
              .width  = 1024,
              .height = 1024,
              .depth  = 1u,
          },
      .nmips        = 1u,
      .nlayers      = 1u,
      .format       = NGF_IMAGE_FORMAT_BGRA8_SRGB,
      .sample_count = NGF_SAMPLE_COUNT_1,
      .usage_hint   = NGF_IMAGE_USAGE_SAMPLE_FROM |
                      NGF_IMAGE_USAGE_ATTACHMENT};
  NGF_SAMPLES_CHECK_NGF_ERROR(state->portal_texture.initialize(portal_texture_image_info));
  ngf_image_info portal_depth_texture_image_info = {
      .type = NGF_IMAGE_TYPE_IMAGE_2D,
      .extent =
          {
              .width  = 1024,
              .height = 1024,
              .depth  = 1u,
          },
      .nmips        = 1u,
      .nlayers      = 1u,
      .format       = NGF_IMAGE_FORMAT_DEPTH32,
      .sample_count = NGF_SAMPLE_COUNT_1,
      .usage_hint   = NGF_IMAGE_USAGE_ATTACHMENT};
  NGF_SAMPLES_CHECK_NGF_ERROR(state->portal_depth_texture.initialize(portal_depth_texture_image_info));

  // set up sampler.
  const ngf_sampler_info samp_info {
    NGF_FILTER_LINEAR,
    NGF_FILTER_LINEAR,
    NGF_FILTER_NEAREST,
    NGF_WRAP_MODE_CLAMP_TO_EDGE,
    NGF_WRAP_MODE_CLAMP_TO_EDGE,
    NGF_WRAP_MODE_CLAMP_TO_EDGE,
    0.0f,
    0.0f,
    0.0f,
    1.0f,
    false
  };
  NGF_SAMPLES_CHECK_NGF_ERROR(state->sampler.initialize(samp_info));

  // set up the portal render target.
  const ngf_attachment_description offscreen_color_attachment_descriptions[] = {
      {.type         = NGF_ATTACHMENT_COLOR,
       .format       = NGF_IMAGE_FORMAT_BGRA8_SRGB,
       .sample_count = NGF_SAMPLE_COUNT_1,
       .is_sampled   = true},
     {.type         = NGF_ATTACHMENT_DEPTH,
       .format       = NGF_IMAGE_FORMAT_DEPTH32,
       .sample_count = NGF_SAMPLE_COUNT_1,
       .is_sampled   = false},
  };
  const ngf_attachment_descriptions attachments_list = {
    .descs = offscreen_color_attachment_descriptions,
    .ndescs = 2u,
  };
  const ngf_image_ref img_refs[] = {
      {.image        = state->portal_texture.get(),
       .mip_level    = 0u,
       .layer        = 0u,
       .cubemap_face = NGF_CUBEMAP_FACE_COUNT},
      {
          .image        = state->portal_depth_texture.get(),
          .mip_level    = 0u,
          .layer        = 0u,
          .cubemap_face = NGF_CUBEMAP_FACE_COUNT,
      }};
  ngf_render_target_info rt_info {
    &attachments_list,
    img_refs
  };
  NGF_SAMPLES_CHECK_NGF_ERROR(state->portal_render_target.initialize(rt_info));

  // set up the pipeline for the main render pass.
  const ngf::shader_stage vertex_shader_stage =
      load_shader_stage("textured-quad", "VSMain", NGF_STAGE_VERTEX);
  const ngf::shader_stage fragment_shader_stage =
      load_shader_stage("textured-quad", "PSMain", NGF_STAGE_FRAGMENT);
  ngf_util_graphics_pipeline_data main_pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(&main_pipeline_data);
  main_pipeline_data.pipeline_info.nshader_stages   = 2;
  main_pipeline_data.pipeline_info.shader_stages[0] = vertex_shader_stage.get();
  main_pipeline_data.pipeline_info.shader_stages[1] = fragment_shader_stage.get();
  main_pipeline_data.multisample_info.sample_count = main_render_target_sample_count;
  main_pipeline_data.depth_stencil_info.depth_test  = true;
  main_pipeline_data.pipeline_info.compatible_rt_attachment_descs =
      ngf_default_render_target_attachment_descs();
  main_pipeline_data.rasterization_info.cull_mode = NGF_CULL_MODE_NONE;
  NGF_SAMPLES_CHECK_NGF_ERROR(state->textured_quad_pipeline.initialize(main_pipeline_data.pipeline_info));
 
  // load the model from a file.
  state->bunny_mesh = load_mesh_from_file("assets/bunny.mesh", xfer_encoder);
  NGF_SAMPLES_ASSERT(state->bunny_mesh.have_normals);
  NGF_SAMPLES_ASSERT(state->bunny_mesh.num_indices > 0u);

  // set up the mesh rendering pipeline.
  const ngf::shader_stage normals_vertex_shader_stage =
      load_shader_stage("mesh-normals", "VSMain", NGF_STAGE_VERTEX);
  const ngf::shader_stage normals_fragment_shader_stage =
      load_shader_stage("mesh-normals", "PSNormalsOnly", NGF_STAGE_FRAGMENT);
  ngf_util_graphics_pipeline_data pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(&pipeline_data);
  pipeline_data.pipeline_info.nshader_stages   = 2;
  pipeline_data.pipeline_info.shader_stages[0] = normals_vertex_shader_stage.get();
  pipeline_data.pipeline_info.shader_stages[1] = normals_fragment_shader_stage.get();
  pipeline_data.multisample_info.sample_count  = NGF_SAMPLE_COUNT_1;
  pipeline_data.depth_stencil_info.depth_test  = true;
  pipeline_data.depth_stencil_info.depth_write = true;
  pipeline_data.pipeline_info.compatible_rt_attachment_descs = &attachments_list;
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
  NGF_SAMPLES_CHECK_NGF_ERROR(state->portal_pipeline.initialize(pipeline_data.pipeline_info))
  pipeline_data.pipeline_info.compatible_rt_attachment_descs =
      ngf_default_render_target_attachment_descs();
  pipeline_data.multisample_info.sample_count  = NGF_SAMPLE_COUNT_8;
  NGF_SAMPLES_CHECK_NGF_ERROR(state->non_portal_pipeline.initialize(pipeline_data.pipeline_info))

  // allocate uniform buffers.
  NGF_SAMPLES_CHECK_NGF_ERROR(state->main_uniforms.initialize(3));
  NGF_SAMPLES_CHECK_NGF_ERROR(state->portal_uniforms.initialize(3));

  // initial parameters.
  state->camera_position = nm::float3 {0.f, 0.f, 0.f};
  state->camera_rotation_y = 0.f;
  state->portal_position   = {0.f, 0.f, -5.f};

  return (void*)state;
}

void sample_draw_frame(
    ngf_render_encoder main_render_pass,
    float /* time_delta */,
    ngf_frame_token frame_token,
    uint32_t  w,
    uint32_t h,
    float,
    void* userdata) {
  auto state = reinterpret_cast<portal_data*>(userdata);

  nm::float4x4 model_to_portal = nm::translation(nm::float3(0.0f, -.25f, -3.f));
  nm::float4x4 portal_to_world = nm::translation(state->portal_position) *
                                 nm::rotation_y(nm::deg2rad(state->portal_rotation_y));
  nm::float4x4 world_to_camera = nm::rotation_y(nm::deg2rad(-state->camera_rotation_y)) *
                                 nm::translation(-state->camera_position);
  nm::float4x4 portal_to_camera = world_to_camera * portal_to_world;
  nm::float4x4 camera_to_clip = nm::perspective(
      nm::deg2rad(72.0f),
      static_cast<float>(w) / static_cast<float>(h),
      0.01f,
      100.0f);


  // render to the portal texture.
  ngf_irect2d    offsc_viewport {0, 0, 1024, 1024};
  ngf_irect2d    onsc_viewport {0, 0, w, h};
  ngf_cmd_buffer offscr_cmd_buf = nullptr;

  {
    auto camera_to_world =
        nm::translation(state->camera_position) * nm::rotation_y(state->camera_rotation_y);
    auto  pcam_position_world = camera_to_world * nm::float4(0.f, 0.f, 0.f, 1.f);
    auto  world_to_pcam       = nm::rotation_y(nm::deg2rad(-state->portal_rotation_y)) * nm::translation(-pcam_position_world.xyz());
    auto  portal_to_pcam      = world_to_pcam * portal_to_world;
    auto  portal_position_pcam                  = portal_to_pcam * nm::float4(0.0f, 0.0f, 0.0f, 1.f);
    const float left   = portal_position_pcam.x() - 1.0f; 
    const float right  = portal_position_pcam.x() + 1.0f;
    const float bottom = portal_position_pcam.y() - 1.f;
    const float top    = portal_position_pcam.y() + 1.f;
    const float distance = fabs(portal_position_pcam.z());
    const float portal_neardist = 0.01f;
    const float bounds_scale    = portal_neardist / distance;
    auto        portal_persp    = nm::perspective(
        left * bounds_scale,
        right * bounds_scale,
        bottom * bounds_scale,
        top * bounds_scale,
        portal_neardist,
        100.f);

    ngf_cmd_buffer_info cmd_info = {};
    ngf_create_cmd_buffer(&cmd_info, &offscr_cmd_buf);
    ngf_start_cmd_buffer(offscr_cmd_buf, frame_token);

    // write uniforms for the portal render pass.
    camera_matrices cam_matrices;
    cam_matrices.world_to_view_transform =  portal_to_pcam * model_to_portal;
    cam_matrices.view_to_clip_transform = portal_persp;
    state->portal_uniforms.write(portal_render_pass_uniforms {cam_matrices});
    ngf::render_encoder
        renc {offscr_cmd_buf, state->portal_render_target, 1.0f, 1.0f, 1.0f, 1.0f, 1.0, 0u};
    ngf_cmd_viewport(renc, &offsc_viewport);
    ngf_cmd_scissor(renc, &offsc_viewport);
    ngf_cmd_bind_gfx_pipeline(renc, state->portal_pipeline);
    ngf::cmd_bind_resources(
        renc,
        state->portal_uniforms
            .bind_op_at_current_offset(0, 0, 0, sizeof(portal_render_pass_uniforms)));
    ngf_cmd_bind_attrib_buffer(renc, state->bunny_mesh.vertex_data.get(), 0, 0);
    ngf_cmd_bind_index_buffer(renc, state->bunny_mesh.index_data.get(), 0, NGF_TYPE_UINT32);
    ngf_cmd_draw(renc, true, 0u, (uint32_t)state->bunny_mesh.num_indices, 1u);
  } if (state->show_mesh) {
    // write uniforms for the portal render pass.
    camera_matrices cam_matrices;
    cam_matrices.world_to_view_transform = world_to_camera * portal_to_world * model_to_portal;
    cam_matrices.view_to_clip_transform  = camera_to_clip;
    state->portal_uniforms.write(portal_render_pass_uniforms {cam_matrices});

    auto renc = main_render_pass;
    ngf_cmd_viewport(renc, &onsc_viewport);
    ngf_cmd_scissor(renc, &onsc_viewport);
    ngf_cmd_bind_gfx_pipeline(renc, state->non_portal_pipeline);
    ngf::cmd_bind_resources(
        renc,
        state->portal_uniforms
            .bind_op_at_current_offset(0, 0, 0, sizeof(portal_render_pass_uniforms)));
    ngf_cmd_bind_attrib_buffer(renc, state->bunny_mesh.vertex_data.get(), 0, 0);
    ngf_cmd_bind_index_buffer(renc, state->bunny_mesh.index_data.get(), 0, NGF_TYPE_UINT32);
    ngf_cmd_draw(
      renc,
      true,
      0u,
      (uint32_t)state->bunny_mesh.num_indices,
      1u);
  }

  {
    ngf_submit_cmd_buffers(1, &offscr_cmd_buf);
    ngf_destroy_cmd_buffer(offscr_cmd_buf);

    // write uniforms for the main render pass.
    nm::float4x4 portal_transform_matrix = camera_to_clip * world_to_camera * portal_to_world;
    state->main_uniforms.write(main_render_pass_uniforms {portal_transform_matrix});

    // execute main render pass.

    ngf_cmd_bind_gfx_pipeline(main_render_pass, state->textured_quad_pipeline);
    ngf_cmd_viewport(main_render_pass, &onsc_viewport);
    ngf_cmd_scissor(main_render_pass, &onsc_viewport);
    ngf::cmd_bind_resources(
        main_render_pass,
        state->main_uniforms.bind_op_at_current_offset(0, 0, 0u, sizeof(nm::float4x4)),
        ngf::descriptor_set<0>::binding<1>::sampler(state->sampler),
        ngf::descriptor_set<1>::binding<0>::texture(state->portal_texture));
    ngf_cmd_draw(main_render_pass, false, 0, 6, 1);
  }
}

void sample_pre_draw_frame(ngf_cmd_buffer, main_render_pass_sync_info*, void*) {
}

void sample_post_draw_frame(ngf_cmd_buffer, ngf_render_encoder, void*) {
}

void sample_draw_ui(void* userdata) { 
  auto data = reinterpret_cast<portal_data*>(userdata);
  ImGui::Begin("Camera control");
  ImGui::DragFloat3("Position", data->camera_position.data, 0.1f);
  ImGui::DragFloat("Yaw (deg)", &data->camera_rotation_y, 0.1f, -90.f, 90.f);
  ImGui::End();

  ImGui::Begin("Portal control");
  ImGui::Checkbox("Show Mesh", &data->show_mesh);
  ImGui::DragFloat3("Position", data->portal_position.data, 0.1f);
  ImGui::DragFloat("Yaw (deg)", &data->portal_rotation_y, 0.1f, -90.f, 90.f);
  ImGui::End();
}

void sample_post_submit(void*) { }

void sample_shutdown(void* userdata) {
  auto data = static_cast<portal_data*>(userdata);
  delete data;
}

}  // namespace ngf_samples