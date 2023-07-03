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

#pragma once

#pragma warning(disable:26812)
#include "nicegraf.h"

#include <stdint.h>

/**
 * Each sample has to implement the functions declared in this header.
 * They are called by the common sample code.
 */
namespace ngf_samples {

/**
 * Samples may specify what compute passes the main render pass should wait on.
 */
struct main_render_pass_sync_info {
  uint32_t                   nsync_compute_resources;
  ngf_sync_compute_resource* sync_compute_resources;
};

/**
 * This function is called once at startup, to let the sample set up whatever it needs.
 * This function may assume that a nicegraf context has already been created and made current on
 * the calling thread.
 * It gets passed the dimensions of the window to be rendered to, as well as the sample count
 * of the main rendertarget.
 * It also gets a transfer encoder, which can samples can use to upload some resources to the
 * GPU.
 * The function shall return a pointer that will be passed in to other callbacks.
 */
void* sample_initialize(
    uint32_t         initial_window_width,
    uint32_t         initial_window_height,
    ngf_sample_count main_render_target_sample_count,
    ngf_xfer_encoder xfer_encoder);

/**
 * This function gets called every frame before beginning the main render pass.
 * It receives the command buffer that the main render pass will eventually be
 * recorded into.
 */
void sample_pre_draw_frame(ngf_cmd_buffer cmd_buffer, main_render_pass_sync_info* sync_op, void* userdata);

/**
 * This function gets called every frame, to render the frame contents.
 * It gets passed a token identifying the frame, the current window dimensions, and a (monotonically
 * increasing) timestamp. Window resizes are generally handled in the common code, but it's up to
 * the specific sample to monitor for size changes and e.g. resize any rendertargets that have to
 * match screen resolution. `userdata` is the pointer returned previously by `sample_initialize`.
 */
void sample_draw_frame(
    ngf_render_encoder main_render_pass,
    float              time_delta_ms,
    ngf_frame_token    frame_token,
    uint32_t           width,
    uint32_t           height,
    float              time,
    void*              userdata);

/**
 * This function gets called every frame after finishing the main render pass.
 * It receives the command buffer that the main render pass was previously
 * recorded into.
 */
void sample_post_draw_frame(ngf_cmd_buffer cmd_buffer, ngf_render_encoder prev_render_encoder, void* userdata);

void sample_post_submit(void* userdata);


/**
 * This function gets called every frame, to render the UI of the sample. It should mostly consist
 * of ImGui calls. `userdata` is the pointer returned previously by `sample_initialize`.
 */
void sample_draw_ui(void* userdata);

/**
 * This function gets called once, before the sample ceases execution, to perform any cleanup
 * actions. This function may assume that a nicegraf context is still present and current on the
 * calling thread. `userdata` is the pointer returned previously by `sample_initialize`.
 */
void sample_shutdown(void* userdata);

}  // namespace ngf_samples
