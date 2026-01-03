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

#pragma once

#include "arena.h"

namespace ngfi {

/**
 * Get the thread-local temporary arena.
 * This arena is reset frequently within operations.
 */
arena& tmp_arena() noexcept;

/**
 * Get the thread-local frame arena.
 * This arena is reset only at frame boundaries.
 */
arena& frame_arena() noexcept;

/**
 * Allocate a single element from the temporary arena.
 */
template<class T>
inline T* tmp_alloc() noexcept {
  return tmp_arena().alloc<T>();
}

/**
 * Allocate an array of n elements from the temporary arena.
 */
template<class T>
inline T* tmp_alloc(size_t n) noexcept {
  return tmp_arena().alloc<T>(n);
}

/**
 * Allocate a single element from the frame arena.
 */
template<class T>
inline T* frame_alloc() noexcept {
  return frame_arena().alloc<T>();
}

/**
 * Allocate an array of n elements from the frame arena.
 */
template<class T>
inline T* frame_alloc(size_t n) noexcept {
  return frame_arena().alloc<T>(n);
}

}  // namespace ngfi
