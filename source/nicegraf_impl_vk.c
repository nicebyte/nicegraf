/**
 * Copyright (c) 2019 nicegraf contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy 
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#define _CRT_SECURE_NO_WARNINGS
#include "nicegraf.h"

#include "dynamic_array.h"
#include "nicegraf_internal.h"

#include <assert.h>
#include <string.h>
#if defined(WIN32)
  #include <malloc.h>
  #undef    alloca
  #define   alloca _malloca
  #define   freea  _freea
#else
  #include <alloca.h>
#endif

// Determine the correct WSI extension to use for VkSurface creation.
// Do not change the relative order of this block, the Volk header include
// directive and the VMA header include directive.
#if defined(_WIN32)||defined(_WIN64)
  #define   WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #define   VK_SURFACE_EXT             "VK_KHR_win32_surface"
  #define   VK_CREATE_SURFACE_FN        vkCreateWin32SurfaceKHR
  #define   VK_CREATE_SURFACE_FN_TYPE   PFN_vkCreateWin32SurfaceKHR
  #define   VK_USE_PLATFORM_WIN32_KHR
#elif defined(__ANDROID__)
  #define   VK_SURFACE_EXT             "VK_KHR_android_surface"
  #define   VK_CREATE_SURFACE_FN        vkCreateAndroidSurfaceKHR
  #define   VK_CREATE_SURFACE_FN_TYPE   PFN_vkCreateAndroidSurfaceKHR
  #define   VK_USE_PLATFORM_ANDROID_KHR
#else
  #include <xcb/xcb.h>
  #include <dlfcn.h>
  #include <X11/Xlib-xcb.h>
  #define   VK_SURFACE_EXT             "VK_KHR_xcb_surface"
  #define   VK_CREATE_SURFACE_FN        vkCreateXcbSurfaceKHR
  #define   VK_CREATE_SURFACE_FN_TYPE   PFN_vkCreateXcbSurfaceKHR
  #define   VK_USE_PLATFORM_XCB_KHR
#endif
#include "volk.h"
#include <vk_mem_alloc.h>

#define _NGF_INVALID_IDX (~0u)

// Singleton for holding vulkan instance and device handles.
struct {
  VkInstance       instance;
  VkPhysicalDevice phys_dev;
  VkDevice         device;
  VkQueue          gfx_queue;
  VkQueue          present_queue;
  VkQueue          xfer_queue;
  pthread_mutex_t  ctx_refcount_mut;
  uint32_t         gfx_family_idx;
  uint32_t         present_family_idx;
  uint32_t         xfer_family_idx;
} _vk;

// State that is common among "shared" contexts (TODO: remove this?)
typedef struct _ngf_context_shared_state {
  pthread_mutex_t cmd_pool_mut;
  pthread_mutex_t record_counter_mut;
  pthread_cond_t  recording_inactive_cond;
  uint32_t        record_counter;
  uint32_t        refcount;
} _ngf_context_shared_state;

// Swapchain state.
typedef struct _ngf_swapchain {
  VkSwapchainKHR   vk_swapchain;
  VkImage         *images;
  VkImageView     *image_views;
  VkSemaphore     *image_semaphores;
  VkFramebuffer   *framebuffers;
  VkPresentModeKHR present_mode;
  uint32_t         num_images; // total number of images in the swapchain.
  uint32_t         image_idx;  // index of currently acquired image.
  uint32_t         width;
  uint32_t         height;
  struct {
    VkAttachmentDescription attachment_descs[2];
    VkAttachmentReference   attachment_refs[2];
    VkSubpassDescription    subpass_desc;
    VkRenderPassCreateInfo  info;
    VkRenderPass            vk_handle;
  } renderpass;
} _ngf_swapchain;

typedef struct ngf_cmd_buffer_t {
  VkCommandBuffer vkcmdbuf;
  VkSemaphore     vksem;
  VkCommandPool   vkpool;
  bool recording;
} ngf_cmd_buffer_t;

typedef struct ngf_attrib_buffer_t {
  VkBuffer vkbuf;
  VkBuffer vkstaging_buf;
  VmaAllocation buf_alloc;
  VmaAllocation staging_buf_alloc;
  size_t   size;
} ngf_attrib_buffer_t;

// Vulkan resources associated with a given frame.
typedef struct _ngf_frame_resources {
 _NGF_DARRAY_OF(ngf_cmd_buffer_t)      submitted_cmdbuffers;
 _NGF_DARRAY_OF(VkSemaphore)           wait_vksemaphores; // 1 per cmdbuf.
 _NGF_DARRAY_OF(VkPipeline)            retire_pipelines;
 _NGF_DARRAY_OF(VkPipelineLayout)      retire_pipeline_layouts;
 _NGF_DARRAY_OF(VkDescriptorSetLayout) retire_dset_layouts;
  VkFence                              fence; // signaled when frame is done.
  bool                                 active;
} _ngf_frame_resources;

typedef struct ngf_context_t {
 _ngf_frame_resources       *frame_res;
 _ngf_context_shared_state **shared_state;
 _ngf_swapchain              swapchain;
  ngf_swapchain_info         swapchain_info;
  VmaAllocator               allocator;
  VkCommandPool              cmd_pool;
  VkSurfaceKHR               surface;
  uint32_t                   frame_number;
  uint32_t                   max_inflight_frames;
} ngf_context_t;

typedef struct ngf_shader_stage_t {
  VkShaderModule         vk_module;
  VkShaderStageFlagBits  vk_stage_bits;
  char                  *entry_point_name;
} ngf_shader_stage_t;

typedef struct ngf_graphics_pipeline_t {
  VkPipeline                           vk_pipeline;
 _NGF_DARRAY_OF(VkDescriptorSetLayout) vk_descriptor_set_layouts;
  VkPipelineLayout                     vk_pipeline_layout;
} ngf_graphics_pipeline_t;

typedef struct ngf_image_t {
  VkImage vkimg;
  VmaAllocation alloc;
} ngf_image_t;

typedef struct ngf_render_target_t {
  VkRenderPass                render_pass;
 _NGF_DARRAY_OF(VkClearValue) clear_values;
  uint32_t                    nclear_values;
  bool                        is_default;
  uint32_t                    width;
  uint32_t                    height;
  // TODO: non-default render target
} ngf_render_target_t;

NGF_THREADLOCAL ngf_context CURRENT_CONTEXT = NULL;
#define CURRENT_SHARED_STATE (*(CURRENT_CONTEXT->shared_state))

static VkSampleCountFlagBits get_vk_sample_count(uint32_t sample_count) {
  switch(sample_count) {
  case 0u:
  case 1u:  return VK_SAMPLE_COUNT_1_BIT;
  case 2u:  return VK_SAMPLE_COUNT_2_BIT;
  case 4u:  return VK_SAMPLE_COUNT_4_BIT;
  case 8u:  return VK_SAMPLE_COUNT_8_BIT;
  case 16u: return VK_SAMPLE_COUNT_16_BIT;
  case 32u: return VK_SAMPLE_COUNT_32_BIT;
  case 64u: return VK_SAMPLE_COUNT_64_BIT;
  default:  assert(false); // TODO: return error?
  }
  return VK_SAMPLE_COUNT_1_BIT;
}

static VkDescriptorType get_vk_descriptor_type(ngf_descriptor_type type) {
  static const VkDescriptorType types[NGF_DESCRIPTOR_TYPE_COUNT] = {
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
    VK_DESCRIPTOR_TYPE_SAMPLER,
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
  };
  return types[type];
}

static VkShaderStageFlags get_vk_stage_flags(uint32_t flags) {
  VkShaderStageFlags result = 0x0u;
  if (flags & NGF_STAGE_FRAGMENT) result &= VK_SHADER_STAGE_FRAGMENT_BIT;
  if (flags & NGF_STAGE_VERTEX) result &= VK_SHADER_STAGE_VERTEX_BIT;
  return result;
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

static VkAttachmentLoadOp get_vk_load_op(ngf_attachment_load_op op) {
  static const VkAttachmentLoadOp ops[NGF_LOAD_OP_COUNT] = {
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,
    VK_ATTACHMENT_LOAD_OP_LOAD,
    VK_ATTACHMENT_LOAD_OP_CLEAR
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
  case NGF_DYNAMIC_STATE_VIEWPORT:
    return VK_DYNAMIC_STATE_VIEWPORT;
  case NGF_DYNAMIC_STATE_SCISSOR:
    return VK_DYNAMIC_STATE_SCISSOR;
  case NGF_DYNAMIC_STATE_LINE_WIDTH:
    return VK_DYNAMIC_STATE_LINE_WIDTH;
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

  if ((size < 1 || size > 4) || (norm && type > NGF_TYPE_UINT16)) {
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

#if defined(XCB_NONE)
xcb_connection_t *XCB_CONNECTION = NULL;
xcb_visualid_t    XCB_VISUALID   = { 0 };

#endif
bool _ngf_query_presentation_support(VkPhysicalDevice phys_dev,
                                     uint32_t queue_family_index) {
#if defined(_WIN32) || defined(_WIN64)
  return vkGetPhysicalDeviceWin32PresentationSupportKHR(phys_dev,
                                                        queue_family_index);
#elif defined(__ANDROID__)
  return true;
#else

  if (XCB_CONNECTION == NULL) {
    int                screen_idx = 0;
    xcb_screen_t      *screen     = NULL;
    xcb_connection_t  *connection = xcb_connect(NULL, &screen_idx);
    const xcb_setup_t *setup      = xcb_get_setup(connection);
    for(xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup);
        screen >=0 && it.rem;
        xcb_screen_next (&it)) {
      if (screen_idx-- == 0) { screen = it.data; }
    }
    assert(screen);
    XCB_CONNECTION = connection;
    XCB_VISUALID   = screen->root_visual;
  }
  return vkGetPhysicalDeviceXcbPresentationSupportKHR(phys_dev, 
                                                      queue_family_index,
                                                      XCB_CONNECTION,
                                                      XCB_VISUALID);
#endif
}

ngf_error ngf_initialize(ngf_device_preference pref) {
  if (_vk.instance == VK_NULL_HANDLE) { // Vulkan not initialized yet.
    volkInitialize(); // Initialize Volk.

    const char * const ext_names[] = { // Names of instance-level extensions.
      "VK_KHR_surface", VK_SURFACE_EXT
    };

    VkApplicationInfo app_info = { // Application information.
      .sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pNext            = NULL,
      .pApplicationName = NULL, // TODO: allow specifying app name.
      .pEngineName      = "nicegraf",
      .engineVersion    = VK_MAKE_VERSION(NGF_VER_MAJ, NGF_VER_MIN, 0),
      .apiVersion       = VK_MAKE_VERSION(1, 0, 9)
    };

    // Create a Vulkan instance.
    VkInstanceCreateInfo inst_info = {
      .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext                   = NULL,
      .flags                   = 0u,
      .pApplicationInfo        = &app_info,
      .enabledLayerCount       = 0u,
      .ppEnabledLayerNames     = NULL,
      .enabledExtensionCount   = NGF_ARRAYSIZE(ext_names),
      .ppEnabledExtensionNames = ext_names 
    };
    VkResult vk_err = vkCreateInstance(&inst_info, NULL, &_vk.instance);
    if (vk_err != VK_SUCCESS) { return NGF_ERROR_CONTEXT_CREATION_FAILED; }

    volkLoadInstance(_vk.instance); // load instance-level Vulkan functions.

    // Obtain a list of available physical devices.
    uint32_t nphysdev = 0u;
    vkEnumeratePhysicalDevices(_vk.instance, &nphysdev, NULL);
    VkPhysicalDevice *physdevs = alloca(nphysdev * sizeof(VkPhysicalDevice));
    assert(physdevs != NULL);
    vkEnumeratePhysicalDevices(_vk.instance, &nphysdev, physdevs);

    // Pick a suitable physical device based on user's preference.
    uint32_t best_device_score = 0U;
    uint32_t best_device_index = _NGF_INVALID_IDX;
    for (uint32_t i = 0; i < nphysdev; ++i) {
      VkPhysicalDeviceProperties dev_props;
      vkGetPhysicalDeviceProperties(physdevs[i], &dev_props);
      uint32_t score = 0U;
      switch(dev_props.deviceType) {
      case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        score += 100U;
        if (pref == NGF_DEVICE_PREFERENCE_DISCRETE) { score += 1000U; }
        break;
      case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        score += 90U;
        if (pref == NGF_DEVICE_PREFERENCE_INTEGRATED) { score += 1000U; }
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
        best_device_index = i;
        best_device_score = score;
      }
    }
    if (best_device_index == _NGF_INVALID_IDX) {
      return NGF_ERROR_INITIALIZATION_FAILED;
    }
    _vk.phys_dev = physdevs[best_device_index];

    // Initialize context refcount mutex.
    pthread_mutex_init(&_vk.ctx_refcount_mut, NULL);
   
    // Obtain a list of queue family properties from the device.
    uint32_t num_queue_families = 0U;
    vkGetPhysicalDeviceQueueFamilyProperties(_vk.phys_dev,
                                             &num_queue_families,
                                             NULL);
    VkQueueFamilyProperties *queue_families =
        alloca(num_queue_families * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(_vk.phys_dev,
                                             &num_queue_families,
                                              queue_families);

    // Pick suitable queue families for graphics and present.
    uint32_t gfx_family_idx     = _NGF_INVALID_IDX;
    uint32_t present_family_idx = _NGF_INVALID_IDX;
    uint32_t xfer_family_idx    = _NGF_INVALID_IDX;
    for (uint32_t q = 0; q < num_queue_families; ++q) {
      const VkQueueFlags flags      = queue_families[q].queueFlags;
      const VkBool32     is_gfx     = (flags & VK_QUEUE_GRAPHICS_BIT) != 0;
      const VkBool32     is_xfer    = (flags & VK_QUEUE_TRANSFER_BIT) != 0;
      const VkBool32     is_present = _ngf_query_presentation_support(
                                          _vk.phys_dev, q);
      if (gfx_family_idx == _NGF_INVALID_IDX && is_gfx) {
        gfx_family_idx = q;
      }
      if ((xfer_family_idx == _NGF_INVALID_IDX ||
           xfer_family_idx == gfx_family_idx) && is_xfer) {
        // Prefer to use different queues for graphics and transfer.
        xfer_family_idx = q;
      }
      if (present_family_idx == _NGF_INVALID_IDX && is_present == VK_TRUE) {
        present_family_idx = q;
      }
    }
    if (gfx_family_idx == _NGF_INVALID_IDX ||
        xfer_family_idx == _NGF_INVALID_IDX ||
        present_family_idx == _NGF_INVALID_IDX) {
      return NGF_ERROR_INITIALIZATION_FAILED;
    }
    _vk.gfx_family_idx     = gfx_family_idx;
    _vk.xfer_family_idx    = xfer_family_idx;
    _vk.present_family_idx = present_family_idx;

    // Create logical device.
    const float queue_prio = 1.0f;
    const VkDeviceQueueCreateInfo gfx_queue_info = {
      .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext            = NULL,
      .flags            = 0,
      .queueFamilyIndex = gfx_family_idx,
      .queueCount       = 1,
      .pQueuePriorities = &queue_prio
    };
    const VkDeviceQueueCreateInfo xfer_queue_info = {
      .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext            = NULL,
      .flags            = 0,
      .queueFamilyIndex = xfer_family_idx,
      .queueCount       = 1,
      .pQueuePriorities = &queue_prio
    };
    const VkDeviceQueueCreateInfo present_queue_info = {
      .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .pNext            = NULL,
      .flags            = 0,
      .queueFamilyIndex = present_family_idx,
      .queueCount       = 1,
      .pQueuePriorities = &queue_prio
    };
    const bool same_gfx_and_xfer    = gfx_family_idx == xfer_family_idx;
    const bool same_gfx_and_present = gfx_family_idx == present_family_idx;
    const VkDeviceQueueCreateInfo queue_infos[] = {
        gfx_queue_info,
        !same_gfx_and_xfer ? xfer_queue_info : present_queue_info,
        present_queue_info
    };
    const uint32_t num_queue_infos =
        1u + (same_gfx_and_present ? 1u : 0u) + (same_gfx_and_xfer ? 1u : 0u);
    const char *device_exts[] = { "VK_KHR_swapchain" };
    const VkDeviceCreateInfo dev_info = {
      .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext                   = NULL,
      .flags                   = 0,
      .queueCreateInfoCount    = num_queue_infos,
      .pQueueCreateInfos       = queue_infos,
      .enabledLayerCount       = 0,
      .ppEnabledLayerNames     = NULL,
      .enabledExtensionCount   = 1u,
      .ppEnabledExtensionNames = device_exts
    };
    vk_err = vkCreateDevice(_vk.phys_dev, &dev_info, NULL, &_vk.device);
    if (vk_err != VK_SUCCESS) {
      return NGF_ERROR_INITIALIZATION_FAILED;
    }

    // Obtain queue handles.
    vkGetDeviceQueue(_vk.device, gfx_family_idx, 0, &_vk.gfx_queue);
    vkGetDeviceQueue(_vk.device, xfer_family_idx, 0, &_vk.xfer_queue);
    vkGetDeviceQueue(_vk.device, present_family_idx, 0, &_vk.present_queue);

    // Load device-level entry points.
    volkLoadDevice(_vk.device);

    // Done!
  }
  return NGF_ERROR_OK;
}

static void _ngf_destroy_swapchain(_ngf_swapchain *swapchain) {
  assert(swapchain);
  vkDeviceWaitIdle(_vk.device);
  for (uint32_t s = 0u;
       swapchain->image_semaphores != NULL && s < swapchain->num_images; ++s) {
    if (swapchain->image_semaphores[s] != VK_NULL_HANDLE) {
      vkDestroySemaphore(_vk.device, swapchain->image_semaphores[s],
                         NULL);
    }
  }
  if (swapchain->image_semaphores != NULL) {
    NGF_FREEN(swapchain->image_semaphores, swapchain->num_images);
  }
  for (uint32_t f = 0u; swapchain->framebuffers && f < swapchain->num_images;
       ++f) {
    vkDestroyFramebuffer(_vk.device, swapchain->framebuffers[f], NULL);
  }
  if (swapchain->framebuffers != NULL) {
    NGF_FREEN(swapchain->framebuffers, swapchain->num_images);
  }
  for (uint32_t v = 0u;
       swapchain->image_views != NULL && v < swapchain->num_images; ++v) {
    vkDestroyImageView(_vk.device, swapchain->image_views[v], NULL);
  }
  if (swapchain->image_views) {
    NGF_FREEN(swapchain->image_views, swapchain->num_images);
  }
  if (swapchain->renderpass.vk_handle != VK_NULL_HANDLE) {
    vkDestroyRenderPass(_vk.device, swapchain->renderpass.vk_handle, NULL);
  }
  if (swapchain->vk_swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(_vk.device, swapchain->vk_swapchain, NULL);
  }
}

static ngf_error _ngf_create_swapchain(
    const ngf_swapchain_info *swapchain_info,
    VkSurfaceKHR surface,
    _ngf_swapchain *swapchain) {
  assert(swapchain_info);
  assert(swapchain);

  ngf_error       err           = NGF_ERROR_OK;
  VkResult        vk_err        = VK_SUCCESS;
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
 
  // Check available present modes and fall back on FIFO if the requested
  // present mode is not supported.
  uint32_t npresent_modes = 0u;;
  vkGetPhysicalDeviceSurfacePresentModesKHR(_vk.phys_dev, surface,
                                            &npresent_modes, NULL);
  VkPresentModeKHR *present_modes =
      alloca(sizeof(VkPresentModeKHR) * npresent_modes);
  vkGetPhysicalDeviceSurfacePresentModesKHR(_vk.phys_dev, surface,
                                            &npresent_modes, present_modes);
  static const VkPresentModeKHR modes[] = {
    VK_PRESENT_MODE_FIFO_KHR,
    VK_PRESENT_MODE_IMMEDIATE_KHR
  };
  const VkPresentModeKHR requested_present_mode =
      modes[swapchain_info->present_mode];
  for (uint32_t p = 0u; p < npresent_modes; ++p) {
    if (present_modes[p] == requested_present_mode) {
      present_mode = present_modes[p];
      break;
    }
  }

  // Check if the requested surface format is valid.
  uint32_t nformats = 0u;
  vkGetPhysicalDeviceSurfaceFormatsKHR(_vk.phys_dev, surface, &nformats, NULL);
  VkSurfaceFormatKHR *formats = alloca(sizeof(VkSurfaceFormatKHR) * nformats);
  vkGetPhysicalDeviceSurfaceFormatsKHR(_vk.phys_dev, surface, &nformats,
                                        formats);
  const VkColorSpaceKHR color_space      = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR; // TODO: use correct colorspace here.
  const VkFormat        requested_format =
      get_vk_image_format(swapchain_info->cfmt);
  if (!(nformats == 1 && formats[0].format == VK_FORMAT_UNDEFINED)) {
    bool found = false;
    for (size_t f = 0; !found && f < nformats; ++f) {
      found = formats[f].format == requested_format;
    }
    if (!found) {
      err = NGF_ERROR_INVALID_SURFACE_FORMAT;
      goto _ngf_create_swapchain_cleanup;
    }
  }
  VkSurfaceCapabilitiesKHR surface_caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_vk.phys_dev, surface,
                                            &surface_caps);

  // Determine if we should use exclusive or concurrent sharing mode for
  // swapchain images.
  const bool exclusive_sharing =
      _vk.gfx_family_idx == _vk.present_family_idx;
  const VkSharingMode sharing_mode =
    exclusive_sharing ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
  const uint32_t num_sharing_queue_families = exclusive_sharing ? 0 : 2;
  const uint32_t sharing_queue_families[] = { _vk.gfx_family_idx,
                                              _vk.present_family_idx };
;
  // Create swapchain.
  const VkSwapchainCreateInfoKHR vk_sc_info = {
    .sType           = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .pNext           = NULL,
    .flags           = 0,
    .surface         = surface,
    .minImageCount   = swapchain_info->capacity_hint,
    .imageFormat     = requested_format,
    .imageColorSpace = color_space,
    .imageExtent     = {
       .width  = swapchain_info->width,
       .height = swapchain_info->height
     },
    .imageArrayLayers      = 1,
    .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .imageSharingMode      = sharing_mode,
    .queueFamilyIndexCount = num_sharing_queue_families,
    .pQueueFamilyIndices   = sharing_queue_families,
    .preTransform          = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
    .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode           = present_mode
  };
  vk_err = vkCreateSwapchainKHR(_vk.device, &vk_sc_info, NULL,
                                &swapchain->vk_swapchain);
  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_SWAPCHAIN_CREATION_FAILED;
    goto _ngf_create_swapchain_cleanup;
  }

  // Obtain swapchain images.
  vk_err = vkGetSwapchainImagesKHR(_vk.device, swapchain->vk_swapchain,
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
  vk_err = vkGetSwapchainImagesKHR(_vk.device, swapchain->vk_swapchain,
                                   &swapchain->num_images, swapchain->images);
  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_SWAPCHAIN_CREATION_FAILED;
    goto _ngf_create_swapchain_cleanup;
  }

  // Create image views for swapchain images.
  swapchain->image_views =  NGF_ALLOCN(VkImageView, swapchain->num_images);
  if (swapchain->image_views == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto _ngf_create_swapchain_cleanup;
  }
  for (uint32_t i = 0u; i < swapchain->num_images; ++i) {
    const VkImageViewCreateInfo image_view_info = {
      .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext      = NULL,
      .flags      = 0u,
      .image      = swapchain->images[i],
      .viewType   = VK_IMAGE_VIEW_TYPE_2D,
      .format     = requested_format,
      .components = {
        .r = VK_COMPONENT_SWIZZLE_IDENTITY,
        .g = VK_COMPONENT_SWIZZLE_IDENTITY,
        .b = VK_COMPONENT_SWIZZLE_IDENTITY,
        .a =VK_COMPONENT_SWIZZLE_IDENTITY
      },
      .subresourceRange = {
        .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel   = 0u,
        .levelCount     = 1u,
        .baseArrayLayer = 0u,
        .layerCount     = 1u
      }
    };
    vk_err = vkCreateImageView(_vk.device, &image_view_info, NULL,
                               &swapchain->image_views[i]);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_SWAPCHAIN_CREATION_FAILED;
      goto _ngf_create_swapchain_cleanup;
    }
  }

  // Create a renderpass to use for framebuffer initialization.
  {
    const VkAttachmentDescription color_attachment_desc = {
      .flags          = 0u,
      .format         = requested_format,
      .samples        = VK_SAMPLE_COUNT_1_BIT, // TODO: multisampling
      .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
    };
    swapchain->renderpass.attachment_descs[0] = color_attachment_desc;
  }
  {
    const VkAttachmentReference color_attachment_ref = {
      .attachment = 0u,
      .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };
    swapchain->renderpass.attachment_refs[0] = color_attachment_ref;
  }
  {
    const VkSubpassDescription subpass_desc = {
      .flags = 0u,
      .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .inputAttachmentCount    = 0u,
      .pInputAttachments       = NULL,
      .colorAttachmentCount    = 1u,
      .pColorAttachments       = swapchain->renderpass.attachment_refs,
      .pResolveAttachments     = NULL, // TODO: multisampling
      .pDepthStencilAttachment = NULL, // TODO: depth,
      .preserveAttachmentCount = 0u,
      .pPreserveAttachments    = NULL
    };
    swapchain->renderpass.subpass_desc = subpass_desc;
  }
  {
    const VkRenderPassCreateInfo renderpass_info = {
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .pNext = NULL,
      .flags = 0u,
      .attachmentCount = 1u, // TODO: depth
      .pAttachments = swapchain->renderpass.attachment_descs,
      .subpassCount = 1u,
      .pSubpasses = &swapchain->renderpass.subpass_desc,
      .dependencyCount = 0u,
      .pDependencies = NULL
    };
    swapchain->renderpass.info = renderpass_info;
  }
  vk_err = vkCreateRenderPass(_vk.device, &swapchain->renderpass.info, NULL,
                              &swapchain->renderpass.vk_handle);
  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_SWAPCHAIN_CREATION_FAILED;
    goto _ngf_create_swapchain_cleanup;
  }

  // Create framebuffers for swapchain images.
  swapchain->framebuffers = NGF_ALLOCN(VkFramebuffer, swapchain->num_images);
  if (swapchain->framebuffers == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto _ngf_create_swapchain_cleanup;
  }
  for (uint32_t f = 0u; f < swapchain->num_images; ++f) {
    VkFramebufferCreateInfo fb_info = {
      .sType           =  VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .pNext           =  NULL,
      .flags           =  0u,
      .renderPass      =  swapchain->renderpass.vk_handle,
      .attachmentCount =  1u, // TODO: handle depth
      .pAttachments    = &swapchain->image_views[f],
      .width           =  swapchain_info->width,
      .height          =  swapchain_info->height,
      .layers          =  1u
    };
    vk_err = vkCreateFramebuffer(_vk.device, &fb_info, NULL,
                                 &swapchain->framebuffers[f]);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_SWAPCHAIN_CREATION_FAILED;
      goto _ngf_create_swapchain_cleanup;
    }
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
    vk_err = vkCreateSemaphore(_vk.device, &sem_info, NULL,
                               &swapchain->image_semaphores[s]);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_SWAPCHAIN_CREATION_FAILED;
      goto _ngf_create_swapchain_cleanup;
    }
  }
  swapchain->image_idx = 0U;
  swapchain->width     = swapchain_info->width;
  swapchain->height    = swapchain_info->height;

_ngf_create_swapchain_cleanup:
  if (err != NGF_ERROR_OK) {
    _ngf_destroy_swapchain(swapchain);
  }
  return err;
}

ngf_error ngf_create_context(const ngf_context_info *info,
                             ngf_context *result) {
  assert(info);
  assert(result);

  ngf_error                 err            = NGF_ERROR_OK;
  VkResult                  vk_err         = VK_SUCCESS;
  const ngf_swapchain_info *swapchain_info = info->swapchain_info;
  const ngf_context         shared         = info->shared_context;

  // Allocate space for context data.
  *result = NGF_ALLOC(struct ngf_context_t);
  ngf_context ctx = *result;
  if (ctx == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_context_cleanup;
  }
  memset(ctx, 0, sizeof(struct ngf_context_t));

  // Create swapchain if necessary.
  if (swapchain_info != NULL) {
    // Begin by creating the window surface.
#if defined(_WIN32) || defined(_WIN64)
    const VkWin32SurfaceCreateInfoKHR surface_info = {
      .sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
      .pNext     = NULL,
      .flags     = 0,
      .hinstance = GetModuleHandle(NULL),
      .hwnd      = (HWND)swapchain_info->native_handle
    };
#elif defined(__ANDROID__)
    const VkAndroidSuraceCreateInfoKHR surface_info = {
      .sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
      .pNext  = NULL,
      .flags  = 0,
      .window = swapchain_info->native_handle
    };
#else
    const VkXcbSurfaceCreateInfoKHR surface_info = {
      .sType     = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
      .pNext     = NULL,
      .flags     = 0,
      .window    = (xcb_window_t)swapchain_info->native_handle,
      .connection = XCB_CONNECTION
    };
#endif
    vk_err =
      VK_CREATE_SURFACE_FN(_vk.instance, &surface_info, NULL, &ctx->surface);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_SURFACE_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }
    VkBool32 surface_supported = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(_vk.phys_dev,
                                         _vk.present_family_idx,
                                          ctx->surface,
                                         &surface_supported);
    if (!surface_supported) {
      err = NGF_ERROR_SURFACE_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }

    // Create the swapchain itself.
    err = _ngf_create_swapchain(swapchain_info, ctx->surface, &ctx->swapchain);
    if (err != NGF_ERROR_OK) goto ngf_create_context_cleanup;
    ctx->swapchain_info = *swapchain_info;
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
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_CONTEXT_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }

    // Create objects for cmd pool synchronization.
    pthread_mutex_init(&shared_state->cmd_pool_mut, NULL);
    pthread_mutex_init(&shared_state->record_counter_mut, NULL);
    pthread_cond_init(&shared_state->recording_inactive_cond, NULL);
    shared_state->record_counter = 0u;
  }

  // Create a command pool.
  VkCommandPoolCreateInfo cmd_pool_info = {
    .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
    .pNext            = NULL,
    .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    .queueFamilyIndex = _vk.gfx_family_idx
  };
  vk_err = vkCreateCommandPool(_vk.device, &cmd_pool_info, NULL,
                               &ctx->cmd_pool);
  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_CONTEXT_CREATION_FAILED;
    goto ngf_create_context_cleanup;
  }

  // Set up VMA.
  VmaVulkanFunctions vma_vk_fns = {
    .vkGetPhysicalDeviceProperties       = vkGetPhysicalDeviceProperties,
    .vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
    .vkAllocateMemory                    = vkAllocateMemory,
    .vkFreeMemory                        = vkFreeMemory,
    .vkMapMemory                         = vkMapMemory,
    .vkUnmapMemory                       = vkUnmapMemory,
    .vkFlushMappedMemoryRanges           = vkFlushMappedMemoryRanges,
    .vkInvalidateMappedMemoryRanges      = vkInvalidateMappedMemoryRanges,
    .vkBindBufferMemory                  = vkBindBufferMemory,
    .vkBindImageMemory                   = vkBindImageMemory,
    .vkGetBufferMemoryRequirements       = vkGetBufferMemoryRequirements,
    .vkGetImageMemoryRequirements        = vkGetImageMemoryRequirements,
    .vkCreateBuffer                      = vkCreateBuffer,
    .vkDestroyBuffer                     = vkDestroyBuffer,
    .vkCreateImage                       = vkCreateImage,
    .vkDestroyImage                      = vkDestroyImage
  };
  VmaAllocatorCreateInfo vma_info = {
    .flags                       = 0u,
    .physicalDevice              = _vk.phys_dev,
    .device                      = _vk.device,
    .preferredLargeHeapBlockSize = 0u,
    .pAllocationCallbacks        = NULL,
    .pDeviceMemoryCallbacks      = NULL,
    .frameInUseCount             = 0u,
    .pHeapSizeLimit              = NULL,
    .pVulkanFunctions            = &vma_vk_fns,
    .pRecordSettings             = NULL
  };
  vk_err = vmaCreateAllocator(&vma_info, &ctx->allocator);

  // Create frame resource holders.
  const uint32_t max_inflight_frames =
      swapchain_info ? ctx->swapchain.num_images : 3u;
  ctx->max_inflight_frames = max_inflight_frames;
  ctx->frame_res = NGF_ALLOCN(_ngf_frame_resources, max_inflight_frames);
  if (ctx->frame_res == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_context_cleanup;
  }
  for (uint32_t f = 0u; f < max_inflight_frames; ++f) {
    _NGF_DARRAY_RESET(ctx->frame_res[f].wait_vksemaphores,
                      max_inflight_frames);
    _NGF_DARRAY_RESET(ctx->frame_res[f].submitted_cmdbuffers,
                      max_inflight_frames);
    _NGF_DARRAY_RESET(ctx->frame_res[f].retire_pipelines, 8);
    _NGF_DARRAY_RESET(ctx->frame_res[f].retire_pipeline_layouts, 8);
    _NGF_DARRAY_RESET(ctx->frame_res[f].retire_dset_layouts, 8);
    ctx->frame_res[f].active = false;
    const VkFenceCreateInfo fence_info = {
      .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0u
    };
    vk_err = vkCreateFence(_vk.device, &fence_info, NULL,
                           &ctx->frame_res[f].fence);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_CONTEXT_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }
  }
  ctx->frame_number = 0u;

ngf_create_context_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_context(ctx);
  }
  return err;
}

ngf_error ngf_resize_context(ngf_context ctx,
                             uint32_t    new_width,
                             uint32_t    new_height) {
  assert(ctx);
  ngf_error err = NGF_ERROR_OK;

  _ngf_destroy_swapchain(&ctx->swapchain);
  ctx->swapchain_info.width  = new_width;
  ctx->swapchain_info.height = new_height;
  err = _ngf_create_swapchain(&ctx->swapchain_info, ctx->surface,
                              &ctx->swapchain);
  return err;
}

void _ngf_retire_resources(_ngf_frame_resources *frame_res) {
  if (frame_res->active) {
    vkWaitForFences(_vk.device, 1u, &frame_res->fence, true, 1000000u);
    vkResetFences(_vk.device, 1u,
                  &frame_res->fence);
  }
  frame_res->active = false;
  for (uint32_t s = 0u;
       s < _NGF_DARRAY_SIZE(frame_res->wait_vksemaphores);
       ++s) {
    ngf_cmd_buffer_t cmd_buffer =
        _NGF_DARRAY_AT(frame_res->submitted_cmdbuffers, s);
    vkFreeCommandBuffers(_vk.device,
                         cmd_buffer.vkpool, 1u, &cmd_buffer.vkcmdbuf);
    vkDestroySemaphore(_vk.device,
                       _NGF_DARRAY_AT(frame_res->wait_vksemaphores, s),
                       NULL);
  }
  for (uint32_t p = 0u;
       p < _NGF_DARRAY_SIZE(frame_res->retire_pipelines);
       ++p) {
    vkDestroyPipeline(_vk.device,
                      _NGF_DARRAY_AT(frame_res->retire_pipelines, p),
                       NULL);
  }
  for (uint32_t p = 0u;
       p < _NGF_DARRAY_SIZE(frame_res->retire_pipeline_layouts);
       ++p) {
    vkDestroyPipelineLayout(_vk.device,
                            _NGF_DARRAY_AT(
                                 frame_res->retire_pipeline_layouts,
                                 p),
                             NULL);
  }
  for (uint32_t p = 0u;
       p < _NGF_DARRAY_SIZE(frame_res->retire_dset_layouts);
       ++p) {
    vkDestroyDescriptorSetLayout(_vk.device,
                                 _NGF_DARRAY_AT(
                                      frame_res->retire_dset_layouts,
                                      p),
                                  NULL);
  }
  _NGF_DARRAY_CLEAR(frame_res->submitted_cmdbuffers);
  _NGF_DARRAY_CLEAR(frame_res->wait_vksemaphores);
  _NGF_DARRAY_CLEAR(frame_res->retire_pipelines);
  _NGF_DARRAY_CLEAR(frame_res->retire_dset_layouts);
  _NGF_DARRAY_CLEAR(frame_res->retire_pipeline_layouts);
}

void ngf_destroy_context(ngf_context ctx) {
  if (ctx != NULL) {
	  pthread_mutex_lock(&_vk.ctx_refcount_mut);
    _ngf_context_shared_state *shared_state = *(ctx->shared_state);
    if (shared_state != NULL) {
      vkDeviceWaitIdle(_vk.device);
      for (uint32_t f = 0u;
           ctx->frame_res != NULL && f < ctx->max_inflight_frames;
           ++f) {
        if (ctx->frame_res[f].active) {
          _ngf_retire_resources(&ctx->frame_res[f]);
        }
        _NGF_DARRAY_DESTROY(ctx->frame_res[f].submitted_cmdbuffers);
        _NGF_DARRAY_DESTROY(ctx->frame_res[f].wait_vksemaphores);
        _NGF_DARRAY_DESTROY(ctx->frame_res[f].retire_pipelines);
        _NGF_DARRAY_DESTROY(ctx->frame_res[f].retire_pipeline_layouts);
        _NGF_DARRAY_DESTROY(ctx->frame_res[f].retire_dset_layouts);
        vkDestroyFence(_vk.device, ctx->frame_res[f].fence, NULL);
      }
      vkDestroyCommandPool(_vk.device, ctx->cmd_pool, NULL);
      _ngf_destroy_swapchain(&ctx->swapchain);
      if (ctx->surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(_vk.instance, ctx->surface, NULL);
      }
      if (ctx->allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(ctx->allocator);
      }
      shared_state->refcount--;
      if (shared_state->refcount == 0) {
        pthread_mutex_destroy(&shared_state->cmd_pool_mut);
        pthread_mutex_destroy(&shared_state->record_counter_mut);
        pthread_cond_destroy(&shared_state->recording_inactive_cond);
        NGF_FREE(shared_state);
        *(ctx->shared_state) = NULL;
      } 
    }
    pthread_mutex_unlock(&_vk.ctx_refcount_mut);
    if (ctx->frame_res != NULL) {
      NGF_FREEN(ctx->frame_res, ctx->max_inflight_frames);
    }
    if (CURRENT_CONTEXT == ctx) CURRENT_CONTEXT = NULL;
    NGF_FREE(ctx);
  }
}

ngf_error ngf_set_context(ngf_context ctx) {
  CURRENT_CONTEXT = ctx;
  return NGF_ERROR_OK;
}

ngf_error ngf_create_cmd_buffer(const ngf_cmd_buffer_info *info,
                                ngf_cmd_buffer *result) {
  assert(info);
  assert(result);
  _NGF_FAKE_USE(info);

  ngf_cmd_buffer cmd_buf = NGF_ALLOC(ngf_cmd_buffer_t);
  *result = cmd_buf;
  if (cmd_buf == NULL) {
    return NGF_ERROR_OUTOFMEM;
  }

  cmd_buf->vkcmdbuf  = VK_NULL_HANDLE;
  cmd_buf->vksem     = VK_NULL_HANDLE;
  cmd_buf->vkpool    = VK_NULL_HANDLE;
  cmd_buf->recording = false;

  return NGF_ERROR_OK;
}

void _ngf_inc_recorder_count() {
  pthread_mutex_lock(&CURRENT_SHARED_STATE->cmd_pool_mut);
  CURRENT_SHARED_STATE->record_counter++;
  pthread_mutex_unlock(&CURRENT_SHARED_STATE->cmd_pool_mut);
}

void _ngf_dec_recorder_count() {
  pthread_mutex_lock(&CURRENT_SHARED_STATE->record_counter_mut);
  assert(CURRENT_SHARED_STATE->record_counter > 0u);
  if (--(CURRENT_SHARED_STATE->record_counter) == 0u) {
    pthread_cond_signal(&CURRENT_SHARED_STATE->recording_inactive_cond);
  }
  pthread_mutex_unlock(&CURRENT_SHARED_STATE->record_counter_mut);
}

void _ngf_cleanup_cmd_buffer(ngf_cmd_buffer cmd_buf) {
  if (cmd_buf  != NULL) {
    if (cmd_buf->vkcmdbuf != VK_NULL_HANDLE) {
      vkFreeCommandBuffers(_vk.device,
                           cmd_buf->vkpool, 1u, &cmd_buf->vkcmdbuf);
      if (cmd_buf->recording) _ngf_dec_recorder_count();
    }
    if (cmd_buf->vksem != VK_NULL_HANDLE) {
      vkDestroySemaphore(_vk.device, cmd_buf->vksem, NULL);
    }
  }
}

ngf_error ngf_start_cmd_buffer(ngf_cmd_buffer cmd_buf) {
  assert(cmd_buf);
  ngf_error err   = NGF_ERROR_OK;
  VkResult vk_err = VK_SUCCESS;

  // Verify we're not recording.
  if (cmd_buf->recording) {
    err = NGF_ERROR_CMD_BUFFER_ALREADY_RECORDING;
    goto ngf_start_cmd_buffer_cleanup;
  }

  // Create command buffer of reset existing one.
  _ngf_inc_recorder_count();
  cmd_buf->recording = true;
  if (cmd_buf->vkcmdbuf == VK_NULL_HANDLE) { // no handle, create new cmdbuf.
    VkCommandBufferAllocateInfo vk_cmdbuf_info = {
      .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext              = NULL,
      .commandPool        = CURRENT_CONTEXT->cmd_pool,
      .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1u
    };
    vk_err = vkAllocateCommandBuffers(_vk.device, &vk_cmdbuf_info,
                                      &cmd_buf->vkcmdbuf);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_OUTOFMEM; // TODO: return appropriate error.
      goto ngf_start_cmd_buffer_cleanup;
    }
    cmd_buf->vkpool = CURRENT_CONTEXT->cmd_pool;
    VkCommandBufferBeginInfo cmd_buf_begin = {
      .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext            = NULL,
      .flags            = 0,
      .pInheritanceInfo = NULL
    };
    vkBeginCommandBuffer(cmd_buf->vkcmdbuf, &cmd_buf_begin);
  } else { // have handle, reuse resources
    vkResetCommandBuffer(cmd_buf->vkcmdbuf, 0);
  }
  
  // Create semaphore.
  if (cmd_buf->vksem == VK_NULL_HANDLE) {
    VkSemaphoreCreateInfo vk_sem_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0u
    };
    vk_err = vkCreateSemaphore(_vk.device, &vk_sem_info, NULL,
                               &cmd_buf->vksem);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_OUTOFMEM; // TODO: return appropriate error code
      goto ngf_start_cmd_buffer_cleanup;
    }
  }

ngf_start_cmd_buffer_cleanup:
  if (err != NGF_ERROR_OK) {
    _ngf_cleanup_cmd_buffer(cmd_buf);
  }

  return err;
}

ngf_error ngf_end_cmd_buffer(ngf_cmd_buffer cmd_buf) {
  assert(cmd_buf);
  if (!cmd_buf->recording) {
    return NGF_ERROR_CMD_BUFFER_WAS_NOT_RECORDING;
  }
  vkEndCommandBuffer(cmd_buf->vkcmdbuf);
  _ngf_dec_recorder_count();
  cmd_buf->recording = false;
  return NGF_ERROR_OK;
}

void ngf_destroy_cmd_buffer(ngf_cmd_buffer buffer) {
  _ngf_cleanup_cmd_buffer(buffer);
}

ngf_error ngf_submit_cmd_buffer(uint32_t nbuffers, ngf_cmd_buffer *bufs) {
  assert(bufs);
  uint32_t fi = CURRENT_CONTEXT->frame_number;
  _ngf_frame_resources *frame_sync_data = &CURRENT_CONTEXT->frame_res[fi];
  for (uint32_t i = 0u; i < nbuffers; ++i) {
    if (bufs[i]->recording) {
      return NGF_ERROR_CMD_BUFFER_ALREADY_RECORDING; // TODO: return appropriate error code
    }
    _NGF_DARRAY_APPEND(frame_sync_data->wait_vksemaphores, bufs[i]->vksem);
    _NGF_DARRAY_APPEND(frame_sync_data->submitted_cmdbuffers,
                       *bufs[i]);
    bufs[i]->vkcmdbuf = VK_NULL_HANDLE;
    bufs[i]->vksem = VK_NULL_HANDLE;
    bufs[i]->vkpool = VK_NULL_HANDLE;
  }
  return NGF_ERROR_OK;
}
ngf_error ngf_begin_frame() {
  ngf_error err = NGF_ERROR_OK;
  uint32_t fi = CURRENT_CONTEXT->frame_number;
  if (CURRENT_CONTEXT->swapchain.vk_swapchain != VK_NULL_HANDLE) {
    const VkResult vk_err =
        vkAcquireNextImageKHR(_vk.device,
                              CURRENT_CONTEXT->swapchain.vk_swapchain,
                              UINT64_MAX,
                              CURRENT_CONTEXT->swapchain.image_semaphores[fi],
                              VK_NULL_HANDLE,
                              &CURRENT_CONTEXT->swapchain.image_idx);
    if (vk_err != VK_SUCCESS) err = NGF_ERROR_BEGIN_FRAME_FAILED;
  }
  CURRENT_CONTEXT->frame_res[fi].active = true;
  _NGF_DARRAY_CLEAR(CURRENT_CONTEXT->frame_res[fi].submitted_cmdbuffers);
  _NGF_DARRAY_CLEAR(CURRENT_CONTEXT->frame_res[fi].wait_vksemaphores);
  return err;
}

ngf_error ngf_end_frame() {
  ngf_error err = NGF_ERROR_OK;

  const uint32_t fi = CURRENT_CONTEXT->frame_number;
  const _ngf_frame_resources *frame_sync = &CURRENT_CONTEXT->frame_res[fi];

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
  const uint32_t nsubmitted_cmdbuffers =
      _NGF_DARRAY_SIZE(frame_sync->submitted_cmdbuffers);
  VkCommandBuffer *submitted_vkcmdbuffers =
      (VkCommandBuffer*)alloca(sizeof(VkCommandBuffer) * nsubmitted_cmdbuffers);
  for (uint32_t c = 0u; c < nsubmitted_cmdbuffers; ++c) {
    submitted_vkcmdbuffers[c] =
        _NGF_DARRAY_AT(frame_sync->submitted_cmdbuffers, c).vkcmdbuf;
  }

  VkSubmitInfo submit_info = {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
    .pNext = NULL,
    .waitSemaphoreCount = wait_sem_count,
    .pWaitSemaphores = wait_sems,
    .pWaitDstStageMask = wait_stage_masks,
    .commandBufferCount = nsubmitted_cmdbuffers,
    .pCommandBuffers = submitted_vkcmdbuffers,
    .signalSemaphoreCount = _NGF_DARRAY_SIZE(frame_sync->wait_vksemaphores),
    .pSignalSemaphores = frame_sync->wait_vksemaphores.data
  };
  vkQueueSubmit(_vk.gfx_queue, 1u, &submit_info, frame_sync->fence);

  if (CURRENT_CONTEXT->swapchain.vk_swapchain != VK_NULL_HANDLE) {
    VkResult present_result = VK_SUCCESS;
    VkPresentInfoKHR present_info = {
      .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
      .pNext = NULL,
      .waitSemaphoreCount =
          (uint32_t)_NGF_DARRAY_SIZE(frame_sync->wait_vksemaphores),
      .pWaitSemaphores = frame_sync->wait_vksemaphores.data,
      .swapchainCount = 1,
      .pSwapchains = &CURRENT_CONTEXT->swapchain.vk_swapchain,
      .pImageIndices = &CURRENT_CONTEXT->swapchain.image_idx,
      .pResults = &present_result
    };
    vkQueuePresentKHR(_vk.present_queue, &present_info);
    if (present_result != VK_SUCCESS) 
      err = NGF_ERROR_END_FRAME_FAILED;
  } 

  // Retire resources
  pthread_mutex_lock(&CURRENT_SHARED_STATE->cmd_pool_mut);
  pthread_mutex_lock(&CURRENT_SHARED_STATE->record_counter_mut);
  while (CURRENT_SHARED_STATE->record_counter > 0u) {
    pthread_cond_wait(&CURRENT_SHARED_STATE->recording_inactive_cond,
                      &CURRENT_SHARED_STATE->record_counter_mut);
  }
  uint32_t next_fi = (fi + 1u) % CURRENT_CONTEXT->max_inflight_frames;
  _ngf_frame_resources *next_frame_sync = &CURRENT_CONTEXT->frame_res[next_fi];
  _ngf_retire_resources(next_frame_sync);
  pthread_mutex_unlock(&CURRENT_SHARED_STATE->record_counter_mut);
  pthread_mutex_unlock(&CURRENT_SHARED_STATE->cmd_pool_mut);
  CURRENT_CONTEXT->frame_number =
      (CURRENT_CONTEXT->frame_number + 1u) %
       CURRENT_CONTEXT->max_inflight_frames;
  return err;
}

ngf_error ngf_create_shader_stage(const ngf_shader_stage_info *info,
                                  ngf_shader_stage *result) {
  assert(info);
  assert(result);

  *result = NGF_ALLOC(ngf_shader_stage_t);
  ngf_shader_stage stage = *result;
  if (stage == NULL) {
    return NGF_ERROR_OUTOFMEM;
  }

  VkShaderModuleCreateInfo vk_sm_info = {
    .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .pNext    = NULL,
    .flags    = 0u,
    .pCode    = (uint32_t*) info->content,
    .codeSize = (info->content_length)
  };
  VkResult vkerr =
    vkCreateShaderModule(_vk.device, &vk_sm_info, NULL, &stage->vk_module);
  if (vkerr != VK_SUCCESS) {
    NGF_FREE(stage);
    return NGF_ERROR_CREATE_SHADER_STAGE_FAILED;
  }
  stage->vk_stage_bits           = get_vk_shader_stage(info->type);
  size_t entry_point_name_length = strlen(info->entry_point_name) + 1u;
  stage->entry_point_name        = NGF_ALLOCN(char, entry_point_name_length);
  strncpy(stage->entry_point_name,
          info->entry_point_name, entry_point_name_length);

  return NGF_ERROR_OK;
}

void ngf_destroy_shader_stage(ngf_shader_stage stage) {
  if (stage) {
    vkDestroyShaderModule(_vk.device, stage->vk_module, NULL);
    NGF_FREEN(stage->entry_point_name, strlen(stage->entry_point_name) + 1u);
    NGF_FREE(stage);
  }
}

ngf_error ngf_create_graphics_pipeline(const ngf_graphics_pipeline_info *info,
                                       ngf_graphics_pipeline *result) {
  assert(info);
  assert(result);
  VkVertexInputBindingDescription *vk_binding_descs = NULL;
  VkVertexInputAttributeDescription *vk_attrib_descs = NULL;
  ngf_error err    = NGF_ERROR_OK;
  VkResult  vk_err = VK_SUCCESS;

  // Allocate space for the pipeline object.
  *result = NGF_ALLOC(ngf_graphics_pipeline_t);
  ngf_graphics_pipeline pipeline = *result;
  if (pipeline == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_graphics_pipeline_cleanup;
  }

  // Prepare shader stages.
  VkPipelineShaderStageCreateInfo vk_shader_stages[5];
  assert(NGF_ARRAYSIZE(vk_shader_stages) ==
         NGF_ARRAYSIZE(info->shader_stages));
  if (info->nshader_stages >= NGF_ARRAYSIZE(vk_shader_stages)) {
    err = NGF_ERROR_OUT_OF_BOUNDS;
    goto ngf_create_graphics_pipeline_cleanup;
  }
  for (uint32_t s = 0u; s < info->nshader_stages; ++s) {
    vk_shader_stages[s].sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vk_shader_stages[s].pNext               = NULL;
    vk_shader_stages[s].flags               = 0u;
    vk_shader_stages[s].stage               = info->shader_stages[s]->vk_stage_bits;
    vk_shader_stages[s].module              = info->shader_stages[s]->vk_module;
    vk_shader_stages[s].pName               = info->shader_stages[s]->entry_point_name,
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
    VkVertexInputBindingDescription   *vk_binding_desc = &vk_binding_descs[i];
    const ngf_vertex_buf_binding_desc *binding_desc =
        &info->input_info->vert_buf_bindings[i];
    vk_binding_desc->binding    = binding_desc->binding;
    vk_binding_descs->stride    = binding_desc->stride;
    vk_binding_descs->inputRate = get_vk_input_rate(binding_desc->input_rate);
  }

  for (uint32_t i = 0u; i < info->input_info->nattribs; ++i) {
    VkVertexInputAttributeDescription *vk_attrib_desc = &vk_attrib_descs[i];
    const ngf_vertex_attrib_desc      *attrib_desc =
        &info->input_info->attribs[i];
    vk_attrib_desc->location = attrib_desc->location;
    vk_attrib_desc->binding  = attrib_desc->binding;
    vk_attrib_desc->offset   = attrib_desc->offset;
    vk_attrib_desc->format   = get_vk_vertex_format(attrib_desc->type,
                                                    attrib_desc->size,
                                                    attrib_desc->normalized);
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

  // Descriptor set layouts.
  _NGF_DARRAY_RESET(pipeline->vk_descriptor_set_layouts,
                    info->layout->ndescriptor_set_layouts);
  for (uint32_t s = 0u; s < info->layout->ndescriptor_set_layouts; ++s) {
    VkDescriptorSetLayoutBinding *vk_descriptor_bindings =
        alloca(sizeof(VkDescriptorSetLayoutBinding) *
               info->layout->descriptor_set_layouts[s].ndescriptors);
    for (uint32_t b = 0u;
         b < info->layout->descriptor_set_layouts[s].ndescriptors;
         ++b) {
      VkDescriptorSetLayoutBinding *vk_d = &vk_descriptor_bindings[b];
      const ngf_descriptor_info *d =
          &info->layout->descriptor_set_layouts[s].descriptors[b];
      vk_d->binding         = d->id;
      vk_d->descriptorCount = 1u;
      vk_d->descriptorType  = get_vk_descriptor_type(d->type);
      vk_d->descriptorCount = 1u;
      vk_d->stageFlags      = get_vk_stage_flags(d->stage_flags);
    }
    const VkDescriptorSetLayoutCreateInfo vk_ds_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = NULL,
      .flags = 0u,
      .bindingCount = info->layout->descriptor_set_layouts[s].ndescriptors,
      .pBindings = vk_descriptor_bindings
    };
    VkDescriptorSetLayout *result_dsl =
        &_NGF_DARRAY_AT(pipeline->vk_descriptor_set_layouts, s);
    vk_err = vkCreateDescriptorSetLayout(_vk.device, &vk_ds_info, NULL,
                                          result_dsl);
    if (vk_err != VK_SUCCESS) {
      // TODO: return error here.
    }
  }

  // Pipeline layout.
  const uint32_t ndescriptor_sets =
      _NGF_DARRAY_SIZE(pipeline->vk_descriptor_set_layouts);
  const VkPipelineLayoutCreateInfo vk_pipeline_layout_info = {
    .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .pNext                  = NULL,
    .flags                  = 0u,
    .setLayoutCount         = ndescriptor_sets,
    .pSetLayouts            = pipeline->vk_descriptor_set_layouts.data,
    .pushConstantRangeCount = 0u,
    .pPushConstantRanges    = NULL
  };
  vk_err = vkCreatePipelineLayout(_vk.device, &vk_pipeline_layout_info, NULL,
                                  &pipeline->vk_pipeline_layout);
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
    .layout = pipeline->vk_pipeline_layout,
    .renderPass = info->compatible_render_target->render_pass,
    .subpass = 0u,
    .basePipelineHandle = VK_NULL_HANDLE,
    .basePipelineIndex = -1
  };

  VkResult vkerr =
      vkCreateGraphicsPipelines(_vk.device,
                                VK_NULL_HANDLE,
                                1u,
                                &vk_pipeline_info,
                                NULL,
                                &pipeline->vk_pipeline);

  if (vkerr != VK_SUCCESS) {
    err = NGF_ERROR_FAILED_TO_CREATE_PIPELINE;
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

void ngf_destroy_graphics_pipeline(ngf_graphics_pipeline p) {
  if (p != NULL) {
    _ngf_frame_resources *res =
        &CURRENT_CONTEXT->frame_res[CURRENT_CONTEXT->frame_number];
    if (p->vk_pipeline != VK_NULL_HANDLE) {
      _NGF_DARRAY_APPEND(res->retire_pipelines, p->vk_pipeline);
    }
    if (p->vk_pipeline_layout != VK_NULL_HANDLE) {
      _NGF_DARRAY_APPEND(res->retire_pipeline_layouts, p->vk_pipeline_layout);
    }
    for (uint32_t l = 0u; l < _NGF_DARRAY_SIZE(p->vk_descriptor_set_layouts);
         ++l) {
      VkDescriptorSetLayout set_layout =
          _NGF_DARRAY_AT(p->vk_descriptor_set_layouts, l);
      if (set_layout != VK_NULL_HANDLE) {
        _NGF_DARRAY_APPEND(res->retire_dset_layouts, set_layout);
      }
    }
    _NGF_DARRAY_DESTROY(p->vk_descriptor_set_layouts);
    NGF_FREE(p);
  }
}


ngf_error ngf_create_image(const ngf_image_info *info, ngf_image *result) {
  assert(info);
  assert(result);
  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_image_t);
  ngf_image img = *result;
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
  vkerr = vmaCreateImage(CURRENT_CONTEXT->allocator, &vk_image_info,
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

void ngf_destroy_image(ngf_image img) {
  if (img != NULL) {
    if (img->vkimg != VK_NULL_HANDLE) {
      vmaDestroyImage(CURRENT_CONTEXT->allocator,
                      img->vkimg, img->alloc);
    }
  }
}

void ngf_destroy_render_target(ngf_render_target target) {
  _NGF_FAKE_USE(target);
  // TODO: implement
}
ngf_error ngf_default_render_target(ngf_attachment_load_op color_load_op,
                                    ngf_attachment_load_op depth_load_op,
                                    ngf_attachment_store_op color_store_op,
                                    ngf_attachment_store_op depth_store_op,
                                    const ngf_clear *clear_color,
                                    const ngf_clear *clear_depth,
                                    ngf_render_target *result) {
  assert(result);
  ngf_render_target rt = NULL;
  ngf_error err = NGF_ERROR_OK;
  _NGF_FAKE_USE(depth_load_op);
  _NGF_FAKE_USE(clear_depth);
  _NGF_FAKE_USE(color_store_op);
  _NGF_FAKE_USE(depth_store_op);
  
  if (CURRENT_CONTEXT->swapchain.vk_swapchain != VK_NULL_HANDLE) {
    rt = NGF_ALLOC(ngf_render_target_t);
    *result = rt;
    if (rt == NULL) {
      err = NGF_ERROR_OUTOFMEM;
      goto ngf_default_render_target_cleanup;
    }
    rt->is_default = true;
    VkAttachmentLoadOp vk_color_load_op = get_vk_load_op(color_load_op);
    // TODO: depth load op
    // TODO: depth store op
    // TODO: color store op
    const _ngf_swapchain *swapchain = &CURRENT_CONTEXT->swapchain;
    VkAttachmentStoreOp vk_color_store_op = VK_ATTACHMENT_STORE_OP_STORE;
    VkAttachmentDescription attachment_descs[2] = {
      swapchain->renderpass.attachment_descs[0],
      swapchain->renderpass.attachment_descs[1]
    };
    attachment_descs[0].loadOp  = vk_color_load_op;
    attachment_descs[0].storeOp = vk_color_store_op;
    attachment_descs[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment_descs[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    // TODO: depth load/store ops and initial/final layouts.
    VkRenderPassCreateInfo renderpass_info = swapchain->renderpass.info;
    renderpass_info.pAttachments = attachment_descs;
    VkResult vk_err = vkCreateRenderPass(_vk.device, &renderpass_info, NULL,
                                         &rt->render_pass);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_RENDER_TARGET_CREATION_FAILED;
      goto ngf_default_render_target_cleanup;
    }
    if (clear_color) {
      _NGF_DARRAY_RESET(rt->clear_values, 2);
      VkClearValue *vk_clear_color = &_NGF_DARRAY_AT(rt->clear_values, 0);
      vk_clear_color->color.float32[0] = clear_color->clear_color[0];
      vk_clear_color->color.float32[1] = clear_color->clear_color[1];
      vk_clear_color->color.float32[2] = clear_color->clear_color[2];
      vk_clear_color->color.float32[3] = clear_color->clear_color[3];
    }
    rt->width  = swapchain->width;
    rt->height = swapchain->height;
    // TODO: depth clear
  } else {
    err = NGF_ERROR_NO_DEFAULT_RENDER_TARGET;
  }

ngf_default_render_target_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_render_target(rt);
  }
  return err;
}

void ngf_debug_message_callback(void *userdata,
                                void(*callback)(const char*, const void*)) {
  // TODO: implement
  _NGF_FAKE_USE(userdata);
  _NGF_FAKE_USE(callback);
}


void ngf_cmd_begin_pass(ngf_cmd_buffer buf, const ngf_render_target target) {
  const _ngf_swapchain *swapchain = &CURRENT_CONTEXT->swapchain;
  const VkFramebuffer fb =
      target->is_default
          ? swapchain->framebuffers[swapchain->image_idx]
          : VK_NULL_HANDLE; // TODO: non-default render targets.
  const VkRenderPassBeginInfo begin_info = {
    .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
    .pNext           = NULL,
    .framebuffer     = fb,
    .clearValueCount = 1u, // TODO: depth
    .pClearValues    = target->clear_values.data,
    .renderPass      = target->render_pass,
    .renderArea = {
      .offset = {0u, 0u},
      .extent = {target->width, target->height}
     }
  };
  vkCmdBeginRenderPass(buf->vkcmdbuf, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
}

void ngf_cmd_end_pass(ngf_cmd_buffer buf) {
  vkCmdEndRenderPass(buf->vkcmdbuf);
}

void ngf_cmd_draw(ngf_cmd_buffer buf, bool indexed,
                  uint32_t first_element, uint32_t nelements,
                  uint32_t ninstances) {
  if (indexed) {
    vkCmdDrawIndexed(buf->vkcmdbuf, nelements, ninstances, first_element,
                     0u, 0u);
  } else {
    vkCmdDraw(buf->vkcmdbuf, nelements, ninstances, first_element, 0u);
  }
}

void ngf_cmd_bind_pipeline(ngf_cmd_buffer buf,
                           const ngf_graphics_pipeline pipeline) {
  vkCmdBindPipeline(buf->vkcmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline->vk_pipeline);
}
void ngf_cmd_viewport(ngf_cmd_buffer buf, const ngf_irect2d *r) {
  const VkViewport viewport = {
    .x        = (float)r->x,
    .y        = (float)r->y,
    .width    = (float)r->width,
    .height   = (float)r->height,
    .minDepth = 0.0f,  // TODO: add depth parameter
    .maxDepth = 1.0f   // TODO: add max depth parameter
  };
  vkCmdSetViewport(buf->vkcmdbuf, 0u, 1u, &viewport);
}

void ngf_cmd_scissor(ngf_cmd_buffer buf, const ngf_irect2d *r) {
  const VkRect2D scissor_rect = {
    .offset = {r->x, r->y},
    .extent = {r->width, r->height}
  };
  vkCmdSetScissor(buf->vkcmdbuf, 0u, 1u, &scissor_rect);
}

void ngf_cmd_bind_attrib_buffer(ngf_cmd_buffer buf,
                                const ngf_attrib_buffer abuf,
                                uint32_t binding,
                                uint32_t offset) {
  VkDeviceSize vkoffset = offset;
  vkCmdBindVertexBuffers(buf->vkcmdbuf, binding, 1, &abuf->vkbuf, &vkoffset);
}

ngf_error ngf_create_attrib_buffer(const ngf_attrib_buffer_info *info,
                                   ngf_attrib_buffer *result) {
  assert(info);
  assert(result);
  ngf_attrib_buffer buf = NGF_ALLOC(ngf_attrib_buffer_t);
  *result = buf;
  if (buf == NULL) {
    return NGF_ERROR_OUTOFMEM;
  }
  buf->size = info->size;/*
  VkBufferCreateInfo staging_buf_vk_info = {
    .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext                 = NULL,
    .flags                 = 0u,
    .size                  = buf->size,
    .usage                 = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0u,
    .pQueueFamilyIndices   = NULL
  };
   VkBufferCreateInfo buf_vk_info = {
    .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext                 = NULL,
    .flags                 = 0u,
    .size                  = buf->size,
    .usage                 = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0u,
    .pQueueFamilyIndices   = NULL
  };
  VmaAllocationCreateInfo staging_buf_alloc_info = {
    .flags          = 0u,
    .usage          = VMA_MEMORY_USAGE_CPU_ONLY,
    .requiredFlags  = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
    .preferredFlags = 0u,
    .memoryTypeBits = 0u,
    .pool           = VK_NULL_HANDLE,
    .pUserData      = NULL
  };
  VmaAllocationCreateInfo buf_alloc_info = {
    .flags          = 0u,
    .usage          = VMA_MEMORY_USAGE_GPU_ONLY,
    .requiredFlags  = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    .preferredFlags = 0u,
    .memoryTypeBits = 0u,
    .pool           = VK_NULL_HANDLE,
    .pUserData      = NULL
  };
  vmaCreateBuffer(CURRENT_CONTEXT->allocator, &buf_vk_info, &buf_alloc_info, &buf->vkbuf, &buf->buf_alloc, NULL);
  vmaCreateBuffer(CURRENT_CONTEXT->allocator, &staging_buf_vk_info, &staging_buf_alloc_info, &buf->vkstaging_buf, &buf->staging_buf_alloc, NULL);
  void *staging_buf_ptr = NULL;
  vmaMapMemory(CURRENT_CONTEXT->allocator, buf->staging_buf_alloc,
               &staging_buf_ptr);
  if (info->buffer_ptr) {
    memcpy(staging_buf_ptr, info->buffer_ptr, buf->size);
  } else {
    info->fill_callback(staging_buf_ptr, buf->size,
                        info->fill_callback_userdata);
  }
  VkBufferCopy copy_region = {
    .srcOffset = 0u,
    .dstOffset = 0u,
    .size      = buf->size
  };
  vkCmdCopyBuffer(info->cmdbuf->vkcmdbuf, buf->vkstaging_buf, buf->vkbuf,
                  1u, &copy_region);
  VkBufferMemoryBarrier buf_mem_bar = {
    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
    .pNext = NULL,
    .buffer = buf->vkbuf,
    .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
    .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
    .srcQueueFamilyIndex = _vk.xfer_family_idx,
    .dstQueueFamilyIndex = _vk.gfx_family_idx,
    .offset = 0,
    .size = buf->size
  };
  vkCmdPipelineBarrier(info->cmdbuf->vkcmdbuf, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0u, NULL,
                       1u, &buf_mem_bar, 0, NULL);*/ 
  return NGF_ERROR_OK;
}

void ngf_destroy_attrib_buffer(ngf_attrib_buffer buffer) {
//TODO: implement
  NGF_FREE(buffer);
}

/*ngf_error ngf_create_index_buffer(const ngf_index_buffer_info *info,
                                  ngf_index_buffer **result) {

}*/
