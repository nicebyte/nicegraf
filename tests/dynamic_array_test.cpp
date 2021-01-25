#include "catch.hpp"
#include "dynamic_array.h"
#include <random>
#include <vector>

struct point { float x, y; };

TEST_CASE("Creating a dynamic array") {
  NGFI_DARRAY_OF(point) pt_array;
  NGFI_DARRAY_RESET(pt_array, 100u);
  REQUIRE(pt_array.data != NULL);
  REQUIRE(pt_array.data == pt_array.endptr);
  REQUIRE(pt_array.capacity == 100u);
  NGFI_DARRAY_DESTROY(pt_array);
}

TEST_CASE("Populate a dynamic array") {
  NGFI_DARRAY_OF(point) pt_array;
  std::vector<point> check_array;
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<float> d(0.0, 1.0);
  NGFI_DARRAY_RESET(pt_array, 100u);
  uint32_t ntests = pt_array.capacity + 1000u;
  for (uint32_t i = 0u; i < ntests; ++i) {
    point p {d(gen), d(gen)};
    NGFI_DARRAY_APPEND(pt_array, p);
    check_array.push_back(NGFI_DARRAY_AT(pt_array, i));
  }
  REQUIRE(NGFI_DARRAY_SIZE(pt_array) == check_array.size());
  for (uint32_t i = 0; i < check_array.size(); ++i) {
    REQUIRE(check_array[i].x == NGFI_DARRAY_AT(pt_array, i).x);
    REQUIRE(check_array[i].y == NGFI_DARRAY_AT(pt_array, i).y);
  }
  NGFI_DARRAY_CLEAR(pt_array);
  check_array.clear();
  REQUIRE(NGFI_DARRAY_SIZE(pt_array) == 0u);
  for (uint32_t i = 0u; i < ntests; ++i) {
    point p {d(gen), d(gen)};
    NGFI_DARRAY_APPEND(pt_array, p);
    check_array.push_back(NGFI_DARRAY_AT(pt_array, i));
  }
  REQUIRE(NGFI_DARRAY_SIZE(pt_array) == check_array.size());
  for (uint32_t i = 0; i < check_array.size(); ++i) {
    REQUIRE(check_array[i].x == NGFI_DARRAY_AT(pt_array, i).x);
    REQUIRE(check_array[i].y == NGFI_DARRAY_AT(pt_array, i).y);
  }
  NGFI_DARRAY_DESTROY(pt_array);
}
