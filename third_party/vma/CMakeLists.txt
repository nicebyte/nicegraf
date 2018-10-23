cmake_minimum_required(VERSION 3.0.2)

project(vma)

find_package(Vulkan REQUIRED)
get_property(VK_INCLUDE_PATH TARGET Vulkan::Vulkan PROPERTY INTERFACE_INCLUDE_DIRECTORIES)

add_library(vma STATIC
    ${CMAKE_CURRENT_LIST_DIR}/src/vk_mem_alloc.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/vk_mem_alloc.h)

target_include_directories(vma PRIVATE ${VK_INCLUDE_PATH})
target_include_directories(vma PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/src)
