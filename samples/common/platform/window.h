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

#pragma once

#include <stdint.h>

namespace ngf_samples {

using window = uintptr_t;

enum class window_event_type { pointer, resize, close };

struct window_event {
  window_event_type type;
  union {
    struct {
      uint32_t x;
      uint32_t y;
      bool     down;
    } pointer_state;
    struct {
      uint32_t width;
      uint32_t height;
    } framebuffer_dimensions;
  };
};

using window_event_callback = void(const window_event&, void*);

void   window_set_event_callback(window_event_callback cb);
window window_create(
    const char* title,
    uint32_t    initial_framebuffer_width,
    uint32_t    initial_framebuffer_height);
void      window_get_size(window win, uint32_t* w, uint32_t* h);
bool      window_is_closed(window win);
uintptr_t window_native_handle(window win);
bool      window_poll_events();
void      window_destroy(window win);

}  // namespace ngf_samples