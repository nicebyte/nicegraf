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

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# These are the compiler flags that are used on all nicegraf targets.
if(WIN32)
    set (NICEGRAF_COMMON_COMPILE_OPTS "/W4" "/WX")
else()
    set(NICEGRAF_COMMON_COMPILE_OPTS "-Wall" "-Wconversion" "-Wno-unknown-pragmas" "-Werror")
endif()

# This function adds a new target and sets some configuration
# common across all targets.
function (ngf_target)
  cmake_parse_arguments(TGT "" "NAME;TYPE" "SRCS;DEPS;COPTS;PUB_INCLUDES;PVT_INCLUDES" ${ARGN})
  if (TGT_TYPE STREQUAL "lib")
    add_library(${TGT_NAME} STATIC ${TGT_SRCS})
  elseif(TGT_TYPE STREQUAL "exe")
    add_executable(${TGT_NAME} ${TGT_SRCS})
  else()
    message(FATAL_ERROR "invalid target type")
  endif()
  if ( TGT_DEPS )
    target_link_libraries(${TGT_NAME} ${TGT_DEPS})
  endif()
  target_include_directories(${TGT_NAME}
                             PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include ${TGT_PUB_INCLUDES}
                             PRIVATE ${CMAKE_CURRENT_LIST_DIR}/source ${TGT_PVT_INCLUDES})
  target_compile_options(${TGT_NAME} PRIVATE ${NICEGRAF_COMMON_COMPILE_OPTS})
  if ( TGT_COPTS )
    target_compile_options(${TGT_NAME} PRIVATE ${TGT_COPTS})
  endif()
endfunction()

# Shortcut for adding a new library target.
function (ngf_library)
   ngf_target(TYPE lib ${ARGN})
endfunction()

# Shortcut for adding a new executable target.
function (ngf_binary)
   ngf_target(TYPE exe ${ARGN})
endfunction()

# A helper library for parsing shader metadata produced by
# nicegraf-shaderc.
ngf_library(NAME ngf_metadata_parser
            SRCS ${CMAKE_CURRENT_LIST_DIR}/include/metadata_parser.h
                 ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/metadata_parser.c)

set(NGF_INTERNAL_SRCS ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf.h
                      ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/macros.h
                      ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/native_binding_map.h
                      ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/cmdbuf_state.h
                      ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/internal.c
                      ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/native_binding_map.c
                      ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/cmdbuf_state.c)

if (NOT APPLE)
    set(NGF_INTERNAL_SRCS ${NGF_INTERNAL_SRCS}
                          ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/stack_alloc.h
                          ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/block_alloc.h
                          ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/stack_alloc.c
                          ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/block_alloc.c)
endif()

# A library with various utilities shared internally across different backends.
ngf_library(NAME nicegraf-internal
            SRCS ${NGF_INTERNAL_SRCS}
            DEPS ngf_metadata_parser)

# nicegraf utility library.
ngf_library(NAME nicegraf-util
            SRCS ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf-util.h
                 ${CMAKE_CURRENT_LIST_DIR}/source/ngf-common/util.c
            DEPS nicegraf-internal)

# Common dependencies used by nicegraf on all platforms.
set(NICEGRAF_COMMON_DEPS nicegraf-internal ngf_metadata_parser)

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
  # Try searching for Vulkan SDK.
  find_package(VulkanHeaders)

  # Only build the Vulkan backend if the Vulkan SDK is present.
  if ( ${VulkanHeaders_FOUND} )
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
    target_include_directories(nicegraf_vk PRIVATE ${VulkanHeaders_INCLUDE_DIR})
  else()
    message(WARNING "Vulkan headers not found - will not build nicegraf-vk.")
  endif()
endif()

