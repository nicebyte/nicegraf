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

#include <stdio.h>

namespace ngf_samples {

struct sample_data {
  uint32_t magic_number = 0xdeadbeef;
};

void* sample_initialize(uint32_t , uint32_t ) {
  printf("sample initializing.\n");
  auto d = new sample_data{};
  d->magic_number = 0xbadf00d;
  printf("sample initialization complete.\n");
  return static_cast<void*>(d);
}

void sample_draw_frame(
    ngf_frame_token ,
    uint32_t        ,
    uint32_t        ,
    float           ,
    void*           ) {
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
