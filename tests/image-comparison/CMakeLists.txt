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
project(nicegraf-imagetests)


set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)

include("${CMAKE_CURRENT_LIST_DIR}/../../build-utils.cmake")

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../ "${CMAKE_CURRENT_BINARY_DIR}/nicegraf")
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../misc/common ${CMAKE_CURRENT_BINARY_DIR}/misc-common)

nmk_static_library(NAME nicegraf-imagetest-framework
                   SRCS ${CMAKE_CURRENT_LIST_DIR}/framework/main.cpp
                   DEPS nicegraf-vk nicegraf-misc-common
                   PUB_DEFINES  ${NGF_IMAGE_COMPARISON_BACKEND_DEFINE}
                   PUB_INCLUDES ${CMAKE_CURRENT_LIST_DIR}/framework)

function (ngf_imagetest_testcase)
	cmake_parse_arguments(TEST "" "NAME" "SRCS" ${ARGN})
	nmk_binary(NAME ${TEST_NAME}
			   SRCS ${TEST_SRCS}
			   DEPS nicegraf-imagetest-framework nicegraf-util nicegraf-misc-common
			   OUTPUT_DIR "${NGF_SAMPLES_OUTPUT_DIR}")
	#add_dependencies(${SAMPLE_NAME} sample-shaders)                   
	set_target_properties(${TEST_NAME} PROPERTIES FOLDER "testcases")
endfunction()

ngf_imagetest_testcase(NAME triangle SRCS ${CMAKE_CURRENT_LIST_DIR}/testcases/ngf-triangle-test.cpp)

