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

#include "arena.h"

#include "macros.h"
#include "util.h"

#include <cstdlib>
#include <new>

namespace ngfi {

// Align a pointer to the given alignment (must be power of two)
static void* align_ptr(void* ptr, size_t alignment) noexcept {
  uintptr_t addr    = reinterpret_cast<uintptr_t>(ptr);
  uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
  return (void*)(aligned);
}

// Internal block structure - header followed by data in single allocation
struct arena::block {
  size_t        capacity;
  size_t        used;
  arena::block* next;

  uint8_t* data() noexcept {
    uintptr_t header_end = reinterpret_cast<uintptr_t>(this) + sizeof(arena::block);
    uintptr_t aligned    = (header_end + NGFI_MAX_ALIGNMENT - 1) & ~(NGFI_MAX_ALIGNMENT - 1);
    return reinterpret_cast<uint8_t*>(aligned);
  }

  const uint8_t* data() const noexcept {
    return const_cast<arena::block*>(this)->data();
  }

  // Calculate total allocation size for a block with given data capacity
  static size_t alloc_size(size_t data_capacity) noexcept {
    constexpr size_t header_size    = sizeof(arena::block);
    const size_t     align_mask     = NGFI_MAX_ALIGNMENT - 1;
    const size_t     aligned_header = (header_size + align_mask) & ~align_mask;
    return aligned_header + data_capacity;
  }

  static arena::block* create(size_t data_capacity) noexcept {
    size_t total_size = block::alloc_size(data_capacity);
    void*  raw        = malloc(total_size);
    if (!raw || total_size < sizeof(arena::block)) return nullptr;
    auto* block     = new (raw) arena::block {};
    block->capacity = data_capacity;
    block->used     = 0;
    block->next     = block;
    return block;
  }

  static void destroy(arena::block* block) noexcept {
    block->~block();
    ::free(block);
  }

  static size_t destroy_chain(arena::block* blks, bool destroy_self) noexcept {
    size_t result = 0;
    if (blks) {
      arena::block* cur = blks->next;
      while (cur != blks) {
        auto prev = cur;
        cur       = cur->next;
        result += alloc_size(prev->capacity);
        destroy(prev);
      }
      if (destroy_self) {
        result += alloc_size(blks->capacity);
        destroy(blks);
      }      else {
        blks->next = blks;
        }
    }
    return result;
  }

  void* alloc(size_t size, size_t alignment, size_t* out_total_used) noexcept {
    uint8_t* data_start   = data();
    uint8_t* current_ptr  = data_start + used;
    void*    aligned_ptr  = align_ptr(current_ptr, alignment);
    size_t   padding      = static_cast<size_t>(static_cast<uint8_t*>(aligned_ptr) - current_ptr);
    size_t   total_needed = padding + size;

    if (used + total_needed > capacity) { return nullptr; }

    used += total_needed;
    *out_total_used += total_needed;
    return aligned_ptr;
  }
};

arena::arena(size_t initial_capacity) noexcept : block_capacity_ { initial_capacity } {}

arena::arena(arena&& other) noexcept
    : current_block_(other.current_block_)
    , total_allocated_(other.total_allocated_)
    , total_used_(other.total_used_)
    , block_capacity_(other.block_capacity_) {
  other.current_block_      = nullptr;
  other.total_allocated_    = 0;
  other.total_used_         = 0;
  other.block_capacity_ = 0;
}

arena::~arena() noexcept {
  block::destroy_chain(current_block_, true);
}

void* arena::alloc(size_t size) noexcept {
  return alloc_aligned(size, NGFI_MAX_ALIGNMENT);
}

void* arena::alloc_aligned(size_t size, size_t alignment) noexcept {
  size_t min_capacity = size + alignment;  // Enough for alignment + allocation
  if (size == 0 || block_capacity_ == 0 || (!current_block_ && !grow(min_capacity))) {
    return nullptr;
  }

  // Try to allocate from current block
  void* result = current_block_->alloc(size, alignment, &total_used_);
  if (result) {
    return result;
  }

  // Need to grow - allocate new block
  if (!grow(min_capacity)) {
    return nullptr;
  }

  // Allocate from new block (should always succeed)
  return current_block_->alloc(size, alignment, &total_used_);
}

void arena::reset() noexcept {
  if (current_block_) {
    total_allocated_ -= block::destroy_chain(current_block_, false);
    current_block_->used = 0;
    current_block_->next = current_block_;
    total_used_          = 0;
  }
}

size_t arena::total_allocated() const noexcept {
  return total_allocated_;
}

size_t arena::total_used() const noexcept {
  return total_used_;
}

bool arena::grow(size_t min_capacity) noexcept {
  // New block size is at least default_block_size_ or min_capacity
  size_t new_capacity = block_capacity_;
  if (new_capacity < min_capacity) {
    new_capacity = min_capacity;
  }

  block* new_block = block::create(new_capacity);
  if (!new_block) {
    return false;
  }

  // Chain to current block
  if (current_block_) {
    new_block->next = current_block_->next;
    current_block_->next = new_block;
  }
  current_block_       = new_block;
  total_allocated_ += block::alloc_size(new_capacity);

  return true;
}

}  // namespace ngfi
