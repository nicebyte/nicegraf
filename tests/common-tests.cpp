#include "ngf-common/arena.h"
#include "ngf-common/array.h"
#include "ngf-common/chunked-list.h"
#include "ngf-common/cmdbuf-state.h"
#include "ngf-common/frame-token.h"
#include "ngf-common/hashtable.h"
#include "ngf-common/unique-ptr.h"
#include "ngf-common/value-or-error.h"

#include "utest.h"

// Use system allocator for tests to avoid NGF allocation callback setup.
template<class T>
using test_array = ngfi::array<T, ngfi::system_alloc_callbacks>;

UTEST_STATE();

int main(int argc, const char* const argv[]) {
  // Initialize NGF allocation callbacks (initializes the mutex).
  ngfi_set_allocation_callbacks(NULL);
  return utest_main(argc, argv);
}

UTEST(array, default_construction) {
  test_array<int> arr;
  ASSERT_EQ(0u, arr.size());
  ASSERT_EQ(0u, arr.capacity());
  ASSERT_TRUE(arr.empty());
  ASSERT_EQ(nullptr, arr.data());
}

UTEST(array, size_construction) {
  test_array<int> arr(10);
  ASSERT_EQ(10u, arr.size());
  ASSERT_EQ(10u, arr.capacity());
  ASSERT_FALSE(arr.empty());
  ASSERT_NE(nullptr, arr.data());
}

UTEST(array, push_back) {
  test_array<int> arr;
  arr.push_back(1);
  arr.push_back(2);
  arr.push_back(3);
  ASSERT_EQ(3u, arr.size());
  ASSERT_EQ(1, arr[0]);
  ASSERT_EQ(2, arr[1]);
  ASSERT_EQ(3, arr[2]);
}

UTEST(array, emplace_back) {
  test_array<int> arr;
  arr.emplace_back(42);
  arr.emplace_back(100);
  ASSERT_EQ(2u, arr.size());
  ASSERT_EQ(42, arr[0]);
  ASSERT_EQ(100, arr[1]);
}

UTEST(array, pop_back) {
  test_array<int> arr;
  arr.push_back(1);
  arr.push_back(2);
  arr.push_back(3);
  arr.pop_back();
  ASSERT_EQ(2u, arr.size());
  ASSERT_EQ(1, arr[0]);
  ASSERT_EQ(2, arr[1]);
}

UTEST(array, pop_back_empty) {
  test_array<int> arr;
  arr.pop_back();  // Should not crash.
  ASSERT_EQ(0u, arr.size());
}

UTEST(array, clear) {
  test_array<int> arr;
  arr.push_back(1);
  arr.push_back(2);
  arr.clear();
  ASSERT_EQ(0u, arr.size());
  ASSERT_TRUE(arr.empty());
  ASSERT_GT(arr.capacity(), 0u);  // Capacity should remain.
}

UTEST(array, front_and_back) {
  test_array<int> arr;
  arr.push_back(10);
  arr.push_back(20);
  arr.push_back(30);
  ASSERT_EQ(10, arr.front());
  ASSERT_EQ(30, arr.back());
}

UTEST(array, resize_grow) {
  test_array<int> arr;
  arr.resize(5);
  ASSERT_EQ(5u, arr.size());
  ASSERT_GE(arr.capacity(), 5u);
}

UTEST(array, resize_shrink) {
  test_array<int> arr;
  arr.push_back(1);
  arr.push_back(2);
  arr.push_back(3);
  arr.resize(1);
  ASSERT_EQ(1u, arr.size());
  ASSERT_EQ(1, arr[0]);
}

UTEST(array, reserve) {
  test_array<int> arr;
  arr.reserve(100);
  ASSERT_EQ(0u, arr.size());
  ASSERT_GE(arr.capacity(), 100u);
}

UTEST(array, reserve_smaller_noop) {
  test_array<int> arr;
  arr.reserve(100);
  size_t cap = arr.capacity();
  arr.reserve(50);
  ASSERT_EQ(cap, arr.capacity());  // Should not shrink.
}

UTEST(array, iterators) {
  test_array<int> arr;
  arr.push_back(1);
  arr.push_back(2);
  arr.push_back(3);

  int sum = 0;
  for (auto it = arr.begin(); it != arr.end(); ++it) {
    sum += *it;
  }
  ASSERT_EQ(6, sum);
}

UTEST(array, range_for) {
  test_array<int> arr;
  arr.push_back(10);
  arr.push_back(20);
  arr.push_back(30);

  int sum = 0;
  for (int val : arr) {
    sum += val;
  }
  ASSERT_EQ(60, sum);
}

UTEST(array, move_construction) {
  test_array<int> arr1;
  arr1.push_back(1);
  arr1.push_back(2);

  test_array<int> arr2(ngfi::move(arr1));
  ASSERT_EQ(2u, arr2.size());
  ASSERT_EQ(1, arr2[0]);
  ASSERT_EQ(2, arr2[1]);
  ASSERT_EQ(0u, arr1.size());
  ASSERT_EQ(nullptr, arr1.data());
}

