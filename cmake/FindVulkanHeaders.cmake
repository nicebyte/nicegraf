# Probe command-line arguments and the environment to see if they specify the
# Vulkan headers installation path.
if(NOT DEFINED VulkanHeaders_INCLUDE_DIR)
  if(NOT DEFINED VULKAN_HEADERS_INSTALL_DIR)
    if (DEFINED ENV{VULKAN_HEADERS_INSTALL_DIR})
      set(VULKAN_HEADERS_INSTALL_DIR "$ENV{VULKAN_HEADERS_INSTALL_DIR}")
    elseif(DEFINED ENV{VULKAN_SDK})
      set(VULKAN_HEADERS_INSTALL_DIR "$ENV{VULKAN_SDK}")
    endif()
  endif()

  if(DEFINED VULKAN_HEADERS_INSTALL_DIR)
    # When CMAKE_FIND_ROOT_PATH_INCLUDE is set to ONLY, the HINTS in find_path()
    # are re-rooted, which prevents VULKAN_HEADERS_INSTALL_DIR to work as
    # expected. So use NO_CMAKE_FIND_ROOT_PATH to avoid it.

    # Use HINTS instead of PATH to search these locations before
    # searching system environment variables like $PATH that may
    # contain SDK directories.

    find_path(VulkanHeaders_INCLUDE_DIR
        NAMES vulkan/vulkan.h
        HINTS ${VULKAN_HEADERS_INSTALL_DIR}/include
        NO_CMAKE_FIND_ROOT_PATH)
  else()
    # If VULKAN_HEADERS_INSTALL_DIR, or one of its variants was not specified,
    # do a normal search without hints.
    find_path(VulkanHeaders_INCLUDE_DIR NAMES vulkan/vulkan.h HINTS "${CMAKE_CURRENT_SOURCE_DIR}/external/Vulkan-Headers/include" NO_CMAKE_FIND_ROOT_PATH)
  endif()
endif()

set(VulkanHeaders_INCLUDE_DIRS ${VulkanHeaders_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VulkanHeaders
    DEFAULT_MSG
    VulkanHeaders_INCLUDE_DIR)

mark_as_advanced(VulkanHeaders_INCLUDE_DIR)
