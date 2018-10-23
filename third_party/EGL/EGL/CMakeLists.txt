cmake_minimum_required(VERSION 3.0.2)

if(WIN32)
  set(EGL_PLATFORM_SOURCES
      ${CMAKE_CURRENT_LIST_DIR}/src/egl_windows.c)
else()
  if(UNIX AND NOT APPLE)
    set(EGL_PLATFORM_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/src/egl_x11.c)
  endif()
endif()

set(EGL_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/egl.c
    ${CMAKE_CURRENT_LIST_DIR}/src/egl_common.c
    ${EGL_PLATFORM_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/src/egl_internal.h
    ${CMAKE_CURRENT_LIST_DIR}/include/EGL/egl.h
    ${CMAKE_CURRENT_LIST_DIR}/include/EGL/egl.h
    ${CMAKE_CURRENT_LIST_DIR}/include/EGL/eglext.h
    ${CMAKE_CURRENT_LIST_DIR}/include/EGL/eglplatform.h
    ${CMAKE_CURRENT_LIST_DIR}/include/KHR/khrplatform.h)

add_library(egl STATIC
    ${EGL_SOURCES})

target_include_directories(egl PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include)
add_definitions(-DEGLAPI=)
option(EGL_NO_GLEW "Do not use GLEW" ON)
if(EGL_NO_GLEW)
  add_definitions(-DEGL_NO_GLEW)
endif()