UTEST(array, move_assignment) {
  test_array<int> arr1;
  arr1.push_back(1);
  arr1.push_back(2);

  test_array<int> arr2;
  arr2.push_back(100);

  arr2 = ngfi::move(arr1);
  ASSERT_EQ(2u, arr2.size());
  ASSERT_EQ(1, arr2[0]);
  ASSERT_EQ(2, arr2[1]);
  ASSERT_EQ(0u, arr1.size());
  ASSERT_EQ(nullptr, arr1.data());
}

UTEST(array, growth_on_push) {
  test_array<int> arr;
  for (int i = 0; i < 100; ++i) {
    arr.push_back(i);
  }
  ASSERT_EQ(100u, arr.size());
  for (int i = 0; i < 100; ++i) {
    ASSERT_EQ(i, arr[(size_t)i]);
  }
}

// Helper struct for value_or_error tests.
struct test_value {
  int x;
  int y;
  test_value(int x_, int y_) : x(x_), y(y_) {}
  test_value(test_value&& other) : x(other.x), y(other.y) {
    other.x = 0;
    other.y = 0;
  }
  test_value& operator=(test_value&& other) {
    x = other.x;
    y = other.y;
    other.x = 0;
    other.y = 0;
    return *this;
  }
};

UTEST(value_or_error, construct_with_value) {
  ngfi::value_or_ngferr<int> result{42};
  ASSERT_FALSE(result.has_error());
  ASSERT_EQ(NGF_ERROR_OK, result.error());
  ASSERT_EQ(42, result.value());
}

UTEST(value_or_error, construct_with_error) {
  ngfi::value_or_ngferr<int> result{NGF_ERROR_OUT_OF_MEM};
  ASSERT_TRUE(result.has_error());
  ASSERT_EQ(NGF_ERROR_OUT_OF_MEM, result.error());
}

UTEST(value_or_error, construct_with_struct_value) {
  ngfi::value_or_ngferr<test_value> result{test_value{10, 20}};
  ASSERT_FALSE(result.has_error());
  ASSERT_EQ(10, result.value().x);
  ASSERT_EQ(20, result.value().y);
}

UTEST(value_or_error, modify_value) {
  ngfi::value_or_ngferr<int> result{100};
  result.value() = 200;
  ASSERT_EQ(200, result.value());
}

UTEST(value_or_error, move_construction_with_value) {
  ngfi::value_or_ngferr<test_value> result1{test_value{5, 10}};
  ngfi::value_or_ngferr<test_value> result2{ngfi::move(result1)};

  ASSERT_FALSE(result2.has_error());
  ASSERT_EQ(5, result2.value().x);
  ASSERT_EQ(10, result2.value().y);
  // After move, result1 should have error (missing_value_error).
  ASSERT_TRUE(result1.has_error());
}

UTEST(value_or_error, move_construction_with_error) {
  ngfi::value_or_ngferr<int> result1{NGF_ERROR_OBJECT_CREATION_FAILED};
  ngfi::value_or_ngferr<int> result2{ngfi::move(result1)};

  ASSERT_TRUE(result2.has_error());
  ASSERT_EQ(NGF_ERROR_OBJECT_CREATION_FAILED, result2.error());
}

UTEST(value_or_error, move_assignment_value_to_value) {
  ngfi::value_or_ngferr<test_value> result1{test_value{1, 2}};
  ngfi::value_or_ngferr<test_value> result2{test_value{3, 4}};

  result2 = ngfi::move(result1);

  ASSERT_FALSE(result2.has_error());
  ASSERT_EQ(1, result2.value().x);
  ASSERT_EQ(2, result2.value().y);
  ASSERT_TRUE(result1.has_error());
}

UTEST(value_or_error, move_assignment_error_to_value) {
  ngfi::value_or_ngferr<int> result1{NGF_ERROR_OUT_OF_MEM};
  ngfi::value_or_ngferr<int> result2{42};

  result2 = ngfi::move(result1);

  ASSERT_TRUE(result2.has_error());
  ASSERT_EQ(NGF_ERROR_OUT_OF_MEM, result2.error());
}

UTEST(value_or_error, move_assignment_value_to_error) {
  ngfi::value_or_ngferr<int> result1{42};
  ngfi::value_or_ngferr<int> result2{NGF_ERROR_OUT_OF_MEM};

  result2 = ngfi::move(result1);

  ASSERT_FALSE(result2.has_error());
  ASSERT_EQ(42, result2.value());
}

UTEST(value_or_error, const_value_access) {
  const ngfi::value_or_ngferr<int> result{99};
  ASSERT_EQ(99, result.value());
}

// Helper struct for unique_ptr tests.
struct tracked_object {
  static int instance_count;
  int value;

  tracked_object(int v = 0) : value(v) { ++instance_count; }
  ~tracked_object() { --instance_count; }
};

int tracked_object::instance_count = 0;

UTEST(unique_ptr, default_construction) {
  ngfi::unique_ptr<int> ptr;
  ASSERT_FALSE(ptr);
  ASSERT_EQ(nullptr, ptr.get());
}

UTEST(unique_ptr, construct_from_pointer) {
  tracked_object::instance_count = 0;
  {
    auto* raw = NGFI_ALLOC(tracked_object);
    ngfi::unique_ptr<tracked_object> ptr{raw};
    ASSERT_TRUE(ptr);
    ASSERT_EQ(raw, ptr.get());
    ASSERT_EQ(1, tracked_object::instance_count);
  }
  ASSERT_EQ(0, tracked_object::instance_count);
}

