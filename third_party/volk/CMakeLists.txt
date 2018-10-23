cmake_minimum_required(VERSION 3.0.2)

project(volk)

find_package(Vulkan REQUIRED)

get_property(VK_INCLUDE_PATH TARGET Vulkan::Vulkan PROPERTY INTERFACE_INCLUDE_DIRECTORIES)

add_library(volk STATIC
    ${CMAKE_CURRENT_LIST_DIR}/volk.h
    ${CMAKE_CURRENT_LIST_DIR}/volk.c)

target_include_directories(volk PRIVATE ${VK_INCLUDE_PATH})

if (CMAKE_SYSTEM_NAME STREQUAL "Windows")
  target_compile_definitions(volk PRIVATE VK_USE_PLATFORM_WIN32_KHR
      VK_KHR_win32_surface)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_compile_definitions(volk PRIVATE VK_USE_PLATFORM_XCB_KHR
      VK_KHR_xcb_surface)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
  target_compile_definitions(volk PRIVATE VK_USE_PLATFORM_ANDROID_KHR
      VK_KHR_android_surface)
else()
  message(FATAL_ERROR "Volk can't be cofigured for " ${CMAKE_SYSTEM_NAME})
endif()
target_include_directories(volk PUBLIC
    ${CMAKE_CURRENT_LIST_DIR})
