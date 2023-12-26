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
project(nicegraf-misc-common)


set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)

include("${CMAKE_CURRENT_LIST_DIR}/../../build-utils.cmake")

if (WIN32)
    set(NICESHADE_PLATFORM win PARENT_SCOPE)
    set(NGF_BACKEND nicegraf-vk)
elseif(APPLE)
    set(NICESHADE_PLATFORM macos PARENT_SCOPE)
    if (NGF_USE_MVK STREQUAL "yes")
      set(NGF_BACKEND nicegraf-vk)
    else()
      set(NGF_BACKEND nicegraf-mtl)
    endif()
elseif(UNIX AND NOT APPLE)
    set(NICESHADE_PLATFORM linux PARENT_SCOPE)
    set(NGF_BACKEND nicegraf-vk)
else()
    message(FATAL_ERROR "Your platform is not currently supported by nicegraf.")
endif()

nmk_header_library(NAME nicegraf
                   PUB_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/../../include
                   PUB_DEPS ${NGF_BACKEND})


string(TOUPPER "NGF_BACKEND_${NGF_BACKEND}" NGF_BACKEND_DEFINE)
string(REPLACE "-" "_" NGF_BACKEND_DEFINE ${NGF_BACKEND_DEFINE})

if (NGF_BUILD_SAMPLES STREQUAL "yes")
  nmk_static_library(NAME nicegraf-misc-common
                     SRCS  ${CMAKE_CURRENT_LIST_DIR}/shader-loader.h
                           ${CMAKE_CURRENT_LIST_DIR}/shader-loader.cpp
                           ${CMAKE_CURRENT_LIST_DIR}/file-utils.h
                           ${CMAKE_CURRENT_LIST_DIR}/file-utils.cpp
                           ${CMAKE_CURRENT_LIST_DIR}/mesh-loader.cpp
                           ${CMAKE_CURRENT_LIST_DIR}/mesh-loader.h
                           ${CMAKE_CURRENT_LIST_DIR}/targa-loader.cpp
                           ${CMAKE_CURRENT_LIST_DIR}/targa-loader.h
                           ${CMAKE_CURRENT_LIST_DIR}/logging.h
                     DEPS  nicegraf
                     PVT_DEFINES  ${NGF_BACKEND_DEFINE}
                     PUB_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/)
endif()
