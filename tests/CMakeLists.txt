#[[
Copyright © 2018 nicegraf contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the “Software”), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
]]

cmake_minimum_required(VERSION 3.0.2)
project(nicegraf-tests)

set (PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

set (TEST_SOURCES
  "${PROJECT_ROOT}/source/nicegraf_internal.c"
  "${PROJECT_ROOT}/source/stack_alloc.c"
  "${PROJECT_ROOT}/source/dynamic_array.h"
  "${PROJECT_ROOT}/tests/block_allocator_test.cpp"
  "${PROJECT_ROOT}/tests/stack_allocator_test.cpp"
  "${PROJECT_ROOT}/tests/dynamic_array_test.cpp"
  "${PROJECT_ROOT}/tests/main.cpp")
  
set (TEST_INCLUDE_PATHS
  "${PROJECT_ROOT}/include"
  "${PROJECT_ROOT}/source"
  "${PROJECT_ROOT}/third_party/catch")
  
add_executable(ngf_tests
  ${TEST_SOURCES})

target_include_directories(ngf_tests PRIVATE
  ${TEST_INCLUDE_PATHS})

set_target_properties(ngf_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}")
set_target_properties(ngf_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_LIST_DIR}")
set_target_properties(ngf_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_LIST_DIR}")
