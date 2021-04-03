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
#include <stdio.h>

namespace ngf_samples {

struct sample_data {
  ngf::graphics_pipeline pipeline;
};

void* sample_initialize(uint32_t , uint32_t ) {
  auto d = new sample_data{};
  
  return static_cast<void*>(d);
}

void sample_draw_frame(
    ngf_frame_token token,
    uint32_t        ,
    uint32_t        ,
    float           ,
    void*           ) {
    ngf_cmd_buffer cmdbuf = nullptr;
    const ngf_cmd_buffer_info cmdbuf_info = {
      .flags = 0u
    };
    ngf_create_cmd_buffer(&cmdbuf_info, &cmdbuf);
    ngf_start_cmd_buffer(cmdbuf, token);
    ngf_render_encoder renc;
    ngf_cmd_buffer_start_render(cmdbuf, &renc);
    constexpr ngf_attachment_load_op load_ops[2] = { NGF_LOAD_OP_DONTCARE, NGF_LOAD_OP_DONTCARE };
    constexpr ngf_attachment_store_op store_ops[2] = { NGF_STORE_OP_DONTCARE, NGF_STORE_OP_DONTCARE }; 
    ngf_render_target default_rt;
    ngf_default_render_target(&default_rt);
    ngf_pass_info pass = {
      .render_target = default_rt,
      .load_ops = load_ops,
      .store_ops = store_ops,
      .clears = nullptr,
    };
    ngf_cmd_begin_pass(renc, &pass);
    ngf_cmd_end_pass(renc);
    ngf_render_encoder_end(renc);
    ngf_submit_cmd_buffers(1, &cmdbuf);
    ngf_destroy_cmd_buffer(cmdbuf);
  //auto data = static_cast<sample_data*>(userdata);
  //printf("drawing frame %d (w %d h %d) at time %f magic number 0x%x\n", frame_token, width, height, time, data->magic_number);
}

void sample_draw_ui(void*) {
}

void sample_shutdown(void* userdata) {
  auto data = static_cast<sample_data*>(userdata);
  delete data;
  printf("shutting down\n");
}

}
