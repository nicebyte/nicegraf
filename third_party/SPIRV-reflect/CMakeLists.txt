cmake_minimum_required(VERSION 3.14.0)
project(spvreflect)

set(CMAKE_C_STANDARD 99)

add_library(spvreflect STATIC
  ${CMAKE_CURRENT_LIST_DIR}/spirv.h
  ${CMAKE_CURRENT_LIST_DIR}/spirv_reflect.h
  ${CMAKE_CURRENT_LIST_DIR}/spirv_reflect.c)

target_include_directories(spvreflect PUBLIC ${CMAKE_CURRENT_LIST_DIR})