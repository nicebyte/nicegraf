#[[
Copyright (c) 2024 nicegraf contributors

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
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

cmake_minimum_required(VERSION 3.23.3)
project(nicegraf)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)

include(${CMAKE_CURRENT_LIST_DIR}/build-utils.cmake)

# These are the compiler flags that are used on all nicegraf targets.
if(MSVC)
    set(NICEMAKE_COMMON_COMPILE_OPTS "/W4" "/WX")
else()
    set(NICEMAKE_COMMON_COMPILE_OPTS "-Wall" "-Wconversion" "-Wno-unknown-pragmas" "-Werror" "-Wno-error=comment" "-Wno-nullability-completeness" "-Wno-deprecated-declarations")
endif()

set(NICEGRAF_COMMON_DEPS nicegraf-internal)

# A library with various utilities shared internally across different backends.
nmk_static_library(
    NAME nicegraf-internal
    SRCS ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf.h
         ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/macros.h
         ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/native-binding-map.h
         ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/dynamic-array.h
         ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/cmdbuf-state.h
         ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/internal.c
         ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/native-binding-map.c
         ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/stack-alloc.h
         ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/stack-alloc.c
         ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/cmdbuf-state.c
         ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/list.h
         ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/chunk-list.h
         ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/dict.h
         ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/dict.c
         ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/block-alloc.h
         ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/block-alloc.c
)

# nicegraf utility library.
nmk_static_library(
    NAME nicegraf-util
    SRCS ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf-util.h
         ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/util.c
    DEPS nicegraf-internal
)

if (APPLE)
    find_library(APPLE_METAL Metal)
    find_library(APPLE_QUARTZ_CORE QuartzCore)
    find_library(APPLE_CORE_GRAPHICS CoreGraphics)
    find_library(APPLE_COCOA Cocoa)
    find_library(APPLE_UIKIT UIKit)
    find_library(APPLE_CORE_FOUNDATION CoreFoundation)
    find_library(APPLE_FOUNDATION Foundation)
    find_library(APPLE_METAL_KIT MetalKit)
endif()

if (APPLE AND NOT (NGF_USE_MVK STREQUAL "yes"))
    # Nicegraf with native Metal backend.
    set(APPLE_LIBS ${APPLE_METAL} ${APPLE_QUARTZ_CORE} ${APPLE_CORE_GRAPHICS})

    if (APPLE_COCOA)
        set(APPLE_LIBS ${APPLE_LIBS} ${APPLE_COCOA}) # macOS
    else()
        set(APPLE_LIBS ${APPLE_LIBS} ${APPLE_UIKIT}) # iOS
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
                       ${CMAKE_CURRENT_LIST_DIR}/source/ngf-vk/impl.c
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
    nmk_binary(
        NAME internal-utils-tests
        SRCS ${CMAKE_CURRENT_LIST_DIR}/tests/internal-utils-tests.c
        SRCS ${CMAKE_CURRENT_LIST_DIR}/tests/test-suite-runner.c
        DEPS nicegraf-internal "$<IF:$<NOT:$<BOOL:${WIN32}>>,pthread,>"
    )
    nmk_static_library(
        NAME nicegraf-vk-for-tests 
        SRCS ${NICEGRAF_VK_SRCS}
        DEPS ${NICEGRAF_VK_DEPS}
        PVT_DEFINES NGFVK_TEST_MODE
    )
    nmk_binary(
        NAME vk-backend-tests
        SRCS ${CMAKE_CURRENT_LIST_DIR}/tests/test-suite-runner.c
        DEPS nicegraf-vk-for-tests "$<IF:$<NOT:$<BOOL:${WIN32}>>,dl,>"
    )
endif()


