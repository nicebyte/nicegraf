#include "catch.hpp"
#include "handle_pool.h"

#include <atomic>
#include <thread>
#include <vector>

namespace {

struct alloc_counters {
  uint32_t nallocs = 0u;
  uint32_t nfrees  = 0u;
};

constexpr uint32_t DEFAULT_POOL_SIZE = 64;

uint64_t unlimited_fake_handle_allocator(void* counters) {
  return ++(static_cast<alloc_counters*>(counters)->nallocs);
}

uint64_t limited_fake_handle_allocator(void* c) {
  auto counters = static_cast<alloc_counters*>(c);
  if (counters->nallocs < DEFAULT_POOL_SIZE) {
    return ++(counters->nallocs);
  } else {
    return 0u;
  }
}

void fake_handle_deallocator(uint64_t h, void* c) {
  auto counters = static_cast<alloc_counters*>(c);
  REQUIRE(h <= counters->nallocs);
  counters->nfrees++;
}

}  // namespace

TEST_CASE("default capacity allocated", "[default capacity]") {
  alloc_counters        counters;
  ngfi_handle_pool_info pool_info {
      .initial_size         = DEFAULT_POOL_SIZE,
      .allocator            = unlimited_fake_handle_allocator,
      .allocator_userdata   = &counters,
      .deallocator          = fake_handle_deallocator,
      .deallocator_userdata = &counters};
  ngfi_handle_pool pool = ngfi_create_handle_pool(&pool_info);

  REQUIRE(pool);
  REQUIRE(counters.nallocs == pool_info.initial_size);
  REQUIRE(counters.nfrees == 0);
}

TEST_CASE("cleanup happens on failed create", "[fail create]") {
  alloc_counters        counters;
  constexpr uint32_t    nadditional_handles = 50;
  ngfi_handle_pool_info pool_info {
      .initial_size         = DEFAULT_POOL_SIZE + nadditional_handles,
      .allocator            = limited_fake_handle_allocator,
      .allocator_userdata   = &counters,
      .deallocator          = fake_handle_deallocator,
      .deallocator_userdata = &counters};
  ngfi_handle_pool pool = ngfi_create_handle_pool(&pool_info);

  REQUIRE(pool == NULL);
  REQUIRE(counters.nallocs == DEFAULT_POOL_SIZE);
  REQUIRE(counters.nfrees == DEFAULT_POOL_SIZE);
}

TEST_CASE("requesting handles within capacity doesn't chage pool size", "[nochange capacity]") {
  alloc_counters        counters;
  ngfi_handle_pool_info pool_info {
      .initial_size         = DEFAULT_POOL_SIZE,
      .allocator            = unlimited_fake_handle_allocator,
      .allocator_userdata   = &counters,
      .deallocator          = fake_handle_deallocator,
      .deallocator_userdata = &counters};
  ngfi_handle_pool pool = ngfi_create_handle_pool(&pool_info);

  for (uint32_t i = 0u; i < pool_info.initial_size; ++i) {
    uint64_t h = ngfi_handle_pool_alloc(pool);
    REQUIRE(h != 0);
    REQUIRE(counters.nallocs == pool_info.initial_size);
  }
}

TEST_CASE("requesting handles over current capacity causes new allocations", "[grow capacity]") {
  alloc_counters        counters;
  ngfi_handle_pool_info pool_info {
      .initial_size         = DEFAULT_POOL_SIZE,
      .allocator            = unlimited_fake_handle_allocator,
      .allocator_userdata   = &counters,
      .deallocator          = fake_handle_deallocator,
      .deallocator_userdata = &counters};
  ngfi_handle_pool pool = ngfi_create_handle_pool(&pool_info);

  constexpr uint32_t nadditional_handles = 50;
  for (uint32_t i = 0; i < pool_info.initial_size + nadditional_handles; ++i) {
    uint64_t h = ngfi_handle_pool_alloc(pool);
    REQUIRE(h != 0);
  }

  REQUIRE(counters.nallocs == pool_info.initial_size + nadditional_handles);
  REQUIRE(counters.nfrees == 0u);
}

TEST_CASE("returning handles to pool doesn't change number of allocations", "[nochange allocs]") {
  alloc_counters        counters;
  ngfi_handle_pool_info pool_info {
      .initial_size         = DEFAULT_POOL_SIZE,
      .allocator            = unlimited_fake_handle_allocator,
      .allocator_userdata   = &counters,
      .deallocator          = fake_handle_deallocator,
      .deallocator_userdata = &counters};
  ngfi_handle_pool pool = ngfi_create_handle_pool(&pool_info);

  constexpr uint32_t    nadditional_handles = 50;
  std::vector<uint64_t> handles;
  for (uint32_t i = 0; i < pool_info.initial_size + nadditional_handles; ++i) {
    handles.push_back(ngfi_handle_pool_alloc(pool));
    REQUIRE(handles.back() != 0);
  }

  REQUIRE(counters.nallocs == pool_info.initial_size + nadditional_handles);
  REQUIRE(counters.nfrees == 0u);

  for (uint64_t i = 0; i < handles.size(); i += 5) {
    ngfi_handle_pool_free(pool, handles[i]);
    REQUIRE(counters.nallocs == pool_info.initial_size + nadditional_handles);
    REQUIRE(counters.nfrees == 0);
  }
}

