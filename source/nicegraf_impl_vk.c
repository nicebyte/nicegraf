/**
Copyright (c) 2019 nicegraf contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "nicegraf.h"

#include "dynamic_array.h"
#include "nicegraf_internal.h"

#if defined(_WIN32)||defined(_WIN64)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define VK_SURFACE_EXT "VK_KHR_win32_surface"
#define VK_CREATE_SURFACE_FN vkCreateWin32SurfaceKHR
#define VK_CREATE_SURFACE_FN_TYPE PFN_vkCreateWin32SurfaceKHR
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__ANDROID__)
#define VK_SURFACE_EXT "VK_KHR_android_surface"
#define VK_CREATE_SURFACE_FN vkCreateAndroidSurfaceKHR
#define VK_CREATE_SURFACE_FN_TYPE PFN_vkCreateAndroidSurfaceKHR
#define VK_USE_PLATFORM_ANDROID_KHR
#else
#include <xcb/xcb.h>
#include <dlfcn.h>
#include <X11/Xlib-xcb.h>
#define VK_SURFACE_EXT "VK_KHR_xcb_surface"
#define VK_CREATE_SURFACE_FN vkCreateXcbSurfaceKHR
#define VK_CREATE_SURFACE_FN_TYPE PFN_vkCreateXcbSurfaceKHR
#define VK_USE_PLATFORM_XCB_KHR
#endif

#include "volk.h"
#include <vk_mem_alloc.h>

#include <assert.h>
#include <string.h>
#if defined(WIN32)
#include <malloc.h>
#else
#include <alloca.h>
#endif

#if defined(WIN32)
#if defined(alloca)
#undef alloca
#endif
#define alloca _malloca
#define freea _freea
#endif

#define _NGF_INVALID_IDX (~0u)

struct {
  VkInstance instance;
  VkPhysicalDevice phys_dev;
  pthread_mutex_t ctx_refcount_mut;
  bool initialized;
} _vk;

typedef struct {
  VkDevice device;
  VmaAllocator allocator;
  VkQueue gfx_queue;
  VkQueue present_queue;
  VkCommandPool cmd_pool;
  uint32_t refcount;
  uint32_t gfx_family_idx;
  uint32_t present_family_idx;
} _ngf_context_shared_state;

typedef struct {
  VkSwapchainKHR vk_swapchain;
  VkImage *images;
  VkSemaphore *image_semaphores;
  uint32_t num_images;
  uint32_t image_idx;
} _ngf_swapchain;

typedef struct _ngf_frame_sync_data {
  _NGF_DARRAY_OF(VkSemaphore) wait_vksemaphores;
  _NGF_DARRAY_OF(VkCommandBuffer) retire_vkcmdbuffers;
  uint32_t frame_idx;
  bool active;
} _ngf_frame_sync_data;

struct ngf_context {
  _ngf_frame_sync_data *frame_sync;
  ngf_swapchain_info swapchain_info;
  _ngf_context_shared_state **shared_state;
  VkSurfaceKHR surface;
  VkFence *frame_fences;
  uint32_t frame_number;
  uint32_t max_inflight_frames;
  _ngf_swapchain swapchain;
};

struct ngf_shader_stage {
  VkShaderModule vkmodule;
  VkShaderStageFlagBits vkstage;
};

struct ngf_graphics_pipeline {
  VkPipeline vkpipeline;
};

struct ngf_pipeline_layout {
  VkPipelineLayout vklayout;
};

struct ngf_image {
  VkImage vkimg;
  VmaAllocation alloc;
};

struct ngf_cmd_buffer {
  VkCommandBuffer vkcmdbuf;
  VkSemaphore vksem;
  bool recording;
};

NGF_THREADLOCAL ngf_context *CURRENT_CONTEXT = NULL;
#define CURRENT_SHARED_STATE (*(CURRENT_CONTEXT->shared_state))

static VkSampleCountFlagBits get_vk_sample_count(uint32_t sample_count) {
  switch(sample_count) {
  case 0u:
  case 1u: return VK_SAMPLE_COUNT_1_BIT;
  case 2u: return VK_SAMPLE_COUNT_2_BIT;
  case 4u: return VK_SAMPLE_COUNT_4_BIT;
  case 8u: return VK_SAMPLE_COUNT_8_BIT;
  case 16u: return VK_SAMPLE_COUNT_16_BIT;
  case 32u: return VK_SAMPLE_COUNT_32_BIT;
  case 64u: return VK_SAMPLE_COUNT_64_BIT;
  default: assert(false); // TODO: return error?
  }
  return VK_SAMPLE_COUNT_1_BIT;
}

static VkImageType get_vk_image_type(ngf_image_type t) {
  static const VkImageType types[NGF_IMAGE_TYPE_COUNT] = {
    VK_IMAGE_TYPE_2D,
    VK_IMAGE_TYPE_3D
  };
  return types[t];
}

static VkCompareOp get_vk_compare_op(ngf_compare_op op) {
  static const VkCompareOp ops[NGF_COMPARE_OP_COUNT] = {
    VK_COMPARE_OP_NEVER,
    VK_COMPARE_OP_LESS,
    VK_COMPARE_OP_LESS_OR_EQUAL,
    VK_COMPARE_OP_EQUAL,
    VK_COMPARE_OP_GREATER_OR_EQUAL,
    VK_COMPARE_OP_GREATER,
    VK_COMPARE_OP_NOT_EQUAL,
    VK_COMPARE_OP_ALWAYS
  };
  return ops[op];
}

static VkStencilOp get_vk_stencil_op(ngf_stencil_op op) {
  static const VkStencilOp ops[NGF_STENCIL_OP_COUNT] = {
    VK_STENCIL_OP_KEEP,
    VK_STENCIL_OP_ZERO,
    VK_STENCIL_OP_REPLACE,
    VK_STENCIL_OP_INCREMENT_AND_CLAMP,
    VK_STENCIL_OP_INCREMENT_AND_WRAP,
    VK_STENCIL_OP_DECREMENT_AND_CLAMP,
    VK_STENCIL_OP_DECREMENT_AND_WRAP,
    VK_STENCIL_OP_INVERT
  };
  return ops[op];
}

static VkBlendFactor get_vk_blend_factor(ngf_blend_factor f) {
  VkBlendFactor factors[NGF_BLEND_FACTOR_COUNT] = {
    VK_BLEND_FACTOR_ZERO,
    VK_BLEND_FACTOR_ONE,
    VK_BLEND_FACTOR_SRC_COLOR,
    VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    VK_BLEND_FACTOR_DST_COLOR,
    VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    VK_BLEND_FACTOR_SRC_ALPHA,
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    VK_BLEND_FACTOR_DST_ALPHA,
    VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
    VK_BLEND_FACTOR_CONSTANT_COLOR,
    VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
    VK_BLEND_FACTOR_CONSTANT_ALPHA,
    VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA
  };
  return factors[f];
}

static VkFormat get_vk_image_format(ngf_image_format f) {
  static VkFormat formats[NGF_IMAGE_FORMAT_COUNT] = {
    VK_FORMAT_R8_UNORM,
    VK_FORMAT_R8G8_UNORM,
    VK_FORMAT_R8G8B8_UNORM,
    VK_FORMAT_R8G8B8A8_UNORM,
    VK_FORMAT_R8G8B8_SRGB,
    VK_FORMAT_R8G8B8A8_SRGB,
    VK_FORMAT_B8G8R8_UNORM,
    VK_FORMAT_B8G8R8A8_UNORM,
    VK_FORMAT_B8G8R8_SRGB,
    VK_FORMAT_B8G8R8A8_SRGB,
    VK_FORMAT_R32_SFLOAT,
    VK_FORMAT_R32G32_SFLOAT,
    VK_FORMAT_R32G32B32_SFLOAT,
    VK_FORMAT_R32G32B32A32_SFLOAT,
    VK_FORMAT_R16_SFLOAT,
    VK_FORMAT_R16G16_SFLOAT,
    VK_FORMAT_R16G16B16_SFLOAT,
    VK_FORMAT_R16G16B16A16_SFLOAT,
    VK_FORMAT_D32_SFLOAT,
    VK_FORMAT_D16_UNORM,
    VK_FORMAT_D24_UNORM_S8_UINT,
    VK_FORMAT_UNDEFINED
  };
  return formats[f];
}

static VkPolygonMode get_vk_polygon_mode(ngf_polygon_mode m) {
  static const VkPolygonMode modes[NGF_POLYGON_MODE_COUNT] = {
    VK_POLYGON_MODE_FILL,
    VK_POLYGON_MODE_LINE,
    VK_POLYGON_MODE_POINT
  };
  return modes[m];
}

static VkDynamicState get_vk_dynamic_state(ngf_dynamic_state_flags s) {
  switch(s) {
  case NGF_DYNAMIC_STATE_VIEWPORT: return VK_DYNAMIC_STATE_VIEWPORT;
  case NGF_DYNAMIC_STATE_SCISSOR: return VK_DYNAMIC_STATE_SCISSOR;
  case NGF_DYNAMIC_STATE_LINE_WIDTH: return VK_DYNAMIC_STATE_LINE_WIDTH;
  case NGF_DYNAMIC_STATE_BLEND_CONSTANTS:
    return VK_DYNAMIC_STATE_BLEND_CONSTANTS;
  case NGF_DYNAMIC_STATE_STENCIL_REFERENCE:
    return NGF_DYNAMIC_STATE_STENCIL_REFERENCE;
  case NGF_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
    return NGF_DYNAMIC_STATE_STENCIL_COMPARE_MASK;
  case NGF_DYNAMIC_STATE_STENCIL_WRITE_MASK:
    return NGF_DYNAMIC_STATE_STENCIL_WRITE_MASK;
  default: assert(false);
  }
  return VK_DYNAMIC_STATE_VIEWPORT; // can't be reached
}

static VkCullModeFlagBits get_vk_cull_mode(ngf_cull_mode m) {
  static const VkCullModeFlagBits modes[NGF_CULL_MODE_COUNT] = {
    VK_CULL_MODE_BACK_BIT,
    VK_CULL_MODE_FRONT_BIT,
    VK_CULL_MODE_FRONT_AND_BACK
  };
  return modes[m];
}

static VkFrontFace get_vk_front_face(ngf_front_face_mode f) {
  static const VkFrontFace modes[NGF_FRONT_FACE_COUNT] = {
    VK_FRONT_FACE_COUNTER_CLOCKWISE,
    VK_FRONT_FACE_CLOCKWISE
  };
  return modes[f];
}

static VkPrimitiveTopology get_vk_primitive_type(ngf_primitive_type p) {
  static const VkPrimitiveTopology topos[NGF_PRIMITIVE_TYPE_COUNT] = {
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
    VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
    VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
    VK_PRIMITIVE_TOPOLOGY_LINE_STRIP
  };
  return topos[p];
}

static VkFormat get_vk_vertex_format(ngf_type type, uint32_t size, bool norm) {
  static const VkFormat normalized_formats[4][4] = {
    {
      VK_FORMAT_R8_SNORM, VK_FORMAT_R8G8_SNORM, VK_FORMAT_R8G8B8_SNORM,
      VK_FORMAT_R8G8B8A8_SNORM
    },
    {
      VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8_UNORM,
      VK_FORMAT_R8G8B8A8_UNORM
    },
    {
      VK_FORMAT_R16_SNORM, VK_FORMAT_R16G16_SNORM, VK_FORMAT_R16G16B16_SNORM,
      VK_FORMAT_R16G16B16A16_SNORM
    },
    {
      VK_FORMAT_R16_UNORM, VK_FORMAT_R16G16_UNORM, VK_FORMAT_R16G16B16_UNORM,
      VK_FORMAT_R16G16B16A16_UNORM
    }
  };
  static const VkFormat formats[9][4] = {
    {
      VK_FORMAT_R8_SINT, VK_FORMAT_R8G8_SINT, VK_FORMAT_R8G8B8_SINT,
      VK_FORMAT_R8G8B8A8_SINT
    },
    {
      VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8B8_UINT,
      VK_FORMAT_R8G8B8A8_UINT
    },
    {
      VK_FORMAT_R16_SINT, VK_FORMAT_R16G16_SINT, VK_FORMAT_R16G16B16_SINT,
      VK_FORMAT_R16G16B16A16_SINT
    },
    {
      VK_FORMAT_R16_UINT, VK_FORMAT_R16G16_UINT, VK_FORMAT_R16G16B16_UINT,
      VK_FORMAT_R16G16B16A16_UINT
    },
    {
      VK_FORMAT_R32_SINT, VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32B32_SINT,
      VK_FORMAT_R32G32B32A32_SINT
    },
    {
      VK_FORMAT_R32_UINT, VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32B32_UINT,
      VK_FORMAT_R32G32B32A32_UINT
    },
    {
      VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT,
      VK_FORMAT_R32G32B32A32_SFLOAT
    },
    {
      VK_FORMAT_R16_SFLOAT, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R16G16B16_SFLOAT,
      VK_FORMAT_R16G16B16A16_SFLOAT
    },
    {
      VK_FORMAT_R64_SFLOAT, VK_FORMAT_R64G64_SFLOAT, VK_FORMAT_R64G64B64_SFLOAT,
      VK_FORMAT_R64G64B64A64_SFLOAT
    }
  };

  if ((size < 1 || size > 4) || norm && type > NGF_TYPE_UINT16) {
   return VK_FORMAT_UNDEFINED;
  } else if (norm) {
    return normalized_formats[type][size];
  } else {
    return formats[type][size];
  }
}

static VkVertexInputRate get_vk_input_rate(ngf_input_rate r) {
  static const VkVertexInputRate rates[NGF_INPUT_RATE_COUNT] = {
    VK_VERTEX_INPUT_RATE_VERTEX,
    VK_VERTEX_INPUT_RATE_INSTANCE
  };
  return rates[r];
}

static VkShaderStageFlagBits get_vk_shader_stage(ngf_stage_type s) {
  static const VkShaderStageFlagBits stages[NGF_STAGE_COUNT] = {
    VK_SHADER_STAGE_VERTEX_BIT,
    VK_SHADER_STAGE_FRAGMENT_BIT
  };
  return stages[s];
}

ngf_error ngf_initialize(ngf_device_preference pref) {
  if (!_vk.initialized) { // Vulkan not initialized yet.
    // Initialize Volk.
    volkInitialize();
    const char * const ext_names[] = {
      "VK_KHR_surface",
      VK_SURFACE_EXT
    };

    // Create a Vulkan instance.
    VkInstanceCreateInfo inst_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .pApplicationInfo = NULL, //TODO: app info
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = NULL, //TODO: validation layers
      .enabledExtensionCount = NGF_ARRAYSIZE(ext_names),
      .ppEnabledExtensionNames = ext_names 
    };
    VkResult vk_err = vkCreateInstance(&inst_info, NULL, &_vk.instance);
    if (vk_err != VK_SUCCESS) {
      return NGF_ERROR_CONTEXT_CREATION_FAILED;
    }
    volkLoadInstance(_vk.instance);

    // Pick a suitable physical device.
    uint32_t nphysdev;
    vkEnumeratePhysicalDevices(_vk.instance, &nphysdev, NULL);
    VkPhysicalDevice *physdevs = alloca(nphysdev * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(_vk.instance, &nphysdev, physdevs);
    uint32_t best_device_score = 0U;
    uint32_t best_device_idx = _NGF_INVALID_IDX;
    for (uint32_t i = 0; i < nphysdev; ++i) {
      VkPhysicalDeviceProperties dev_props;
      vkGetPhysicalDeviceProperties(physdevs[i], &dev_props);
      uint32_t score = 0U;
      switch(dev_props.deviceType) {
      case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        score += 100U;
        if (pref == NGF_DEVICE_PREFERENCE_DISCRETE) score += 1000U;
        break;
      case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        score += 90U;
        if (pref == NGF_DEVICE_PREFERENCE_INTEGRATED) score += 1000U;
        break;
      case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        score += 80U;
        break;
      case VK_PHYSICAL_DEVICE_TYPE_CPU:
        score += 70U;
        break;
      default: score += 10U;
      }
      if (score > best_device_score) {
        best_device_idx = i;
      }
    }
    if (best_device_idx == _NGF_INVALID_IDX) {
      return NGF_ERROR_INITIALIZATION_FAILED;
    }
    _vk.phys_dev = physdevs[best_device_idx];

		// Initialize context refcount mutex.
		pthread_mutex_init(&_vk.ctx_refcount_mut, NULL);

    // Done!
    _vk.initialized = true;
  }
  return NGF_ERROR_OK;
}

static void _ngf_destroy_swapchain(
    const _ngf_context_shared_state *shared_state,
    _ngf_swapchain *swapchain) {
  assert(swapchain);
  for (uint32_t s = 0u;
       swapchain->image_semaphores != NULL && s < swapchain->num_images; ++s) {
    if (swapchain->image_semaphores[s] != VK_NULL_HANDLE) {
      vkDestroySemaphore(shared_state->device, swapchain->image_semaphores[s],
                         NULL);
    }
  }
  if (swapchain->image_semaphores != NULL) {
    NGF_FREEN(swapchain->image_semaphores, swapchain->num_images);
  }
  if (swapchain->vk_swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(shared_state->device, swapchain->vk_swapchain, NULL);
  }
}

static ngf_error _ngf_create_swapchain(
    const ngf_swapchain_info *swapchain_info,
    VkSurfaceKHR surface,
    const _ngf_context_shared_state *shared_state,
    _ngf_swapchain *swapchain) {
  assert(swapchain_info);
  assert(shared_state);
  assert(swapchain);

  ngf_error err = NGF_ERROR_OK;
  VkResult vk_err = VK_SUCCESS;

  // Create swapchain.
  assert(shared_state->present_family_idx != _NGF_INVALID_IDX);
  const bool single_queue =
    (shared_state->gfx_family_idx == shared_state->present_family_idx);
  const VkSharingMode sharing_mode =
    single_queue ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
  const uint32_t num_sharing_queue_families = single_queue ? 0 : 2;
  const uint32_t sharing_queue_families[] = {
    shared_state->gfx_family_idx,
    shared_state->present_family_idx };
  static const VkPresentModeKHR vk_present_modes[] = {
    VK_PRESENT_MODE_FIFO_KHR,
    VK_PRESENT_MODE_IMMEDIATE_KHR
  };
  uint32_t nformats;
  vkGetPhysicalDeviceSurfaceFormatsKHR(_vk.phys_dev, surface, &nformats, NULL);
  VkSurfaceFormatKHR *formats = alloca(sizeof(VkSurfaceFormatKHR) * nformats);
  vkGetPhysicalDeviceSurfaceFormatsKHR(_vk.phys_dev, surface, &nformats,
                                        formats);
  const VkFormat requested_format = get_vk_image_format(swapchain_info->cfmt);
  const VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  if (!(nformats == 1 && formats[0].format == VK_FORMAT_UNDEFINED)) {
    bool found = false;
    for (size_t f = 0; !found && f < nformats; ++f)
      found = formats[f].format == requested_format;
    if (!found) {
      err = NGF_ERROR_INVALID_SURFACE_FORMAT;
      goto _ngf_create_swapchain_cleanup;
    }
  }
  VkSurfaceCapabilitiesKHR surface_caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_vk.phys_dev, surface,
                                            &surface_caps);
  const VkSwapchainCreateInfoKHR vk_sc_info = {
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .pNext = NULL,
    .flags = 0,
    .surface = surface,
    .minImageCount = swapchain_info->capacity_hint,
    .imageFormat = requested_format,
    .imageColorSpace = color_space,
    .imageExtent = {
      .width = swapchain_info->width,
      .height = swapchain_info->height
    },
    .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .imageSharingMode = sharing_mode,
    .queueFamilyIndexCount = num_sharing_queue_families,
    .pQueueFamilyIndices = sharing_queue_families,
    .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
    .compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    .presentMode = vk_present_modes[swapchain_info->present_mode]
  };
  vk_err = vkCreateSwapchainKHR(shared_state->device, &vk_sc_info, NULL,
                                &swapchain->vk_swapchain);
  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_SWAPCHAIN_CREATION_FAILED;
    goto _ngf_create_swapchain_cleanup;
  }

  // Obtain swapchain images.
  vk_err =
      vkGetSwapchainImagesKHR(shared_state->device, swapchain->vk_swapchain,
                              &swapchain->num_images, NULL);
  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_SWAPCHAIN_CREATION_FAILED;
    goto _ngf_create_swapchain_cleanup;
  }
  swapchain->images = NGF_ALLOCN(VkImage, swapchain->num_images);
  if (swapchain->images == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto _ngf_create_swapchain_cleanup;
  }
  vk_err =
      vkGetSwapchainImagesKHR(shared_state->device, swapchain->vk_swapchain,
                              &swapchain->num_images, swapchain->images);
  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_SWAPCHAIN_CREATION_FAILED;
    goto _ngf_create_swapchain_cleanup;
  }

  // Create semaphores to be signaled when a swapchain image becomes available.
  swapchain->image_semaphores = NGF_ALLOCN(VkSemaphore, swapchain->num_images);
  if (swapchain->image_semaphores == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto _ngf_create_swapchain_cleanup;
  }
  for (uint32_t s = 0u; s < swapchain->num_images; ++s) {
    const VkSemaphoreCreateInfo sem_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0
    };
    vk_err =
        vkCreateSemaphore(shared_state->device, &sem_info, NULL,
                          &swapchain->image_semaphores[s]);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_SWAPCHAIN_CREATION_FAILED;
      goto _ngf_create_swapchain_cleanup;
    }
  }
  swapchain->image_idx = 0U;

_ngf_create_swapchain_cleanup:
  if (err != NGF_ERROR_OK) {
    _ngf_destroy_swapchain(shared_state, swapchain);
  }
  return err;
}

ngf_error ngf_create_context(const ngf_context_info *info,
                             ngf_context **result) {
  assert(info);
  assert(result);
  ngf_error err = NGF_ERROR_OK;
  VkResult vk_err = VK_SUCCESS;
  const ngf_swapchain_info *swapchain_info = info->swapchain_info;
  const ngf_context *shared = info->shared_context;

  if (swapchain_info != NULL &&
      shared != NULL &&
      (*(shared->shared_state))->present_queue == VK_NULL_HANDLE) {
    return NGF_ERROR_CANT_SHARE_CONTEXT;
  }

  *result = NGF_ALLOC(ngf_context);
  ngf_context *ctx = *result;
  if (ctx == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_context_cleanup;
  }
  memset(ctx, 0, sizeof(ngf_context));

  // If a swapchain was requested, create the window surface.
  if (swapchain_info != NULL) {
#if defined(_WIN32) || defined(_WIN64)
    const VkWin32SurfaceCreateInfoKHR surface_info = {
      .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
      .pNext = NULL,
      .flags = 0,
      .hinstance = GetModuleHandle(NULL),
      .hwnd = (HWND)swapchain_info->native_handle
    };
#elif defined(__ANDROID__)
    const VkAndroidSuraceCreateInfoKHR surface_info = {
      .sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
      .pNext = NULL,
      .flags = 0,
      .window = swapchain_info->native_handle
    };
#else
    static xcb_connection_t* (*GetXCBConnection)(Display*) = NULL;
    if (GetXCBConnection == NULL) { // dynamically load XGetXCBConnection
      void *libxcb = dlopen("libX11-xcb.so.1", RTLD_LAZY);
      GetXCBConnection = dlsym(libxcb, "XGetXCBConnection");
    }
    const VkXcbSurfaceCreateInfoKHR surface_info = {
      .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .pNext = NULL,
      .flags = 0,
      .window = swapchain_info->native_handle,
      .connection = GetXCBConnection(XOpenDisplay(NULL))
    };
#endif
    vk_err =
      VK_CREATE_SURFACE_FN(_vk.instance, &surface_info, NULL, &ctx->surface);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_SURFACE_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }
  }

  if (shared != NULL) {
		pthread_mutex_lock(&_vk.ctx_refcount_mut);
		_ngf_context_shared_state *shared_state = *(shared->shared_state);
    if (shared_state == NULL) {
      /* shared context might have been deleted */
      err = NGF_ERROR_CONTEXT_CREATION_FAILED;
      pthread_mutex_unlock(&_vk.ctx_refcount_mut);
      goto ngf_create_context_cleanup;
    }
    shared_state->refcount++;
    ctx->shared_state = shared->shared_state;
		pthread_mutex_unlock(&_vk.ctx_refcount_mut);
  } else {
    ctx->shared_state = NGF_ALLOC(_ngf_context_shared_state*);
    if (ctx->shared_state == NULL) {
      err = NGF_ERROR_OUTOFMEM;
      goto ngf_create_context_cleanup;
    }
    *(ctx->shared_state) = NGF_ALLOC(_ngf_context_shared_state);
    _ngf_context_shared_state *shared_state = *(ctx->shared_state);
    if (shared_state == NULL) {
      err = NGF_ERROR_OUTOFMEM;
      goto ngf_create_context_cleanup;
    }
    memset(shared_state, 0, sizeof(_ngf_context_shared_state));
    shared_state->refcount = 1;

    // Pick suitable queue families for graphics and present.
    uint32_t num_queue_families = 0U;
    vkGetPhysicalDeviceQueueFamilyProperties(_vk.phys_dev,
                                             &num_queue_families,
                                             NULL);
    VkQueueFamilyProperties *queue_families =
        alloca(num_queue_families * sizeof(VkQueueFamilyProperties));
    shared_state->gfx_family_idx = _NGF_INVALID_IDX;
    shared_state->present_family_idx = _NGF_INVALID_IDX;
    vkGetPhysicalDeviceQueueFamilyProperties(_vk.phys_dev,
                                             &num_queue_families,
                                              queue_families);
    for (uint32_t q = 0; q < num_queue_families; ++q) {
      const VkQueueFlags flags = queue_families[q].queueFlags;
      if (shared_state->gfx_family_idx == _NGF_INVALID_IDX &&
          (flags & VK_QUEUE_GRAPHICS_BIT)) {
        shared_state->gfx_family_idx = q;
      }
      VkBool32 present_supported = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(_vk.phys_dev,
                                            q,
                                            ctx->surface,
                                            &present_supported);
      if (shared_state->present_family_idx == _NGF_INVALID_IDX &&
          present_supported == VK_TRUE) {
        shared_state->present_family_idx = q;
      }
    }
    if (shared_state->gfx_family_idx == _NGF_INVALID_IDX ||
        (swapchain_info != NULL &&
         shared_state->present_family_idx == _NGF_INVALID_IDX)) {
      err = NGF_ERROR_INITIALIZATION_FAILED;
      goto ngf_create_context_cleanup;
    }

    // Create logical device.
    const float queue_prio = 1.0f;
    const VkDeviceQueueCreateInfo gfx_queue_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .queueFamilyIndex = shared_state->gfx_family_idx,
      .queueCount = 1,
      .pQueuePriorities = &queue_prio
    };
    const VkDeviceQueueCreateInfo present_queue_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .queueFamilyIndex = shared_state->present_family_idx,
      .queueCount = 1,
      .pQueuePriorities = &queue_prio
    };
    const VkDeviceQueueCreateInfo queue_infos[] =
        { gfx_queue_info, present_queue_info };
    const bool single_queue_family =
      shared_state->gfx_family_idx == shared_state->present_family_idx ||
      shared_state->present_family_idx == -1;
    const uint32_t num_queue_infos = single_queue_family ? 1 : 2;
    const char *device_exts[] = { "VK_KHR_swapchain" };
    const VkDeviceCreateInfo dev_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .queueCreateInfoCount = num_queue_infos,
      .pQueueCreateInfos = queue_infos,
      .enabledLayerCount = 0,
      .ppEnabledLayerNames = NULL, // TODO: validation layers
      .enabledExtensionCount = shared_state->present_family_idx == -1 ? 0 : 1,
      .ppEnabledExtensionNames = device_exts
    };
    vk_err = vkCreateDevice(_vk.phys_dev, &dev_info, NULL,
                             &shared_state->device);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_CONTEXT_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }

    // Obtain queue handles.
    vkGetDeviceQueue(shared_state->device,
                     shared_state->gfx_family_idx, 0,
                     &shared_state->gfx_queue);
    vkGetDeviceQueue(shared_state->device,
                     shared_state->present_family_idx, 0,
                     &shared_state->present_queue);

    // Set up VMA.
    VmaVulkanFunctions vma_vk_fns = {
      .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
      .vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
      .vkAllocateMemory = vkAllocateMemory,
      .vkFreeMemory = vkFreeMemory,
      .vkMapMemory = vkMapMemory,
      .vkUnmapMemory = vkUnmapMemory,
      .vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
      .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
      .vkBindBufferMemory = vkBindBufferMemory,
      .vkBindImageMemory = vkBindImageMemory,
      .vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
      .vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
      .vkCreateBuffer = vkCreateBuffer,
      .vkDestroyBuffer = vkDestroyBuffer,
      .vkCreateImage = vkCreateImage,
      .vkDestroyImage = vkDestroyImage
    };
    VmaAllocatorCreateInfo vma_info = {
      .flags = 0u,
      .physicalDevice = _vk.phys_dev,
      .device = shared_state->device,
      .preferredLargeHeapBlockSize = 0u,
      .pAllocationCallbacks = NULL,
      .pDeviceMemoryCallbacks = NULL,
      .frameInUseCount = 0u,
      .pHeapSizeLimit = NULL,
      .pVulkanFunctions = &vma_vk_fns,
      .pRecordSettings = NULL
    };
    vk_err = vmaCreateAllocator(&vma_info, &shared_state->allocator);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_CONTEXT_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }
    // Create a command pool.
    VkCommandPoolCreateInfo cmd_pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .pNext = NULL,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = shared_state->gfx_family_idx
    };
    vk_err = vkCreateCommandPool(shared_state->device, &cmd_pool_info, NULL,
                                 &shared_state->cmd_pool);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_CONTEXT_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }
  }

  if (swapchain_info != NULL) {
    err = _ngf_create_swapchain(swapchain_info, ctx->surface,
                                *(ctx->shared_state), &ctx->swapchain);
    if (err != NGF_ERROR_OK) goto ngf_create_context_cleanup;
    ctx->swapchain_info = *swapchain_info;
  }
  ctx->frame_number = 0u;

  const uint32_t max_inflight_frames =
      swapchain_info ? ctx->swapchain.num_images : 3u;
  ctx->max_inflight_frames = max_inflight_frames;
  ctx->frame_sync = NGF_ALLOCN(_ngf_frame_sync_data, max_inflight_frames);
  if (ctx->frame_sync == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_context_cleanup;
  }
  // Create fences to signal when a frame is done rendering.
  ctx->frame_fences = NGF_ALLOCN(VkFence, max_inflight_frames);
  if (ctx->frame_fences == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_context_cleanup;
  }

  for (uint32_t f = 0u; f < max_inflight_frames; ++f) {
    _NGF_DARRAY_RESET(ctx->frame_sync[f].wait_vksemaphores,
                      max_inflight_frames);
    _NGF_DARRAY_RESET(ctx->frame_sync[f].retire_vkcmdbuffers,
                      max_inflight_frames);
    ctx->frame_sync[f].frame_idx = 0u;
    ctx->frame_sync[f].active = false;
    const VkFenceCreateInfo fence_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0u
    };
    vk_err = vkCreateFence((*ctx->shared_state)->device, &fence_info, NULL,
                           &ctx->frame_fences[f]);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_CONTEXT_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }
  }

