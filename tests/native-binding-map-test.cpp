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
 #include "ngf-common/native-binding-map.h"

 TEST_CASE("NBM magic can be found") {
  const char test_string[] = "nothing /* NGF_NATIVE_BINDING_MAP */ more of nothing";
  const char* result = ngfi_find_serialized_native_binding_map(test_string);
  REQUIRE(result != NULL);
  REQUIRE(result - test_string == 33);
 
  const char test_string2[] = "nothing /**\nNGF_NATIVE_BINDING_MAP\n**/\nmore of nothing";
  const char* result2 = ngfi_find_serialized_native_binding_map(test_string2);
  REQUIRE(result2 != NULL);
  REQUIRE(result2 - test_string2 == 34);
 }

 TEST_CASE("NBM magic missing") {
  const char test_string[] = "/*NGF_NATIVE_";
  const char* result = ngfi_find_serialized_native_binding_map(test_string);
  REQUIRE(result == NULL);

  const char test_string2[] = "";
  const char* result2 = ngfi_find_serialized_native_binding_map(test_string2);
  REQUIRE(result2 == NULL);
 }

 TEST_CASE("NBM magic not in comment") {
   const char  test_string[] = "*/nothing NGF_NATIVE_BINDING_MAP */ more of nothing";
   const char* result = ngfi_find_serialized_native_binding_map(test_string);
   REQUIRE(result == NULL);
 }

TEST_CASE("First occurrence of NBM magic is found") {
  const char test_string[] = "0123456/*NGF_NATIVE_BINDING_MAP*/012/*NGF_NATIVE_BINDING_MAP*/";
  const char* result1 = ngfi_find_serialized_native_binding_map(test_string);
  REQUIRE(result1 != NULL);
  REQUIRE(result1 - test_string == 31);
  const char* result2 = ngfi_find_serialized_native_binding_map(result1);
  REQUIRE(result2 != NULL);
  REQUIRE(result2 - test_string == sizeof(test_string)-3);
}

TEST_CASE("Parse simple NBM") {
  const char test_string[] = "( 0 1 \n) : 2\n(2 0) : 3\naaaa";
  ngfi_native_binding_map* map = ngfi_parse_serialized_native_binding_map(test_string);
  REQUIRE(ngfi_native_binding_map_lookup(map, 0, 1) == 2u);
  REQUIRE(ngfi_native_binding_map_lookup(map, 2, 0) == 3u);
  REQUIRE(ngfi_native_binding_map_lookup(map, 2, 1) == ~0u);
  REQUIRE(ngfi_native_binding_map_lookup(map, 0, 0) == ~0u);
  REQUIRE(ngfi_native_binding_map_lookup(map, 3, 0) == ~0u);
  REQUIRE(ngfi_native_binding_map_lookup(map, 3, 2) == ~0u);
  ngfi_destroy_native_binding_map(map);
}

TEST_CASE("Ill formed NBM") {
  const char test_string1[] = "(0 1 : 2\n(2 0) : 3\naaaa";
  ngfi_native_binding_map* map1 = ngfi_parse_serialized_native_binding_map(test_string1);
  REQUIRE(map1 == NULL);
}

TEST_CASE("Parsing stops at error") {
  const char test_string2[] = "(0 1 ): 2\n2 0) : 3\naaaa";
  ngfi_native_binding_map* map2 = ngfi_parse_serialized_native_binding_map(test_string2);
  REQUIRE(map2 != NULL);
  REQUIRE(ngfi_native_binding_map_lookup(map2, 0, 1) == 2u);
  REQUIRE(ngfi_native_binding_map_lookup(map2, 2, 0) == ~0u);
  ngfi_destroy_native_binding_map(map2);
}

TEST_CASE("Parsing stops at (-1 -1 -1)") {
  const char test_string2[] = "(0 1): 2\n(-1 -1):-1 (2 0) : 3\naaaa";
  ngfi_native_binding_map* map2 = ngfi_parse_serialized_native_binding_map(test_string2);
  REQUIRE(map2 != NULL);
  REQUIRE(ngfi_native_binding_map_lookup(map2, 0, 1) == 2u);
  REQUIRE(ngfi_native_binding_map_lookup(map2, 2, 0) == ~0u);
  ngfi_destroy_native_binding_map(map2);
}