TEST_CASE("handle request returns 0 when no more can be allocated", "[fail alloc]") {
  alloc_counters        counters;
  ngfi_handle_pool_info pool_info {
      .initial_size         = DEFAULT_POOL_SIZE,
      .allocator            = limited_fake_handle_allocator,
      .allocator_userdata   = &counters,
      .deallocator          = fake_handle_deallocator,
      .deallocator_userdata = &counters};
  ngfi_handle_pool pool = ngfi_create_handle_pool(&pool_info);

  REQUIRE(pool);
  REQUIRE(counters.nallocs == pool_info.initial_size);
  REQUIRE(counters.nfrees == 0);

  // Exhaust pool.
  for (uint32_t i = 0; i < pool_info.initial_size; ++i) {
    const uint64_t h = ngfi_handle_pool_alloc(pool);
    REQUIRE(h != 0);
  }

  // Pool should have no available handles now.
  const uint64_t h = ngfi_handle_pool_alloc(pool);
  REQUIRE(h == 0);
  REQUIRE(counters.nallocs == pool_info.initial_size);
  REQUIRE(counters.nfrees == 0);
}

TEST_CASE("handles are recycled", "[recycle]") {
  alloc_counters        counters;
  ngfi_handle_pool_info pool_info {
      .initial_size         = DEFAULT_POOL_SIZE,
      .allocator            = limited_fake_handle_allocator,
      .allocator_userdata   = &counters,
      .deallocator          = fake_handle_deallocator,
      .deallocator_userdata = &counters};
  ngfi_handle_pool pool = ngfi_create_handle_pool(&pool_info);

  REQUIRE(pool);

  std::vector<uint64_t> handles;

  // Almost exhaust pool.
  constexpr uint32_t left_handles = 3u;
  for (uint32_t i = 0; i < pool_info.initial_size - left_handles; ++i) {
    handles.push_back(ngfi_handle_pool_alloc(pool));
    REQUIRE(handles.back() != 0);
  }

  REQUIRE(handles.size() / 2 > left_handles * 2);

  for (uint32_t i = 0; i < handles.size() / 2; ++i) { ngfi_handle_pool_free(pool, handles[i]); }

  for (uint32_t i = 0; i < left_handles * 2; ++i) {
    const uint64_t h = ngfi_handle_pool_alloc(pool);
    REQUIRE(h != 0);
  }
}

TEST_CASE("destroying pool fails if not all handles are returned", "[fail destroy]") {
  alloc_counters        counters;
  ngfi_handle_pool_info pool_info {
      .initial_size         = DEFAULT_POOL_SIZE,
      .allocator            = limited_fake_handle_allocator,
      .allocator_userdata   = &counters,
      .deallocator          = fake_handle_deallocator,
      .deallocator_userdata = &counters};
  ngfi_handle_pool pool = ngfi_create_handle_pool(&pool_info);

  REQUIRE(pool);

  std::vector<uint64_t> handles;
  for (uint32_t i = 0; i < pool_info.initial_size; ++i) {
    handles.push_back(ngfi_handle_pool_alloc(pool));
    REQUIRE(handles.back() != 0);
  }

  REQUIRE(counters.nfrees == 0);

  const bool failed_destroy_result = ngfi_destroy_handle_pool(pool);
  REQUIRE_FALSE(failed_destroy_result);

  for (uint64_t h : handles) { ngfi_handle_pool_free(pool, h); }

  const bool successful_destroy_result = ngfi_destroy_handle_pool(pool);
  REQUIRE(successful_destroy_result);
  REQUIRE(counters.nfrees == pool_info.initial_size);
}

TEST_CASE("multithreaded setup", "[multithread]") {
  alloc_counters        counters;
  ngfi_handle_pool_info pool_info {
      .initial_size         = DEFAULT_POOL_SIZE,
      .allocator            = unlimited_fake_handle_allocator,
      .allocator_userdata   = &counters,
      .deallocator          = fake_handle_deallocator,
      .deallocator_userdata = &counters};
  ngfi_handle_pool pool = ngfi_create_handle_pool(&pool_info);

  srand(static_cast<unsigned int>(time(NULL)));
  auto worker = [pool]() {
    constexpr uint32_t    nallocs_per_thread = 50000;
    std::vector<uint64_t> handles;
    for (uint32_t i = 0; i < nallocs_per_thread; ++i) {
      const uint64_t handle = ngfi_handle_pool_alloc(pool);
      REQUIRE(handle);
      handles.push_back(handle);
      if (rand() % 10 < 5) {
        const uint32_t j = static_cast<uint32_t>(rand() % handles.size());
        std::swap(handles[handles.size() - 1], handles[j]);
        ngfi_handle_pool_free(pool, handles.back());
        handles.pop_back();
      }
    }
    for (const uint64_t h : handles) { ngfi_handle_pool_free(pool, h); }
  };

  std::vector<std::thread> threads;
  constexpr uint32_t       nthreads = 5;
  for (uint32_t i = 0u; i < nthreads; ++i) { threads.push_back(std::thread(worker)); }

  std::atomic<int> end = 0;
  std::thread      straggler {[pool, &end]() {
    const uint64_t h = ngfi_handle_pool_alloc(pool);
    while (end.load() == 0) std::this_thread::yield();
    ngfi_handle_pool_free(pool, h);
  }};

  for (std::thread& t : threads) { t.join(); }

  const bool failed_destroy_result = ngfi_destroy_handle_pool(pool);
  REQUIRE_FALSE(failed_destroy_result);

  end = 1;
  straggler.join();

  const bool successful_destroy_result = ngfi_destroy_handle_pool(pool);
  REQUIRE(successful_destroy_result);
  REQUIRE(counters.nallocs == counters.nfrees);
}
