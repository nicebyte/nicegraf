/**
 * Copyright (c) 2021 nicegraf contributors
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

#include "catch.hpp"
#include "ngf-common/dynamic_array.h"
#include <random>
#include <vector>
#include <queue>

struct point { float x, y; };

TEST_CASE("Creating a dynamic array") {
  NGFI_DARRAY_OF(point) pt_array;
  NGFI_DARRAY_RESET(pt_array, 100u);
  REQUIRE(pt_array.data != NULL);
  REQUIRE(pt_array.data == pt_array.endptr);
  REQUIRE(pt_array.capacity == 100u);
  NGFI_DARRAY_DESTROY(pt_array);
}

static point random_point() {
  static std::mt19937 gen(std::random_device{}());
  static std::uniform_real_distribution<float> d { 0.0f, 1.0f };
  return point { d(gen), d(gen) };
}

TEST_CASE("Populate a dynamic array") {
  NGFI_DARRAY_OF(point) pt_array;
  std::vector<point> check_array;
  NGFI_DARRAY_RESET(pt_array, 100u);
  uint32_t ntests = pt_array.capacity + 1000u;
  for (uint32_t i = 0u; i < ntests; ++i) {
    NGFI_DARRAY_APPEND(pt_array, random_point());
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
    NGFI_DARRAY_APPEND(pt_array, random_point());
    check_array.push_back(NGFI_DARRAY_AT(pt_array, i));
  }
  REQUIRE(NGFI_DARRAY_SIZE(pt_array) == check_array.size());
  for (uint32_t i = 0; i < check_array.size(); ++i) {
    REQUIRE(check_array[i].x == NGFI_DARRAY_AT(pt_array, i).x);
    REQUIRE(check_array[i].y == NGFI_DARRAY_AT(pt_array, i).y);
  }
  NGFI_DARRAY_DESTROY(pt_array);
}

TEST_CASE("FOREACH visits each element") {
  NGFI_DARRAY_OF(point) pt_array;
  std::queue<point> check_array;
  constexpr size_t array_size = 10;
  NGFI_DARRAY_RESET(pt_array, array_size);
  for (size_t i = 0; i < array_size; ++i) {
    point p = random_point();
    NGFI_DARRAY_APPEND(pt_array, p);
    check_array.push(p);
  }
  NGFI_DARRAY_FOREACH(pt_array, i) {
    REQUIRE(NGFI_DARRAY_AT(pt_array, i).x == check_array.front().x);
    REQUIRE(NGFI_DARRAY_AT(pt_array, i).y == check_array.front().y);
    check_array.pop();
  }
  REQUIRE(check_array.size() == 0u);
}
