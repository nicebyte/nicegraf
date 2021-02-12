#[[
Copyright (c) 2021 nicegraf contributors

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

cmake_minimum_required(VERSION 3.14.0)
project(nicegraf)


set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)

# These are the compiler flags that are used on all nicegraf targets.
if(WIN32)
    set (NICEGRAF_COMMON_COMPILE_OPTS "/W4" "/WX")
else()
    set(NICEGRAF_COMMON_COMPILE_OPTS "-Wall" "-Wconversion" "-Wno-unknown-pragmas" "-Werror")
endif()
    

# This function adds a new library target and sets some configuration
# common across all libs.
function (ngf_target)
  cmake_parse_arguments(TGT "" "NAME" "TYPE;SRCS;DEPS;COPTS" ${ARGN})
  if (TGT_TYPE STREQUAL "lib")
    add_library(${TGT_NAME} STATIC ${TGT_SRCS})
  elseif(TGT_TYPE STREQUAL "bin")
    add_executable(${TGT_NAME} ${TGT_SRCS})
  else()
    message(FATAL_ERROR "invalid target type")
  endif()
  if ( TGT_DEPS )
    target_link_libraries(${TGT_NAME} ${TGT_DEPS})
  endif()
  target_include_directories(${TGT_NAME}
                             PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
                             PRIVATE ${CMAKE_CURRENT_LIST_DIR}/source)
  target_compile_options(${TGT_NAME} PRIVATE ${NICEGRAF_COMMON_COMPILE_OPTS})
  if ( TGT_COPTS )
    target_compile_options(${TGT_NAME} PRIVATE ${TGT_COPTS})
  endif()
endfunction()

# Shortcut for adding a new library target.
function (ngf_library)
   ngf_target(TYPE lib ${ARGN})
endfunction()

# Shortcut for adding a new binary target.
function (ngf_binary)
   ngf_target(TYPE bin ${ARGN})
endfunction()

# A helper library for parsing shader metadata produced by
# nicegraf-shaderc.
ngf_library(NAME ngf_metadata_parser
            SRCS ${CMAKE_CURRENT_LIST_DIR}/include/metadata_parser.h
                 ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/metadata_parser.c)

# A library with various utilities shared internally across different backends.
ngf_library(NAME nicegraf_internal
            SRCS ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf.h
                 ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/nicegraf_internal.h
                 ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/stack_alloc.h
                 ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/block_alloc.h
                 ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/native_binding_map.h
                 ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/cmdbuf_state.h
                 ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/nicegraf_internal.c
                 ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/stack_alloc.c
                 ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/block_alloc.c
                 ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/native_binding_map.c
                 ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/cmdbuf_state.c
            DEPS ngf_metadata_parser)

# nicegraf utility library.
ngf_library(NAME nicegraf_util
            SRCS ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf_util.h
                 ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/util.c
            DEPS nicegraf_internal)

# Common dependencies used by nicegraf on all platforms.
set(NICEGRAF_COMMON_DEPS nicegraf_internal ngf_metadata_parser)

if (APPLE)
  # Nicegraf with Metal backend.
  find_library(APPLE_METAL Metal)
  find_library(APPLE_COCOA Cocoa)
  find_library(APPLE_QUARTZ Quartz)
  ngf_library(NAME nicegraf_mtl
              SRCS ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf.h
                   ${CMAKE_CURRENT_LIST_DIR}/source/ngf-mtl/impl.mm
              DEPS ${NICEGRAF_COMMON_DEPS} ${APPLE_METAL} ${APPLE_COCOA} ${APPLE_QUARTZ}
              COPTS "-fobjc-arc")
else()
  # Import the EGL library for context creation.
  set(EGL_NO_GLEW ON)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/third_party/EGL/EGL)

  # Nicegraf with GL backend.
  ngf_library(NAME nicegraf_gl
              SRCS ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf.h
                   ${CMAKE_CURRENT_LIST_DIR}/source/ngf-gl/impl_43.c
                   ${CMAKE_CURRENT_LIST_DIR}/source/ngf-gl/gl_43_core.c
              DEPS egl)

  # Try searching for Vulkan SDK.
  find_package(Vulkan)

  # Only build the Vulkan backend if the Vulkan SDK is present.
  if ( Vulkan_FOUND )
    # Import VMA for handling vulkan memory allocation.
    add_definitions("-DVMA_STATIC_VULKAN_FUNCTIONS=0")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/third_party/vma)

    # Some vulkan-specific dependencies.
    set(NICEGRAF_VK_DEPS vma)
    if (NOT WIN32)
      set(NICEGRAF_VK_DEPS ${NICEGRAF_VK_DEPS} xcb)
    endif()

    # Vulkan backend.
    ngf_library(NAME nicegraf_vk
                SRCS ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf.h
                     ${CMAKE_CURRENT_LIST_DIR}/source/ngf-vk/impl.c
                     ${CMAKE_CURRENT_LIST_DIR}/source/ngf-vk/vk_10.c
                DEPS ${NICEGRAF_COMMON_DEPS} ${NICEGRAF_VK_DEPS})

    # Add Vulkan header directory to nicegraf_vk include paths.
    get_property(VK_INCLUDE_PATH TARGET Vulkan::Vulkan PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(nicegraf_vk PRIVATE ${VK_INCLUDE_PATH})
  else()
    message(WARNING "nicegraf Vulkan backend will not be built because Vulkan SDK was not found.")
  endif()
endif()

if (NGF_BUILD_TESTS STREQUAL "yes")
    ngf_binary(NAME ngf_tests
               SRCS ${CMAKE_CURRENT_LIST_DIR}/tests/block_allocator_test.cpp
                    ${CMAKE_CURRENT_LIST_DIR}/tests/stack_allocator_test.cpp
                    ${CMAKE_CURRENT_LIST_DIR}/tests/dynamic_array_test.cpp
                    ${CMAKE_CURRENT_LIST_DIR}/tests/frame_token_test.cpp
                    ${CMAKE_CURRENT_LIST_DIR}/tests/main.cpp
               DEPS nicegraf_internal)
    target_include_directories(ngf_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/third_party/catch")
    if (NOT WIN32)
        target_link_libraries(ngf_tests pthread)
    endif()
endif()