UTEST(unique_ptr, make) {
  tracked_object::instance_count = 0;
  {
    auto ptr = ngfi::unique_ptr<tracked_object>::make(42);
    ASSERT_TRUE(ptr);
    ASSERT_EQ(42, ptr->value);
    ASSERT_EQ(1, tracked_object::instance_count);
  }
  ASSERT_EQ(0, tracked_object::instance_count);
}

UTEST(unique_ptr, arrow_operator) {
  auto ptr = ngfi::unique_ptr<tracked_object>::make(100);
  ASSERT_EQ(100, ptr->value);
  ptr->value = 200;
  ASSERT_EQ(200, ptr->value);
}

UTEST(unique_ptr, release) {
  tracked_object::instance_count = 0;
  tracked_object* raw = nullptr;
  {
    auto ptr = ngfi::unique_ptr<tracked_object>::make(5);
    raw = ptr.release();
    ASSERT_FALSE(ptr);
    ASSERT_EQ(nullptr, ptr.get());
    ASSERT_EQ(1, tracked_object::instance_count);
  }
  // Object should still exist after unique_ptr destruction.
  ASSERT_EQ(1, tracked_object::instance_count);
  ASSERT_EQ(5, raw->value);
  NGFI_FREE(raw);
  ASSERT_EQ(0, tracked_object::instance_count);
}

UTEST(unique_ptr, move_construction) {
  tracked_object::instance_count = 0;
  {
    auto ptr1 = ngfi::unique_ptr<tracked_object>::make(10);
    auto* raw = ptr1.get();

    ngfi::unique_ptr<tracked_object> ptr2{ngfi::move(ptr1)};

    ASSERT_FALSE(ptr1);
    ASSERT_TRUE(ptr2);
    ASSERT_EQ(raw, ptr2.get());
    ASSERT_EQ(10, ptr2->value);
    ASSERT_EQ(1, tracked_object::instance_count);
  }
  ASSERT_EQ(0, tracked_object::instance_count);
}

UTEST(unique_ptr, move_assignment) {
  tracked_object::instance_count = 0;
  {
    auto ptr1 = ngfi::unique_ptr<tracked_object>::make(1);
    auto ptr2 = ngfi::unique_ptr<tracked_object>::make(2);
    ASSERT_EQ(2, tracked_object::instance_count);

    auto* raw1 = ptr1.get();
    ptr2 = ngfi::move(ptr1);

    ASSERT_FALSE(ptr1);
    ASSERT_TRUE(ptr2);
    ASSERT_EQ(raw1, ptr2.get());
    ASSERT_EQ(1, ptr2->value);
    // Old ptr2 object should be destroyed.
    ASSERT_EQ(1, tracked_object::instance_count);
  }
  ASSERT_EQ(0, tracked_object::instance_count);
}

UTEST(unique_ptr, move_assignment_to_empty) {
  tracked_object::instance_count = 0;
  {
    auto ptr1 = ngfi::unique_ptr<tracked_object>::make(7);
    ngfi::unique_ptr<tracked_object> ptr2;

    ptr2 = ngfi::move(ptr1);

    ASSERT_FALSE(ptr1);
    ASSERT_TRUE(ptr2);
    ASSERT_EQ(7, ptr2->value);
    ASSERT_EQ(1, tracked_object::instance_count);
  }
  ASSERT_EQ(0, tracked_object::instance_count);
}

UTEST(unique_ptr, const_get) {
  auto ptr = ngfi::unique_ptr<tracked_object>::make(50);
  const auto& const_ptr = ptr;
  ASSERT_EQ(ptr.get(), const_ptr.get());
  ASSERT_EQ(50, const_ptr.get()->value);
}

UTEST(unique_ptr, bool_conversion) {
  ngfi::unique_ptr<int> empty;
  auto filled = ngfi::unique_ptr<int>::make();

  ASSERT_FALSE(empty);
  ASSERT_TRUE(filled);

  if (empty) {
    ASSERT_TRUE(false);  // Should not reach here.
  }
  if (filled) {
    ASSERT_TRUE(true);  // Should reach here.
  } else {
    ASSERT_TRUE(false);  // Should not reach here.
  }
}

UTEST(hashtable, default_construction) {
  ngfi::hashtable<int> ht;
  ASSERT_EQ(0u, ht.size());
  ASSERT_EQ(0u, ht.capacity());
  ASSERT_TRUE(ht.empty());
}

UTEST(hashtable, construction_with_capacity) {
  ngfi::hashtable<int> ht(200);
  ASSERT_EQ(0u, ht.size());
  ASSERT_TRUE(ht.empty());
  // Capacity is only allocated on first insert.
}

UTEST(hashtable, insert_and_get) {
  ngfi::hashtable<int> ht;
  int* val = ht.insert(42, 100);
  ASSERT_NE(nullptr, val);
  ASSERT_EQ(100, *val);
  ASSERT_EQ(1u, ht.size());
  ASSERT_FALSE(ht.empty());

  int* retrieved = ht.get(42);
  ASSERT_NE(nullptr, retrieved);
  ASSERT_EQ(100, *retrieved);
}

UTEST(hashtable, get_nonexistent) {
  ngfi::hashtable<int> ht;
  ht.insert(1, 10);
  int* val = ht.get(999);
  ASSERT_EQ(nullptr, val);
}

