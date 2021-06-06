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
#include "ngf-common/list.h"
#include <algorithm>
#include <random>

typedef struct test_struct {
  ngfi_list_node test_list;
  int tag;
} test_struct;

TEST_CASE("list_container_of returns the container struct") {
  test_struct s;
  ngfi_list_init(&s.test_list);
  s.tag = 0xbadbeef;
  const test_struct* container_ptr =  NGFI_LIST_CONTAINER_OF(&s.test_list, test_struct, test_list);
  REQUIRE(container_ptr->tag == s.tag);
  REQUIRE(container_ptr == &s);
}

TEST_CASE("single-element list iteration") {
  test_struct s;
  ngfi_list_init(&s.test_list);
  s.tag = 0xbadbeef;
  int num_iters = 0u;
  NGFI_LIST_FOR_EACH(&s.test_list, n) {
    const test_struct* s_ptr = NGFI_LIST_CONTAINER_OF(n, test_struct, test_list);
    REQUIRE(s_ptr->tag == s.tag);
    REQUIRE(num_iters++ < 1);
  }
}

namespace {

std::vector<int> init_elements(int num_elements, test_struct* elements) {
  std::vector<int> indices;
  for (int i = 0; i < num_elements; ++i) {
    ngfi_list_init(&elements[i].test_list);
    elements[i].tag = i;
    indices.push_back(i);
  }

  for (int i = 1; i < num_elements; ++i) {
    ngfi_list_append(&elements[i].test_list, &elements[0].test_list);
  }
  return indices;
}
}

TEST_CASE("sequence") {
  constexpr int num_elements = 80;
  test_struct elements[num_elements];
  init_elements(num_elements, elements);

  int num_iters = 0;
  NGFI_LIST_FOR_EACH(&elements[0].test_list, n) {
    const test_struct* s_ptr = NGFI_LIST_CONTAINER_OF(n, test_struct, test_list);
    REQUIRE(s_ptr->tag == num_iters);
    ++num_iters;
  }
  REQUIRE(num_iters == num_elements);
}

TEST_CASE("order maintained after remove") {
  constexpr int num_elements = 80;
  test_struct elements[num_elements];
  std::vector<int> indices = init_elements(num_elements, elements);
  std::vector<int> removal_order = indices;
  std::random_device rd;
  std::mt19937       g(rd());
  std::shuffle(removal_order.begin(), removal_order.end(), g);

  auto validate = [](ngfi_list_node* head, const std::vector<int>& expected_tag_values) {
    int num_iters = 0;
    NGFI_LIST_FOR_EACH(head, n) {
      const test_struct* s_ptr = NGFI_LIST_CONTAINER_OF(n, test_struct, test_list);
      REQUIRE(s_ptr->tag == expected_tag_values[num_iters++]);
    }
    REQUIRE(num_iters == expected_tag_values.size());
  };

  ngfi_list_node* head = &elements[0].test_list;
  for (int remove_id : removal_order) {
    if (remove_id == (NGFI_LIST_CONTAINER_OF(head, test_struct, test_list))->tag) head = head->next;
    ngfi_list_remove(&elements[remove_id].test_list);
    indices.erase(std::find(indices.begin(), indices.end(), remove_id));
    if (indices.size() > 0) {
      validate(head, indices);
    }
  }
}

TEST_CASE("append after remove") {
  constexpr int num_elements = 3;
  test_struct elements[num_elements];
  init_elements(num_elements, elements);
  auto get_tags = [](ngfi_list_node* head) {
    std::vector<int> tags;
    NGFI_LIST_FOR_EACH(head, n) {
      const test_struct* s_ptr = NGFI_LIST_CONTAINER_OF(n, test_struct, test_list);
      tags.push_back(s_ptr->tag);
    }
    return tags;
  };
  std::vector<int> tags = get_tags(&elements[0].test_list);
  REQUIRE(tags.size() == 3);
  REQUIRE(tags[0] == 0);
  REQUIRE(tags[1] == 1);
  REQUIRE(tags[2] == 2);
  ngfi_list_remove(&elements[1].test_list);
  tags = get_tags(&elements[0].test_list);
  REQUIRE(tags.size() == 2);
  REQUIRE(tags[0] == 0);
  REQUIRE(tags[1] == 2);
  ngfi_list_append(&elements[1].test_list, &elements[0].test_list);
  tags = get_tags(&elements[0].test_list);
  REQUIRE(tags.size() == 3);
  REQUIRE(tags[0] == 0);
  REQUIRE(tags[1] == 2);
  REQUIRE(tags[2] == 1);
}
