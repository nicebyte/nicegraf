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
project(nicegraf)

#TODO: separate targets for different backend
set(NGF_PLATFORM "GL")
set(NICEGRAF_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf.h
    ${CMAKE_CURRENT_LIST_DIR}/source/nicegraf_internal.h
    ${CMAKE_CURRENT_LIST_DIR}/source/nicegraf_internal.c)

set(EGL_NO_GLEW ON)

if(WIN32)
set (NICEGRAF_COMPILE_OPTS "/W4" "/WX")
endif()

if("${NGF_PLATFORM}" STREQUAL "GL")
  set(NICEGRAF_SOURCES
    ${NICEGRAF_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/source/nicegraf_impl_gl43.c
    ${CMAKE_CURRENT_LIST_DIR}/source/gl_43_core.c)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/third_party/EGL/EGL)
  set(NICEGRAF_DEPS
      ${NICEGRAF_DEPS}
      egl)
elseif("${NGF_PLATFORM}" STREQUAL "VK")
  find_package(Vulkan REQUIRED)
  get_property(VK_INCLUDE_PATH TARGET Vulkan::Vulkan PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
  add_definitions("-DVMA_STATIC_VULKAN_FUNCTIONS=0")
  set(NICEGRAF_SOURCES
    ${NICEGRAF_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/source/nicegraf_impl_vk.c)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/third_party/volk)
  add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/third_party/vma)
  set(NICEGRAF_DEPS
      ${NICEGRAF_DEPS}
      volk
      vma)
  set(NICEGRAF_PRIVATE_INCLUDES ${VK_INCLUDE_PATH})
endif()

set(NICEGRAF_UTIL_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/include/nicegraf_util.h
    ${CMAKE_CURRENT_LIST_DIR}/source/nicegraf_internal.c
    ${CMAKE_CURRENT_LIST_DIR}/source/nicegraf_util.c)

add_library (nicegraf ${NICEGRAF_SOURCES})
target_include_directories(nicegraf PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include)
target_include_directories(nicegraf PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/source
	${NICEGRAF_PRIVATE_INCLUDES})
target_link_libraries(nicegraf ${NICEGRAF_DEPS})
target_compile_options(nicegraf PRIVATE ${NICEGRAF_COMPILE_OPTS})

add_library (nicegraf_util ${NICEGRAF_UTIL_SOURCES})
target_compile_options(nicegraf_util PRIVATE ${NICEGRAF_COMPILE_OPTS})
target_include_directories(nicegraf_util PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(nicegraf_util nicegraf)