UTEST(hashtable, get_empty_table) {
  ngfi::hashtable<int> ht;
  int* val = ht.get(42);
  ASSERT_EQ(nullptr, val);
}

UTEST(hashtable, insert_update_existing) {
  ngfi::hashtable<int> ht;
  ht.insert(5, 50);
  ASSERT_EQ(1u, ht.size());

  int* val = ht.insert(5, 500);
  ASSERT_NE(nullptr, val);
  ASSERT_EQ(500, *val);
  ASSERT_EQ(1u, ht.size());  // Size should not increase.
}

UTEST(hashtable, multiple_inserts) {
  ngfi::hashtable<int> ht;
  for (uint64_t i = 0; i < 50; ++i) {
    ht.insert(i, static_cast<int>(i * 10));
  }
  ASSERT_EQ(50u, ht.size());

  for (uint64_t i = 0; i < 50; ++i) {
    int* val = ht.get(i);
    ASSERT_NE(nullptr, val);
    ASSERT_EQ(static_cast<int>(i * 10), *val);
  }
}

UTEST(hashtable, get_or_insert_new) {
  ngfi::hashtable<int> ht;
  bool is_new = false;
  int* val = ht.get_or_insert(10, 100, is_new);
  ASSERT_NE(nullptr, val);
  ASSERT_EQ(100, *val);
  ASSERT_TRUE(is_new);
  ASSERT_EQ(1u, ht.size());
}

UTEST(hashtable, get_or_insert_existing) {
  ngfi::hashtable<int> ht;
  ht.insert(10, 100);

  bool is_new = true;
  int* val = ht.get_or_insert(10, 999, is_new);
  ASSERT_NE(nullptr, val);
  ASSERT_EQ(100, *val);  // Should return existing value, not default.
  ASSERT_FALSE(is_new);
  ASSERT_EQ(1u, ht.size());
}

UTEST(hashtable, clear) {
  ngfi::hashtable<int> ht;
  ht.insert(1, 10);
  ht.insert(2, 20);
  ht.insert(3, 30);
  ASSERT_EQ(3u, ht.size());

  ht.clear();
  ASSERT_EQ(0u, ht.size());
  ASSERT_TRUE(ht.empty());
  ASSERT_GT(ht.capacity(), 0u);  // Capacity should remain.

  // Verify entries are gone.
  ASSERT_EQ(nullptr, ht.get(1));
  ASSERT_EQ(nullptr, ht.get(2));
  ASSERT_EQ(nullptr, ht.get(3));
}

UTEST(hashtable, prehashed_operations) {
  ngfi::hashtable<int> ht;
  auto kh = ngfi::hashtable<int>::compute_hash(42);

  int* val = ht.insert_prehashed(kh, 100);
  ASSERT_NE(nullptr, val);
  ASSERT_EQ(100, *val);

  int* retrieved = ht.get_prehashed(kh);
  ASSERT_NE(nullptr, retrieved);
  ASSERT_EQ(100, *retrieved);
}

UTEST(hashtable, move_construction) {
  ngfi::hashtable<int> ht1;
  ht1.insert(1, 10);
  ht1.insert(2, 20);

  ngfi::hashtable<int> ht2(ngfi::move(ht1));

  ASSERT_EQ(0u, ht1.size());
  ASSERT_EQ(2u, ht2.size());
  ASSERT_EQ(10, *ht2.get(1));
  ASSERT_EQ(20, *ht2.get(2));
}

UTEST(hashtable, move_assignment) {
  ngfi::hashtable<int> ht1;
  ht1.insert(1, 10);
  ht1.insert(2, 20);

  ngfi::hashtable<int> ht2;
  ht2.insert(100, 1000);

  ht2 = ngfi::move(ht1);

  ASSERT_EQ(0u, ht1.size());
  ASSERT_EQ(2u, ht2.size());
  ASSERT_EQ(10, *ht2.get(1));
  ASSERT_EQ(20, *ht2.get(2));
  ASSERT_EQ(nullptr, ht2.get(100));  // Old entry should be gone.
}

UTEST(hashtable, iteration) {
  ngfi::hashtable<int> ht;
  ht.insert(1, 10);
  ht.insert(2, 20);
  ht.insert(3, 30);

  int sum_keys = 0;
  int sum_values = 0;
  int count = 0;
  for (auto it = ht.begin(); it != ht.end(); ++it) {
    sum_keys += static_cast<int>(it->key);
    sum_values += it->value;
    ++count;
  }

  ASSERT_EQ(3, count);
  ASSERT_EQ(6, sum_keys);    // 1 + 2 + 3
  ASSERT_EQ(60, sum_values); // 10 + 20 + 30
}

UTEST(hashtable, iteration_empty) {
  ngfi::hashtable<int> ht;
  int count = 0;
  for (auto it = ht.begin(); it != ht.end(); ++it) {
    ++count;
  }
  ASSERT_EQ(0, count);
}