# Build tests only if explicitly requested.
if (NGF_BUILD_TESTS STREQUAL "yes")
    ngf_binary(NAME ngf_tests
               SRCS ${CMAKE_CURRENT_LIST_DIR}/tests/block_allocator_test.cpp
                    ${CMAKE_CURRENT_LIST_DIR}/tests/stack_allocator_test.cpp
                    ${CMAKE_CURRENT_LIST_DIR}/tests/dynamic_array_test.cpp
                    ${CMAKE_CURRENT_LIST_DIR}/tests/frame_token_test.cpp
                    ${CMAKE_CURRENT_LIST_DIR}/tests/main.cpp
               DEPS nicegraf-internal)
    target_include_directories(ngf_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/third_party/catch")
    if (NOT WIN32)
        target_link_libraries(ngf_tests pthread)
    endif()
endif()

# Build samples only if explicitly requested.
if (NGF_BUILD_SAMPLES STREQUAL "yes")
    # Set the folder to hold all samples binaries.
    set(NGF_SAMPLES_OUTPUT_DIR ${CMAKE_CURRENT_LIST_DIR}/samples-bin)

    # Ensure we have the shader compiler available.
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/samples/nicegraf-shaderc)
    if (TARGET nicegraf_shaderc)
        set_target_properties(nicegraf_shaderc PROPERTIES FOLDER "samples")
    else()
        message(FATAL_ERROR "Building samples requires nicegraf-shaderc. Make sure to run `git submodule update` from the repo root.")
    endif()

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
        foreach(tech ${tech_names})
          list(APPEND output_files_list "${NGF_SAMPLES_OUTPUT_DIR}/shaders/${tech}.vs.12.msl")
          list(APPEND output_files_list "${NGF_SAMPLES_OUTPUT_DIR}/shaders/${tech}.ps.12.msl")
          list(APPEND output_files_list "${NGF_SAMPLES_OUTPUT_DIR}/shaders/${tech}.vs.spv")
          list(APPEND output_files_list "${NGF_SAMPLES_OUTPUT_DIR}/shaders/${tech}.ps.spv")
          list(APPEND output_files_list "${NGF_SAMPLES_OUTPUT_DIR}/shaders/${tech}.pipeline")
        endforeach(tech)
        get_filename_component(header_file_name ${source_path} NAME_WE)
        list(APPEND output_files_list "${NGF_SAMPLES_OUTPUT_DIR}/shaders/${header_file_name}_binding_consts.h")
        add_custom_command(OUTPUT ${output_files_list}
                           MAIN_DEPENDENCY ${source_path}
                           DEPENDS nicegraf_shaderc
                           COMMAND ${CMAKE_CURRENT_LIST_DIR}/samples/nicegraf-shaderc/nicegraf_shaderc ARGS ${source_path} "-t" "msl12" "-t" "spv" "-O" "${NGF_SAMPLES_OUTPUT_DIR}/shaders" "-h" "${header_file_name}_binding_consts.h"
                           WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/samples/shaders)
        set(generated_shaders_list "${output_files_list};${generated_shaders_list}")
      endif()
    endforeach(source_path)
    add_custom_target(sample-shaders DEPENDS ${generated_shaders_list})
    set_target_properties(sample-shaders PROPERTIES FOLDER "samples")

    # Determine the backend to build the samples with based on the platform.
    if (WIN32)
        set(NGF_SAMPLES_BACKEND nicegraf_vk)
        set(NGF_SAMPLES_PLATFORM_SOURCE ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/windows/window.cpp)
    else()
        #set(NGF_SAMPLES_BACKEND nicegraf_mtl)
        message(FATAL_ERROR "Your platform is not currently supported by nicegraf samples.")
    endif()
    string(TOUPPER "NGF_SAMPLES_BACKEND_${NGF_SAMPLES_BACKEND}" NGF_SAMPLES_BACKEND_DEFINE)

    ngf_library(NAME ngf-samples-common
                SRCS ${CMAKE_CURRENT_LIST_DIR}/samples/common/main.cpp
                     ${CMAKE_CURRENT_LIST_DIR}/samples/common/diagnostic-callback.cpp
                     ${CMAKE_CURRENT_LIST_DIR}/samples/common/shader-loader.cpp
                     ${CMAKE_CURRENT_LIST_DIR}/samples/common/file-utils.cpp
                     ${CMAKE_CURRENT_LIST_DIR}/samples/common/file-utils.h
                     ${CMAKE_CURRENT_LIST_DIR}/samples/common/shader-loader.h
                     ${CMAKE_CURRENT_LIST_DIR}/samples/common/sample-interface.h
                     ${CMAKE_CURRENT_LIST_DIR}/samples/common/nicegraf-exception.h
                     ${CMAKE_CURRENT_LIST_DIR}/samples/common/logging.h
                     ${CMAKE_CURRENT_LIST_DIR}/samples/common/diagnostic-callback.h
                     ${CMAKE_CURRENT_LIST_DIR}/samples/common/platform/window.h
                     ${NGF_SAMPLES_PLATFORM_SOURCE}
                PUB_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/samples/common)
    set_target_properties(ngf-samples-common PROPERTIES FOLDER "samples")
    target_compile_definitions(ngf-samples-common PUBLIC ${NGF_SAMPLES_BACKEND_DEFINE})

    function (ngf_sample)
        cmake_parse_arguments(SAMPLE "" "NAME" "" ${ARGN})
        file(GLOB_RECURSE SAMPLE_SRCS ${CMAKE_CURRENT_LIST_DIR}/samples/${SAMPLE_NAME}/*.cpp)
        ngf_binary(NAME ${SAMPLE_NAME}
                   SRCS ${SAMPLE_SRCS}
                   DEPS ngf-samples-common ${NGF_SAMPLES_BACKEND} nicegraf-util  
                   PVT_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/samples/${SAMPLE_NAME})
        add_dependencies(${SAMPLE_NAME} sample-shaders)
        set_target_properties(${SAMPLE_NAME} PROPERTIES FOLDER "samples")
        set_target_properties(${SAMPLE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${NGF_SAMPLES_OUTPUT_DIR}")
        set_target_properties(${SAMPLE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG "${NGF_SAMPLES_OUTPUT_DIR}")
        set_target_properties(${SAMPLE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${NGF_SAMPLES_OUTPUT_DIR}")
        set_target_properties(${SAMPLE_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${NGF_SAMPLES_OUTPUT_DIR}")
    endfunction()
    ngf_sample(NAME 00-template)
    ngf_sample(NAME 01-fullscreen-triangle)
    ngf_sample(NAME 08-render-to-texture)
endif()