ngf_create_context_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_context(ctx);
  }
  return err;
}

ngf_error ngf_resize_context(ngf_context *ctx,
                             uint32_t new_width,
                             uint32_t new_height) {
  assert(ctx);
  ngf_error err = NGF_ERROR_OK;
  _ngf_destroy_swapchain(*(ctx->shared_state), &ctx->swapchain);
  ctx->swapchain_info.width = new_width;
  ctx->swapchain_info.height = new_height;
  err = _ngf_create_swapchain(&ctx->swapchain_info, ctx->surface,
                              *(ctx->shared_state), &ctx->swapchain);
  return err;
}

void ngf_destroy_context(ngf_context *ctx) {
  if (ctx != NULL) {
    // TODO: wait till device idle

    if (ctx->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(_vk.instance, ctx->surface, NULL);
    }

	  pthread_mutex_lock(&_vk.ctx_refcount_mut);
    _ngf_context_shared_state *shared_state = *(ctx->shared_state);
    if (shared_state != NULL) {
      _ngf_destroy_swapchain(*(ctx->shared_state), &ctx->swapchain);
      shared_state->refcount--;
      if (shared_state->refcount == 0) {
        if (shared_state->device != VK_NULL_HANDLE) {
          vkDestroyDevice(shared_state->device, NULL);
        }
        if (shared_state->allocator != VK_NULL_HANDLE) {
          vmaDestroyAllocator(shared_state->allocator);
        }
        NGF_FREE(shared_state);
        *(ctx->shared_state) = NULL;
      } 
    }
    for (uint32_t f = 0u; f < ctx->max_inflight_frames; ++f) {
      _NGF_DARRAY_DESTROY(ctx->frame_sync[f].retire_vkcmdbuffers);
      _NGF_DARRAY_DESTROY(ctx->frame_sync[f].wait_vksemaphores);
      if (ctx->frame_fences != NULL) {
        vkDestroyFence(shared_state->device, ctx->frame_fences[f], NULL);
      }

    }
    if (ctx->frame_fences != NULL) {
      NGF_FREEN(ctx->frame_fences, ctx->max_inflight_frames);
    }
    NGF_FREEN(ctx->frame_sync, ctx->max_inflight_frames);
    pthread_mutex_unlock(&_vk.ctx_refcount_mut);
    if (CURRENT_CONTEXT == ctx) CURRENT_CONTEXT = NULL;
    NGF_FREE(ctx);
  }
}