UTEST(hashtable, rehash_on_load) {
  ngfi::hashtable<int> ht(10);  // Small initial capacity.
  size_t initial_cap = 0;

  for (uint64_t i = 0; i < 100; ++i) {
    ht.insert(i, static_cast<int>(i));
    if (i == 0) {
      initial_cap = ht.capacity();
    }
  }

  ASSERT_EQ(100u, ht.size());
  ASSERT_GT(ht.capacity(), initial_cap);  // Should have grown.

  // Verify all entries are still accessible.
  for (uint64_t i = 0; i < 100; ++i) {
    int* val = ht.get(i);
    ASSERT_NE(nullptr, val);
    ASSERT_EQ(static_cast<int>(i), *val);
  }
}

UTEST(hashtable, const_get) {
  ngfi::hashtable<int> ht;
  ht.insert(42, 100);

  const auto& const_ht = ht;
  const int* val = const_ht.get(42);
  ASSERT_NE(nullptr, val);
  ASSERT_EQ(100, *val);
}

UTEST(hashtable, struct_value) {
  struct point {
    int x;
    int y;
  };

  ngfi::hashtable<point> ht;
  ht.insert(1, point{10, 20});
  ht.insert(2, point{30, 40});

  point* p1 = ht.get(1);
  ASSERT_NE(nullptr, p1);
  ASSERT_EQ(10, p1->x);
  ASSERT_EQ(20, p1->y);

  point* p2 = ht.get(2);
  ASSERT_NE(nullptr, p2);
  ASSERT_EQ(30, p2->x);
  ASSERT_EQ(40, p2->y);
}

// Mock command buffer for testing state transitions.
struct mock_cmd_buffer {
  ngfi::cmd_buffer_state state;
  bool                   renderpass_active;
  bool                   compute_pass_active;
  bool                   xfer_pass_active;

  void reset() {
    state              = ngfi::CMD_BUFFER_STATE_NEW;
    renderpass_active  = false;
    compute_pass_active = false;
    xfer_pass_active   = false;
  }
};

UTEST(cmdbuf_state, new_to_ready) {
  mock_cmd_buffer buf;
  buf.reset();

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_READY);
  ASSERT_TRUE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_READY, buf.state);
}

UTEST(cmdbuf_state, ready_to_recording) {
  mock_cmd_buffer buf;
  buf.reset();
  buf.state = ngfi::CMD_BUFFER_STATE_READY;

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_RECORDING);
  ASSERT_TRUE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_RECORDING, buf.state);
}

UTEST(cmdbuf_state, recording_to_ready_to_submit) {
  mock_cmd_buffer buf;
  buf.reset();
  buf.state = ngfi::CMD_BUFFER_STATE_RECORDING;

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_READY_TO_SUBMIT);
  ASSERT_TRUE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_READY_TO_SUBMIT, buf.state);
}

UTEST(cmdbuf_state, recording_to_ready_to_submit_fails_with_active_renderpass) {
  mock_cmd_buffer buf;
  buf.reset();
  buf.state             = ngfi::CMD_BUFFER_STATE_RECORDING;
  buf.renderpass_active = true;

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_READY_TO_SUBMIT);
  ASSERT_FALSE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_RECORDING, buf.state);  // State unchanged.
}

UTEST(cmdbuf_state, recording_to_ready_to_submit_fails_with_active_compute_pass) {
  mock_cmd_buffer buf;
  buf.reset();
  buf.state              = ngfi::CMD_BUFFER_STATE_RECORDING;
  buf.compute_pass_active = true;

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_READY_TO_SUBMIT);
  ASSERT_FALSE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_RECORDING, buf.state);
}

UTEST(cmdbuf_state, recording_to_ready_to_submit_fails_with_active_xfer_pass) {
  mock_cmd_buffer buf;
  buf.reset();
  buf.state            = ngfi::CMD_BUFFER_STATE_RECORDING;
  buf.xfer_pass_active = true;

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_READY_TO_SUBMIT);
  ASSERT_FALSE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_RECORDING, buf.state);
}

UTEST(cmdbuf_state, ready_to_submit_to_pending) {
  mock_cmd_buffer buf;
  buf.reset();
  buf.state = ngfi::CMD_BUFFER_STATE_READY_TO_SUBMIT;

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_PENDING);
  ASSERT_TRUE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_PENDING, buf.state);
}

UTEST(cmdbuf_state, ready_to_pending) {
  mock_cmd_buffer buf;
  buf.reset();
  buf.state = ngfi::CMD_BUFFER_STATE_READY;

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_PENDING);
  ASSERT_TRUE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_PENDING, buf.state);
}

UTEST(cmdbuf_state, pending_to_submitted) {
  mock_cmd_buffer buf;
  buf.reset();
  buf.state = ngfi::CMD_BUFFER_STATE_PENDING;

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_SUBMITTED);
  ASSERT_TRUE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_SUBMITTED, buf.state);
}

UTEST(cmdbuf_state, submitted_to_ready) {
  mock_cmd_buffer buf;
  buf.reset();
  buf.state = ngfi::CMD_BUFFER_STATE_SUBMITTED;

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_READY);
  ASSERT_TRUE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_READY, buf.state);
}

UTEST(cmdbuf_state, ready_to_ready) {
  mock_cmd_buffer buf;
  buf.reset();
  buf.state = ngfi::CMD_BUFFER_STATE_READY;

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_READY);
  ASSERT_TRUE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_READY, buf.state);
}

UTEST(cmdbuf_state, ready_to_submit_to_recording) {
  mock_cmd_buffer buf;
  buf.reset();
  buf.state = ngfi::CMD_BUFFER_STATE_READY_TO_SUBMIT;

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_RECORDING);
  ASSERT_TRUE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_RECORDING, buf.state);
}

