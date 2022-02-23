#[[
Copyright (c) 2022 nicegraf contributors

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

# This function adds a new target and sets some configuration options for it.
# Parameters:
#  TYPE - type of the target. Must be one of: 
#    - `lib`, for a static library;
#    - `hdr`, for a header-only library;
#    - `exe`, for an executable binary.
#  SRCS - a list of source files for the target.
#  COPTS - a list of compiler options.
#  PVT_INCLUDES - a list of paths to add to this target's include paths.
#  PUB_INCLUDES - a list of paths to add to the include paths of all targets depending on this target.
#  PVT_DEFINES  - a list of preprocessor definitions to add for this target.
#  PUB_DEFINES  - a list of preprocessor definitions to add to all targets depending on this target.
#  OUTPUT_DIR   - the path to the folder where the output for this target shall be stored.
function (nmk_target)
	cmake_parse_arguments(TGT "" "NAME;TYPE" "SRCS;DEPS;COPTS;PUB_INCLUDES;PVT_INCLUDES;PUB_DEFINES;PVT_DEFINES;OUTPUT_DIR;VS_DEBUGGER_WORKING_DIR" ${ARGN})
  if (TGT_TYPE STREQUAL "lib")
    add_library(${TGT_NAME} STATIC ${TGT_SRCS})
  elseif(TGT_TYPE STREQUAL "hdr")    
    add_library(${TGT_NAME} INTERFACE ${TGT_SRCS})
  elseif(TGT_TYPE STREQUAL "exe")
    add_executable(${TGT_NAME} ${TGT_SRCS})
  else()
    message(FATAL_ERROR "invalid target type")
  endif()
  if ( TGT_DEPS )
    target_link_libraries(${TGT_NAME} ${TGT_DEPS})
  endif()
  if ( TGT_TYPE STREQUAL "hdr")
    target_include_directories(${TGT_NAME}
                               INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include ${TGT_PUB_INCLUDES})
  else()
    target_include_directories(${TGT_NAME}
                               PRIVATE ${CMAKE_CURRENT_LIST_DIR}/source ${TGT_PVT_INCLUDES})  
    target_include_directories(${TGT_NAME}
                               PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include ${TGT_PUB_INCLUDES})  
  endif()
  target_compile_options(${TGT_NAME} PRIVATE ${NICEMAKE_COMMON_COMPILE_OPTS})
  target_compile_definitions(${TGT_NAME} PUBLIC ${TGT_PUB_DEFINES} PRIVATE ${TGT_PVT_DEFINES})
  if ( TGT_COPTS )
    target_compile_options(${TGT_NAME} PRIVATE ${TGT_COPTS})
  endif()
  if( TGT_OUTPUT_DIR )
    set_target_properties(${TGT_NAME} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${TGT_OUTPUT_DIR}")
    set_target_properties(${TGT_NAME} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_DEBUG "${TGT_OUTPUT_DIR}")
    set_target_properties(${TGT_NAME} PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY_RELEASE "${TGT_OUTPUT_DIR}")  
    set_target_properties(${TGT_NAME} PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${TGT_OUTPUT_DIR}")      
  endif()
endfunction()

# Shortcut for adding a new library target.
function (nmk_static_library)
   nmk_target(TYPE lib ${ARGN})
endfunction()

# Shortcut for adding a new header-only library target.
function (nmk_header_library)
   nmk_target(TYPE hdr ${ARGN})
endfunction()

# Shortcut for adding a new executable target.
function (nmk_binary)
   nmk_target(TYPE exe ${ARGN})
endfunction()
