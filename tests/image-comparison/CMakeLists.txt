#[[
Copyright (c) 2023 nicegraf contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the “Software”), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
]]

cmake_minimum_required(VERSION 3.23.3)
project(nicegraf-image-comparison)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(NICEGRAF_DIR ${CMAKE_CURRENT_LIST_DIR}/../../)

include("${CMAKE_CURRENT_LIST_DIR}/../../build-utils.cmake")
add_subdirectory(${NICEGRAF_DIR} ${CMAKE_CURRENT_LIST_DIR}/bin)

if (WIN32)
    set(NICEGRAF_SHADERC_PLATFORM win)
    set(NGF_IMAGE_COMPARISON_BACKEND nicegraf-vk)
elseif(APPLE)
    set(NICEGRAF_SHADERC_PLATFORM macos)
    if (NGF_USE_MVK STREQUAL "yes")
      set(NGF_IMAGE_COMPARISON_BACKEND nicegraf-vk)
    else()
      set(NGF_IMAGE_COMPARISON_BACKEND nicegraf-mtl)
    endif()
elseif(UNIX AND NOT APPLE)
    set(NICEGRAF_SHADERC_PLATFORM linux)
    set(NGF_IMAGE_COMPARISON_BACKEND nicegraf-vk)
else()
    message(FATAL_ERROR "Your platform is not currently supported by nicegraf-image-comparison-tests.")
endif()

string(TOUPPER "NGF_IMAGE_COMPARISON_BACKEND_${NGF_IMAGE_COMPARISON_BACKEND}" NGF_IMAGE_COMPARISON_BACKEND_DEFINE)
string(REPLACE "-" "_" NGF_IMAGE_COMPARISON_BACKEND_DEFINE ${NGF_IMAGE_COMPARISON_BACKEND_DEFINE})

nmk_static_library(NAME nicegraf-image-comparison-tests-common
                   SRCS  ${CMAKE_CURRENT_LIST_DIR}/common/shader-loader.h
                         ${CMAKE_CURRENT_LIST_DIR}/common/shader-loader.cpp
                         ${CMAKE_CURRENT_LIST_DIR}/common/test-util.cpp
                         ${CMAKE_CURRENT_LIST_DIR}/common/file-utils.h
                         ${CMAKE_CURRENT_LIST_DIR}/common/file-utils.cpp
                   DEPS ${NICEGRAF_IMAGE_COMPARISON_BACKEND} 
                   PUB_DEFINES  ${NGF_IMAGE_COMPARISON_BACKEND_DEFINE}
                   PUB_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/common ${NICEGRAF_DIR}/include)

set(NICEGRAF_IMAGE_COMPARISON_SRCS
    ${CMAKE_CURRENT_LIST_DIR}/unit-tests/ngf-triangle-test.cpp)

nmk_binary(NAME nicegraf-image-comparison-tests
  SRCS ${NICEGRAF_IMAGE_COMPARISON_SRCS}
  DEPS nicegraf-image-comparison-tests-common ${NGF_IMAGE_COMPARISON_BACKEND} nicegraf-util
  PVT_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/common ${NICEGRAF_DIR}/include)
