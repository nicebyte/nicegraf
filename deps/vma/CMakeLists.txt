cmake_minimum_required(VERSION 3.0.2)

project(vma)


add_library(vma STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/vk_mem_alloc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/vk_mem_alloc.h)

target_include_directories(vma PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../vulkan-headers)
target_include_directories(vma PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/src)
