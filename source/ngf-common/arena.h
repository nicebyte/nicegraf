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

#include <cstddef>
#include <cstdint>

namespace ngfi {

// Forward declaration of internal block structure
struct arena_block;

/**
 * Arena allocator - fast linear allocation with bulk deallocation.
 *
 * The arena allocates memory by bumping a pointer forward. Individual
 * allocations cannot be freed; instead, all memory is released at once
 * via reset() or when the arena is destroyed.
 *
 * When the current block is exhausted, a new block is automatically
 * allocated and chained to the arena.
 *
 * This is a move-only type. Use create() to construct an arena and
 * check is_valid() to verify successful initialization.
 */
class arena {
public:
  /**
   * Create an arena with the given initial capacity.
   * Returns an arena by value. Check is_valid() to verify success.
   */
  static arena create(size_t initial_capacity) noexcept;

  /**
   * Default constructor creates an invalid arena.
   */
  arena() noexcept = default;

  /**
   * Move constructor - transfers ownership from other arena.
   */
  arena(arena&& other) noexcept;

  /**
   * Destructor - frees all allocated memory.
   */
  ~arena() noexcept;

  /**
   * Check if the arena was initialized successfully.
   * Returns false for default-constructed or moved-from arenas.
   */
  bool is_valid() const noexcept;

  /**
   * Allocate memory with default max alignment.
   * Returns nullptr if allocation fails.
   */
  void* alloc(size_t size) noexcept;

  /**
   * Allocate memory with custom alignment.
   * Alignment must be a power of two.
   * Returns nullptr if allocation fails.
   */
  void* alloc_aligned(size_t size, size_t alignment) noexcept;

  /**
   * Allocate memory for a single object of type T.
   * Returns nullptr if allocation fails.
   */
  template<class T>
  T* alloc() noexcept {
    return static_cast<T*>(alloc(sizeof(T)));
  }

  /**
   * Allocate memory for an array of n objects of type T.
   * Returns nullptr if allocation fails.
   */
  template<class T>
  T* alloc(size_t n) noexcept {
    return static_cast<T*>(alloc(sizeof(T) * n));
  }

  /**
   * Reset the arena - invalidates all previous allocations.
   * Keeps the first block for reuse, frees overflow blocks.
   */
  void reset() noexcept;

  /**
   * Get total bytes allocated from the system (including block headers).
   */
  size_t total_allocated() const noexcept;

  /**
   * Get total bytes used by user allocations.
   */
  size_t total_used() const noexcept;

private:
  // Prevent copying and move-assignment
  arena(const arena&) = delete;
  arena& operator=(const arena&) = delete;
  arena& operator=(arena&&) = delete;

  // Allocate a new block and chain it to the arena
  bool grow(size_t min_capacity) noexcept;

  arena_block* first_block_   = nullptr;
  arena_block* current_block_ = nullptr;
  size_t total_allocated_     = 0;
  size_t total_used_          = 0;
  size_t default_block_size_  = 0;
};

}  // namespace ngfi