ngf_error ngf_set_context(ngf_context *ctx) {
  CURRENT_CONTEXT = ctx;
  return NGF_ERROR_OK;
}

ngf_error ngf_create_cmd_buffer(const ngf_cmd_buffer_info *info,
                                ngf_cmd_buffer **result) {
  assert(info);
  assert(result);
  _NGF_FAKE_USE(info);
  ngf_cmd_buffer *cmd_buf = NGF_ALLOC(ngf_cmd_buffer);
  *result = cmd_buf;
  cmd_buf->vkcmdbuf = VK_NULL_HANDLE;
  cmd_buf->vksem = VK_NULL_HANDLE;
  cmd_buf->recording = false;
  return NGF_ERROR_OK;
}

ngf_error ngf_start_cmd_buffer(ngf_cmd_buffer *cmd_buf) {
  assert(cmd_buf);
  ngf_error err = NGF_ERROR_OK;
  VkResult vk_err = VK_SUCCESS;
  if (cmd_buf->recording) {
    err = NGF_ERROR_CMD_BUFFER_ALREADY_RECORDING;
    goto ngf_start_cmd_buffer_cleanup;
  }
  if (cmd_buf->vkcmdbuf == VK_NULL_HANDLE) {
    VkCommandBufferAllocateInfo vk_cmdbuf_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext = NULL,
      .commandPool = CURRENT_SHARED_STATE->cmd_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1u
    };
    vk_err = vkAllocateCommandBuffers(CURRENT_SHARED_STATE->device,
                                      &vk_cmdbuf_info, &cmd_buf->vkcmdbuf);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_OUTOFMEM; // TODO: return appropriate error.
      goto ngf_start_cmd_buffer_cleanup;
    }
    VkCommandBufferBeginInfo cmd_buf_begin = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext = NULL,
      .flags = 0,
      .pInheritanceInfo = NULL
    };
    vkBeginCommandBuffer(cmd_buf->vkcmdbuf, &cmd_buf_begin);
  } else {
    vkResetCommandBuffer(cmd_buf->vkcmdbuf,
                         VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
  }
  if (cmd_buf->vksem == VK_NULL_HANDLE) {
    VkSemaphoreCreateInfo vk_sem_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0u
    };
    vk_err = vkCreateSemaphore(CURRENT_SHARED_STATE->device, &vk_sem_info, NULL,
                               &cmd_buf->vksem);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_OUTOFMEM; // TODO: return appropriate error code
      goto ngf_start_cmd_buffer_cleanup;
    }
  }
  cmd_buf->recording = true;
