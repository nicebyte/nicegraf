cmake_minimum_required(VERSION 3.0.2)

project(vma)

find_package(VulkanHeaders REQUIRED)

if ( ${VulkanHeaders_FOUND} )
    add_library(vma STATIC
        ${CMAKE_CURRENT_LIST_DIR}/src/vk_mem_alloc.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/vk_mem_alloc.h)

    target_include_directories(vma PRIVATE ${VulkanHeaders_INCLUDE_DIR})
    target_include_directories(vma PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/src)
else()
    message(WARNING "Vulkan headers not found - will not build VMA.")
endif()
