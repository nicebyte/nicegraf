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

#include "imgui-backend.h"

#include "check.h"
#include "nicegraf-util.h"
#include "shader-loader.h"

#include <vector>

namespace ngf_samples {

ngf_imgui::ngf_imgui(
    ngf_xfer_encoder     enc,
    ngf_sample_count     main_render_target_sample_count,
    const unsigned char* font_atlas_bytes,
    uint32_t             font_atlas_width,
    uint32_t             font_atlas_height) {
#if !defined(NGF_NO_IMGUI)
  vertex_stage_   = load_shader_stage("imgui", "VSMain", NGF_STAGE_VERTEX);
  fragment_stage_ = load_shader_stage("imgui", "PSMain", NGF_STAGE_FRAGMENT);

  ngf_error err = NGF_ERROR_OK;

  // Initialize the streamed uniform object.
  uniform_data_.initialize(3);

  // Initial pipeline configuration with OpenGL-style defaults.
  ngf_util_graphics_pipeline_data pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(&pipeline_data);

  // Set up blend state.
  ngf_blend_info blend_info;
  blend_info.enable                 = true;
  blend_info.src_color_blend_factor = NGF_BLEND_FACTOR_SRC_ALPHA;
  blend_info.dst_color_blend_factor = NGF_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend_info.src_alpha_blend_factor = NGF_BLEND_FACTOR_SRC_ALPHA;
  blend_info.dst_alpha_blend_factor = NGF_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  blend_info.blend_op_color         = NGF_BLEND_OP_ADD;
  blend_info.blend_op_alpha         = NGF_BLEND_OP_ADD;
  blend_info.color_write_mask       = NGF_COLOR_MASK_WRITE_BIT_R | NGF_COLOR_MASK_WRITE_BIT_G |
                                NGF_COLOR_MASK_WRITE_BIT_B | NGF_COLOR_MASK_WRITE_BIT_A;
  pipeline_data.pipeline_info.color_attachment_blend_states = &blend_info;
  memset(
      pipeline_data.pipeline_info.blend_consts,
      0,
      sizeof(pipeline_data.pipeline_info.blend_consts));

  // Set up depth & stencil state.
  pipeline_data.depth_stencil_info.depth_test   = false;
  pipeline_data.depth_stencil_info.stencil_test = false;

  // Set up multisampling.
  pipeline_data.multisample_info.sample_count = main_render_target_sample_count;

  // Assign programmable stages.
  ngf_graphics_pipeline_info& pipeline_info = pipeline_data.pipeline_info;
  pipeline_info.nshader_stages              = 2u;
  pipeline_info.shader_stages[0]            = vertex_stage_.get();
  pipeline_info.shader_stages[1]            = fragment_stage_.get();

  // Disable backface culling.
  pipeline_data.rasterization_info.cull_mode = NGF_CULL_MODE_NONE;

  // Configure vertex input.
  ngf_vertex_attrib_desc vertex_attribs[] = {
      {0u, 0u, offsetof(ImDrawVert, pos), NGF_TYPE_FLOAT, 2u, false},
      {1u, 0u, offsetof(ImDrawVert, uv), NGF_TYPE_FLOAT, 2u, false},
      {2u, 0u, offsetof(ImDrawVert, col), NGF_TYPE_UINT8, 4u, true},
  };
  pipeline_data.vertex_input_info.attribs  = vertex_attribs;
  pipeline_data.vertex_input_info.nattribs = 3u;
  ngf_vertex_buf_binding_desc binding_desc = {
      0u,                    // binding
      sizeof(ImDrawVert),    // stride
      NGF_INPUT_RATE_VERTEX  // input rate
  };
  pipeline_data.vertex_input_info.nvert_buf_bindings = 1u;
  pipeline_data.vertex_input_info.vert_buf_bindings  = &binding_desc;
  pipeline_data.pipeline_info.compatible_rt_attachment_descs =
      ngf_default_render_target_attachment_descs();
  err = pipeline_.initialize(pipeline_data.pipeline_info);
  NGF_SAMPLES_ASSERT(err == NGF_ERROR_OK);

  // Create and populate font texture.
  const ngf_image_info font_texture_info = {
      NGF_IMAGE_TYPE_IMAGE_2D,                                        // type
      {(uint32_t)font_atlas_width, (uint32_t)font_atlas_height, 1u},  // extent
      1u,                                                             // nmips
      1u,                                                             // nlayers
      NGF_IMAGE_FORMAT_RGBA8,                                         // image_format
      NGF_SAMPLE_COUNT_1,                                             // samples
      NGF_IMAGE_USAGE_SAMPLE_FROM | NGF_IMAGE_USAGE_XFER_DST          // usage_hint
  };
  err = font_texture_.initialize(font_texture_info);
  NGF_SAMPLES_ASSERT(err == NGF_ERROR_OK);
  ImGui::GetIO().Fonts->TexID = (ImTextureID)(uintptr_t)font_texture_.get();
  const ngf_buffer_info pbuffer_info {
      4u * (size_t)font_atlas_width * (size_t)font_atlas_height,
      NGF_BUFFER_STORAGE_HOST_WRITEABLE,
      NGF_BUFFER_USAGE_XFER_SRC};
  err = texture_data_.initialize(pbuffer_info);
  NGF_SAMPLES_ASSERT(err == NGF_ERROR_OK);
  void* mapped_texture_data = ngf_buffer_map_range(
      texture_data_.get(),
      0,
      4 * (size_t)font_atlas_width * (size_t)font_atlas_height);
  memcpy(
      mapped_texture_data,
      font_atlas_bytes,
      4 * (size_t)font_atlas_width * (size_t)font_atlas_height);
  ngf_buffer_flush_range(
      texture_data_.get(),
      0,
      4 * (size_t)font_atlas_width * (size_t)font_atlas_height);
  ngf_buffer_unmap(texture_data_.get());
  ngf_image_ref font_texture_ref;
  font_texture_ref.image     = font_texture_.get();
  font_texture_ref.layer     = 0u;
  font_texture_ref.mip_level = 0u;
  ngf_cmd_write_image(
      enc,
      texture_data_.get(),
      0,
      font_texture_ref,
      ngf_offset3d {},
      ngf_extent3d {(uint32_t)font_atlas_width, (uint32_t)font_atlas_height, 1u},
      1u);

  // Create a sampler for the font texture.
  ngf_sampler_info sampler_info {
      NGF_FILTER_NEAREST,
      NGF_FILTER_NEAREST,
      NGF_FILTER_NEAREST,
      NGF_WRAP_MODE_CLAMP_TO_EDGE,
      NGF_WRAP_MODE_CLAMP_TO_EDGE,
      NGF_WRAP_MODE_CLAMP_TO_EDGE,
      0.0f,
      0.0f,
      0.0f,
      1.0f,
      false};
  tex_sampler_.initialize(sampler_info);
#endif
}

void ngf_imgui::record_rendering_commands(ngf_render_encoder enc) {
  ImGui::Render();
  ImDrawData* data = ImGui::GetDrawData();
  if (data->TotalIdxCount <= 0) return;
  // Compute effective viewport width and height, apply scaling for
  // retina/high-dpi displays.
  ImGuiIO& io        = ImGui::GetIO();
  int      fb_width  = (int)(data->DisplaySize.x * io.DisplayFramebufferScale.x);
  int      fb_height = (int)(data->DisplaySize.y * io.DisplayFramebufferScale.y);
  data->ScaleClipRects(io.DisplayFramebufferScale);

  // Avoid rendering when minimized.
  if (fb_width <= 0 || fb_height <= 0) { return; }

  // Build projection matrix.
  const ImVec2&      pos              = data->DisplayPos;
  const float        L                = pos.x;
  const float        R                = pos.x + data->DisplaySize.x;
  const float        T                = pos.y;
  const float        B                = pos.y + data->DisplaySize.y;
  const uniform_data ortho_projection = {{
      {2.0f / (R - L), 0.0f, 0.0f, 0.0f},
      {0.0f, 2.0f / (B - T), 0.0f, 0.0f},
      {0.0f, 0.0f, -1.0f, 0.0f},
      {(R + L) / (L - R), (T + B) / (T - B), 0.0f, 1.0f},
  }};
  uniform_data_.write(ortho_projection);

  // Bind the ImGui rendering pipeline.
  ngf_cmd_bind_gfx_pipeline(enc, pipeline_);

  // Bind resources.
  ngf::cmd_bind_resources(
      enc,
      uniform_data_.bind_op_at_current_offset(0u, 0u),
      ngf::descriptor_set<0>::binding<1>::texture(font_texture_.get()),
      ngf::descriptor_set<0>::binding<2>::sampler(tex_sampler_.get()));

  // Set viewport.
  ngf_irect2d viewport_rect = {0u, 0u, (uint32_t)fb_width, (uint32_t)fb_height};
  ngf_cmd_viewport(enc, &viewport_rect);
  ngf_cmd_scissor(enc, &viewport_rect);

  // These vectors will store vertex and index data for the draw calls.
  // Later this data will be transferred to GPU buffers.
  std::vector<ImDrawVert> vertex_data((size_t)data->TotalVtxCount, ImDrawVert());
  std::vector<ImDrawIdx>  index_data((size_t)data->TotalIdxCount, 0u);
  struct draw_data {
    ngf_irect2d scissor;
    uint32_t    first_elem;
    uint32_t    nelem;
  };
  std::vector<draw_data> draw_data;

  uint32_t last_vertex = 0u;
  uint32_t last_index  = 0u;

  // Process each ImGui command list and translate it into the nicegraf
  // command buffer.
  for (int i = 0u; i < data->CmdListsCount; ++i) {
    // Append vertex data.
    const ImDrawList* imgui_cmd_list = data->CmdLists[i];
    memcpy(
        &vertex_data[last_vertex],
        imgui_cmd_list->VtxBuffer.Data,
        sizeof(ImDrawVert) * (size_t)imgui_cmd_list->VtxBuffer.Size);

    // Append index data.
    for (int a = 0u; a < imgui_cmd_list->IdxBuffer.Size; ++a) {
      // ImGui uses separate index buffers, but we'll use just one. We will
      // update the index values accordingly.
      index_data[last_index + (size_t)a] = (ImDrawIdx)(last_vertex + imgui_cmd_list->IdxBuffer[a]);
    }
    last_vertex += (uint32_t)imgui_cmd_list->VtxBuffer.Size;

    // Process each ImGui command in the draw list.
    uint32_t idx_buffer_sub_offset = 0u;
    for (int j = 0u; j < imgui_cmd_list->CmdBuffer.Size; ++j) {
      const ImDrawCmd& cmd = imgui_cmd_list->CmdBuffer[j];
      if (cmd.UserCallback != nullptr) {
        cmd.UserCallback(imgui_cmd_list, &cmd);
      } else {
        ImVec4 clip_rect = ImVec4(
            cmd.ClipRect.x - pos.x,
            cmd.ClipRect.y - pos.y,
            cmd.ClipRect.z - pos.x,
            cmd.ClipRect.w - pos.y);
        if (clip_rect.x < (float)fb_width && clip_rect.y < (float)fb_height &&
            clip_rect.z >= 0.0f && clip_rect.w >= 0.0f) {
          const ngf_irect2d scissor_rect {
              (int32_t)clip_rect.x,
              (int32_t)clip_rect.y,
              (uint32_t)(clip_rect.z - clip_rect.x),
              (uint32_t)(clip_rect.w - clip_rect.y)};
          draw_data.push_back(
              {scissor_rect, last_index + idx_buffer_sub_offset, (uint32_t)cmd.ElemCount});
          idx_buffer_sub_offset += (uint32_t)cmd.ElemCount;
        }
      }
    }
    last_index += (uint32_t)imgui_cmd_list->IdxBuffer.Size;
  }

  // Create new vertex and index buffers.
  ngf_buffer_info attrib_buffer_info {
      sizeof(ImDrawVert) * vertex_data.size(),  // data size
      NGF_BUFFER_STORAGE_HOST_READABLE_WRITEABLE,
      NGF_BUFFER_USAGE_VERTEX_BUFFER};
  ngf_buffer attrib_buffer = nullptr;
  ngf_create_buffer(&attrib_buffer_info, &attrib_buffer);
  attrib_buffer_.reset(attrib_buffer);
  void* mapped_attrib_buffer = ngf_buffer_map_range(attrib_buffer, 0, attrib_buffer_info.size);
  NGF_SAMPLES_ASSERT(mapped_attrib_buffer != nullptr);
  memcpy(mapped_attrib_buffer, vertex_data.data(), attrib_buffer_info.size);
  ngf_buffer_flush_range(attrib_buffer, 0, attrib_buffer_info.size);
  ngf_buffer_unmap(attrib_buffer);

  ngf_buffer_info index_buffer_info {
      sizeof(ImDrawIdx) * index_data.size(),
      NGF_BUFFER_STORAGE_HOST_READABLE_WRITEABLE,
      NGF_BUFFER_USAGE_INDEX_BUFFER};
  ngf_buffer index_buffer = nullptr;
  ngf_create_buffer(&index_buffer_info, &index_buffer);
  index_buffer_.reset(index_buffer);
  void* mapped_index_buffer = ngf_buffer_map_range(index_buffer, 0, index_buffer_info.size);
  NGF_SAMPLES_ASSERT(mapped_index_buffer != nullptr);
  memcpy(mapped_index_buffer, index_data.data(), index_buffer_info.size);
  ngf_buffer_flush_range(index_buffer, 0, index_buffer_info.size);
  ngf_buffer_unmap(index_buffer);

  ngf_cmd_bind_index_buffer(
      enc,
      index_buffer,
      0u,
      sizeof(ImDrawIdx) < 4 ? NGF_TYPE_UINT16 : NGF_TYPE_UINT32);
  ngf_cmd_bind_attrib_buffer(enc, attrib_buffer, 0u, 0u);
  for (const auto& draw : draw_data) {
    ngf_cmd_scissor(enc, &draw.scissor);
    ngf_cmd_draw(enc, true, draw.first_elem, draw.nelem, 1u);
  }
}

}  // namespace ngf_samples
