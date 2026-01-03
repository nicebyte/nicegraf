/**
 * Copyright (c) 2025 nicegraf contributors
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

#include "default-arenas.h"

namespace ngfi {

static thread_local arena tmp_arena_storage;
static thread_local arena frame_arena_storage;
static thread_local bool  arenas_initialized = false;

static void ensure_initialized() noexcept {
  if (!arenas_initialized) {
    tmp_arena_storage   = arena::create(100u * 1024u);  // 100KB
    frame_arena_storage = arena::create(4u * 1024u);    // 4KB
    arenas_initialized  = true;
  }
}

arena& tmp_arena() noexcept {
  ensure_initialized();
  return tmp_arena_storage;
}

arena& frame_arena() noexcept {
  ensure_initialized();
  return frame_arena_storage;
}

}  // namespace ngfi
