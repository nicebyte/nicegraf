#include "test-interface.h"
#include "test-util.h"
#include "nicegraf-wrappers.h"
#include "nicegraf-util.h"
#include "shader-loader.h"
#include "check.h"

#include <stdio.h>

namespace ngf_tests {

void* test_initialize(
    uint32_t         initial_window_width,
    uint32_t         initial_window_height,
    ngf_sample_count main_render_target_sample_count,
    ngf_xfer_encoder xfer_encoder){
  auto state = new render_to_texture_data{};
  
  /* Create the image to render to. */
  const ngf_extent3d img_size { 512u, 512u, 1u };
  const ngf_image_info img_info {
    NGF_IMAGE_TYPE_IMAGE_2D,
    img_size,
    1u,
    1u,
    NGF_IMAGE_FORMAT_BGRA8_SRGB,
    NGF_SAMPLE_COUNT_1,
    NGF_IMAGE_USAGE_SAMPLE_FROM | NGF_IMAGE_USAGE_ATTACHMENT
  };
  NGF_SAMPLES_CHECK_NGF_ERROR(state->rt_texture.initialize(img_info));
 
  /* Create the offscreen render target.*/
  const ngf_attachment_description offscreen_color_attachment_description {
    .type = NGF_ATTACHMENT_COLOR,
    .format = NGF_IMAGE_FORMAT_BGRA8_SRGB,
    .sample_count = NGF_SAMPLE_COUNT_1,
    .is_sampled = true
  };
  const ngf_attachment_descriptions attachments_list = {
    .descs = &offscreen_color_attachment_description,
    .ndescs = 1u,
  };
  const ngf_image_ref img_ref = {
    .image = state->rt_texture.get(),
    .mip_level = 0u,
    .layer = 0u,
    .cubemap_face = NGF_CUBEMAP_FACE_COUNT
  };
  ngf_render_target_info rt_info {
    &attachments_list,
    &img_ref
  };
  NGF_SAMPLES_CHECK_NGF_ERROR(state->offscreen_rt.initialize(rt_info));

  /**
   * Load shader stages.
   */
  const ngf::shader_stage offscreen_vertex_stage =
    load_shader_stage("small-triangle", "VSMain", NGF_STAGE_VERTEX);
  const ngf::shader_stage offscreen_fragment_stage =
    load_shader_stage("small-triangle", "PSMain", NGF_STAGE_FRAGMENT);

  /**
   * Create pipeline for offscreen pass.
   */
  ngf_util_graphics_pipeline_data offscreen_pipeline_data;
  ngf_util_create_default_graphics_pipeline_data(&offscreen_pipeline_data);
  ngf_graphics_pipeline_info &offscreen_pipe_info =
      offscreen_pipeline_data.pipeline_info;
  offscreen_pipe_info.nshader_stages = 2u;
  offscreen_pipe_info.shader_stages[0] = offscreen_vertex_stage.get();
  offscreen_pipe_info.shader_stages[1] = offscreen_fragment_stage.get();
  offscreen_pipe_info.compatible_rt_attachment_descs = &attachments_list;
  NGF_SAMPLES_CHECK_NGF_ERROR(state->offscreen_pipeline.initialize(offscreen_pipe_info));
 
  /* Create sampler.*/
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

  return static_cast<void*>(state);
    
}

void test_pre_draw_frame(ngf_cmd_buffer cmd_buffer, main_render_pass_sync_info* sync_op, void* userdata){

}

void test_draw_frame(
    ngf_render_encoder main_render_pass,
    float              time_delta_ms,
    ngf_frame_token    frame_token,
    uint32_t           width,
    uint32_t           height,
    float              time,
    void*              userdata){
  auto state = reinterpret_cast<render_to_texture_data*>(userdata);
  ngf_irect2d         offsc_viewport {0, 0, 512, 512};
  ngf_irect2d         onsc_viewport {0, 0, width, height};
  ngf_cmd_buffer      offscr_cmd_buf = nullptr;
  ngf_cmd_buffer_info cmd_info       = {};
  ngf_create_cmd_buffer(&cmd_info, &offscr_cmd_buf);
  ngf_start_cmd_buffer(offscr_cmd_buf, frame_token);
  {
    ngf::render_encoder renc {offscr_cmd_buf, state->offscreen_rt, .0f, 0.0f, 0.0f, 0.0f, 1.0, 0u};
    ngf_cmd_bind_gfx_pipeline(renc, state->offscreen_pipeline);
    ngf_cmd_viewport(renc, &offsc_viewport);
    ngf_cmd_scissor(renc, &offsc_viewport);
    ngf_cmd_draw(renc, false, 0u, 3u, 1u);
  }
  ngf_submit_cmd_buffers(1, &offscr_cmd_buf);
  ngf_destroy_cmd_buffer(offscr_cmd_buf);
}

void test_post_draw_frame(ngf_cmd_buffer cmd_buffer, ngf_render_encoder prev_render_encoder, void* userdata){

}

void test_post_submit(void* userdata){
  if(!ngf_validate_result(userdata, "references/triangle_reference.data"))
  {
    
  }
}

void test_shutdown(void* userdata){
    auto data = static_cast<render_to_texture_data*>(userdata);
    delete data;
    printf("shutting down\n");
}
}