ngf_start_cmd_buffer_cleanup:
  return err;
}

ngf_error ngf_end_cmd_buffer(ngf_cmd_buffer *cmd_buf) {
  assert(cmd_buf);
  if (!cmd_buf->recording) {
    return NGF_ERROR_CMD_BUFFER_WAS_NOT_RECORDING;
  }
  vkEndCommandBuffer(cmd_buf->vkcmdbuf);
  cmd_buf->recording = false;
  return NGF_ERROR_OK;
}

ngf_error ngf_submit_cmd_buffer(uint32_t nbuffers, ngf_cmd_buffer **bufs) {
  assert(bufs);
  uint32_t fi =
      CURRENT_CONTEXT->frame_number % CURRENT_CONTEXT->max_inflight_frames;
  _ngf_frame_sync_data *frame_sync_data = &CURRENT_CONTEXT->frame_sync[fi];
  for (uint32_t i = 0u; i < nbuffers; ++i) {
    if (bufs[i]->recording) {
      return NGF_ERROR_CMD_BUFFER_ALREADY_RECORDING; // TODO: return appropriate error code
    }
    _NGF_DARRAY_APPEND(frame_sync_data->wait_vksemaphores, bufs[i]->vksem);
    _NGF_DARRAY_APPEND(frame_sync_data->retire_vkcmdbuffers,
                       bufs[i]->vkcmdbuf);
    bufs[i]->vkcmdbuf = VK_NULL_HANDLE;
    bufs[i]->vksem = VK_NULL_HANDLE;
  }
  return NGF_ERROR_OK;
}
ngf_error ngf_begin_frame(ngf_context *ctx) {
  ngf_error err = NGF_ERROR_OK;
  uint32_t fi = ctx->frame_number % ctx->swapchain.num_images;
  if (ctx->swapchain.vk_swapchain != VK_NULL_HANDLE) {
    const VkResult vk_err =
        vkAcquireNextImageKHR((*(ctx->shared_state))->device,
                              ctx->swapchain.vk_swapchain,
                              UINT64_MAX,
                              ctx->swapchain.image_semaphores[fi],
                              VK_NULL_HANDLE,
                              &ctx->swapchain.image_idx);
    if (vk_err != VK_SUCCESS) err = NGF_ERROR_BEGIN_FRAME_FAILED;
  }
  CURRENT_CONTEXT->frame_sync[fi].active = true;
  _NGF_DARRAY_CLEAR(CURRENT_CONTEXT->frame_sync[fi].retire_vkcmdbuffers);
  _NGF_DARRAY_CLEAR(CURRENT_CONTEXT->frame_sync[fi].wait_vksemaphores);
  return err;
}