UTEST(cmdbuf_state, cannot_transition_to_new) {
  mock_cmd_buffer buf;
  buf.reset();
  buf.state = ngfi::CMD_BUFFER_STATE_READY;

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_NEW);
  ASSERT_FALSE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_READY, buf.state);  // State unchanged.
}

UTEST(cmdbuf_state, new_to_recording_fails) {
  mock_cmd_buffer buf;
  buf.reset();

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_RECORDING);
  ASSERT_FALSE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_NEW, buf.state);
}

UTEST(cmdbuf_state, new_to_pending_fails) {
  mock_cmd_buffer buf;
  buf.reset();

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_PENDING);
  ASSERT_FALSE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_NEW, buf.state);
}

UTEST(cmdbuf_state, recording_to_pending_fails) {
  mock_cmd_buffer buf;
  buf.reset();
  buf.state = ngfi::CMD_BUFFER_STATE_RECORDING;

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_PENDING);
  ASSERT_FALSE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_RECORDING, buf.state);
}

UTEST(cmdbuf_state, ready_to_submitted_fails) {
  mock_cmd_buffer buf;
  buf.reset();
  buf.state = ngfi::CMD_BUFFER_STATE_READY;

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_SUBMITTED);
  ASSERT_FALSE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_READY, buf.state);
}

UTEST(cmdbuf_state, pending_to_ready_fails) {
  mock_cmd_buffer buf;
  buf.reset();
  buf.state = ngfi::CMD_BUFFER_STATE_PENDING;

  bool result = ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_READY);
  ASSERT_FALSE(result);
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_PENDING, buf.state);
}

UTEST(cmdbuf_state, full_lifecycle) {
  mock_cmd_buffer buf;
  buf.reset();

  // NEW -> READY
  ASSERT_TRUE(ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_READY));
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_READY, buf.state);

  // READY -> RECORDING
  ASSERT_TRUE(ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_RECORDING));
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_RECORDING, buf.state);

  // RECORDING -> READY_TO_SUBMIT
  ASSERT_TRUE(ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_READY_TO_SUBMIT));
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_READY_TO_SUBMIT, buf.state);

  // READY_TO_SUBMIT -> PENDING
  ASSERT_TRUE(ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_PENDING));
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_PENDING, buf.state);

  // PENDING -> SUBMITTED
  ASSERT_TRUE(ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_SUBMITTED));
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_SUBMITTED, buf.state);

  // SUBMITTED -> READY (reuse)
  ASSERT_TRUE(ngfi::transition_cmd_buf(&buf, ngfi::CMD_BUFFER_STATE_READY));
  ASSERT_EQ(ngfi::CMD_BUFFER_STATE_READY, buf.state);
}

UTEST(chunked_list, append_single_element) {
  ngfi::arena a(1024);
  ngfi::chunked_list<int> list;

  int* ptr = list.append(42, a);
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(42, *ptr);
}

UTEST(chunked_list, append_multiple_elements) {
  ngfi::arena a(1024);
  ngfi::chunked_list<int> list;

  for (int i = 0; i < 5; ++i) {
    int* ptr = list.append(i * 10, a);
    ASSERT_NE(nullptr, ptr);
    ASSERT_EQ(i * 10, *ptr);
  }
}

UTEST(chunked_list, iterate_elements) {
  ngfi::arena a(1024);
  ngfi::chunked_list<int> list;

  list.append(1, a);
  list.append(2, a);
  list.append(3, a);

  int sum = 0;
  int count = 0;
  for (auto it = list.begin(); !(it == list.end()); ++it) {
    sum += *it;
    ++count;
  }

  ASSERT_EQ(3, count);
  ASSERT_EQ(6, sum);
}

UTEST(chunked_list, iterate_empty) {
  ngfi::chunked_list<int> list;

  int count = 0;
  for (auto it = list.begin(); !(it == list.end()); ++it) {
    ++count;
  }

  ASSERT_EQ(0, count);
}

UTEST(chunked_list, clear) {
  ngfi::arena a(1024);
  ngfi::chunked_list<int> list;

  list.append(1, a);
  list.append(2, a);
  list.append(3, a);

  list.clear();

  int count = 0;
  for (auto it = list.begin(); !(it == list.end()); ++it) {
    ++count;
  }

  ASSERT_EQ(0, count);
}

UTEST(chunked_list, append_after_clear) {
  ngfi::arena a(1024);
  ngfi::chunked_list<int> list;

  list.append(1, a);
  list.append(2, a);
  list.clear();

  int* ptr = list.append(100, a);
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(100, *ptr);

  int count = 0;
  int value = 0;
  for (auto it = list.begin(); !(it == list.end()); ++it) {
    value = *it;
    ++count;
  }

  ASSERT_EQ(1, count);
  ASSERT_EQ(100, value);
}

