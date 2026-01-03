#[[
Copyright (c) 2026 nicegraf contributors

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
project(nicegraf)


set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)

include("${CMAKE_CURRENT_LIST_DIR}/build-utils.cmake")

# These are the compiler flags that are used on all nicegraf targets.
if(MSVC)
    set (NICEMAKE_COMMON_COMPILE_OPTS "/W4" "/WX")
else()
	set(NICEMAKE_COMMON_COMPILE_OPTS "-Wall" "-Wconversion" "-Wno-unknown-pragmas" "-Werror" "-Wno-error=comment")
endif()
set(NICEGRAF_COMMON_DEPS nicegraf-internal)

# A library with various utilities shared internally across different backends.
nmk_static_library(NAME nicegraf-internal
                   SRCS ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf.h
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/macros.h
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/unique-ptr.h
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/value-or-error.h
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/cmdbuf-state.h
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/internal.c
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/util.h
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/arena.h
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/arena.cpp
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/default-arenas.h
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/default-arenas.cpp
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/chunked-list.h
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/hashtable.h
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/array.h)

# nicegraf utility library.
nmk_static_library(NAME nicegraf-util
                   SRCS ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf-util.h
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/util.c
                   DEPS nicegraf-internal)


if (APPLE)
  find_library(APPLE_METAL Metal)
  find_library(APPLE_QUARTZ QuartzCore)
  find_library(APPLE_COREGRAPHICS CoreGraphics)
  find_library(APPLE_COCOA Cocoa)
  find_library(APPLE_UIKIT UIKit)
endif()

if (APPLE AND NOT (NGF_USE_MVK STREQUAL "yes"))
  # Nicegraf with native Metal backend.
  set(APPLE_LIBS ${APPLE_METAL} ${APPLE_QUARTZ} ${APPLE_COREGRAPHICS})

  if (APPLE_COCOA)
    set(APPLE_LIBS ${APPLE_LIBS} ${APPLE_COCOA}) # macOS
  else()
    set(APPLE_LIBS ${APPLE_LIBS} ${UIKit}) # iOS
  endif()

  nmk_static_library(NAME nicegraf-mtl
                     SRCS ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf.h
                          ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf-mtl-handles.h
                          ${CMAKE_CURRENT_LIST_DIR}/source/ngf-mtl/impl.cpp
                          ${CMAKE_CURRENT_LIST_DIR}/source/ngf-mtl/layer.mm
                     DEPS ${NICEGRAF_COMMON_DEPS} ${APPLE_LIBS}
                     PVT_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/deps/metal-cpp
                     COPTS "-fobjc-arc")
else()
  nmk_header_library(NAME nicegraf-vk-headers
                     PUB_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/deps/vulkan-headers)
  nmk_header_library(NAME nicegraf-renderdoc
                     PUB_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/deps/renderdoc)

  # Import VMA for handling vulkan memory allocation.
  add_definitions("-DVMA_STATIC_VULKAN_FUNCTIONS=0")
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/deps/vma)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/deps/SPIRV-reflect)

  # Some vulkan-specific dependencies.
  set(NICEGRAF_VK_DEPS vma spvreflect nicegraf-vk-headers nicegraf-renderdoc)
  if (NOT WIN32 AND NOT APPLE)
    set(NICEGRAF_VK_DEPS ${NICEGRAF_VK_DEPS} xcb)
  elseif (APPLE)
    set(NICEGRAF_VK_DEPS ${NICEGRAF_VK_DEPS} ${APPLE_QUARTZ})
  endif()

  set(NICEGRAF_VK_DEPS ${NICEGRAF_VK_DEPS} ${NICEGRAF_COMMON_DEPS})

  set(NICEGRAF_VK_SRCS ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf.h
                       ${CMAKE_CURRENT_LIST_DIR}/source/ngf-vk/impl.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/source/ngf-vk/vk_10.c)
  if (NGF_USE_MVK STREQUAL "yes")
    set(NICEGRAF_VK_SRCS ${NICEGRAF_VK_SRCS} ${CMAKE_CURRENT_LIST_DIR}/source/ngf-vk/ca-metal-layer.mm)
  endif()

  # Vulkan backend.
  nmk_static_library(NAME nicegraf-vk
                     SRCS ${NICEGRAF_VK_SRCS}
                     PUB_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/include
                     DEPS ${NICEGRAF_VK_DEPS})
endif()