ngf_error ngf_end_frame(ngf_context *ctx) {
  ngf_error err = NGF_ERROR_OK;

  const uint32_t fi =
      CURRENT_CONTEXT->frame_number % CURRENT_CONTEXT->max_inflight_frames;
  const _ngf_frame_sync_data *frame_sync = &CURRENT_CONTEXT->frame_sync[fi];

  // Submit the pending command buffers.
  const VkPipelineStageFlags color_attachment_stage =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  uint32_t wait_sem_count = 0u;
  VkSemaphore *wait_sems = NULL;
  const VkPipelineStageFlags *wait_stage_masks = NULL;
  if (CURRENT_CONTEXT->swapchain.vk_swapchain != VK_NULL_HANDLE) {
    wait_sem_count = 1u;
    wait_sems =
      &CURRENT_CONTEXT->swapchain.image_semaphores[fi];
    wait_stage_masks = &color_attachment_stage; 
  }
  VkSubmitInfo submit_info = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext = NULL,
    .waitSemaphoreCount = wait_sem_count,
    .pWaitSemaphores = wait_sems,
    .pWaitDstStageMask = wait_stage_masks,
    .commandBufferCount = _NGF_DARRAY_SIZE(frame_sync->retire_vkcmdbuffers),
    .pCommandBuffers = frame_sync->retire_vkcmdbuffers.data,
    .signalSemaphoreCount = _NGF_DARRAY_SIZE(frame_sync->wait_vksemaphores),
    .pSignalSemaphores = frame_sync->wait_vksemaphores.data
  };
  vkQueueSubmit(CURRENT_SHARED_STATE->gfx_queue, 1u, &submit_info,
                ctx->frame_fences[fi]);

  if (ctx->swapchain.vk_swapchain != VK_NULL_HANDLE) {
    VkResult present_result = VK_SUCCESS;
    VkPresentInfoKHR present_info = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .pNext = NULL,
      .waitSemaphoreCount =
          (uint32_t)_NGF_DARRAY_SIZE(frame_sync->wait_vksemaphores),
      .pWaitSemaphores = frame_sync->wait_vksemaphores.data,
      .swapchainCount = 1,
      .pSwapchains = &ctx->swapchain.vk_swapchain,
      .pImageIndices = &ctx->swapchain.image_idx,
      .pResults = &present_result
    };
    vkQueuePresentKHR((*(ctx->shared_state))->present_queue, &present_info);
    if (present_result != VK_SUCCESS) 
      err = NGF_ERROR_END_FRAME_FAILED;
  } 

  // Retire resources
  uint32_t next_fi = (fi + 1u) % ctx->max_inflight_frames;
  _ngf_frame_sync_data *next_frame_sync = &ctx->frame_sync[next_fi];
  if (next_frame_sync->active) {
    vkWaitForFences(CURRENT_SHARED_STATE->device, 1u,
                    &ctx->frame_fences[next_fi], true, 1000000u);
    vkResetFences(CURRENT_SHARED_STATE->device, 1u,
                  &ctx->frame_fences[next_fi]);
  }
  next_frame_sync->active = false;
  vkFreeCommandBuffers(CURRENT_SHARED_STATE->device,
                       CURRENT_SHARED_STATE->cmd_pool,
                       _NGF_DARRAY_SIZE(next_frame_sync->retire_vkcmdbuffers),
                       next_frame_sync->retire_vkcmdbuffers.data);
  _NGF_DARRAY_CLEAR(next_frame_sync->retire_vkcmdbuffers);

  for (uint32_t s = 0u;
       s < _NGF_DARRAY_SIZE(next_frame_sync->wait_vksemaphores);
       ++s) {
    vkDestroySemaphore(CURRENT_SHARED_STATE->device,
                       _NGF_DARRAY_AT(next_frame_sync->wait_vksemaphores, s),
                       NULL);
  }
  _NGF_DARRAY_CLEAR(next_frame_sync->wait_vksemaphores);

  ctx->frame_number++;
  return err;
}