UTEST(chunked_list, spans_multiple_chunks) {
  ngfi::arena a(4096);
  // Use small chunk capacity to force multiple chunks.
  ngfi::chunked_list<int, 3> list;

  // Insert more elements than one chunk can hold.
  for (int i = 0; i < 10; ++i) {
    int* ptr = list.append(i, a);
    ASSERT_NE(nullptr, ptr);
    ASSERT_EQ(i, *ptr);
  }

  // Verify all elements are accessible via iteration.
  int count = 0;
  int sum = 0;
  for (auto it = list.begin(); !(it == list.end()); ++it) {
    sum += *it;
    ++count;
  }

  ASSERT_EQ(10, count);
  ASSERT_EQ(45, sum);  // 0+1+2+...+9 = 45
}

UTEST(chunked_list, iteration_order_preserved) {
  ngfi::arena a(4096);
  ngfi::chunked_list<int, 2> list;  // Very small chunks.

  int expected[] = {10, 20, 30, 40, 50, 60, 70};
  for (int val : expected) {
    list.append(val, a);
  }

  int idx = 0;
  for (auto it = list.begin(); !(it == list.end()); ++it) {
    ASSERT_EQ(expected[idx], *it);
    ++idx;
  }
  ASSERT_EQ(7, idx);
}

UTEST(chunked_list, struct_elements) {
  struct point {
    int x;
    int y;
  };

  ngfi::arena a(1024);
  ngfi::chunked_list<point> list;

  point* p1 = list.append(point{1, 2}, a);
  point* p2 = list.append(point{3, 4}, a);
  point* p3 = list.append(point{5, 6}, a);

  ASSERT_NE(nullptr, p1);
  ASSERT_NE(nullptr, p2);
  ASSERT_NE(nullptr, p3);

  ASSERT_EQ(1, p1->x);
  ASSERT_EQ(2, p1->y);
  ASSERT_EQ(3, p2->x);
  ASSERT_EQ(4, p2->y);
  ASSERT_EQ(5, p3->x);
  ASSERT_EQ(6, p3->y);
}

UTEST(chunked_list, const_iteration) {
  ngfi::arena a(1024);
  ngfi::chunked_list<int> list;

  list.append(10, a);
  list.append(20, a);
  list.append(30, a);

  const auto& const_list = list;

  int sum = 0;
  for (auto it = const_list.begin(); !(it == const_list.end()); ++it) {
    sum += *it;
  }

  ASSERT_EQ(60, sum);
}

UTEST(chunked_list, exact_chunk_boundary) {
  ngfi::arena a(4096);
  ngfi::chunked_list<int, 5> list;  // Chunk capacity of 5.

  // Insert exactly 5 elements (fills one chunk exactly).
  for (int i = 0; i < 5; ++i) {
    list.append(i, a);
  }

  int count = 0;
  for (auto it = list.begin(); !(it == list.end()); ++it) {
    ++count;
  }
  ASSERT_EQ(5, count);

  // Insert one more to trigger new chunk.
  list.append(5, a);

  count = 0;
  for (auto it = list.begin(); !(it == list.end()); ++it) {
    ++count;
  }
  ASSERT_EQ(6, count);
}

UTEST(arena, default_construction) {
  ngfi::arena a;
  ASSERT_EQ(0u, a.total_allocated());
  ASSERT_EQ(0u, a.total_used());
}

UTEST(arena, construction_with_capacity) {
  ngfi::arena a(1024);
  // No allocation until first alloc call.
  ASSERT_EQ(0u, a.total_allocated());
  ASSERT_EQ(0u, a.total_used());
}

UTEST(arena, alloc_basic) {
  ngfi::arena a(1024);
  void* ptr = a.alloc(64);
  ASSERT_NE(nullptr, ptr);
  ASSERT_GT(a.total_allocated(), 0u);
  ASSERT_GT(a.total_used(), 0u);
}

UTEST(arena, alloc_typed_single) {
  ngfi::arena a(1024);
  int* ptr = a.alloc<int>();
  ASSERT_NE(nullptr, ptr);
  *ptr = 42;
  ASSERT_EQ(42, *ptr);
}

UTEST(arena, alloc_typed_array) {
  ngfi::arena a(1024);
  int* arr = a.alloc<int>(10);
  ASSERT_NE(nullptr, arr);

  for (int i = 0; i < 10; ++i) {
    arr[i] = i * 10;
  }

  for (int i = 0; i < 10; ++i) {
    ASSERT_EQ(i * 10, arr[i]);
  }
}

UTEST(arena, alloc_struct) {
  struct test_struct {
    int x;
    float y;
    char z;
  };

  ngfi::arena a(1024);
  test_struct* ptr = a.alloc<test_struct>();
  ASSERT_NE(nullptr, ptr);

  ptr->x = 100;
  ptr->y = 3.14f;
  ptr->z = 'A';

  ASSERT_EQ(100, ptr->x);
  ASSERT_EQ(3.14f, ptr->y);
  ASSERT_EQ('A', ptr->z);
}

UTEST(arena, multiple_allocations) {
  ngfi::arena a(1024);

  int* i1 = a.alloc<int>();
  int* i2 = a.alloc<int>();
  int* i3 = a.alloc<int>();

  ASSERT_NE(nullptr, i1);
  ASSERT_NE(nullptr, i2);
  ASSERT_NE(nullptr, i3);

  // Pointers should be different.
  ASSERT_NE(i1, i2);
  ASSERT_NE(i2, i3);
  ASSERT_NE(i1, i3);

  *i1 = 1;
  *i2 = 2;
  *i3 = 3;

  ASSERT_EQ(1, *i1);
  ASSERT_EQ(2, *i2);
  ASSERT_EQ(3, *i3);
}

