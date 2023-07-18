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
project(nicegraf)


set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)

include("${CMAKE_CURRENT_LIST_DIR}/build-utils.cmake")

# These are the compiler flags that are used on all nicegraf targets.
if(WIN32)
    set (NICEMAKE_COMMON_COMPILE_OPTS "/W4" "/WX")
else()
	set(NICEMAKE_COMMON_COMPILE_OPTS "-Wall" "-Wconversion" "-Wno-unknown-pragmas" "-Werror" "-Wno-error=comment")
endif()
set(NICEGRAF_COMMON_DEPS nicegraf-internal)

# A library with various utilities shared internally across different backends.
nmk_static_library(NAME nicegraf-internal
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
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/block-alloc.h
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/block-alloc.c)

# nicegraf utility library.
nmk_static_library(NAME nicegraf-util
                   SRCS ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf-util.h
                        ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/util.c
                   DEPS nicegraf-internal)


if (APPLE)
  find_library(APPLE_METAL Metal)
  find_library(APPLE_COCOA Cocoa)
  find_library(APPLE_QUARTZ Quartz)
endif()

if (APPLE AND NOT (NGF_USE_MVK STREQUAL "yes"))
  # Nicegraf with native Metal backend.	
  nmk_static_library(NAME nicegraf-mtl	
                     SRCS ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf.h	
                          ${CMAKE_CURRENT_LIST_DIR}/source/ngf-mtl/impl.mm	
                     DEPS ${NICEGRAF_COMMON_DEPS} ${APPLE_METAL} ${APPLE_COCOA} ${APPLE_QUARTZ}	
                     COPTS "-fobjc-arc")	

  nmk_static_library(NAME nicegraf-mtl-cpp
                     SRCS ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf.h
                          ${CMAKE_CURRENT_LIST_DIR}/source/ngf-mtl/impl.cpp
                          ${CMAKE_CURRENT_LIST_DIR}/source/ngf-mtl/layer.mm
                     DEPS ${NICEGRAF_COMMON_DEPS} ${APPLE_METAL} ${APPLE_COCOA} ${APPLE_QUARTZ}
                     PUB_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/deps/metal-cpp
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
                     DEPS ${NICEGRAF_VK_DEPS})
  nmk_static_library(NAME nicegraf-vk-for-tests 
                     SRCS ${NICEGRAF_VK_SRCS}
                     DEPS ${NICEGRAF_VK_DEPS}
                     PVT_DEFINES NGFVK_TEST_MODE)
                     
  nmk_binary(NAME vk-backend-tests
             SRCS ${CMAKE_CURRENT_LIST_DIR}/tests/test-suite-runner.c
             DEPS nicegraf-vk-for-tests "$<IF:$<NOT:$<BOOL:${WIN32}>>,dl,>")
endif()

# Build tests only if explicitly requested.
if (NGF_BUILD_TESTS STREQUAL "yes")
    nmk_binary(NAME internal-utils-tests
           SRCS ${CMAKE_CURRENT_LIST_DIR}/tests/internal-utils-tests.c
           SRCS ${CMAKE_CURRENT_LIST_DIR}/tests/test-suite-runner.c
                    DEPS nicegraf-internal "$<IF:$<NOT:$<BOOL:${WIN32}>>,pthread,>")
endif()

