cmake_minimum_required(VERSION 3.0.2)

project(vma)


add_library(vma STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/vk_mem_alloc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/vk_mem_alloc.h)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(vma PRIVATE -Wno-nullability-completeness)
endif()

target_include_directories(vma PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../vulkan-headers)
target_include_directories(vma PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/src)
target_compile_definitions(vma PUBLIC VMA_STATS_STRING_ENABLED=0)