UTEST(arena, reset) {
  ngfi::arena a(1024);

  a.alloc<int>();
  a.alloc<int>();
  a.alloc<int>();

  size_t used_before = a.total_used();
  ASSERT_GT(used_before, 0u);

  a.reset();

  ASSERT_EQ(0u, a.total_used());
  // Can allocate again after reset.
  int* ptr = a.alloc<int>();
  ASSERT_NE(nullptr, ptr);
}

UTEST(arena, reset_reuses_memory) {
  ngfi::arena a(1024);

  int* ptr1 = a.alloc<int>();
  ASSERT_NE(nullptr, ptr1);
  size_t allocated_after_first = a.total_allocated();

  a.reset();

  int* ptr2 = a.alloc<int>();
  ASSERT_NE(nullptr, ptr2);

  // Should reuse the same block, so total_allocated stays the same.
  ASSERT_EQ(allocated_after_first, a.total_allocated());
}

UTEST(arena, grows_when_needed) {
  ngfi::arena a(64);  // Small block size.

  // Allocate more than one block can hold.
  void* ptrs[20];
  for (int i = 0; i < 20; ++i) {
    ptrs[i] = a.alloc(32);
    ASSERT_NE(nullptr, ptrs[i]);
  }

  // Should have grown.
  ASSERT_GT(a.total_allocated(), 64u);
}

UTEST(arena, alignment_basic) {
  ngfi::arena a(1024);

  // Allocate with 16-byte alignment.
  void* ptr = a.alloc_aligned(32, 16);
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(0u, reinterpret_cast<uintptr_t>(ptr) % 16);
}

UTEST(arena, alignment_various) {
  ngfi::arena a(4096);

  for (size_t align = 1; align <= 128; align *= 2) {
    void* ptr = a.alloc_aligned(16, align);
    ASSERT_NE(nullptr, ptr);
    ASSERT_EQ(0u, reinterpret_cast<uintptr_t>(ptr) % align);
  }
}

UTEST(arena, typed_alloc_alignment) {
  struct alignas(32) aligned_struct {
    char data[32];
  };

  ngfi::arena a(1024);
  aligned_struct* ptr = a.alloc<aligned_struct>();
  ASSERT_NE(nullptr, ptr);
  ASSERT_EQ(0u, reinterpret_cast<uintptr_t>(ptr) % 32);
}

UTEST(arena, alloc_zero_size_returns_null) {
  ngfi::arena a(1024);
  void* ptr = a.alloc(0);
  ASSERT_EQ(nullptr, ptr);
}

UTEST(arena, alloc_without_capacity_returns_null) {
  ngfi::arena a;  // No capacity set.
  void* ptr = a.alloc(64);
  ASSERT_EQ(nullptr, ptr);
}

UTEST(arena, set_block_size) {
  ngfi::arena a;
  a.set_block_size(512);

  void* ptr = a.alloc(64);
  ASSERT_NE(nullptr, ptr);
  ASSERT_GT(a.total_allocated(), 0u);
}

UTEST(arena, move_construction) {
  ngfi::arena a1(1024);
  int* ptr = a1.alloc<int>();
  *ptr = 42;

  size_t allocated = a1.total_allocated();
  size_t used = a1.total_used();

  ngfi::arena a2(ngfi::move(a1));

  ASSERT_EQ(allocated, a2.total_allocated());
  ASSERT_EQ(used, a2.total_used());
  ASSERT_EQ(0u, a1.total_allocated());
  ASSERT_EQ(0u, a1.total_used());

  // Original pointer should still be valid.
  ASSERT_EQ(42, *ptr);
}

UTEST(arena, total_used_tracks_allocations) {
  ngfi::arena a(1024);

  size_t used0 = a.total_used();
  a.alloc<int>();
  size_t used1 = a.total_used();
  a.alloc<int>();
  size_t used2 = a.total_used();

  ASSERT_EQ(0u, used0);
  ASSERT_GT(used1, used0);
  ASSERT_GT(used2, used1);
}

UTEST(arena, large_allocation) {
  ngfi::arena a(64);  // Small default block size.

  // Request larger than default block size.
  void* ptr = a.alloc(256);
  ASSERT_NE(nullptr, ptr);
  ASSERT_GE(a.total_allocated(), 256u);
}

UTEST(arena, many_small_allocations) {
  ngfi::arena a(1024);

  for (int i = 0; i < 100; ++i) {
    char* ptr = a.alloc<char>();
    ASSERT_NE(nullptr, ptr);
    *ptr = static_cast<char>(i);
  }

  ASSERT_GE(a.total_used(), 100u);
}

UTEST (frame_token, encode_decode) {
  const uint16_t  test_ctx_id              = 65534u;
  const uint8_t   test_max_inflight_frames = 3u, test_frame_id = 255u;
  const uintptr_t test_token =
  ngfi_encode_frame_token(test_ctx_id, test_max_inflight_frames, test_frame_id);
  ASSERT_EQ(test_ctx_id, ngfi_frame_ctx_id(test_token));
  ASSERT_EQ(test_max_inflight_frames, ngfi_frame_max_inflight_frames(test_token));
  ASSERT_EQ(test_frame_id, ngfi_frame_id(test_token));
}