# Build samples only if explicitly requested.
if (NGF_BUILD_SAMPLES STREQUAL "yes")
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
    if (WIN32)
        set(NICEGRAF_SHADERC_PLATFORM win)
        set(NGF_SAMPLES_BACKEND nicegraf-vk)
    elseif(APPLE)
        set(NICEGRAF_SHADERC_PLATFORM macos)
        if (NGF_USE_MVK STREQUAL "yes")
          set(NGF_SAMPLES_BACKEND nicegraf-vk)
        else()
          set(NGF_SAMPLES_BACKEND nicegraf-mtl)
        endif()
        set(NGF_PLATFORM_SOURCE ${NGF_PLATFORM_SOURCE}
                                ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/macos/main.mm
                                ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/macos/glfw-cocoa-contentview.mm
                                ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/macos/glfw-cocoa-contentview.h)
    elseif(UNIX AND NOT APPLE)
        set(NICEGRAF_SHADERC_PLATFORM linux)
	set(NGF_SAMPLES_BACKEND nicegraf-vk)
    else()
        message(FATAL_ERROR "Your platform is not currently supported by nicegraf samples.")
    endif()
    
    # Set the folder to hold all samples binaries.
    set(NGF_SAMPLES_OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/samples/binaries)



    # Custom target for generated shaders.
    file(GLOB shader_files ${CMAKE_CURRENT_LIST_DIR}/samples/shaders/*.hlsl)
    foreach(source_path ${shader_files})
      file(STRINGS ${source_path} tech_lines REGEX "// *T *: *([a-zA-Z0-9_]+)")
      if (tech_lines)
        set(tech_names "")
        foreach(tech_line ${tech_lines})
          string(REPLACE ":" ";" tmp ${tech_line})
          list(GET tmp 1 tmp)
          string(STRIP "${tmp}" tmp)
          string(REGEX REPLACE " +" ";" tmp ${tmp})
          list(GET tmp 0 tech_name)
          list(APPEND tech_names "${tech_name}")
        endforeach(tech_line)
        set(output_files_list "")
        get_filename_component(header_file_name ${source_path} NAME_WE)
        if(NOT ${header_file_name} MATCHES "compute-*")
          foreach(tech ${tech_names})
            list(APPEND output_files_list "${NGF_SAMPLES_OUTPUT_DIR}/shaders/${tech}.vs.21.msl")
            list(APPEND output_files_list "${NGF_SAMPLES_OUTPUT_DIR}/shaders/${tech}.ps.21.msl")
            list(APPEND output_files_list "${NGF_SAMPLES_OUTPUT_DIR}/shaders/${tech}.vs.spv")
            list(APPEND output_files_list "${NGF_SAMPLES_OUTPUT_DIR}/shaders/${tech}.ps.spv")
            list(APPEND output_files_list "${NGF_SAMPLES_OUTPUT_DIR}/shaders/${tech}.pipeline")
          endforeach(tech)
        else()
          foreach(tech ${tech_names})
            list(APPEND output_files_list "${NGF_SAMPLES_OUTPUT_DIR}/shaders/${tech}.cs.21.msl")
            list(APPEND output_files_list "${NGF_SAMPLES_OUTPUT_DIR}/shaders/${tech}.cs.spv")
            list(APPEND output_files_list "${NGF_SAMPLES_OUTPUT_DIR}/shaders/${tech}.pipeline")
          endforeach(tech)
        endif()
        list(APPEND output_files_list "${NGF_SAMPLES_OUTPUT_DIR}/shaders/${header_file_name}_binding_consts.h")
        add_custom_command(OUTPUT ${output_files_list}
                           MAIN_DEPENDENCY ${source_path}
                           COMMAND ${CMAKE_CURRENT_LIST_DIR}/samples/deps/niceshade/${NICEGRAF_SHADERC_PLATFORM}/niceshade ARGS ${source_path} "-t" "msl21" "-t" "spv" "-O" "${NGF_SAMPLES_OUTPUT_DIR}/shaders" "-h" "${header_file_name}_binding_consts.h"
                           WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/samples/shaders)
        set(generated_shaders_list "${output_files_list};${generated_shaders_list}")
      endif()
    endforeach(source_path)
    add_custom_target(sample-shaders DEPENDS ${generated_shaders_list})
    set_target_properties(sample-shaders PROPERTIES FOLDER "samples")

    string(TOUPPER "NGF_SAMPLES_BACKEND_${NGF_SAMPLES_BACKEND}" NGF_SAMPLES_BACKEND_DEFINE)
    string(REPLACE "-" "_" NGF_SAMPLES_BACKEND_DEFINE ${NGF_SAMPLES_BACKEND_DEFINE})

    set(NGF_IMGUI_SRCS ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui_draw.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui_tables.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui_widgets.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui_demo.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui.cpp
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/imgui.h
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/backends/imgui_impl_glfw.h
                       ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/backends/imgui_impl_glfw.cpp)
	       
    if(WIN32)
        set(NGF_IMGUI_COPTS "")
    else()
        # Turn off reporting warnings as errors for ImGui on gcc/clang, because it has a lot of them.
        set(NGF_IMGUI_COPTS "-Wno-error")
    endif()

    nmk_static_library(NAME ngf-imgui
                SRCS ${NGF_IMGUI_SRCS}
                DEPS glfw
                PUB_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui
                             ${CMAKE_CURRENT_LIST_DIR}/samples/deps/imgui/backends
	              PVT_DEFINES "GLFW_INCLUDE_NONE"
	            	COPTS ${NGF_IMGUI_COPTS})
    nmk_static_library(NAME ngf-samples-common
                       SRCS ${CMAKE_CURRENT_LIST_DIR}/samples/common/main.cpp
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/diagnostic-callback.cpp
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/shader-loader.cpp
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/file-utils.cpp
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/file-utils.h
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/shader-loader.h
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/sample-interface.h
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/logging.h
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/diagnostic-callback.h
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/imgui-backend.h
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/imgui-backend.cpp
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/targa-loader.h
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/targa-loader.cpp
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/mesh-loader.h
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/mesh-loader.cpp
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/staging-image.h
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/staging-image.cpp
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/camera-controller.h
                            ${CMAKE_CURRENT_LIST_DIR}/samples/common/camera-controller.cpp
                            ${NGF_PLATFORM_SOURCE}
                       DEPS ngf-imgui glfw
                       PUB_DEFINES  ${NGF_SAMPLES_BACKEND_DEFINE}
                       PUB_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/samples/common
                                    ${CMAKE_CURRENT_LIST_DIR}/samples/deps/nicemath)
    set_target_properties(ngf-samples-common PROPERTIES FOLDER "samples")

    function (ngf_sample)
        cmake_parse_arguments(SAMPLE "" "NAME" "" ${ARGN})
        file(GLOB_RECURSE SAMPLE_SRCS ${CMAKE_CURRENT_LIST_DIR}/samples/${SAMPLE_NAME}/*.cpp)
        nmk_binary(NAME ${SAMPLE_NAME}
                   SRCS ${SAMPLE_SRCS}
                   DEPS ngf-samples-common ${NGF_SAMPLES_BACKEND} nicegraf-util
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
endif()
