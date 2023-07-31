#pragma once

#include "nicegraf.h"
#include <stdint.h>

namespace ngf_tests{

struct main_render_pass_sync_info {
  uint32_t                   nsync_compute_resources;
  ngf_sync_compute_resource* sync_compute_resources;
};

struct render_to_texture_data {
  ngf::render_target default_rt;
  ngf::render_target offscreen_rt;
  ngf::graphics_pipeline offscreen_pipeline;
  ngf::image rt_texture;
  ngf::sampler sampler;
  ngf_frame_token frame_token;
};

void* test_initialize(
    uint32_t         initial_window_width,
    uint32_t         initial_window_height,
    ngf_sample_count main_render_target_sample_count,
    ngf_xfer_encoder xfer_encoder);

void test_pre_draw_frame(ngf_cmd_buffer cmd_buffer, main_render_pass_sync_info* sync_op, void* userdata);

void test_draw_frame(
    ngf_render_encoder main_render_pass,
    float              time_delta_ms,
    ngf_frame_token    frame_token,
    uint32_t           width,
    uint32_t           height,
    float              time,
    void*              userdata);

void test_post_draw_frame(ngf_cmd_buffer cmd_buffer, ngf_render_encoder prev_render_encoder, void* userdata);

void test_post_submit(void* userdata);

void test_shutdown(void* userdata);
}