# Build tests only if explicitly requested.
if (NGF_BUILD_TESTS STREQUAL "yes")
  nmk_header_library(NAME utest
                     PUB_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/deps/utest)
  nmk_binary(NAME common-tests
             SRCS ${CMAKE_CURRENT_LIST_DIR}/tests/common-tests.cpp
             DEPS utest nicegraf-internal "$<IF:$<NOT:$<BOOL:${WIN32}>>,pthread,>")
  nmk_binary(NAME vk-backend-tests
             SRCS ${NICEGRAF_VK_SRCS}
             DEPS utest ${NICEGRAF_VK_DEPS}
             PVT_DEFINES NGFVK_TEST_MODE)
endif()


# Build samples only if explicitly requested.
if (NGF_BUILD_SAMPLES STREQUAL "yes")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/misc/common)
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/samples/deps/glfw)

    # Ensure we have the required dependencies are available.
    if (TARGET glfw)
        set_target_properties(glfw PROPERTIES FOLDER "samples")
    else()
        message(FATAL_ERROR "Dependencies required for building samples not found. Make sure to run `git submodule update` from the repo root.")
    endif()
    
    # Determine the backend to build the samples with based on the platform, and
    # any platform-specific source files.
    set(NGF_PLATFORM_SOURCE "")
    if(APPLE)
      set(NGF_PLATFORM_SOURCE ${NGF_PLATFORM_SOURCE}
                              ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/macos/glfw-cocoa-contentview.mm
                              ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/macos/glfw-cocoa-contentview.h)
    endif()
    
    # Set the folder to hold all samples binaries.
    set(NGF_SAMPLES_OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/samples/binaries)

    # Custom target for generated shaders.
    file(GLOB shader_files ${CMAKE_CURRENT_LIST_DIR}/samples/shaders/*.hlsl)
    include(${CMAKE_CURRENT_LIST_DIR}/misc/shaders.cmake)
    ngf_shaders_target(NAME sample-shaders
                       OUTPUT_DIR ${NGF_SAMPLES_OUTPUT_DIR}/shaders
                       NICESHADE_PATH ${CMAKE_CURRENT_LIST_DIR}/samples/deps/niceshade/${NICESHADE_PLATFORM}
                       SRCS ${shader_files})
    set_target_properties(sample-shaders PROPERTIES FOLDER "samples")

    set(NGF_IMGUI_SRCS ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui_draw.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui_tables.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui_widgets.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui_demo.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui.h
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/backends/imgui_impl_glfw.h
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/backends/imgui_impl_glfw.cpp)
	       
    if(MSVC)
        set(NGF_IMGUI_COPTS "")
    else()
        # Turn off reporting warnings as errors for ImGui on gcc/clang, because it has a lot of them.
        set(NGF_IMGUI_COPTS "-Wno-error")
    endif()

    nmk_static_library(NAME ngf-imgui
                SRCS ${NGF_IMGUI_SRCS}
                DEPS glfw
                PVT_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui
                PUB_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui
                             ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/backends
	            PVT_DEFINES "GLFW_INCLUDE_NONE"
	           	COPTS ${NGF_IMGUI_COPTS})
    nmk_static_library(NAME ngf-samples-common
                       SRCS ${CMAKE_CURRENT_LIST_DIR}/samples/common/main.cpp
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/diagnostic-callback.cpp
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/sample-interface.h
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/diagnostic-callback.h
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/imgui-backend.h
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/imgui-backend.cpp
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/staging-image.h
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/staging-image.cpp
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/camera-controller.h
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/camera-controller.cpp
                            ${NGF_PLATFORM_SOURCE}
                       DEPS ngf-imgui glfw nicegraf-misc-common
                       PVT_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/samples/deps/nicemath
                                    ${CMAKE_CURRENT_LIST_DIR}/samples/common
                       PUB_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/samples/common
                                    ${CMAKE_CURRENT_LIST_DIR}/samples/deps/nicemath)
    set_target_properties(ngf-samples-common PROPERTIES FOLDER "samples")

    function (ngf_sample)
        cmake_parse_arguments(SAMPLE "" "NAME" "" ${ARGN})
        file(GLOB_RECURSE SAMPLE_SRCS ${CMAKE_CURRENT_LIST_DIR}/samples/${SAMPLE_NAME}/*.cpp)

        nmk_binary(NAME ${SAMPLE_NAME}
                   SRCS ${SAMPLE_SRCS}
                   DEPS nicegraf ngf-samples-common nicegraf-misc-common nicegraf-util ngf-imgui
                   PVT_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/samples/${SAMPLE_NAME}
		           PVT_DEFINES "GLFW_INCLUDE_NONE"
                   OUTPUT_DIR "${NGF_SAMPLES_OUTPUT_DIR}")
        add_dependencies(${SAMPLE_NAME} sample-shaders)                   
        set_target_properties(${SAMPLE_NAME} PROPERTIES FOLDER "samples")
    endfunction()
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/samples/binaries/shaders)
    ngf_sample(NAME 01-fullscreen-triangle)
    ngf_sample(NAME 02-render-to-texture)
    ngf_sample(NAME 03-uniform-buffers)
    ngf_sample(NAME 04-texture-sampling)
    ngf_sample(NAME 05-cubemap)
    ngf_sample(NAME 06-vertex-attribs)
    ngf_sample(NAME 07-blinn-phong)    
    ngf_sample(NAME 08-image-arrays)    
    ngf_sample(NAME 09-volume-rendering)
    ngf_sample(NAME 0a-compute-mandelbrot)
    ngf_sample(NAME 0b-compute-vertices)
    ngf_sample(NAME 0c-render-to-multisample-texture)
endif()

# Build image tests only if explicitly requested.
# These tests run samples headlessly and compare rendered output against golden images.
# Requires NGF_BUILD_SAMPLES=yes since image tests depend on sample shaders and utilities.
if (NGF_BUILD_IMAGE_TESTS STREQUAL "yes")
    if (NOT NGF_BUILD_SAMPLES STREQUAL "yes")
        message(FATAL_ERROR "NGF_BUILD_IMAGE_TESTS requires NGF_BUILD_SAMPLES=yes")
    endif()

    # Sample utility sources needed by image tests (excluding main.cpp and factory.cpp)
    set(IMAGE_TEST_SAMPLE_UTILS
        ${CMAKE_CURRENT_LIST_DIR}/samples/common/staging-image.cpp
        ${CMAKE_CURRENT_LIST_DIR}/samples/common/camera-controller.cpp
    )

    # All sample sources (excluding factory.cpp which is only for interactive samples)
    set(IMAGE_TEST_SAMPLE_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/samples/01-fullscreen-triangle/fullscreen-triangle.cpp
        ${CMAKE_CURRENT_LIST_DIR}/samples/02-render-to-texture/render-to-texture.cpp
        ${CMAKE_CURRENT_LIST_DIR}/samples/03-uniform-buffers/uniform-buffers.cpp
        ${CMAKE_CURRENT_LIST_DIR}/samples/04-texture-sampling/texture-sampling.cpp
        ${CMAKE_CURRENT_LIST_DIR}/samples/05-cubemap/cubemap.cpp
        ${CMAKE_CURRENT_LIST_DIR}/samples/06-vertex-attribs/vertex-attribs.cpp
        ${CMAKE_CURRENT_LIST_DIR}/samples/07-blinn-phong/blinn-phong.cpp
        ${CMAKE_CURRENT_LIST_DIR}/samples/08-image-arrays/image-arrays.cpp
        ${CMAKE_CURRENT_LIST_DIR}/samples/09-volume-rendering/volume-rendering.cpp
        ${CMAKE_CURRENT_LIST_DIR}/samples/0a-compute-mandelbrot/compute-mandelbrot.cpp
        ${CMAKE_CURRENT_LIST_DIR}/samples/0b-compute-vertices/compute-vertices.cpp
        ${CMAKE_CURRENT_LIST_DIR}/samples/0c-render-to-multisample-texture/render-to-multisample-texture.cpp
    )

    # Image test sources
    set(IMAGE_TEST_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/tests/image-tests/image-test-main.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/image-tests/headless-harness.cpp
        ${CMAKE_CURRENT_LIST_DIR}/tests/image-tests/image-comparator.cpp
        ${IMAGE_TEST_SAMPLE_UTILS}
        ${IMAGE_TEST_SAMPLE_SOURCES}
    )

    # Ensure golden images directory exists
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/tests/golden)

    # Create unified image test executable
    nmk_binary(NAME image-tests
               SRCS ${IMAGE_TEST_SOURCES}
               DEPS nicegraf nicegraf-util nicegraf-misc-common ngf-imgui
               PVT_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/tests
                            ${CMAKE_CURRENT_LIST_DIR}/tests/image-tests
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common
                            ${CMAKE_CURRENT_LIST_DIR}/samples/deps/nicemath
                            ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui
               OUTPUT_DIR "${NGF_SAMPLES_OUTPUT_DIR}")
    add_dependencies(image-tests sample-shaders)
    set_target_properties(image-tests PROPERTIES FOLDER "tests")
endif()