# Build samples only if explicitly requested.
if (NGF_BUILD_SAMPLES STREQUAL "yes")
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/misc/common)
    if (NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "iOS")
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/samples/deps/glfw)

        # Ensure we have the required dependencies are available.
        if (TARGET glfw)
            set_target_properties(glfw PROPERTIES FOLDER "samples")
        else()
            message(FATAL_ERROR "Dependencies required for building samples not found. Make sure to run `git submodule update` from the repo root.")
        endif()
    endif()

    # Determine the backend to build the samples with based on the platform, and
    # any platform-specific source files.
    set(NGF_IMGUI_PLATFORM_SOURCE ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/backends/imgui_impl_glfw.h
                                  ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/backends/imgui_impl_glfw.cpp)
    set(NGF_SAMPLES_DEPS glfw)
    set(NGF_IMGUI_DEPS glfw)
    set(NGF_SAMPLES_PVT_DEFINES "GLFW_INCLUDE_NONE")
    set(NGF_PLATFORM_SOURCE "")

    if(WIN32)
        set(NGF_PLATFORM_SOURCE ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/windows/platform.cpp)
    elseif(APPLE)
        if (${CMAKE_SYSTEM_NAME} STREQUAL "iOS")
            set(NGF_IMGUI_PLATFORM_SOURCE ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/backends/imgui_impl_metal.h
                                          ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/backends/imgui_impl_metal.mm)
            set(NGF_PLATFORM_SOURCE ${NGF_PLATFORM_SOURCE}
                                    ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/ios/platform.mm
                                    ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/ios/AppDelegate.h
                                    ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/ios/AppDelegate.mm
                                    ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/ios/AppView.h
                                    ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/ios/AppView.mm)
            set(NGF_SAMPLES_DEPS ${APPLE_METAL}
                                 ${APPLE_METAL_KIT}
                                 ${APPLE_FOUNDATION}
                                 ${APPLE_CORE_FOUNDATION}
                                 ${APPLE_QUARTZ_CORE}
                                 ${APPLE_CORE_GRAPHICS}
                                 ${APPLE_UIKIT})
            unset(NGF_SAMPLES_PVT_DEFINES)
            unset(NGF_IMGUI_DEPS)
        else()
            set(NGF_PLATFORM_SOURCE ${NGF_PLATFORM_SOURCE}
                                    ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/macos/glfw-cocoa-contentview.mm
                                    ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/macos/glfw-cocoa-contentview.h
                                    ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/macos/platform.cpp)
            set(NGF_SAMPLES_DEPS ${NGF_SAMPLES_DEPS}
                                 ${APPLE_CORE_FOUNDATION}
                                 ${APPLE_FOUNDATION})
        endif()
    elseif(UNIX AND NOT APPLE)
        set(NGF_PLATFORM_SOURCE ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/linux/platform.cpp)
    endif()

    # Set the folder to hold all samples binaries.
    set(NGF_SAMPLES_OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/samples/binaries)

    # Custom target for generated shaders.
    file(GLOB shader_files ${CMAKE_CURRENT_LIST_DIR}/samples/shaders/*.hlsl)
    include(${CMAKE_CURRENT_LIST_DIR}/misc/shaders.cmake)
    ngf_shaders_target(
        NAME sample-shaders
        OUTPUT_DIR ${NGF_SAMPLES_OUTPUT_DIR}/shaders
        NICESHADE_PATH ${CMAKE_CURRENT_LIST_DIR}/samples/deps/niceshade/${NICESHADE_PLATFORM}
        SRCS ${shader_files}
    )
    set_target_properties(sample-shaders PROPERTIES FOLDER "samples")

    set(NGF_IMGUI_SRCS ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui.h
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui_draw.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui_tables.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui_widgets.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui_demo.cpp
                       ${NGF_IMGUI_PLATFORM_SOURCE})
	       
    if(MSVC)
        set(NGF_IMGUI_COPTS "")
    else()
        # Turn off reporting warnings as errors for ImGui on gcc/clang, because it has a lot of them.
        set(NGF_IMGUI_COPTS "-Wno-error")
    endif()


    nmk_static_library(
        NAME ngf-imgui
        SRCS ${NGF_IMGUI_SRCS}
        DEPS ${NGF_IMGUI_DEPS}
        PVT_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui
        PUB_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui
                     ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/backends
        PVT_DEFINES ${NGF_SAMPLES_PVT_DEFINES}
        COPTS ${NGF_IMGUI_COPTS}
    )
    nmk_static_library(
        NAME ngf-samples-common
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
             ${CMAKE_CURRENT_LIST_DIR}/samples/common/common.h
             ${CMAKE_CURRENT_LIST_DIR}/samples/common/common.cpp
             ${NGF_PLATFORM_SOURCE}
        DEPS ngf-imgui nicegraf-misc-common ${NGF_SAMPLES_DEPS}
        PVT_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/samples/deps/nicemath
                     ${CMAKE_CURRENT_LIST_DIR}/samples/common
        PUB_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/samples/common
                     ${CMAKE_CURRENT_LIST_DIR}/samples/deps/nicemath
    )
    set_target_properties(ngf-samples-common PROPERTIES FOLDER "samples")

    if (${CMAKE_SYSTEM_NAME} STREQUAL "iOS")
        set_target_properties(ngf-samples-common PROPERTIES XCODE_ATTRIBUTE_GCC_INPUT_FILETYPE "sourcecode.cpp.objcpp")
    endif()

    include(${CMAKE_CURRENT_LIST_DIR}/assets.cmake)
    function (ngf_sample)
        cmake_parse_arguments(SAMPLE "" "NAME" "" ${ARGN})

        # Get the sample source files.
        file(GLOB_RECURSE SAMPLE_SRCS ${CMAKE_CURRENT_LIST_DIR}/samples/${SAMPLE_NAME}/*.cpp)

        # Set the output directory for the sample
        set(NGF_SAMPLE_OUTPUT_DIR "${NGF_SAMPLES_OUTPUT_DIR}/${SAMPLE_NAME}")

        # Set the output directory for the resources
        set(NGF_SAMPLE_RESOURCES_DIR ${NGF_SAMPLE_OUTPUT_DIR})
        if (${CMAKE_SYSTEM_NAME} STREQUAL "iOS")
            set(NGF_SAMPLE_RESOURCES_DIR "${NGF_SAMPLE_OUTPUT_DIR}/${SAMPLE_NAME}.app")
        endif()

        nmk_assets_target(
            NAME ${SAMPLE_NAME}-assets
            DEPS sample-shaders
            JSON_PATH ${CMAKE_CURRENT_LIST_DIR}/samples/${SAMPLE_NAME}/assets.json
            SOURCES_BASE_DIR ${NGF_SAMPLES_OUTPUT_DIR}
            DESTINATION_BASE_DIR ${NGF_SAMPLE_RESOURCES_DIR}
            PROPERTIES FOLDER "samples/${SAMPLE_NAME}"
        )

        nmk_binary(
            NAME ${SAMPLE_NAME}
            SRCS ${SAMPLE_SRCS}
            DEPS nicegraf ngf-samples-common nicegraf-misc-common nicegraf-util ngf-imgui
            PVT_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/samples/${SAMPLE_NAME}
            PVT_DEFINES ${NGF_SAMPLES_PVT_DEFINES}
            OUTPUT_DIR ${NGF_SAMPLE_OUTPUT_DIR}
        )

        add_dependencies(${SAMPLE_NAME} ${SAMPLE_NAME}-assets)                   
        set_target_properties(${SAMPLE_NAME} PROPERTIES FOLDER "samples/${SAMPLE_NAME}")

        if (${CMAKE_SYSTEM_NAME} STREQUAL "iOS")
            set_target_properties(${SAMPLE_NAME} PROPERTIES
                MACOSX_BUNDLE TRUE
                MACOSX_BUNDLE_BUNDLE_NAME ${SAMPLE_NAME}
                MACOSX_BUNDLE_GUI_IDENTIFIER "com.snowblaze.${SAMPLE_NAME}"
                MACOSX_BUNDLE_BUNDLE_VERSION "0.0.1"
                MACOSX_BUNDLE_SHORT_VERSION_STRING "0.0.1"
                MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/ios/Info.plist.in
                XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "{YOUR_APPLE_DEVELOPER_TEAM_ID}"
                XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
            )
        endif()
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
