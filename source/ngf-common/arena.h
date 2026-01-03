/**
 * Copyright (c) 2026 nicegraf contributors
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

#include <stddef.h>
#include <stdint.h>

namespace ngfi {

class arena {
private:
  struct block;

  block* current_block_ = nullptr;

  size_t block_capacity_ = 0;
  size_t total_allocated_    = 0;
  size_t total_used_         = 0;

public:
  arena() = default;
  explicit arena(size_t initial_capacity) noexcept;
  arena(arena&& other) noexcept;
  ~arena() noexcept;
  arena(const arena&)            = delete;
  arena& operator=(const arena&) = delete;
  arena& operator=(arena&&)      = delete;

  void                 reset() noexcept;
  void*                alloc(size_t size) noexcept;
  void*                alloc_aligned(size_t size, size_t alignment) noexcept;
  template<class T> T* alloc() noexcept {
    return (T*)(alloc_aligned(sizeof(T), alignof(T)));
  }
  template<class T> T* alloc(size_t n) noexcept {
    // Check for overflow before computing sizeof(T) * n
    if (n != 0 && SIZE_MAX / sizeof(T) < n) {
      return nullptr;
    }
    return (T*)(alloc_aligned(sizeof(T) * n, alignof(T)));
  }

  size_t total_allocated() const noexcept;
  size_t total_used() const noexcept;
  void set_block_size(size_t size) { block_capacity_ = size; }

private:
  bool grow(size_t min_capacity) noexcept;
};

}  // namespace ngfi