ngf_error ngf_create_shader_stage(const ngf_shader_stage_info *info,
                                  ngf_shader_stage **result) {
  assert(info);
  assert(result);

  *result = NGF_ALLOC(ngf_shader_stage);
  ngf_shader_stage *stage = *result;
  if (stage == NULL) {
    return NGF_ERROR_OUTOFMEM;
  }
  VkShaderModuleCreateInfo vk_sm_info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0u,
    .pCode = (uint32_t*) info->content,
    .codeSize = (info->content_length << 2)
  };
  VkResult vkerr =
    vkCreateShaderModule((*(CURRENT_CONTEXT->shared_state))->device,
                         &vk_sm_info, NULL, &stage->vkmodule);
  if (vkerr != VK_SUCCESS) {
    NGF_FREE(stage);
    return NGF_ERROR_CREATE_SHADER_STAGE_FAILED;
  }
  stage->vkstage = get_vk_shader_stage(info->type);
  return NGF_ERROR_OK;
}

void ngf_destroy_shader_stage(ngf_shader_stage *stage) {
  if (stage) {
    vkDestroyShaderModule(CURRENT_SHARED_STATE->device, stage->vkmodule, NULL);
    NGF_FREE(stage);
  }
}

ngf_error ngf_create_graphics_pipeline(const ngf_graphics_pipeline_info *info,
                                       ngf_graphics_pipeline **result) {
  assert(info);
  assert(result);
  VkVertexInputBindingDescription *vk_binding_descs = NULL;
  VkVertexInputAttributeDescription *vk_attrib_descs = NULL;
  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_graphics_pipeline);
  ngf_graphics_pipeline *pipeline = *result;
  if (pipeline == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_graphics_pipeline_cleanup;
  }

  // Prepare shader stages.
  VkPipelineShaderStageCreateInfo vk_shader_stages[5];
  assert(NGF_ARRAYSIZE(vk_shader_stages) == NGF_ARRAYSIZE(info->shader_stages));
  if (info->nshader_stages >= NGF_ARRAYSIZE(vk_shader_stages)) {
    err = NGF_ERROR_OUT_OF_BOUNDS;
    goto ngf_create_graphics_pipeline_cleanup;
  }
  for (uint32_t s = 0u; s < info->nshader_stages; ++s) {
    vk_shader_stages[s].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vk_shader_stages[s].pNext = NULL;
    vk_shader_stages[s].flags = 0u;
    vk_shader_stages[s].stage = info->shader_stages[s]->vkstage;
    vk_shader_stages[s].module = info->shader_stages[s]->vkmodule;
    vk_shader_stages[s].pName = NULL;
    vk_shader_stages[s].pSpecializationInfo = NULL;
  }

  // Prepare vertex input.
  vk_binding_descs = NGF_ALLOCN(VkVertexInputBindingDescription,
                                info->input_info->nvert_buf_bindings);
  vk_attrib_descs = NGF_ALLOCN(VkVertexInputAttributeDescription,
                               info->input_info->nattribs);

  if (vk_binding_descs == NULL || vk_attrib_descs == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_graphics_pipeline_cleanup;
  }

  for (uint32_t i = 0u; i < info->input_info->nvert_buf_bindings; ++i) {
    vk_binding_descs[i].binding =
        info->input_info->vert_buf_bindings[i].binding;
    vk_binding_descs[i].stride =
        info->input_info->vert_buf_bindings[i].stride;
    vk_binding_descs[i].inputRate = get_vk_input_rate(
        info->input_info->vert_buf_bindings[i].input_rate);
  }

  for (uint32_t i = 0u; i < info->input_info->nattribs; ++i) {
    vk_attrib_descs[i].location = info->input_info->attribs[i].location;
    vk_attrib_descs[i].binding = info->input_info->attribs[i].binding;
    vk_attrib_descs[i].offset = info->input_info->attribs[i].offset;
    vk_attrib_descs[i].format = get_vk_vertex_format(
        info->input_info->attribs[i].type,
        info->input_info->attribs[i].size,
        info->input_info->attribs[i].normalized);
  }

  VkPipelineVertexInputStateCreateInfo vertex_input = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0u,
    .vertexBindingDescriptionCount = info->input_info->nvert_buf_bindings,
    .pVertexBindingDescriptions = vk_binding_descs,
    .vertexAttributeDescriptionCount = info->input_info->nattribs,
    .pVertexAttributeDescriptions = vk_attrib_descs 
  };

  // Prepare input assembly.
  VkPipelineInputAssemblyStateCreateInfo input_assembly = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0u,
    .topology = get_vk_primitive_type(info->primitive_type),
    .primitiveRestartEnable = VK_FALSE
  };

  // Prepare tessellation state.
  VkPipelineTessellationStateCreateInfo tess = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0u,
    .patchControlPoints = 1u
  };

  // Prepare viewport/scissor state.
  VkViewport viewport = {
    .x = (float)info->viewport->x,
    .y = (float)info->viewport->y,
    .width = (float)info->viewport->width,
    .height = (float)info->viewport->height,
    .minDepth = info->depth_stencil->min_depth,
    .maxDepth = info->depth_stencil->max_depth,
  };
  VkRect2D scissor = {
    .offset = {
      .x = info->scissor->x,
      .y = info->scissor->y,
    },
    .extent = {
      .width = info->scissor->width,
      .height = info->scissor->height,
    }
  };
  VkPipelineViewportStateCreateInfo viewport_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0u,
    .viewportCount = 1u,
    .scissorCount = 1u,
    .pViewports = &viewport,
    .pScissors = &scissor
  };

  // Prepare rasterization state.
  VkPipelineRasterizationStateCreateInfo rasterization = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0u,
    .flags = 0u,
    .depthClampEnable = VK_FALSE,
    .rasterizerDiscardEnable = info->rasterization->discard,
    .polygonMode = get_vk_polygon_mode(info->rasterization->polygon_mode),
    .cullMode = get_vk_cull_mode(info->rasterization->cull_mode),
    .frontFace = get_vk_front_face(info->rasterization->front_face),
    .depthBiasEnable = VK_FALSE,
    .depthBiasConstantFactor = 0.0f,
    .depthBiasClamp = 0.0f,
    .depthBiasSlopeFactor = 0.0f,
    .lineWidth = info->rasterization->line_width
  };

  // Prepare multisampling.
  // TODO: use proper number of samples
  // TODO: use specified alpha-to-coverage
  VkPipelineMultisampleStateCreateInfo multisampling = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0u,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    .sampleShadingEnable = VK_FALSE,
    .minSampleShading = 0.0f,
    .pSampleMask = NULL,
    .alphaToCoverageEnable = VK_FALSE,
    .alphaToOneEnable = VK_FALSE
  };

  // Prepare depth/stencil.
  VkPipelineDepthStencilStateCreateInfo depth_stencil = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0u,
    .depthTestEnable = info->depth_stencil->depth_test,
    .depthWriteEnable = info->depth_stencil->depth_write,
    .depthCompareOp = get_vk_compare_op(info->depth_stencil->depth_compare),
    .depthBoundsTestEnable = VK_FALSE,
    .stencilTestEnable = info->depth_stencil->stencil_test,
    .front = {
      .failOp = get_vk_stencil_op(info->depth_stencil->front_stencil.fail_op),
      .passOp = get_vk_stencil_op(info->depth_stencil->front_stencil.pass_op),
      .depthFailOp =
          get_vk_stencil_op(info->depth_stencil->front_stencil.depth_fail_op),
      .compareOp =
          get_vk_compare_op(info->depth_stencil->front_stencil.compare_op),
      .compareMask = info->depth_stencil->front_stencil.compare_mask,
      .writeMask = info->depth_stencil->front_stencil.write_mask,
      .reference = info->depth_stencil->front_stencil.reference
    },
    .back = {
      .failOp = get_vk_stencil_op(info->depth_stencil->back_stencil.fail_op),
      .passOp = get_vk_stencil_op(info->depth_stencil->back_stencil.pass_op),
      .depthFailOp =
          get_vk_stencil_op(info->depth_stencil->back_stencil.depth_fail_op),
      .compareOp =
          get_vk_compare_op(info->depth_stencil->back_stencil.compare_op),
      .compareMask = info->depth_stencil->back_stencil.compare_mask,
      .writeMask = info->depth_stencil->back_stencil.write_mask,
      .reference = info->depth_stencil->back_stencil.reference
    },
    .minDepthBounds = 0.0f,
    .maxDepthBounds = 0.0f
  };

  // Prepare blend state.
  VkPipelineColorBlendAttachmentState attachment_blend_state = {
    .blendEnable = info->blend->enable,
    .srcColorBlendFactor = get_vk_blend_factor(info->blend->sfactor),
    .dstColorBlendFactor = get_vk_blend_factor(info->blend->dfactor),
    .colorBlendOp = VK_BLEND_OP_ADD,
    .srcAlphaBlendFactor = get_vk_blend_factor(info->blend->sfactor),
    .dstAlphaBlendFactor = get_vk_blend_factor(info->blend->dfactor),
    .alphaBlendOp = VK_BLEND_OP_ADD,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | //TODO: set color write mask
                      VK_COLOR_COMPONENT_G_BIT |
                      VK_COLOR_COMPONENT_B_BIT |
                      VK_COLOR_COMPONENT_A_BIT
  };

  VkPipelineColorBlendStateCreateInfo color_blend = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0u,
    .logicOpEnable = VK_FALSE,
    .logicOp = VK_LOGIC_OP_SET,
    .attachmentCount = 1u, // TODO: use number of attachments in renderpass
    .pAttachments = &attachment_blend_state,
    .blendConstants = { 0.0f, .0f, .0f, .0f }
  };

  // Dynamic state.
  VkDynamicState dynamic_states[7];
  uint32_t ndynamic_states = 0u;
  for (ngf_dynamic_state_flags s = NGF_DYNAMIC_STATE_VIEWPORT;
       s <= NGF_DYNAMIC_STATE_STENCIL_WRITE_MASK;
       s = (s << 1u)) {
    if (info->dynamic_state_mask & s) {
      dynamic_states[ndynamic_states++] = get_vk_dynamic_state(s);
    }
  }
  VkPipelineDynamicStateCreateInfo dynamic_state = {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0u,
    .dynamicStateCount = ndynamic_states,
    .pDynamicStates = dynamic_states
  };
  // TODO: assert layout is not NULL
  // TODO: compatible renderpass
  VkGraphicsPipelineCreateInfo vk_pipeline_info = {
    .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0u,
    .stageCount = info->nshader_stages,
    .pStages = vk_shader_stages,
    .pVertexInputState = &vertex_input,
    .pInputAssemblyState = &input_assembly,
    .pTessellationState = &tess,
    .pViewportState = &viewport_state,
    .pRasterizationState = &rasterization,
    .pMultisampleState = &multisampling,
    .pDepthStencilState = &depth_stencil,
    .pColorBlendState = &color_blend,
    .pDynamicState = &dynamic_state,
    .layout = VK_NULL_HANDLE, // TODO set layout
    .renderPass = VK_NULL_HANDLE,
    .subpass = 0u,
    .basePipelineHandle = VK_NULL_HANDLE,
    .basePipelineIndex = -1
  };

  VkResult vkerr =
      vkCreateGraphicsPipelines((*(CURRENT_CONTEXT->shared_state))->device,
                                VK_NULL_HANDLE,
                                1u,
                                &vk_pipeline_info,
                                NULL,
                                &pipeline->vkpipeline);

  if (vkerr != VK_SUCCESS) {
    err = NGF_ERROR_FAILED_TO_CREATE_PIPELINE;
    pipeline->vkpipeline = VK_NULL_HANDLE;
    goto ngf_create_graphics_pipeline_cleanup;
  }

