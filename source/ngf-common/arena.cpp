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

#include "arena.h"

#include "macros.h"
#include "util.h"

#include <cstdlib>
#include <new>

namespace ngfi {

// Internal block structure - header followed by data in single allocation
struct arena_block {
  size_t capacity;
  size_t used;
  arena_block* next;

  // Compute data pointer using uintptr_t (avoids UB from pointer arithmetic)
  uint8_t* data() noexcept {
    uintptr_t header_end = reinterpret_cast<uintptr_t>(this) + sizeof(arena_block);
    uintptr_t aligned    = (header_end + NGFI_MAX_ALIGNMENT - 1) & ~(NGFI_MAX_ALIGNMENT - 1);
    return reinterpret_cast<uint8_t*>(aligned);
  }

  const uint8_t* data() const noexcept {
    return const_cast<arena_block*>(this)->data();
  }
};

// Calculate total allocation size for a block with given data capacity
static size_t block_alloc_size(size_t data_capacity) noexcept {
  constexpr size_t header_size    = sizeof(arena_block);
  const size_t     align_mask     = NGFI_MAX_ALIGNMENT - 1;
  const size_t     aligned_header = (header_size + align_mask) & ~align_mask;
  return aligned_header + data_capacity;
}

// Create a new block with given data capacity
static arena_block* create_block(size_t data_capacity) noexcept {
  size_t total_size = block_alloc_size(data_capacity);
  void*  raw        = std::malloc(total_size);
  if (!raw) return nullptr;

  // Placement new for block header
  auto* block     = new (raw) arena_block{};
  block->capacity = data_capacity;
  block->used     = 0;
  block->next     = nullptr;
  return block;
}

// Destroy a block (matches create_block)
static void destroy_block(arena_block* block) noexcept {
  block->~arena_block();
  std::free(block);
}

// Free a chain of blocks starting from the given block
static void free_block_chain(arena_block* block) noexcept {
  while (block) {
    arena_block* next = block->next;
    destroy_block(block);
    block = next;
  }
}

// Align a pointer to the given alignment (must be power of two)
static void* align_ptr(void* ptr, size_t alignment) noexcept {
  uintptr_t addr    = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
  return reinterpret_cast<void*>(aligned);
}

// Try to allocate from a block with given alignment
// Returns nullptr if the block doesn't have enough space
// On success, updates block->used and returns the aligned pointer
static void* try_alloc_from_block(
    arena_block* block,
    size_t       size,
    size_t       alignment,
    size_t*      out_total_used) noexcept {
  uint8_t* data_start   = block->data();
  uint8_t* current_ptr  = data_start + block->used;
  void*    aligned_ptr  = align_ptr(current_ptr, alignment);
  size_t   padding      = static_cast<size_t>(static_cast<uint8_t*>(aligned_ptr) - current_ptr);
  size_t   total_needed = padding + size;

  if (block->used + total_needed > block->capacity) {
    return nullptr;
  }

  block->used += total_needed;
  *out_total_used += total_needed;
  return aligned_ptr;
}

arena arena::create(size_t initial_capacity) noexcept {
  arena a;
  a.default_block_size_ = initial_capacity;

  arena_block* block = create_block(initial_capacity);
  if (!block) {
    return a;  // Invalid arena
  }

  a.first_block_     = block;
  a.current_block_   = block;
  a.total_allocated_ = block_alloc_size(initial_capacity);
  a.total_used_      = 0;

  return a;
}

arena::arena(arena&& other) noexcept {
  *this = ngfi::move(other);
}

arena& arena::operator=(arena&& other) noexcept {
  if (this != &other) {
    // Free existing blocks
    free_block_chain(first_block_);

    // Transfer ownership
    first_block_        = other.first_block_;
    current_block_      = other.current_block_;
    total_allocated_    = other.total_allocated_;
    total_used_         = other.total_used_;
    default_block_size_ = other.default_block_size_;

    other.first_block_        = nullptr;
    other.current_block_      = nullptr;
    other.total_allocated_    = 0;
    other.total_used_         = 0;
    other.default_block_size_ = 0;
  }
  return *this;
}

arena::~arena() noexcept {
  free_block_chain(first_block_);
}

bool arena::is_valid() const noexcept {
  return first_block_ != nullptr;
}

void* arena::alloc(size_t size) noexcept {
  return alloc_aligned(size, NGFI_MAX_ALIGNMENT);
}

void* arena::alloc_aligned(size_t size, size_t alignment) noexcept {
  if (!is_valid() || size == 0) {
    return nullptr;
  }

  // Try to allocate from current block
  void* result = try_alloc_from_block(current_block_, size, alignment, &total_used_);
  if (result) {
    return result;
  }

  // Need to grow - allocate new block
  size_t min_capacity = size + alignment;  // Enough for alignment + allocation
  if (!grow(min_capacity)) {
    return nullptr;
  }

  // Allocate from new block (should always succeed)
  return try_alloc_from_block(current_block_, size, alignment, &total_used_);
}

void arena::reset() noexcept {
  if (!is_valid()) {
    return;
  }

  // Free all blocks except the first one
  arena_block* block = first_block_->next;
  while (block) {
    arena_block* next = block->next;
    total_allocated_ -= block_alloc_size(block->capacity);
    destroy_block(block);
    block = next;
  }

  // Reset first block
  first_block_->used = 0;
  first_block_->next = nullptr;
  current_block_     = first_block_;
  total_used_        = 0;
}

size_t arena::total_allocated() const noexcept {
  return total_allocated_;
}

size_t arena::total_used() const noexcept {
  return total_used_;
}

bool arena::grow(size_t min_capacity) noexcept {
  // New block size is at least default_block_size_ or min_capacity
  size_t new_capacity = default_block_size_;
  if (new_capacity < min_capacity) {
    new_capacity = min_capacity;
  }

  arena_block* new_block = create_block(new_capacity);
  if (!new_block) {
    return false;
  }

  // Chain to current block
  current_block_->next = new_block;
  current_block_       = new_block;
  total_allocated_ += block_alloc_size(new_capacity);

  return true;
}

}  // namespace ngfi
