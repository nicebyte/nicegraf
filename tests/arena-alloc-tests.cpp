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

// Disable warning about setjmp/longjmp interaction with C++ object destruction.
// This is expected when using nicetest with C++ objects - memory may leak on
// assertion failure, but this is acceptable for testing.
#if defined(_MSC_VER)
#pragma warning(disable : 4611)
#endif

#define NT_BREAK_ON_ASSERT_FAIL
#include "ngf-common/arena.h"
#include "ngf-common/macros.h"

// Wrap nicetest.h in extern "C" to match linkage with test-suite-runner.c
extern "C" {
#include "nicetest.h"
}

#include <cstdlib>
#include <cstring>
#include <ctime>

// Helper to check pointer alignment
static bool is_aligned(void* ptr, size_t alignment) {
  return (reinterpret_cast<uintptr_t>(ptr) & (alignment - 1)) == 0;
}

NT_TESTSUITE {

  /* Basic tests */

  NT_TESTCASE("arena: create and destroy") {
    ngfi::arena arena = ngfi::arena::create(1024);
    NT_ASSERT(arena.is_valid());
    NT_ASSERT(arena.total_allocated() > 0);
    NT_ASSERT(arena.total_used() == 0);
  }

  NT_TESTCASE("arena: default constructed is invalid") {
    ngfi::arena arena;
    NT_ASSERT(!arena.is_valid());
    NT_ASSERT(arena.total_allocated() == 0);
    NT_ASSERT(arena.total_used() == 0);
  }

  NT_TESTCASE("arena: single allocation") {
    ngfi::arena arena = ngfi::arena::create(1024);
    NT_ASSERT(arena.is_valid());

    void* ptr = arena.alloc(64);
    NT_ASSERT(ptr != nullptr);
    NT_ASSERT(is_aligned(ptr, NGFI_MAX_ALIGNMENT));
    NT_ASSERT(arena.total_used() >= 64);

    // Write to the memory to verify it's usable
    std::memset(ptr, 0xAB, 64);
  }

  NT_TESTCASE("arena: multiple sequential allocations") {
    ngfi::arena arena = ngfi::arena::create(1024);
    NT_ASSERT(arena.is_valid());

    void* ptrs[10];
    for (int i = 0; i < 10; ++i) {
      ptrs[i] = arena.alloc(32);
      NT_ASSERT(ptrs[i] != nullptr);
      NT_ASSERT(is_aligned(ptrs[i], NGFI_MAX_ALIGNMENT));

      // Write unique pattern
      std::memset(ptrs[i], i + 1, 32);
    }

    // Verify patterns
    for (int i = 0; i < 10; ++i) {
      uint8_t* bytes = static_cast<uint8_t*>(ptrs[i]);
      for (int j = 0; j < 32; ++j) {
        NT_ASSERT(bytes[j] == static_cast<uint8_t>(i + 1));
      }
    }
  }

  NT_TESTCASE("arena: allocations are distinct") {
    ngfi::arena arena = ngfi::arena::create(1024);
    NT_ASSERT(arena.is_valid());

    void* ptr1 = arena.alloc(100);
    void* ptr2 = arena.alloc(100);
    void* ptr3 = arena.alloc(100);

    NT_ASSERT(ptr1 != nullptr);
    NT_ASSERT(ptr2 != nullptr);
    NT_ASSERT(ptr3 != nullptr);
    NT_ASSERT(ptr1 != ptr2);
    NT_ASSERT(ptr2 != ptr3);
    NT_ASSERT(ptr1 != ptr3);
  }

  /* Capacity tests */

  NT_TESTCASE("arena: fill initial capacity") {
    const size_t capacity = 256;
    ngfi::arena   arena    = ngfi::arena::create(capacity);
    NT_ASSERT(arena.is_valid());

    // Allocate small chunks until we exceed initial capacity
    size_t total_alloc = 0;
    while (total_alloc < capacity * 2) {
      void* ptr = arena.alloc(16);
      NT_ASSERT(ptr != nullptr);
      total_alloc += 16;
    }
  }

  NT_TESTCASE("arena: trigger block growth") {
    const size_t initial_capacity = 64;
    ngfi::arena   arena            = ngfi::arena::create(initial_capacity);
    NT_ASSERT(arena.is_valid());

    size_t initial_allocated = arena.total_allocated();

    // Allocate more than initial capacity
    void* ptr1 = arena.alloc(initial_capacity);
    NT_ASSERT(ptr1 != nullptr);

    void* ptr2 = arena.alloc(initial_capacity);
    NT_ASSERT(ptr2 != nullptr);

    // Should have grown
    NT_ASSERT(arena.total_allocated() > initial_allocated);
  }

  NT_TESTCASE("arena: large allocation exceeding initial capacity") {
    const size_t initial_capacity = 64;
    ngfi::arena   arena            = ngfi::arena::create(initial_capacity);
    NT_ASSERT(arena.is_valid());

    // Allocate more than initial capacity in one go
    void* ptr = arena.alloc(initial_capacity * 4);
    NT_ASSERT(ptr != nullptr);
    NT_ASSERT(is_aligned(ptr, NGFI_MAX_ALIGNMENT));
  }

  NT_TESTCASE("arena: many small allocations") {
    ngfi::arena arena = ngfi::arena::create(128);
    NT_ASSERT(arena.is_valid());

    // Many small allocations
    for (int i = 0; i < 1000; ++i) {
      void* ptr = arena.alloc(1);
      NT_ASSERT(ptr != nullptr);
    }
  }

  /* Reset tests */

  NT_TESTCASE("arena: reset and reallocate") {
    ngfi::arena arena = ngfi::arena::create(256);
    NT_ASSERT(arena.is_valid());

    void* ptr1 = arena.alloc(100);
    NT_ASSERT(ptr1 != nullptr);
    NT_ASSERT(arena.total_used() >= 100);

    arena.reset();
    NT_ASSERT(arena.total_used() == 0);

    void* ptr2 = arena.alloc(100);
    NT_ASSERT(ptr2 != nullptr);
    NT_ASSERT(arena.total_used() >= 100);
  }

  NT_TESTCASE("arena: multiple reset cycles") {
    ngfi::arena arena = ngfi::arena::create(128);
    NT_ASSERT(arena.is_valid());

    for (int cycle = 0; cycle < 10; ++cycle) {
      for (int i = 0; i < 20; ++i) {
        void* ptr = arena.alloc(16);
        NT_ASSERT(ptr != nullptr);
      }
      arena.reset();
      NT_ASSERT(arena.total_used() == 0);
    }
  }

  NT_TESTCASE("arena: reset releases overflow blocks") {
    const size_t initial_capacity = 64;
    ngfi::arena   arena            = ngfi::arena::create(initial_capacity);
    NT_ASSERT(arena.is_valid());

    size_t initial_allocated = arena.total_allocated();

    // Force overflow blocks
    for (int i = 0; i < 10; ++i) {
      arena.alloc(initial_capacity);
    }
    NT_ASSERT(arena.total_allocated() > initial_allocated);

    arena.reset();
    NT_ASSERT(arena.total_used() == 0);
    NT_ASSERT(arena.total_allocated() == initial_allocated);
  }

  /* Alignment tests */

  NT_TESTCASE("arena: default alignment") {
    ngfi::arena arena = ngfi::arena::create(1024);
    NT_ASSERT(arena.is_valid());

    for (int i = 0; i < 100; ++i) {
      void* ptr = arena.alloc(1 + (i % 32));
      NT_ASSERT(ptr != nullptr);
      NT_ASSERT(is_aligned(ptr, NGFI_MAX_ALIGNMENT));
    }
  }

  NT_TESTCASE("arena: custom alignments") {
    ngfi::arena arena = ngfi::arena::create(4096);
    NT_ASSERT(arena.is_valid());

    size_t alignments[] = {1, 2, 4, 8, 16, 32, 64};
    for (size_t align : alignments) {
      void* ptr = arena.alloc_aligned(32, align);
      NT_ASSERT(ptr != nullptr);
      NT_ASSERT(is_aligned(ptr, align));
    }
  }

  NT_TESTCASE("arena: alignment near block boundary") {
    const size_t initial_capacity = 128;
    ngfi::arena   arena            = ngfi::arena::create(initial_capacity);
    NT_ASSERT(arena.is_valid());

    // Fill most of the block
    arena.alloc(initial_capacity - 20);

    // Allocate with large alignment - should go to new block
    void* ptr = arena.alloc_aligned(16, 64);
    NT_ASSERT(ptr != nullptr);
    NT_ASSERT(is_aligned(ptr, 64));
  }

  /* Edge cases */

  NT_TESTCASE("arena: zero-size allocation returns nullptr") {
    ngfi::arena arena = ngfi::arena::create(1024);
    NT_ASSERT(arena.is_valid());

    void* ptr = arena.alloc(0);
    NT_ASSERT(ptr == nullptr);
  }

  NT_TESTCASE("arena: alloc on invalid arena returns nullptr") {
    ngfi::arena arena;
    NT_ASSERT(!arena.is_valid());

    void* ptr = arena.alloc(64);
    NT_ASSERT(ptr == nullptr);
  }

  NT_TESTCASE("arena: reset on invalid arena is safe") {
    ngfi::arena arena;
    NT_ASSERT(!arena.is_valid());
    arena.reset();  // Should not crash
  }

  /* Move semantics tests */

  NT_TESTCASE("arena: move constructor") {
    ngfi::arena arena1 = ngfi::arena::create(256);
    NT_ASSERT(arena1.is_valid());

    void* ptr = arena1.alloc(64);
    NT_ASSERT(ptr != nullptr);

    size_t used      = arena1.total_used();
    size_t allocated = arena1.total_allocated();

    ngfi::arena arena2(static_cast<ngfi::arena&&>(arena1));

    // arena2 should have taken ownership
    NT_ASSERT(arena2.is_valid());
    NT_ASSERT(arena2.total_used() == used);
    NT_ASSERT(arena2.total_allocated() == allocated);

    // arena1 should be invalid
    NT_ASSERT(!arena1.is_valid());
    NT_ASSERT(arena1.total_used() == 0);
    NT_ASSERT(arena1.total_allocated() == 0);

    // Can still allocate from arena2
    void* ptr2 = arena2.alloc(64);
    NT_ASSERT(ptr2 != nullptr);
  }

  /* Fuzz tests */

  NT_TESTCASE("arena: fuzz random allocation sizes") {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    ngfi::arena arena = ngfi::arena::create(256);
    NT_ASSERT(arena.is_valid());

    for (int i = 0; i < 1000; ++i) {
      size_t size = 1 + (std::rand() % 256);
      void*  ptr  = arena.alloc(size);
      NT_ASSERT(ptr != nullptr);
      NT_ASSERT(is_aligned(ptr, NGFI_MAX_ALIGNMENT));
    }
  }

  NT_TESTCASE("arena: fuzz random reset patterns") {
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    ngfi::arena arena = ngfi::arena::create(128);
    NT_ASSERT(arena.is_valid());

    for (int i = 0; i < 500; ++i) {
      size_t size = 1 + (std::rand() % 64);
      void*  ptr  = arena.alloc(size);
      NT_ASSERT(ptr != nullptr);

      // Randomly reset
      if (std::rand() % 10 == 0) {
        arena.reset();
        NT_ASSERT(arena.total_used() == 0);
      }
    }
  }

  /* Statistics tests */

  NT_TESTCASE("arena: total_allocated tracking") {
    ngfi::arena arena = ngfi::arena::create(256);
    NT_ASSERT(arena.is_valid());

    size_t initial = arena.total_allocated();
    NT_ASSERT(initial > 0);

    // Force growth
    arena.alloc(512);
    NT_ASSERT(arena.total_allocated() > initial);
  }

  NT_TESTCASE("arena: total_used tracking") {
    ngfi::arena arena = ngfi::arena::create(1024);
    NT_ASSERT(arena.is_valid());

    NT_ASSERT(arena.total_used() == 0);

    arena.alloc(64);
    NT_ASSERT(arena.total_used() >= 64);

    size_t used_before = arena.total_used();
    arena.alloc(128);
    NT_ASSERT(arena.total_used() >= used_before + 128);

    arena.reset();
    NT_ASSERT(arena.total_used() == 0);
  }

}