ngf_create_graphics_pipeline_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_graphics_pipeline(pipeline);
  }
  NGF_FREE(vk_binding_descs);
  NGF_FREE(vk_attrib_descs);
  return err;  
}

void ngf_destroy_graphics_pipeline(ngf_graphics_pipeline *p) {
  vkDestroyPipeline((*(CURRENT_CONTEXT->shared_state))->device,
                     p->vkpipeline, NULL);
}


ngf_error ngf_create_image(const ngf_image_info *info, ngf_image **result) {
  assert(info);
  assert(result);
  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_image);
  ngf_image *img = *result;
  if (img == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_image_cleanup;
  }

  VkImageUsageFlagBits usage_flags = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (info->usage_hint & NGF_IMAGE_USAGE_SAMPLE_FROM) {
    usage_flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
  }
  if (info->usage_hint & NGF_IMAGE_USAGE_ATTACHMENT) {
    if (info->format == NGF_IMAGE_FORMAT_DEPTH32 ||
        info->format == NGF_IMAGE_FORMAT_DEPTH16 ||
        info->format == NGF_IMAGE_FORMAT_DEPTH24_STENCIL8) {
      usage_flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    } else {
      usage_flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
  }
  // TODO: add "written from CPU" usage hint?

  VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkImageCreateInfo vk_image_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .pNext = NULL,
    .flags = 0u,
    .imageType = get_vk_image_type(info->type),
    .extent = {
      .width = info->extent.width,
      .height = info->extent.height,
      .depth = info->extent.depth
    },
    .format = get_vk_image_format(info->format),
    .mipLevels = info->nmips,
    .arrayLayers = 1u, // TODO: layered images
    .samples =  get_vk_sample_count(info->nsamples),
    .usage = usage_flags,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0u,
    .pQueueFamilyIndices = NULL,
    .initialLayout = layout
  };

  VmaAllocationCreateInfo vma_alloc_info = {
    .flags = 0u,
    .usage = VMA_MEMORY_USAGE_GPU_ONLY,
    .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    .preferredFlags = 0u,
    .memoryTypeBits = 0u,
    .pool = VK_NULL_HANDLE,
    .pUserData = NULL
  };

  VkResult vkerr;
  vkerr = vmaCreateImage(CURRENT_SHARED_STATE->allocator, &vk_image_info,
                         &vma_alloc_info,
                         &img->vkimg, &img->alloc, NULL);
  if (vkerr != VK_SUCCESS) {
   err = NGF_ERROR_IMAGE_CREATION_FAILED;
   goto ngf_create_image_cleanup;
  }

ngf_create_image_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_image(img);
  }
  return err;
}

void ngf_destroy_image(ngf_image *img) {
  if (img != NULL) {
    if (img->vkimg != VK_NULL_HANDLE) {
      vmaDestroyImage(CURRENT_SHARED_STATE->allocator,
                      img->vkimg, img->alloc);
    }
  }
}

void ngf_destroy_render_target(ngf_render_target *target) {
  _NGF_FAKE_USE(target);
  // TODO: implement
}

ngf_error ngf_default_render_target(
  ngf_attachment_load_op color_load_op,
  ngf_attachment_load_op depth_load_op,
  const ngf_clear *clear_color,
  const ngf_clear *clear_depth,
  ngf_render_target **result) {
  _NGF_FAKE_USE(color_load_op);
  _NGF_FAKE_USE(depth_load_op);
  _NGF_FAKE_USE(clear_color);
  _NGF_FAKE_USE(clear_depth);
  _NGF_FAKE_USE(result);
  // TODO: implement
  return NGF_ERROR_OK;
}


void ngf_destroy_cmd_buffer(ngf_cmd_buffer *buffer) {
  // TODO:implement
  _NGF_FAKE_USE(buffer);
}

void ngf_debug_message_callback(void *userdata,
                                void(*callback)(const char*, const void*)) {
  // TODO: implement
  _NGF_FAKE_USE(userdata);
  _NGF_FAKE_USE(callback);
}
