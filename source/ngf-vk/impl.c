/**
 * Copyright (c) 2023 nicegraf contributors
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#define _CRT_SECURE_NO_WARNINGS
#include "ngf-common/block-alloc.h"
#include "ngf-common/cmdbuf-state.h"
#include "ngf-common/dynamic-array.h"
#include "ngf-common/frame-token.h"
#include "ngf-common/macros.h"
#include "ngf-common/stack-alloc.h"
#include "nicegraf.h"
#include "vk_10.h"

#include <assert.h>
#include <renderdoc_app.h>
#include <spirv_reflect.h>
#include <string.h>
#include <vk_mem_alloc.h>

#pragma region constants

#define NGFVK_INVALID_IDX                      (~0u)
#define NGFVK_MAX_PHYS_DEV                     (64u)  // 64 GPUs oughta be enough for everybody.
#define NGFVK_BIND_OP_CHUNK_SIZE               (10u)
#define NGFVK_MAX_COLOR_ATTACHMENTS            16u
#define NGFVK_IMAGE_USAGE_TRANSIENT_ATTACHMENT (1u << 31u)

#define NGFVK_GFX_PIPELINE_STAGE_MASK                                                   \
  (VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |           \
   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | \
   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT)

#pragma endregion

#pragma region internal_struct_definitions

// Singleton for holding vulkan instance, device and queue handles.
// This is shared by all contexts.
struct {
  VkInstance               instance;
  VkPhysicalDevice         phys_dev;
  VkDevice                 device;
  VkQueue                  gfx_queue;
  VkQueue                  present_queue;
  uint32_t                 gfx_family_idx;
  uint32_t                 present_family_idx;
  VkExtensionProperties*   supported_phys_dev_exts;
  uint32_t                 nsupported_phys_dev_exts;
  bool                     validation_enabled;
  VkDebugUtilsMessengerEXT debug_messenger;
#if defined(__linux__)
  xcb_connection_t* xcb_connection;
  xcb_visualid_t    xcb_visualid;
#endif
} _vk;

// Swapchain state.
typedef struct ngfvk_swapchain {
  VkSwapchainKHR   vk_swapchain;
  VkImage*         images;
  ngf_image*       multisample_images;
  VkImageView*     image_views;
  VkImageView*     multisample_image_views;
  VkSemaphore*     image_semaphores;
  VkFramebuffer*   framebuffers;
  VkPresentModeKHR present_mode;
  ngf_image        depth_image;
  uint32_t         num_images;  // < Total number of images in the swapchain.
  uint32_t         image_idx;   // < The index of currently acquired image.
  uint32_t         width;
  uint32_t         height;
} ngfvk_swapchain;

typedef struct {
  VmaAllocator  parent_allocator;
  uintptr_t     obj_handle;
  VmaAllocation vma_alloc;
  void*         mapped_data;
} ngfvk_alloc;

typedef struct {
  VkBufferViewCreateInfo vk_info;
  VkBufferView           vk_handle;
} ngfvk_buffer_view_info;

typedef uint32_t ngfvk_desc_count[NGF_DESCRIPTOR_TYPE_COUNT];

typedef struct {
  uint32_t         sets;
  ngfvk_desc_count descriptors;
} ngfvk_desc_pool_capacity;

typedef struct {
  VkDescriptorSetLayout vk_handle;
  ngfvk_desc_count      counts;
} ngfvk_desc_set_layout;

typedef struct ngfvk_desc_pool {
  struct ngfvk_desc_pool*  next;
  VkDescriptorPool         vk_pool;
  ngfvk_desc_pool_capacity capacity;
  ngfvk_desc_pool_capacity utilization;
} ngfvk_desc_pool;

typedef struct ngfvk_desc_pools_t {
  ngfvk_desc_pool* active_pool;
  ngfvk_desc_pool* list;
} ngfvk_desc_pools_list;

typedef struct ngfvk_desc_superpool_t {
  uint16_t               ctx_id;
  ngfvk_desc_pools_list* pools_lists;
  uint8_t                num_lists;
} ngfvk_desc_superpool;

typedef struct ngfvk_bind_op_chunk {
  struct ngfvk_bind_op_chunk* next;
  ngf_resource_bind_op        data[NGFVK_BIND_OP_CHUNK_SIZE];
  size_t                      last_idx;
} ngfvk_bind_op_chunk;

typedef struct ngfvk_bind_op_chunk_list {
  ngfvk_bind_op_chunk* first;
  ngfvk_bind_op_chunk* last;
  uint32_t             size;
} ngfvk_bind_op_chunk_list;

// Vulkan resources associated with a given frame.
typedef struct ngfvk_frame_resources {
  NGFI_DARRAY_OF(VkCommandBuffer) cmd_bufs;  // < Submitted vulkan command buffers.
  NGFI_DARRAY_OF(VkCommandPool) cmd_pools;   // < The parent command pools of each  cmd buffer.
  VkSemaphore semaphore;                     // < Signalled when the last cmd buffer finishes.

  // Resources that should be disposed of at some point after this
  // frame's completion.
  NGFI_DARRAY_OF(VkPipeline) retire_pipelines;
  NGFI_DARRAY_OF(VkPipelineLayout) retire_pipeline_layouts;
  NGFI_DARRAY_OF(VkDescriptorSetLayout) retire_dset_layouts;
  NGFI_DARRAY_OF(VkFramebuffer) retire_framebuffers;
  NGFI_DARRAY_OF(VkRenderPass) retire_render_passes;
  NGFI_DARRAY_OF(VkSampler) retire_samplers;
  NGFI_DARRAY_OF(VkImageView) retire_image_views;
  NGFI_DARRAY_OF(VkBufferView) retire_buffer_views;
  NGFI_DARRAY_OF(VkEvent) retire_events;
  NGFI_DARRAY_OF(VkEvent) real_retire_events;
  NGFI_DARRAY_OF(ngfvk_alloc) retire_images;
  NGFI_DARRAY_OF(ngfvk_alloc) retire_buffers;
  NGFI_DARRAY_OF(ngfvk_desc_pools_list*) reset_desc_pools_lists;

  // Fences that will be signaled at the end of the frame.
  VkFence fences[2];

  // Number of fences to wait on to complete all submissions related to this
  // frame.
  uint32_t nwait_fences;
} ngfvk_frame_resources;

typedef struct {
  uint16_t       ctx_id;
  uint8_t        num_pools;
  VkCommandPool* cmd_pools;
} ngfvk_command_superpool;

typedef struct ngfvk_attachment_pass_desc {
  VkImageLayout       initial_layout;
  VkImageLayout       layout;
  VkImageLayout       final_layout;
  VkAttachmentLoadOp  load_op;
  VkAttachmentStoreOp store_op;
  bool                is_resolve;
} ngfvk_attachment_pass_desc;

typedef struct ngfvk_renderpass_cache_entry {
  ngf_render_target rt;
  uint64_t          ops_key;
  VkRenderPass      renderpass;
} ngfvk_renderpass_cache_entry;

#define NGFVK_ENC2CMDBUF(enc) ((ngf_cmd_buffer)((void*)enc.pvt_data_donotuse.d0))

typedef struct ngfvk_device_id {
  uint32_t vendor_id;
  uint32_t device_id;
} ngfvk_device_id;

typedef struct ngfvk_generic_pipeline {
  VkPipeline vk_pipeline;
  NGFI_DARRAY_OF(ngfvk_desc_set_layout) descriptor_set_layouts;
  VkPipelineLayout     vk_pipeline_layout;
  VkSpecializationInfo vk_spec_info;
} ngfvk_generic_pipeline;

#pragma endregion

#pragma region external_struct_definitions

typedef struct ngf_cmd_buffer_t {
  ngf_frame_token          parent_frame;     // < The frame this cmd buffer is associated with.
  ngfi_cmd_buffer_state    state;            // < State of the cmd buffer (i.e. new/recording/etc.)
  VkCommandBuffer          vk_cmd_buffer;    // < Active vulkan command buffer.
  VkCommandPool            vk_cmd_pool;      // < Active vulkan command pool.
  ngf_graphics_pipeline    active_gfx_pipe;  // < The bound graphics pipeline.
  ngf_compute_pipeline     active_compute_pipe;  // < The bound compute pipeline.
  ngf_render_target        active_rt;            // < Active render target.
  ngfvk_bind_op_chunk_list pending_bind_ops;     // < Bind ops to be performed before the next draw.
  ngfvk_desc_pools_list* desc_pools_list;  // < List of descriptor pools used in the buffer's frame.
  bool                   renderpass_active;    // < Has an active renderpass.
  bool                   compute_pass_active;  // < Has an active compute pass.
} ngf_cmd_buffer_t;

typedef struct ngf_sampler_t {
  VkSampler vksampler;
} ngf_sampler_t;

typedef struct ngf_buffer_t {
  ngfvk_alloc             alloc;
  size_t                  size;
  size_t                  mapped_offset;
  uint32_t                usage_flags;
  ngf_buffer_storage_type storage_type;
} ngf_buffer_t;

typedef struct ngf_texel_buffer_view_t {
  VkBufferView vk_buf_view;
} ngf_texel_buffer_view_t;

typedef struct ngf_image_t {
  ngfvk_alloc    alloc;
  ngf_image_type type;
  VkImageView    vkview;
  VkFormat       vkformat;
  ngf_extent3d   extent;
  uint32_t       usage_flags;
  uint32_t       nlevels;
  uint32_t       nlayers;
} ngf_image_t;

// Singleton class for holding on to RenderDoc API
struct {
  RENDERDOC_API_1_6_0* api;
  bool                 is_capturing_next_frame;
} _renderdoc;

typedef struct ngf_context_t {
  ngfvk_frame_resources*      frame_res;
  ngfvk_swapchain             swapchain;
  ngf_swapchain_info          swapchain_info;
  VmaAllocator                allocator;
  VkSurfaceKHR                surface;
  uint32_t                    frame_id;
  uint32_t                    max_inflight_frames;
  ngfi_block_allocator*       bind_op_chunk_allocator;
  ngf_frame_token             current_frame_token;
  ngf_attachment_descriptions default_attachment_descriptions_list;
  ngf_render_target           default_render_target;
  uint64_t                    cmd_buffer_counter;
  NGFI_DARRAY_OF(ngfvk_command_superpool) command_superpools;
  NGFI_DARRAY_OF(ngfvk_desc_superpool) desc_superpools;
  NGFI_DARRAY_OF(ngfvk_renderpass_cache_entry) renderpass_cache;
} ngf_context_t;

typedef struct ngf_shader_stage_t {
  VkShaderModule         vk_module;
  VkShaderStageFlagBits  vk_stage_bits;
  SpvReflectShaderModule spv_reflect_module;
  char*                  entry_point_name;
} ngf_shader_stage_t;

typedef struct ngf_graphics_pipeline_t {
  ngfvk_generic_pipeline generic_pipeline;
  VkRenderPass           compatible_render_pass;
} ngf_graphics_pipeline_t;

typedef struct ngf_compute_pipeline_t {
  ngfvk_generic_pipeline generic_pipeline;
} ngf_compute_pipeline_t;

typedef struct ngf_render_target_t {
  VkFramebuffer               frame_buffer;
  VkRenderPass                compat_render_pass;
  uint32_t                    nattachments;
  ngf_attachment_description* attachment_descs;
  VkImageView*                attachment_image_views; /* unused in default RT, set to NULL. */
  ngfvk_attachment_pass_desc* attachment_compat_pass_descs;
  bool                        is_default;
  uint32_t                    width;
  uint32_t                    height;
} ngf_render_target_t;

typedef struct ngf_event_t {
  VkEvent event;
} ngf_event_t;

#pragma endregion

#pragma region               global_vars
NGFI_THREADLOCAL ngf_context CURRENT_CONTEXT = NULL;

static struct {
  pthread_mutex_t lock;
  NGFI_DARRAY_OF(VkImageMemoryBarrier) barriers;
} NGFVK_PENDING_IMG_BARRIER_QUEUE;

uint32_t         NGFVK_DEVICE_COUNT   = 0u;
ngf_device*      NGFVK_DEVICE_LIST    = NULL;
ngfvk_device_id* NGFVK_DEVICE_ID_LIST = NULL;

ngf_device_capabilities DEVICE_CAPS;

#pragma endregion

#pragma region vk_enum_maps

static VkFilter get_vk_filter(ngf_sampler_filter filter) {
  static const VkFilter vkfilters[NGF_FILTER_COUNT] = {VK_FILTER_NEAREST, VK_FILTER_LINEAR};
  return vkfilters[filter];
}

static VkSamplerAddressMode get_vk_address_mode(ngf_sampler_wrap_mode mode) {
  static const VkSamplerAddressMode vkmodes[NGF_WRAP_MODE_COUNT] = {
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      VK_SAMPLER_ADDRESS_MODE_REPEAT,
      VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT};
  return vkmodes[mode];
}

static VkSamplerMipmapMode get_vk_mipmode(ngf_sampler_filter filter) {
  static const VkSamplerMipmapMode vkmipmodes[NGF_FILTER_COUNT] = {
      VK_SAMPLER_MIPMAP_MODE_NEAREST,
      VK_SAMPLER_MIPMAP_MODE_LINEAR};
  return vkmipmodes[filter];
}

static VkSampleCountFlagBits get_vk_sample_count(ngf_sample_count sample_count) {
  switch (sample_count) {
  case NGF_SAMPLE_COUNT_1:
    return VK_SAMPLE_COUNT_1_BIT;
  case NGF_SAMPLE_COUNT_2:
    return VK_SAMPLE_COUNT_2_BIT;
  case NGF_SAMPLE_COUNT_4:
    return VK_SAMPLE_COUNT_4_BIT;
  case NGF_SAMPLE_COUNT_8:
    return VK_SAMPLE_COUNT_8_BIT;
  case NGF_SAMPLE_COUNT_16:
    return VK_SAMPLE_COUNT_16_BIT;
  case NGF_SAMPLE_COUNT_32:
    return VK_SAMPLE_COUNT_32_BIT;
  case NGF_SAMPLE_COUNT_64:
    return VK_SAMPLE_COUNT_64_BIT;
  default:
    assert(false);  // TODO: return error?
  }
  return VK_SAMPLE_COUNT_1_BIT;
}

static VkDescriptorType get_vk_descriptor_type(ngf_descriptor_type type) {
  static const VkDescriptorType types[NGF_DESCRIPTOR_TYPE_COUNT] = {
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
      VK_DESCRIPTOR_TYPE_SAMPLER,
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
  return types[type];
}

static VkImageType get_vk_image_type(ngf_image_type t) {
  static const VkImageType types[NGF_IMAGE_TYPE_COUNT] = {
      VK_IMAGE_TYPE_2D,
      VK_IMAGE_TYPE_3D,
      VK_IMAGE_TYPE_2D  // In Vulkan cubemaps are treated as array of 2D images.
  };
  return types[t];
}

static VkImageViewType get_vk_image_view_type(ngf_image_type t, size_t nlayers) {
  if (t == NGF_IMAGE_TYPE_IMAGE_2D && nlayers == 1u) {
    return VK_IMAGE_VIEW_TYPE_2D;
  } else if (t == NGF_IMAGE_TYPE_IMAGE_2D && nlayers > 1u) {
    return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  } else if (t == NGF_IMAGE_TYPE_IMAGE_3D) {
    return VK_IMAGE_VIEW_TYPE_3D;
  } else if (t == NGF_IMAGE_TYPE_CUBE && nlayers == 1u) {
    return VK_IMAGE_VIEW_TYPE_CUBE;
  } else if (t == NGF_IMAGE_TYPE_CUBE && nlayers > 1u) {
    return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
  } else {
    NGFI_DIAG_ERROR("Invalid image type");
    assert(false);
    return VK_IMAGE_VIEW_TYPE_2D;
  }
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
      VK_COMPARE_OP_ALWAYS};
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
      VK_STENCIL_OP_INVERT};
  return ops[op];
}

static VkAttachmentLoadOp get_vk_load_op(ngf_attachment_load_op op) {
  static const VkAttachmentLoadOp ops[NGF_LOAD_OP_COUNT] = {
      VK_ATTACHMENT_LOAD_OP_DONT_CARE,
      VK_ATTACHMENT_LOAD_OP_LOAD,
      VK_ATTACHMENT_LOAD_OP_CLEAR};
  return ops[op];
}

static VkAttachmentStoreOp get_vk_store_op(ngf_attachment_store_op op) {
  static const VkAttachmentStoreOp ops[NGF_STORE_OP_COUNT] = {
      VK_ATTACHMENT_STORE_OP_DONT_CARE,
      VK_ATTACHMENT_STORE_OP_STORE,
  };
  return ops[op];
}

static VkBlendFactor get_vk_blend_factor(ngf_blend_factor f) {
  static const VkBlendFactor factors[NGF_BLEND_FACTOR_COUNT] = {
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
      VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA};
  return factors[f];
}

static VkBlendOp get_vk_blend_op(ngf_blend_op op) {
  static const VkBlendOp ops[NGF_BLEND_OP_COUNT] = {
      VK_BLEND_OP_ADD,
      VK_BLEND_OP_SUBTRACT,
      VK_BLEND_OP_REVERSE_SUBTRACT,
      VK_BLEND_OP_MIN,
      VK_BLEND_OP_MAX};
  return ops[op];
}

static VkFormat get_vk_image_format(ngf_image_format f) {
  static const VkFormat formats[NGF_IMAGE_FORMAT_COUNT] = {
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
      VK_FORMAT_A2B10G10R10_UNORM_PACK32,
      VK_FORMAT_R32_SFLOAT,
      VK_FORMAT_R32G32_SFLOAT,
      VK_FORMAT_R32G32B32_SFLOAT,
      VK_FORMAT_R32G32B32A32_SFLOAT,
      VK_FORMAT_R16_SFLOAT,
      VK_FORMAT_R16G16_SFLOAT,
      VK_FORMAT_R16G16B16_SFLOAT,
      VK_FORMAT_R16G16B16A16_SFLOAT,
      VK_FORMAT_B10G11R11_UFLOAT_PACK32,
      VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,
      VK_FORMAT_R16_UNORM,
      VK_FORMAT_R16_SNORM,
      VK_FORMAT_R16G16_UNORM,
      VK_FORMAT_R16G16_SNORM,
      VK_FORMAT_R16G16B16A16_UNORM,
      VK_FORMAT_R16G16B16A16_SNORM,
      VK_FORMAT_R8_UINT,
      VK_FORMAT_R8_SINT,
      VK_FORMAT_R16_UINT,
      VK_FORMAT_R16_SINT,
      VK_FORMAT_R16G16_UINT,
      VK_FORMAT_R16G16B16_UINT,
      VK_FORMAT_R16G16B16A16_UINT,
      VK_FORMAT_R32_UINT,
      VK_FORMAT_R32G32_UINT,
      VK_FORMAT_R32G32B32_UINT,
      VK_FORMAT_R32G32B32A32_UINT,
      VK_FORMAT_BC7_UNORM_BLOCK,
      VK_FORMAT_BC7_SRGB_BLOCK,
      VK_FORMAT_ASTC_4x4_UNORM_BLOCK,
      VK_FORMAT_ASTC_4x4_SRGB_BLOCK,
      VK_FORMAT_ASTC_5x4_UNORM_BLOCK,
      VK_FORMAT_ASTC_5x4_SRGB_BLOCK,
      VK_FORMAT_ASTC_5x5_UNORM_BLOCK,
      VK_FORMAT_ASTC_5x5_SRGB_BLOCK,
      VK_FORMAT_ASTC_6x5_UNORM_BLOCK,
      VK_FORMAT_ASTC_6x5_SRGB_BLOCK,
      VK_FORMAT_ASTC_6x6_UNORM_BLOCK,
      VK_FORMAT_ASTC_6x6_SRGB_BLOCK,
      VK_FORMAT_ASTC_8x5_UNORM_BLOCK,
      VK_FORMAT_ASTC_8x5_SRGB_BLOCK,
      VK_FORMAT_ASTC_8x6_UNORM_BLOCK,
      VK_FORMAT_ASTC_8x6_SRGB_BLOCK,
      VK_FORMAT_ASTC_8x8_UNORM_BLOCK,
      VK_FORMAT_ASTC_8x8_SRGB_BLOCK,
      VK_FORMAT_ASTC_10x5_UNORM_BLOCK,
      VK_FORMAT_ASTC_10x5_SRGB_BLOCK,
      VK_FORMAT_ASTC_10x6_UNORM_BLOCK,
      VK_FORMAT_ASTC_10x6_SRGB_BLOCK,
      VK_FORMAT_ASTC_10x8_UNORM_BLOCK,
      VK_FORMAT_ASTC_10x8_SRGB_BLOCK,
      VK_FORMAT_ASTC_10x10_UNORM_BLOCK,
      VK_FORMAT_ASTC_10x10_SRGB_BLOCK,
      VK_FORMAT_ASTC_12x10_UNORM_BLOCK,
      VK_FORMAT_ASTC_12x10_SRGB_BLOCK,
      VK_FORMAT_ASTC_12x12_UNORM_BLOCK,
      VK_FORMAT_ASTC_12x12_SRGB_BLOCK,
      VK_FORMAT_D32_SFLOAT,
      VK_FORMAT_D16_UNORM,
      VK_FORMAT_D24_UNORM_S8_UINT,
      VK_FORMAT_UNDEFINED};
  return formats[f];
}

static VkPolygonMode get_vk_polygon_mode(ngf_polygon_mode m) {
  static const VkPolygonMode modes[NGF_POLYGON_MODE_COUNT] = {
      VK_POLYGON_MODE_FILL,
      VK_POLYGON_MODE_LINE,
      VK_POLYGON_MODE_POINT};
  return modes[m];
}

static VkCullModeFlagBits get_vk_cull_mode(ngf_cull_mode m) {
  static const VkCullModeFlagBits modes[NGF_CULL_MODE_COUNT] = {
      VK_CULL_MODE_BACK_BIT,
      VK_CULL_MODE_FRONT_BIT,
      VK_CULL_MODE_FRONT_AND_BACK};
  return modes[m];
}

static VkFrontFace get_vk_front_face(ngf_front_face_mode f) {
  static const VkFrontFace modes[NGF_FRONT_FACE_COUNT] = {
      VK_FRONT_FACE_COUNTER_CLOCKWISE,
      VK_FRONT_FACE_CLOCKWISE};
  return modes[f];
}

static VkPrimitiveTopology get_vk_primitive_type(ngf_primitive_topology p) {
  static const VkPrimitiveTopology topos[NGF_PRIMITIVE_TOPOLOGY_COUNT] = {
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
      VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
      VK_PRIMITIVE_TOPOLOGY_LINE_STRIP};
  return topos[p];
}

static VkFormat get_vk_vertex_format(ngf_type type, uint32_t size, bool norm) {
  static const VkFormat normalized_formats[4][4] = {
      {VK_FORMAT_R8_SNORM, VK_FORMAT_R8G8_SNORM, VK_FORMAT_R8G8B8_SNORM, VK_FORMAT_R8G8B8A8_SNORM},
      {VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8A8_UNORM},
      {VK_FORMAT_R16_SNORM,
       VK_FORMAT_R16G16_SNORM,
       VK_FORMAT_R16G16B16_SNORM,
       VK_FORMAT_R16G16B16A16_SNORM},
      {VK_FORMAT_R16_UNORM,
       VK_FORMAT_R16G16_UNORM,
       VK_FORMAT_R16G16B16_UNORM,
       VK_FORMAT_R16G16B16A16_UNORM}};
  static const VkFormat formats[9][4] = {
      {VK_FORMAT_R8_SINT, VK_FORMAT_R8G8_SINT, VK_FORMAT_R8G8B8_SINT, VK_FORMAT_R8G8B8A8_SINT},
      {VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8B8_UINT, VK_FORMAT_R8G8B8A8_UINT},
      {VK_FORMAT_R16_SINT,
       VK_FORMAT_R16G16_SINT,
       VK_FORMAT_R16G16B16_SINT,
       VK_FORMAT_R16G16B16A16_SINT},
      {VK_FORMAT_R16_UINT,
       VK_FORMAT_R16G16_UINT,
       VK_FORMAT_R16G16B16_UINT,
       VK_FORMAT_R16G16B16A16_UINT},
      {VK_FORMAT_R32_SINT,
       VK_FORMAT_R32G32_SINT,
       VK_FORMAT_R32G32B32_SINT,
       VK_FORMAT_R32G32B32A32_SINT},
      {VK_FORMAT_R32_UINT,
       VK_FORMAT_R32G32_UINT,
       VK_FORMAT_R32G32B32_UINT,
       VK_FORMAT_R32G32B32A32_UINT},
      {VK_FORMAT_R32_SFLOAT,
       VK_FORMAT_R32G32_SFLOAT,
       VK_FORMAT_R32G32B32_SFLOAT,
       VK_FORMAT_R32G32B32A32_SFLOAT},
      {VK_FORMAT_R16_SFLOAT,
       VK_FORMAT_R16G16_SFLOAT,
       VK_FORMAT_R16G16B16_SFLOAT,
       VK_FORMAT_R16G16B16A16_SFLOAT},
      {VK_FORMAT_R64_SFLOAT,
       VK_FORMAT_R64G64_SFLOAT,
       VK_FORMAT_R64G64B64_SFLOAT,
       VK_FORMAT_R64G64B64A64_SFLOAT}};

  if ((size < 1 || size > 4) || (norm && type > NGF_TYPE_UINT16)) {
    return VK_FORMAT_UNDEFINED;
  } else if (norm) {
    return normalized_formats[type][size - 1];
  } else {
    return formats[type][size - 1];
  }
}

static VkVertexInputRate get_vk_input_rate(ngf_vertex_input_rate r) {
  static const VkVertexInputRate rates[NGF_VERTEX_INPUT_RATE_COUNT] = {
      VK_VERTEX_INPUT_RATE_VERTEX,
      VK_VERTEX_INPUT_RATE_INSTANCE};
  return rates[r];
}

static VkShaderStageFlagBits get_vk_shader_stage(ngf_stage_type s) {
  static const VkShaderStageFlagBits stages[NGF_STAGE_COUNT] = {
      VK_SHADER_STAGE_VERTEX_BIT,
      VK_SHADER_STAGE_FRAGMENT_BIT,
      VK_SHADER_STAGE_COMPUTE_BIT};
  return stages[s];
}

static VkBufferUsageFlags get_vk_buffer_usage(uint32_t usage) {
  VkBufferUsageFlags flags = 0u;
  if (usage & NGF_BUFFER_USAGE_XFER_DST) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  if (usage & NGF_BUFFER_USAGE_XFER_SRC) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  if (usage & NGF_BUFFER_USAGE_UNIFORM_BUFFER) flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  if (usage & NGF_BUFFER_USAGE_INDEX_BUFFER) flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  if (usage & NGF_BUFFER_USAGE_VERTEX_BUFFER) flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  if (usage & NGF_BUFFER_USAGE_TEXEL_BUFFER) flags |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
  if (usage & NGF_BUFFER_USAGE_STORAGE_BUFFER) flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  return flags;
}

static VkMemoryPropertyFlags get_vk_memory_flags(ngf_buffer_storage_type s) {
  switch (s) {
  case NGF_BUFFER_STORAGE_HOST_READABLE:
    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  case NGF_BUFFER_STORAGE_HOST_WRITEABLE:
  case NGF_BUFFER_STORAGE_HOST_READABLE_WRITEABLE:
    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  case NGF_BUFFER_STORAGE_PRIVATE:
    return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  }
  return 0;
}

static VkIndexType get_vk_index_type(ngf_type t) {
  switch (t) {
  case NGF_TYPE_UINT16:
    return VK_INDEX_TYPE_UINT16;
  case NGF_TYPE_UINT32:
    return VK_INDEX_TYPE_UINT32;
  default:
    return VK_INDEX_TYPE_MAX_ENUM;
  }
}

static VkAccessFlags get_vk_buffer_access_flags(ngf_buffer buf) {
  VkAccessFlags result = 0x00;
  if (buf->usage_flags & NGF_BUFFER_USAGE_VERTEX_BUFFER)
    result |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
  if (buf->usage_flags & NGF_BUFFER_USAGE_INDEX_BUFFER) result |= VK_ACCESS_INDEX_READ_BIT;
  if (buf->usage_flags & NGF_BUFFER_USAGE_UNIFORM_BUFFER) result |= VK_ACCESS_UNIFORM_READ_BIT;
  if (buf->usage_flags & NGF_BUFFER_USAGE_TEXEL_BUFFER) result |= VK_ACCESS_SHADER_READ_BIT;
  if (buf->usage_flags & NGF_BUFFER_USAGE_STORAGE_BUFFER)
    result |= (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
  if (buf->usage_flags & NGF_BUFFER_USAGE_XFER_DST) result |= VK_ACCESS_TRANSFER_WRITE_BIT;
  if (buf->usage_flags & NGF_BUFFER_USAGE_XFER_SRC) result |= VK_ACCESS_TRANSFER_READ_BIT;
  if (buf->storage_type == NGF_BUFFER_STORAGE_HOST_READABLE) result |= VK_ACCESS_HOST_READ_BIT;
  if (buf->storage_type == NGF_BUFFER_STORAGE_HOST_WRITEABLE) result |= VK_ACCESS_HOST_WRITE_BIT;
  if (buf->storage_type == NGF_BUFFER_STORAGE_HOST_READABLE_WRITEABLE)
    result |= VK_ACCESS_HOST_READ_BIT | VK_ACCESS_HOST_WRITE_BIT;
  return result;
}

static bool ngfvk_format_is_depth(VkFormat image_format) {
  return image_format == VK_FORMAT_D16_UNORM || image_format == VK_FORMAT_D16_UNORM_S8_UINT ||
         image_format == VK_FORMAT_D24_UNORM_S8_UINT || image_format == VK_FORMAT_D32_SFLOAT ||
         image_format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

static bool ngfvk_format_is_stencil(VkFormat image_format) {
  return image_format == VK_FORMAT_D24_UNORM_S8_UINT ||
         image_format == VK_FORMAT_D16_UNORM_S8_UINT ||
         image_format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

static VkAccessFlags get_vk_image_access_flags(ngf_image img) {
  VkAccessFlags result = 0x00;
  if ((img->usage_flags & NGF_IMAGE_USAGE_ATTACHMENT)) {
    result |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    if (ngfvk_format_is_depth(img->vkformat) || ngfvk_format_is_stencil(img->vkformat)) {
      result |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
  }
  if (img->usage_flags & NGF_IMAGE_USAGE_SAMPLE_FROM) { result |= VK_ACCESS_SHADER_READ_BIT; }
  if (img->usage_flags & NGF_IMAGE_USAGE_STORAGE) { result |= VK_ACCESS_SHADER_WRITE_BIT; }
  if (img->usage_flags & NGF_IMAGE_USAGE_XFER_DST) { result |= VK_ACCESS_TRANSFER_WRITE_BIT; }
  if (img->usage_flags & NGF_IMAGE_USAGE_XFER_SRC) { result |= VK_ACCESS_TRANSFER_READ_BIT; }
  return result;
}

static VkPipelineStageFlags get_vk_buffer_pipeline_stage_flags(ngf_buffer buf) {
  VkPipelineStageFlags result = 0x00;
  if ((buf->usage_flags & NGF_BUFFER_USAGE_VERTEX_BUFFER) ||
      (buf->usage_flags & NGF_BUFFER_USAGE_INDEX_BUFFER))
    result |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
  if (buf->usage_flags & NGF_BUFFER_USAGE_UNIFORM_BUFFER ||
      buf->usage_flags & NGF_BUFFER_USAGE_TEXEL_BUFFER ||
      buf->usage_flags & NGF_BUFFER_USAGE_STORAGE_BUFFER)
    result |=
        (VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  if (buf->usage_flags & NGF_BUFFER_USAGE_XFER_DST || buf->usage_flags & NGF_BUFFER_USAGE_XFER_SRC)
    result |= VK_PIPELINE_STAGE_TRANSFER_BIT;
  return result;
}

#pragma endregion

#pragma region internal_funcs

void             ngfi_set_allocation_callbacks(const ngf_allocation_callbacks* callbacks);
ngf_sample_count ngfi_get_highest_sample_count(size_t counts_bitmap);

// Handler for messages from validation layers, etc.
// All messages are forwarded to the user-provided debug callback.
static VKAPI_ATTR VkBool32 VKAPI_CALL ngfvk_debug_message_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             type,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*                                       userdata) {
  NGFI_IGNORE_VAR(type);
  NGFI_IGNORE_VAR(userdata);
  ngf_diagnostic_message_type ngf_msg_type;
  switch (severity) {
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
    ngf_msg_type = NGF_DIAGNOSTIC_INFO;
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
    ngf_msg_type = NGF_DIAGNOSTIC_WARNING;
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
  default:
    ngf_msg_type = NGF_DIAGNOSTIC_ERROR;
    break;
  }
  if (ngfi_diag_info.callback) {
    ngfi_diag_info.callback(ngf_msg_type, ngfi_diag_info.userdata, data->pMessage);
  }
  return VK_FALSE;
}

static bool
ngfvk_query_presentation_support(VkPhysicalDevice phys_dev, uint32_t queue_family_index) {
#if defined(_WIN32) || defined(_WIN64)
  return vkGetPhysicalDeviceWin32PresentationSupportKHR(phys_dev, queue_family_index);
#elif defined(__ANDROID__)
  return true;  // All Android queues surfaces support present.
#elif defined(__APPLE__)
  return true;
#else

  if (_vk.xcb_connection == NULL) {
    int                screen_idx = 0;
    xcb_screen_t*      screen     = NULL;
    xcb_connection_t*  connection = xcb_connect(NULL, &screen_idx);
    const xcb_setup_t* setup      = xcb_get_setup(connection);
    for (xcb_screen_iterator_t it = xcb_setup_roots_iterator(setup); screen >= 0 && it.rem;
         xcb_screen_next(&it)) {
      if (screen_idx-- == 0) { screen = it.data; }
    }
    assert(screen);
    _vk.xcb_connection = connection;
    _vk.xcb_visualid   = screen->root_visual;
  }
  return vkGetPhysicalDeviceXcbPresentationSupportKHR(
      phys_dev,
      queue_family_index,
      _vk.xcb_connection,
      _vk.xcb_visualid);
#endif
}

static ngf_error ngfvk_create_vk_image_view(
    VkImage         image,
    VkImageViewType image_type,
    VkFormat        image_format,
    uint32_t        nmips,
    uint32_t        nlayers,
    VkImageView*    result) {
  const bool is_depth   = ngfvk_format_is_depth(image_format);
  const bool is_stencil = ngfvk_format_is_stencil(image_format);

  const VkImageViewCreateInfo image_view_info = {
      .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .pNext    = NULL,
      .flags    = 0u,
      .image    = image,
      .viewType = image_type,
      .format   = image_format,
      .components =
          {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
           .g = VK_COMPONENT_SWIZZLE_IDENTITY,
           .b = VK_COMPONENT_SWIZZLE_IDENTITY,
           .a = VK_COMPONENT_SWIZZLE_IDENTITY},
      .subresourceRange = {
          .aspectMask     = is_depth ? (VK_IMAGE_ASPECT_DEPTH_BIT |
                                    (is_stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0))
                                     : VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel   = 0u,
          .levelCount     = nmips,
          .baseArrayLayer = 0u,
          .layerCount     = nlayers}};
  const VkResult create_view_vkerr = vkCreateImageView(_vk.device, &image_view_info, NULL, result);
  if (create_view_vkerr != VK_SUCCESS) {
    return NGF_ERROR_INVALID_OPERATION;
  } else {
    return NGF_ERROR_OK;
  }
}

static void ngfvk_destroy_swapchain(ngfvk_swapchain* swapchain) {
  assert(swapchain);
  vkDeviceWaitIdle(_vk.device);

  for (uint32_t s = 0u; swapchain->image_semaphores != NULL && s < swapchain->num_images; ++s) {
    if (swapchain->image_semaphores[s] != VK_NULL_HANDLE) {
      vkDestroySemaphore(_vk.device, swapchain->image_semaphores[s], NULL);
    }
  }
  if (swapchain->image_semaphores != NULL) {
    NGFI_FREEN(swapchain->image_semaphores, swapchain->num_images);
  }

  for (uint32_t f = 0u; swapchain->framebuffers && f < swapchain->num_images; ++f) {
    vkDestroyFramebuffer(_vk.device, swapchain->framebuffers[f], NULL);
  }
  if (swapchain->framebuffers != NULL) {
    NGFI_FREEN(swapchain->framebuffers, swapchain->num_images);
  }

  for (uint32_t v = 0u; swapchain->image_views != NULL && v < swapchain->num_images; ++v) {
    vkDestroyImageView(_vk.device, swapchain->image_views[v], NULL);
  }
  if (swapchain->image_views) { NGFI_FREEN(swapchain->image_views, swapchain->num_images); }

  for (uint32_t v = 0u; swapchain->multisample_image_views != NULL && v < swapchain->num_images;
       ++v) {
    vkDestroyImageView(_vk.device, swapchain->multisample_image_views[v], NULL);
  }
  if (swapchain->multisample_image_views) {
    NGFI_FREEN(swapchain->multisample_image_views, swapchain->num_images);
  }

  for (uint32_t i = 0u; swapchain->multisample_images && i < swapchain->num_images; ++i) {
    ngf_destroy_image(swapchain->multisample_images[i]);
  }
  if (swapchain->multisample_images) {
    NGFI_FREEN(swapchain->multisample_images, swapchain->num_images);
  }

  if (swapchain->vk_swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(_vk.device, swapchain->vk_swapchain, NULL);
  }

  if (swapchain->depth_image) { ngf_destroy_image(swapchain->depth_image); }
}

static ngf_error ngfvk_create_swapchain(
    const ngf_swapchain_info* swapchain_info,
    VkSurfaceKHR              surface,
    ngfvk_swapchain*          swapchain) {
  assert(swapchain_info);
  assert(swapchain);

  ngf_error        err          = NGF_ERROR_OK;
  VkResult         vk_err       = VK_SUCCESS;
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;

  memset(swapchain, 0, sizeof(ngfvk_swapchain));

  // Check available present modes and fall back on FIFO if the requested
  // present mode is not supported.
  {
    uint32_t npresent_modes = 0u;
    vkGetPhysicalDeviceSurfacePresentModesKHR(_vk.phys_dev, surface, &npresent_modes, NULL);
    VkPresentModeKHR* present_modes = NGFI_ALLOCN(VkPresentModeKHR, npresent_modes);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        _vk.phys_dev,
        surface,
        &npresent_modes,
        present_modes);
    static const VkPresentModeKHR modes[] = {
        VK_PRESENT_MODE_FIFO_KHR,
        VK_PRESENT_MODE_IMMEDIATE_KHR};
    const VkPresentModeKHR requested_present_mode = modes[swapchain_info->present_mode];
    for (uint32_t p = 0u; p < npresent_modes; ++p) {
      if (present_modes[p] == requested_present_mode) {
        present_mode = present_modes[p];
        break;
      }
    }
    NGFI_FREEN(present_modes, npresent_modes);
    present_modes = NULL;
  }

  // Check if the requested surface format is valid.
  uint32_t nformats = 0u;
  vkGetPhysicalDeviceSurfaceFormatsKHR(_vk.phys_dev, surface, &nformats, NULL);
  VkSurfaceFormatKHR* formats = NGFI_ALLOCN(VkSurfaceFormatKHR, nformats);
  assert(formats);
  vkGetPhysicalDeviceSurfaceFormatsKHR(_vk.phys_dev, surface, &nformats, formats);
  const VkFormat requested_format = get_vk_image_format(swapchain_info->color_format);
  if (!(nformats == 1 && formats[0].format == VK_FORMAT_UNDEFINED)) {
    bool found = false;
    for (size_t f = 0; !found && f < nformats; ++f) {
      found = formats[f].format == requested_format;
    }
    if (!found) {
      NGFI_DIAG_ERROR("Invalid swapchain image format requested.");
      err = NGF_ERROR_INVALID_FORMAT;
      goto ngfvk_create_swapchain_cleanup;
    }
  }
  NGFI_FREEN(formats, nformats);
  formats = NULL;
  VkSurfaceCapabilitiesKHR surface_caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_vk.phys_dev, surface, &surface_caps);
  const VkExtent2D min_surface_extent = surface_caps.minImageExtent;
  const VkExtent2D max_surface_extent = surface_caps.maxImageExtent;

  // Determine if we should use exclusive or concurrent sharing mode for
  // swapchain images.
  const bool          exclusive_sharing = _vk.gfx_family_idx == _vk.present_family_idx;
  const VkSharingMode sharing_mode =
      exclusive_sharing ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
  const uint32_t num_sharing_queue_families = exclusive_sharing ? 0 : 2;
  const uint32_t sharing_queue_families[]   = {_vk.gfx_family_idx, _vk.present_family_idx};
  ;
  // Create swapchain.
  const VkSwapchainCreateInfoKHR vk_sc_info = {
      .sType           = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .pNext           = NULL,
      .flags           = 0,
      .surface         = surface,
      .minImageCount   = swapchain_info->capacity_hint,
      .imageFormat     = requested_format,
      .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
      .imageExtent =
          {.width = NGFI_MIN(
               max_surface_extent.width,
               NGFI_MAX(min_surface_extent.width, swapchain_info->width)),
           .height = NGFI_MIN(
               max_surface_extent.height,
               NGFI_MAX(min_surface_extent.height, swapchain_info->height))},
      .imageArrayLayers      = 1,
      .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .imageSharingMode      = sharing_mode,
      .queueFamilyIndexCount = num_sharing_queue_families,
      .pQueueFamilyIndices   = sharing_queue_families,
      .preTransform          = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .compositeAlpha        = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode           = present_mode};
  vk_err = vkCreateSwapchainKHR(_vk.device, &vk_sc_info, NULL, &swapchain->vk_swapchain);
  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngfvk_create_swapchain_cleanup;
  }

  // Obtain swapchain images.
  vk_err =
      vkGetSwapchainImagesKHR(_vk.device, swapchain->vk_swapchain, &swapchain->num_images, NULL);
  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngfvk_create_swapchain_cleanup;
  }
  swapchain->images = NGFI_ALLOCN(VkImage, swapchain->num_images);
  if (swapchain->images == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngfvk_create_swapchain_cleanup;
  }
  vk_err = vkGetSwapchainImagesKHR(
      _vk.device,
      swapchain->vk_swapchain,
      &swapchain->num_images,
      swapchain->images);
  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngfvk_create_swapchain_cleanup;
  }

  const bool is_multisampled = swapchain_info->sample_count > 1u;

  // Create multisampled images, if necessary.
  if (is_multisampled) {
    const ngf_image_info ms_image_info = {
        .type    = NGF_IMAGE_TYPE_IMAGE_2D,
        .extent  = {.width = swapchain_info->width, .height = swapchain_info->height, .depth = 1u},
        .nmips   = 1u,
        .nlayers = 1u,
        .format  = swapchain_info->color_format,
        .sample_count = swapchain_info->sample_count,
        .usage_hint   = NGF_IMAGE_USAGE_ATTACHMENT | NGFVK_IMAGE_USAGE_TRANSIENT_ATTACHMENT,
    };
    swapchain->multisample_images = NGFI_ALLOCN(ngf_image, swapchain->num_images);
    if (swapchain->multisample_images == NULL) {
      err = NGF_ERROR_OUT_OF_MEM;
      goto ngfvk_create_swapchain_cleanup;
    }
    for (size_t i = 0u; i < swapchain->num_images; ++i) {
      const ngf_error img_create_error =
          ngf_create_image(&ms_image_info, &swapchain->multisample_images[i]);
      if (img_create_error != NGF_ERROR_OK) {
        err = img_create_error;
        goto ngfvk_create_swapchain_cleanup;
      }
    }
    // Create image views for multisample images.
    swapchain->multisample_image_views = NGFI_ALLOCN(VkImageView, swapchain->num_images);
    if (swapchain->multisample_image_views == NULL) {
      err = NGF_ERROR_OUT_OF_MEM;
      goto ngfvk_create_swapchain_cleanup;
    }
    for (uint32_t i = 0u; i < swapchain->num_images; ++i) {
      err = ngfvk_create_vk_image_view(
          (VkImage)swapchain->multisample_images[i]->alloc.obj_handle,
          VK_IMAGE_VIEW_TYPE_2D,
          requested_format,
          1u,
          1u,
          &swapchain->multisample_image_views[i]);
      if (err != NGF_ERROR_OK) { goto ngfvk_create_swapchain_cleanup; }
    }

  } else {
    swapchain->multisample_images      = NULL;
    swapchain->multisample_image_views = NULL;
  }

  // Create image views for swapchain images.
  swapchain->image_views = NGFI_ALLOCN(VkImageView, swapchain->num_images);
  if (swapchain->image_views == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngfvk_create_swapchain_cleanup;
  }
  for (uint32_t i = 0u; i < swapchain->num_images; ++i) {
    err = ngfvk_create_vk_image_view(
        swapchain->images[i],
        VK_IMAGE_VIEW_TYPE_2D,
        requested_format,
        1u,
        1u,
        &swapchain->image_views[i]);
    if (err != NGF_ERROR_OK) { goto ngfvk_create_swapchain_cleanup; }
  }

  // Determine if we need a depth attachment.
  const bool have_depth_attachment = swapchain_info->depth_format != NGF_IMAGE_FORMAT_UNDEFINED;

  // Create an image for the depth attachment if necessary.
  if (have_depth_attachment) {
    const ngf_image_info depth_image_info = {
        .type    = NGF_IMAGE_TYPE_IMAGE_2D,
        .extent  = {.width = swapchain_info->width, .height = swapchain_info->height, .depth = 1u},
        .nmips   = 1u,
        .nlayers = 1u,
        .sample_count = swapchain_info->sample_count,
        .format       = swapchain_info->depth_format,
        .usage_hint   = NGF_IMAGE_USAGE_ATTACHMENT |
                      (is_multisampled ? NGFVK_IMAGE_USAGE_TRANSIENT_ATTACHMENT : 0u)};
    err = ngf_create_image(&depth_image_info, &swapchain->depth_image);
    if (err != NGF_ERROR_OK) { goto ngfvk_create_swapchain_cleanup; }
  } else {
    swapchain->depth_image = NULL;
  }

  // Create framebuffers for swapchain images.
  swapchain->framebuffers = NGFI_ALLOCN(VkFramebuffer, swapchain->num_images);
  if (swapchain->framebuffers == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngfvk_create_swapchain_cleanup;
  }
  const bool     have_resolve_attachment      = swapchain_info->sample_count > 1u;
  const uint32_t depth_stencil_attachment_idx = swapchain->depth_image ? 1u : VK_ATTACHMENT_UNUSED;
  const uint32_t resolve_attachment_idx =
      have_resolve_attachment ? (swapchain->depth_image ? 2u : 1u) : VK_ATTACHMENT_UNUSED;
  const uint32_t nattachments = CURRENT_CONTEXT->default_render_target->nattachments;
  for (uint32_t f = 0u; f < swapchain->num_images; ++f) {
    VkImageView attachment_views[3];
    attachment_views[0] =
        is_multisampled ? swapchain->multisample_image_views[f] : swapchain->image_views[f];
    if (depth_stencil_attachment_idx != VK_ATTACHMENT_UNUSED) {
      attachment_views[depth_stencil_attachment_idx] = swapchain->depth_image->vkview;
    }
    if (resolve_attachment_idx != VK_ATTACHMENT_UNUSED) {
      attachment_views[resolve_attachment_idx] = swapchain->image_views[f];
    }
    const VkFramebufferCreateInfo fb_info = {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .pNext           = NULL,
        .flags           = 0u,
        .renderPass      = CURRENT_CONTEXT->default_render_target->compat_render_pass,
        .attachmentCount = nattachments,
        .pAttachments    = attachment_views,
        .width           = swapchain_info->width,
        .height          = swapchain_info->height,
        .layers          = 1u};
    vk_err = vkCreateFramebuffer(_vk.device, &fb_info, NULL, &swapchain->framebuffers[f]);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_OBJECT_CREATION_FAILED;
      goto ngfvk_create_swapchain_cleanup;
    }
  }

  // Create semaphores to be signaled when a swapchain image becomes available.
  swapchain->image_semaphores = NGFI_ALLOCN(VkSemaphore, swapchain->num_images);
  if (swapchain->image_semaphores == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngfvk_create_swapchain_cleanup;
  }
  memset(swapchain->image_semaphores, 0, sizeof(VkSemaphore) * swapchain->num_images);
  for (uint32_t s = 0u; s < swapchain->num_images; ++s) {
    const VkSemaphoreCreateInfo sem_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0};
    vk_err = vkCreateSemaphore(_vk.device, &sem_info, NULL, &swapchain->image_semaphores[s]);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_OBJECT_CREATION_FAILED;
      goto ngfvk_create_swapchain_cleanup;
    }
  }
  swapchain->image_idx = 0U;
  swapchain->width     = swapchain_info->width;
  swapchain->height    = swapchain_info->height;

ngfvk_create_swapchain_cleanup:
  if (err != NGF_ERROR_OK) { ngfvk_destroy_swapchain(swapchain); }
  return err;
}

static void ngfvk_retire_resources(ngfvk_frame_resources* frame_res) {
  if (frame_res->nwait_fences > 0u) {
    VkResult wait_status = VK_SUCCESS;
    do {
      wait_status = vkWaitForFences(
          _vk.device,
          frame_res->nwait_fences,
          frame_res->fences,
          VK_TRUE,
          0x3B9ACA00ul);
    } while (wait_status == VK_TIMEOUT);
    vkResetFences(_vk.device, frame_res->nwait_fences, frame_res->fences);
    frame_res->nwait_fences = 0;
  }

  NGFI_DARRAY_FOREACH(frame_res->retire_pipelines, p) {
    vkDestroyPipeline(_vk.device, NGFI_DARRAY_AT(frame_res->retire_pipelines, p), NULL);
  }

  NGFI_DARRAY_FOREACH(frame_res->retire_pipeline_layouts, p) {
    vkDestroyPipelineLayout(
        _vk.device,
        NGFI_DARRAY_AT(frame_res->retire_pipeline_layouts, p),
        NULL);
  }

  NGFI_DARRAY_FOREACH(frame_res->retire_dset_layouts, p) {
    vkDestroyDescriptorSetLayout(
        _vk.device,
        NGFI_DARRAY_AT(frame_res->retire_dset_layouts, p),
        NULL);
  }

  NGFI_DARRAY_FOREACH(frame_res->retire_framebuffers, p) {
    vkDestroyFramebuffer(_vk.device, NGFI_DARRAY_AT(frame_res->retire_framebuffers, p), NULL);
  }

  NGFI_DARRAY_FOREACH(frame_res->retire_render_passes, p) {
    vkDestroyRenderPass(_vk.device, NGFI_DARRAY_AT(frame_res->retire_render_passes, p), NULL);
  }

  NGFI_DARRAY_FOREACH(frame_res->retire_samplers, s) {
    vkDestroySampler(_vk.device, NGFI_DARRAY_AT(frame_res->retire_samplers, s), NULL);
  }

  NGFI_DARRAY_FOREACH(frame_res->retire_images, s) {
    ngfvk_alloc img = NGFI_DARRAY_AT(frame_res->retire_images, s);
    vmaDestroyImage(img.parent_allocator, (VkImage)img.obj_handle, img.vma_alloc);
  }

  NGFI_DARRAY_FOREACH(frame_res->retire_image_views, s) {
    vkDestroyImageView(_vk.device, NGFI_DARRAY_AT(frame_res->retire_image_views, s), NULL);
  }

  NGFI_DARRAY_FOREACH(frame_res->retire_buffer_views, s) {
    vkDestroyBufferView(_vk.device, NGFI_DARRAY_AT(frame_res->retire_buffer_views, s), NULL);
  }

  NGFI_DARRAY_FOREACH(frame_res->real_retire_events, s) {
    vkDestroyEvent(_vk.device, NGFI_DARRAY_AT(frame_res->real_retire_events, s), NULL);
  }
  NGFI_DARRAY_CLEAR(frame_res->real_retire_events);
  NGFI_DARRAY_FOREACH(frame_res->retire_events, s) {
    NGFI_DARRAY_APPEND(frame_res->real_retire_events, NGFI_DARRAY_AT(frame_res->retire_events, s));
  }

  NGFI_DARRAY_FOREACH(frame_res->retire_buffers, a) {
    ngfvk_alloc* b = &(NGFI_DARRAY_AT(frame_res->retire_buffers, a));
    vmaDestroyBuffer(b->parent_allocator, (VkBuffer)b->obj_handle, b->vma_alloc);
  }

  NGFI_DARRAY_FOREACH(frame_res->reset_desc_pools_lists, p) {
    ngfvk_desc_pools_list* superpool = NGFI_DARRAY_AT(frame_res->reset_desc_pools_lists, p);
    for (ngfvk_desc_pool* pool = superpool->list; pool; pool = pool->next) {
      vkResetDescriptorPool(_vk.device, pool->vk_pool, 0u);
      memset(&pool->utilization, 0, sizeof(pool->utilization));
    }
    superpool->active_pool = superpool->list;
  }

  NGFI_DARRAY_FOREACH(frame_res->cmd_bufs, i) {
    if (frame_res->cmd_pools.data[i]) {
      vkFreeCommandBuffers(
          _vk.device,
          frame_res->cmd_pools.data[i],
          1,
          &(frame_res->cmd_bufs.data[i]));
    }
  }

  NGFI_DARRAY_FOREACH(frame_res->cmd_pools, i) {
    if (frame_res->cmd_pools.data[i]) {
      vkResetCommandPool(
          _vk.device,
          frame_res->cmd_pools.data[i],
          VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
    }
  }

  NGFI_DARRAY_CLEAR(frame_res->cmd_bufs);
  NGFI_DARRAY_CLEAR(frame_res->cmd_pools);
  NGFI_DARRAY_CLEAR(frame_res->retire_pipelines);
  NGFI_DARRAY_CLEAR(frame_res->retire_dset_layouts);
  NGFI_DARRAY_CLEAR(frame_res->retire_framebuffers);
  NGFI_DARRAY_CLEAR(frame_res->retire_render_passes);
  NGFI_DARRAY_CLEAR(frame_res->retire_samplers);
  NGFI_DARRAY_CLEAR(frame_res->retire_image_views);
  NGFI_DARRAY_CLEAR(frame_res->retire_buffer_views);
  NGFI_DARRAY_CLEAR(frame_res->retire_events);
  NGFI_DARRAY_CLEAR(frame_res->retire_images);
  NGFI_DARRAY_CLEAR(frame_res->retire_pipeline_layouts);
  NGFI_DARRAY_CLEAR(frame_res->retire_buffers);
  NGFI_DARRAY_CLEAR(frame_res->reset_desc_pools_lists);
}

static void ngfvk_cleanup_pending_binds(ngf_cmd_buffer cmd_buf) {
  ngfvk_bind_op_chunk* chunk = cmd_buf->pending_bind_ops.first;
  while (chunk) {
    ngfvk_bind_op_chunk* next = chunk->next;
    ngfi_blkalloc_free(CURRENT_CONTEXT->bind_op_chunk_allocator, chunk);
    chunk = next;
  }
  cmd_buf->pending_bind_ops.first = cmd_buf->pending_bind_ops.last = NULL;
  cmd_buf->pending_bind_ops.size                                   = 0u;
}

static ngf_error ngfvk_encoder_start(ngf_cmd_buffer cmd_buf) {
  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_RECORDING);
  return NGF_ERROR_OK;
}

static ngf_error
ngfvk_initialize_generic_encoder(ngf_cmd_buffer cmd_buf, struct ngfi_private_encoder_data* enc) {
  enc->d0                          = (uintptr_t)cmd_buf;
  const VkEventCreateInfo event_ci = {
      .sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO,
      .pNext = NULL,
      .flags = 0u,
  };
  VkEvent        ev     = VK_NULL_HANDLE;
  const VkResult result = vkCreateEvent(_vk.device, &event_ci, NULL, &ev);
  enc->d1               = (uintptr_t)ev;

  return result == VK_SUCCESS ? NGF_ERROR_OK : NGF_ERROR_OBJECT_CREATION_FAILED;
}

static ngf_error ngfvk_encoder_end(
    ngf_cmd_buffer                    cmd_buf,
    struct ngfi_private_encoder_data* generic_enc,
    VkPipelineStageFlags              stage_mask) {
  vkCmdSetEvent(cmd_buf->vk_cmd_buffer, (VkEvent)generic_enc->d1, stage_mask);
  ngfvk_cleanup_pending_binds(cmd_buf);
  const size_t fi = CURRENT_CONTEXT->frame_id;
  NGFI_DARRAY_APPEND(CURRENT_CONTEXT->frame_res[fi].retire_events, (VkEvent)generic_enc->d1);
  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_AWAITING_SUBMIT);
  return NGF_ERROR_OK;
}

static void ngfvk_cmd_copy_buffer(
    VkCommandBuffer      vkcmdbuf,
    VkBuffer             src,
    VkBuffer             dst,
    size_t               size,
    size_t               src_offset,
    size_t               dst_offset,
    VkAccessFlags        usage_access_mask,
    VkPipelineStageFlags usage_stage_mask) {
  const VkBufferCopy copy_region = {.srcOffset = src_offset, .dstOffset = dst_offset, .size = size};

  VkBufferMemoryBarrier pre_xfer_mem_bar = {
      .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .pNext               = NULL,
      .buffer              = dst,
      .srcAccessMask       = usage_access_mask,
      .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .offset              = dst_offset,
      .size                = size};
  vkCmdPipelineBarrier(
      vkcmdbuf,
      usage_stage_mask,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      0,
      0u,
      NULL,
      1u,
      &pre_xfer_mem_bar,
      0,
      NULL);
  vkCmdCopyBuffer(vkcmdbuf, src, dst, 1u, &copy_region);

  VkBufferMemoryBarrier post_xfer_mem_bar = {
      .sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
      .pNext               = NULL,
      .buffer              = dst,
      .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask       = usage_access_mask,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .offset              = dst_offset,
      .size                = size};

  vkCmdPipelineBarrier(
      vkcmdbuf,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      usage_stage_mask,
      0,
      0u,
      NULL,
      1u,
      &post_xfer_mem_bar,
      0,
      NULL);
}

static void ngfvk_destroy_cmd_pools(VkCommandPool* pools, uint32_t npools) {
  if (pools) {
    for (size_t i = 0; i < npools; ++i) {
      if (pools[i]) { vkDestroyCommandPool(_vk.device, pools[i], NULL); }
    }
    NGFI_FREEN(pools, npools);
  }
}

static ngf_error
ngfvk_initialize_cmd_pools(uint32_t queue_family_idx, VkCommandPool* pools, uint32_t npools) {
  memset(pools, 0, sizeof(VkCommandPool) * npools);
  for (uint32_t i = 0; i < npools; ++i) {
    const VkCommandPoolCreateInfo pool_ci = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext            = NULL,
        .queueFamilyIndex = queue_family_idx,
        .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT};
    if (vkCreateCommandPool(_vk.device, &pool_ci, NULL, &pools[i]) != VK_SUCCESS)
      return NGF_ERROR_OBJECT_CREATION_FAILED;
  }
  return NGF_ERROR_OK;
}

static void ngfvk_destroy_command_superpool(ngfvk_command_superpool* superpool) {
  ngfvk_destroy_cmd_pools(superpool->cmd_pools, superpool->num_pools);
}

static ngf_error ngfvk_initialize_command_superpool(
    ngfvk_command_superpool* superpool,
    uint8_t                  npools,
    uint16_t                 ctx_id) {
  ngf_error err        = NGF_ERROR_OK;
  superpool->ctx_id    = ctx_id;
  superpool->num_pools = npools;
  superpool->cmd_pools = NGFI_ALLOCN(VkCommandPool, npools);
  if (superpool->cmd_pools == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngfvk_initialize_command_superpool_cleanup;
  }
  if (ngfvk_initialize_cmd_pools(_vk.gfx_family_idx, superpool->cmd_pools, npools) !=
      NGF_ERROR_OK) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngfvk_initialize_command_superpool_cleanup;
  }

ngfvk_initialize_command_superpool_cleanup:
  if (err != NGF_ERROR_OK) ngfvk_destroy_command_superpool(superpool);
  return err;
}

static ngfvk_command_superpool* ngfvk_find_command_superpool(uint16_t ctx_id, uint8_t nframes) {
  ngfvk_command_superpool* result = NULL;
  NGFI_DARRAY_FOREACH(CURRENT_CONTEXT->command_superpools, i) {
    if (CURRENT_CONTEXT->command_superpools.data[i].ctx_id == ctx_id) {
      result = &CURRENT_CONTEXT->command_superpools.data[i];
      break;
    }
  }

  if (result == NULL) {
    ngfvk_command_superpool s = {
        .ctx_id    = (uint16_t)~0,
        .num_pools = 0,
    };
    NGFI_DARRAY_APPEND(CURRENT_CONTEXT->command_superpools, s);
    ngfvk_initialize_command_superpool(
        NGFI_DARRAY_BACKPTR(CURRENT_CONTEXT->command_superpools),
        nframes,
        ctx_id);
    result = NGFI_DARRAY_BACKPTR(CURRENT_CONTEXT->command_superpools);
  }

  return result;
}

static ngf_error ngfvk_cmd_buffer_allocate_for_frame(
    ngf_frame_token  frame_token,
    VkCommandPool*   pool,
    VkCommandBuffer* cmd_buf) {
  const ngfvk_command_superpool* superpool = ngfvk_find_command_superpool(
      ngfi_frame_ctx_id(frame_token),
      ngfi_frame_max_inflight_frames(frame_token));
  if (superpool == NULL || superpool->cmd_pools == NULL) {
    NGFI_DIAG_ERROR("failed to allocate command buffer");
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }
  const VkCommandPool* pools                       = superpool->cmd_pools;
  *pool                                            = pools[ngfi_frame_id(frame_token)];
  const VkCommandBufferAllocateInfo vk_cmdbuf_info = {
      .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext              = NULL,
      .commandPool        = *pool,
      .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1u};
  const VkResult vk_err = vkAllocateCommandBuffers(_vk.device, &vk_cmdbuf_info, cmd_buf);
  if (vk_err != VK_SUCCESS) {
    NGFI_DIAG_ERROR("Failed to allocate cmd buffer, VK error: %d", vk_err);
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }
  const VkCommandBufferBeginInfo cmd_buf_begin = {
      .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext            = NULL,
      .flags            = 0,
      .pInheritanceInfo = NULL};
  vkBeginCommandBuffer(*cmd_buf, &cmd_buf_begin);
  return NGF_ERROR_OK;
}

static ngf_error
ngfvk_create_desc_superpool(ngfvk_desc_superpool* superpool, uint8_t pools_lists, uint16_t ctx_id) {
  superpool->ctx_id      = ctx_id;
  superpool->pools_lists = NGFI_ALLOCN(ngfvk_desc_pools_list, pools_lists);
  superpool->num_lists   = pools_lists;
  memset(superpool->pools_lists, 0, pools_lists * sizeof(ngfvk_desc_pools_list));
  return NGF_ERROR_OK;
}

static void ngfvk_destroy_desc_superpool(ngfvk_desc_superpool* superpool) {
  for (uint8_t i = 0u; i < superpool->num_lists; ++i) {
    ngfvk_desc_pool* p = superpool->pools_lists[i].list;
    while (p) {
      vkDestroyDescriptorPool(_vk.device, p->vk_pool, NULL);
      ngfvk_desc_pool* next = p->next;
      NGFI_FREE(p);
      p = next;
    }
  }
  NGFI_FREEN(superpool->pools_lists, superpool->num_lists);
}

static ngfvk_desc_pools_list* ngfvk_find_desc_pools_list(ngf_frame_token token) {
  const uint16_t ctx_id   = ngfi_frame_ctx_id(token);
  const uint8_t  nframes  = ngfi_frame_max_inflight_frames(token);
  const uint8_t  frame_id = ngfi_frame_id(token);

  ngfvk_desc_superpool* superpool = NULL;
  NGFI_DARRAY_FOREACH(CURRENT_CONTEXT->desc_superpools, i) {
    if (CURRENT_CONTEXT->desc_superpools.data[i].ctx_id == ctx_id) {
      superpool = &CURRENT_CONTEXT->desc_superpools.data[i];
      break;
    }
  }

  if (superpool == NULL) {
    ngfvk_desc_superpool new_superpool = {
        .ctx_id      = (uint16_t)~0,
        .num_lists   = 0,
        .pools_lists = NULL};
    NGFI_DARRAY_APPEND(CURRENT_CONTEXT->desc_superpools, new_superpool);
    superpool = NGFI_DARRAY_BACKPTR(CURRENT_CONTEXT->desc_superpools);
    ngfvk_create_desc_superpool(superpool, nframes, ctx_id);
  }

  return &superpool->pools_lists[frame_id];
}

static VkDescriptorSet ngfvk_desc_pools_list_allocate_set(
    ngfvk_desc_pools_list*       pools,
    const ngfvk_desc_set_layout* set_layout) {
  // Ensure we have an active desriptor pool that is able to service the
  // request.
  const bool have_active_pool    = (pools->active_pool != NULL);
  bool       fresh_pool_required = !have_active_pool;

  if (have_active_pool) {
    // Check if the active descriptor pool can fit the required descriptor
    // set.
    ngfvk_desc_pool*                pool     = pools->active_pool;
    const ngfvk_desc_pool_capacity* capacity = &pool->capacity;
    ngfvk_desc_pool_capacity*       usage    = &pool->utilization;
    for (ngf_descriptor_type i = 0; !fresh_pool_required && i < NGF_DESCRIPTOR_TYPE_COUNT; ++i) {
      fresh_pool_required |=
          (usage->descriptors[i] + set_layout->counts[i] >= capacity->descriptors[i]);
    }
    fresh_pool_required |= (usage->sets + 1u >= capacity->sets);
  }
  if (fresh_pool_required) {
    if (!have_active_pool || pools->active_pool->next == NULL) {
      // TODO: make this tweakable
      ngfvk_desc_pool_capacity capacity;
      capacity.sets = 100u;
      for (int i = 0; i < NGF_DESCRIPTOR_TYPE_COUNT; ++i) capacity.descriptors[i] = 100u;

      // Prepare descriptor counts.
      VkDescriptorPoolSize* vk_pool_sizes =
          ngfi_sa_alloc(ngfi_tmp_store(), sizeof(VkDescriptorPoolSize) * NGF_DESCRIPTOR_TYPE_COUNT);
      for (int i = 0; i < NGF_DESCRIPTOR_TYPE_COUNT; ++i) {
        vk_pool_sizes[i].descriptorCount = capacity.descriptors[i];
        vk_pool_sizes[i].type            = get_vk_descriptor_type((ngf_descriptor_type)i);
      }

      // Prepare a createinfo structure for the new pool.
      const VkDescriptorPoolCreateInfo vk_pool_ci = {
          .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          .pNext         = NULL,
          .flags         = 0u,
          .maxSets       = capacity.sets,
          .poolSizeCount = NGF_DESCRIPTOR_TYPE_COUNT,
          .pPoolSizes    = vk_pool_sizes};

      // Create the new pool.
      ngfvk_desc_pool* new_pool = NGFI_ALLOC(ngfvk_desc_pool);
      new_pool->next            = NULL;
      new_pool->capacity        = capacity;
      memset(&new_pool->utilization, 0, sizeof(new_pool->utilization));
      const VkResult vk_pool_create_result =
          vkCreateDescriptorPool(_vk.device, &vk_pool_ci, NULL, &new_pool->vk_pool);
      if (vk_pool_create_result == VK_SUCCESS) {
        if (pools->active_pool != NULL && pools->active_pool->next == NULL) {
          pools->active_pool->next = new_pool;
        } else if (pools->active_pool == NULL) {
          pools->list = new_pool;
        } else {  // shouldn't happen
          assert(false);
        }
        pools->active_pool = new_pool;
      } else {
        NGFI_FREE(new_pool);
        assert(false);
      }
    } else {
      pools->active_pool = pools->active_pool->next;
    }
  }

  // Allocate the new descriptor set from the pool.
  ngfvk_desc_pool* pool = pools->active_pool;

  const VkDescriptorSetAllocateInfo vk_desc_set_info = {
      .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .pNext              = NULL,
      .descriptorPool     = pool->vk_pool,
      .descriptorSetCount = 1u,
      .pSetLayouts        = &set_layout->vk_handle};
  VkDescriptorSet result = VK_NULL_HANDLE;
  const VkResult  desc_set_alloc_result =
      vkAllocateDescriptorSets(_vk.device, &vk_desc_set_info, &result);
  if (desc_set_alloc_result != VK_SUCCESS) { return VK_NULL_HANDLE; }

  // Update usage counters for the active descriptor pool.
  for (ngf_descriptor_type i = 0; i < NGF_DESCRIPTOR_TYPE_COUNT; ++i) {
    pool->utilization.descriptors[i] += set_layout->counts[i];
  }
  pool->utilization.sets++;

  return result;
}

static void ngfvk_execute_pending_binds(ngf_cmd_buffer cmd_buf) {
  // Binding resources requires an active pipeline.
  ngfvk_generic_pipeline* pipeline_data = NULL;
  if (!(cmd_buf->renderpass_active ^ cmd_buf->compute_pass_active)) {
    NGFI_DIAG_ERROR("either a render or compute pass needs to be active to bind resources");
    return;
  }
  if (cmd_buf->renderpass_active)
    pipeline_data = &cmd_buf->active_gfx_pipe->generic_pipeline;
  else if (cmd_buf->compute_pass_active)
    pipeline_data = &cmd_buf->active_compute_pipe->generic_pipeline;
  assert(pipeline_data);

  // Get the number of active descriptor set layouts in the pipeline.
  const uint32_t ndesc_set_layouts = NGFI_DARRAY_SIZE(pipeline_data->descriptor_set_layouts);

  // Reset temp. storage to make sure we have all of it available.
  ngfi_sa_reset(ngfi_tmp_store());

  // Allocate an array of descriptor set handles from temporary storage and
  // set them all to null. As we process bind operations, we'll allocate
  // descriptor sets and put them into the array as necessary.
  const size_t     vk_desc_sets_size_bytes = sizeof(VkDescriptorSet) * ndesc_set_layouts;
  VkDescriptorSet* vk_desc_sets = ngfi_sa_alloc(ngfi_tmp_store(), vk_desc_sets_size_bytes);
  memset(vk_desc_sets, (uintptr_t)VK_NULL_HANDLE, vk_desc_sets_size_bytes);

  const uint32_t nbind_operations = cmd_buf->pending_bind_ops.size;

  // Allocate an array of vulkan descriptor set writes from temp storage, one write per
  // pending bind op.
  VkWriteDescriptorSet* vk_writes =
      ngfi_sa_alloc(ngfi_tmp_store(), nbind_operations * sizeof(VkWriteDescriptorSet));

  // Find a descriptor pools list to allocate from.
  ngfvk_desc_pools_list* pools = ngfvk_find_desc_pools_list(cmd_buf->parent_frame);
  cmd_buf->desc_pools_list     = pools;

  // Process each bind operation, constructing a corresponding
  // vulkan descriptor set write operation.
  ngfvk_bind_op_chunk* chunk                = cmd_buf->pending_bind_ops.first;
  uint32_t             descriptor_write_idx = 0u;
  while (chunk) {
    for (size_t boi = 0; boi < chunk->last_idx; ++boi) {
      const ngf_resource_bind_op* bind_op = &chunk->data[boi];

      // Ensure that a valid descriptor set is referenced by this
      // bind operation.
      if (bind_op->target_set >= ndesc_set_layouts) {
        NGFI_DIAG_ERROR(
            "invalid descriptor set %d referenced by bind operation (max. "
            "allowed is %d)",
            bind_op->target_set,
            ndesc_set_layouts);
        return;
      }

      // Allocate a new descriptor set if necessary.
      const bool need_new_desc_set = vk_desc_sets[bind_op->target_set] == VK_NULL_HANDLE;
      if (need_new_desc_set) {
        // Find the corresponding descriptor set layout.
        const ngfvk_desc_set_layout* set_layout =
            &NGFI_DARRAY_AT(pipeline_data->descriptor_set_layouts, bind_op->target_set);
        VkDescriptorSet set = ngfvk_desc_pools_list_allocate_set(pools, set_layout);
        if (set == VK_NULL_HANDLE) {
          NGFI_DIAG_WARNING(
              "Failed to bind graphics resources - could not allocate descriptor set");
          return;
        }
        vk_desc_sets[bind_op->target_set] = set;
      }

      // At this point, we have a valid descriptor set in the `vk_sets` array.
      // We'll use it in the write operation corresponding to the current bind_op.
      VkDescriptorSet set = vk_desc_sets[bind_op->target_set];

      // Construct a vulkan descriptor set write corresponding to this bind
      // operation.
      VkWriteDescriptorSet* vk_write = &vk_writes[descriptor_write_idx++];

      vk_write->sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      vk_write->pNext           = NULL;
      vk_write->dstSet          = set;
      vk_write->dstBinding      = bind_op->target_binding;
      vk_write->descriptorCount = 1u;
      vk_write->dstArrayElement = 0u;
      vk_write->descriptorType  = get_vk_descriptor_type(bind_op->type);

      switch (bind_op->type) {
      case NGF_DESCRIPTOR_STORAGE_BUFFER:
      case NGF_DESCRIPTOR_UNIFORM_BUFFER: {
        const ngf_buffer_bind_info* bind_info = &bind_op->info.buffer;
        VkDescriptorBufferInfo*     vk_bind_info =
            ngfi_sa_alloc(ngfi_tmp_store(), sizeof(VkDescriptorBufferInfo));

        vk_bind_info->buffer = (VkBuffer)bind_info->buffer->alloc.obj_handle;
        vk_bind_info->offset = bind_info->offset;
        vk_bind_info->range  = bind_info->range;

        vk_write->pBufferInfo = vk_bind_info;
        break;
      }
      case NGF_DESCRIPTOR_TEXEL_BUFFER: {
        vk_write->pTexelBufferView = &(bind_op->info.texel_buffer_view->vk_buf_view);
        break;
      }
      case NGF_DESCRIPTOR_STORAGE_IMAGE:
        if (cmd_buf->renderpass_active) {
          NGFI_DIAG_ERROR("Binding storage images to non-compute shader is currently unsupported.");
          continue;
        }
      /* break omitted intentionally */
      case NGF_DESCRIPTOR_IMAGE:
      case NGF_DESCRIPTOR_SAMPLER:
      case NGF_DESCRIPTOR_IMAGE_AND_SAMPLER: {
        const ngf_image_sampler_bind_info* bind_info = &bind_op->info.image_sampler;
        VkDescriptorImageInfo*             vk_bind_info =
            ngfi_sa_alloc(ngfi_tmp_store(), sizeof(VkDescriptorImageInfo));
        vk_bind_info->imageView   = VK_NULL_HANDLE;
        vk_bind_info->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vk_bind_info->sampler     = VK_NULL_HANDLE;
        if (bind_op->type == NGF_DESCRIPTOR_IMAGE ||
            bind_op->type == NGF_DESCRIPTOR_IMAGE_AND_SAMPLER) {
          vk_bind_info->imageView     = bind_info->image->vkview;
          const bool is_storage_image = bind_info->image->usage_flags & NGF_IMAGE_USAGE_STORAGE;
          vk_bind_info->imageLayout   = (is_storage_image) ? VK_IMAGE_LAYOUT_GENERAL
                                                           : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        } else if (bind_op->type == NGF_DESCRIPTOR_STORAGE_IMAGE) {
          vk_bind_info->imageView   = bind_info->image->vkview;
          vk_bind_info->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        } else if (
            bind_op->type == NGF_DESCRIPTOR_SAMPLER ||
            bind_op->type == NGF_DESCRIPTOR_IMAGE_AND_SAMPLER) {
          vk_bind_info->sampler = bind_info->sampler->vksampler;
        }
        vk_write->pImageInfo = vk_bind_info;
        break;
      }

      default:
        assert(false);
      }
    }
    ngfvk_bind_op_chunk* prev_chunk = chunk;
    chunk                           = prev_chunk->next;
    ngfi_blkalloc_free(CURRENT_CONTEXT->bind_op_chunk_allocator, prev_chunk);
  }
  cmd_buf->pending_bind_ops.first = cmd_buf->pending_bind_ops.last = NULL;
  cmd_buf->pending_bind_ops.size                                   = 0u;

  // perform all the vulkan descriptor set write operations to populate the
  // newly allocated descriptor sets.
  vkUpdateDescriptorSets(_vk.device, nbind_operations, vk_writes, 0, NULL);

  // bind each of the descriptor sets individually (this ensures that desc.
  // sets bound for a compatible pipeline earlier in this command buffer
  // don't get clobbered).
  for (uint32_t s = 0; s < ndesc_set_layouts; ++s) {
    if (vk_desc_sets[s] != VK_NULL_HANDLE) {
      vkCmdBindDescriptorSets(
          cmd_buf->vk_cmd_buffer,
          cmd_buf->renderpass_active ? VK_PIPELINE_BIND_POINT_GRAPHICS
                                     : VK_PIPELINE_BIND_POINT_COMPUTE,
          pipeline_data->vk_pipeline_layout,
          s,
          1,
          &vk_desc_sets[s],
          0,
          NULL);
    }
  }
}

static VkResult ngfvk_renderpass_from_attachment_descs(
    uint32_t                          nattachments,
    const ngf_attachment_description* attachment_descs,
    const ngfvk_attachment_pass_desc* attachment_compat_pass_descs,
    VkRenderPass*                     result) {
  const size_t vk_attachment_descs_size_bytes = sizeof(VkAttachmentDescription) * nattachments;
  VkAttachmentDescription* vk_attachment_descs =
      ngfi_sa_alloc(ngfi_tmp_store(), vk_attachment_descs_size_bytes);
  const size_t vk_color_attachment_references_size_bytes =
      sizeof(VkAttachmentReference) * nattachments;
  VkAttachmentReference* vk_color_attachment_refs =
      ngfi_sa_alloc(ngfi_tmp_store(), vk_color_attachment_references_size_bytes);
  VkAttachmentReference* vk_resolve_attachment_refs =
      ngfi_sa_alloc(ngfi_tmp_store(), vk_color_attachment_references_size_bytes);
  uint32_t              ncolor_attachments   = 0u;
  uint32_t              nresolve_attachments = 0u;
  VkAttachmentReference depth_stencil_attachment_ref;
  bool                  have_depth_stencil_attachment = false;
  bool                  have_sampled_attachments      = false;
  bool                  have_color_attachment         = false;

  for (uint32_t a = 0u; a < nattachments; ++a) {
    const ngf_attachment_description* ngf_attachment_desc  = &attachment_descs[a];
    const ngfvk_attachment_pass_desc* attachment_pass_desc = &attachment_compat_pass_descs[a];
    VkAttachmentDescription*          vk_attachment_desc   = &vk_attachment_descs[a];

    vk_attachment_desc->flags   = 0u;
    vk_attachment_desc->format  = get_vk_image_format(ngf_attachment_desc->format);
    vk_attachment_desc->samples = get_vk_sample_count(ngf_attachment_desc->sample_count);
    vk_attachment_desc->loadOp  = attachment_pass_desc->load_op;
    vk_attachment_desc->storeOp = attachment_pass_desc->store_op;
    vk_attachment_desc->stencilLoadOp =
        VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // attachment_pass_desc->load_op;
    vk_attachment_desc->stencilStoreOp =
        VK_ATTACHMENT_STORE_OP_DONT_CARE;  // attachment_pass_desc->store_op;
    vk_attachment_desc->initialLayout = attachment_pass_desc->initial_layout;
    vk_attachment_desc->finalLayout   = attachment_pass_desc->final_layout;

    if (ngf_attachment_desc->type == NGF_ATTACHMENT_COLOR) {
      if (!attachment_pass_desc->is_resolve) {
        VkAttachmentReference* vk_color_attachment_reference =
            &vk_color_attachment_refs[ncolor_attachments++];
        vk_color_attachment_reference->attachment = a;
        vk_color_attachment_reference->layout     = attachment_pass_desc->layout;
      } else {
        VkAttachmentReference* vk_resolve_attachment_reference =
            &vk_resolve_attachment_refs[nresolve_attachments++];
        vk_resolve_attachment_reference->attachment = a;
        vk_resolve_attachment_reference->layout     = attachment_pass_desc->layout;
      }
    }
    if (ngf_attachment_desc->type == NGF_ATTACHMENT_DEPTH ||
        ngf_attachment_desc->type == NGF_ATTACHMENT_DEPTH_STENCIL) {
      if (have_depth_stencil_attachment) {
        // TODO: insert diag. log here
        return VK_ERROR_UNKNOWN;
      } else {
        have_depth_stencil_attachment           = true;
        depth_stencil_attachment_ref.attachment = a;
        depth_stencil_attachment_ref.layout     = attachment_pass_desc->layout;
      }
    } else if (ngf_attachment_desc->type == NGF_ATTACHMENT_COLOR) {
      have_color_attachment = true;
    }

    have_sampled_attachments |= ngf_attachment_desc->is_sampled;
  }
  if (nresolve_attachments > 0u && nresolve_attachments != ncolor_attachments) {
    // TODO: insert diag. log here.
    return VK_ERROR_UNKNOWN;
  }

  const VkSubpassDescription subpass_desc = {
      .flags                = 0u,
      .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
      .inputAttachmentCount = 0u,
      .pInputAttachments    = NULL,
      .colorAttachmentCount = ncolor_attachments,
      .pColorAttachments    = vk_color_attachment_refs,
      .pResolveAttachments  = nresolve_attachments > 0u ? vk_resolve_attachment_refs : NULL,
      .pDepthStencilAttachment =
          have_depth_stencil_attachment ? &depth_stencil_attachment_ref : NULL,
      .preserveAttachmentCount = 0u,
      .pPreserveAttachments    = NULL};

  VkSubpassDependency subpass_deps[3];
  uint32_t            nsubpass_deps = 0u;

  if (have_depth_stencil_attachment) {
    VkSubpassDependency* dependency_for_depth_stencil = &subpass_deps[nsubpass_deps++];
    dependency_for_depth_stencil->srcSubpass          = VK_SUBPASS_EXTERNAL;
    dependency_for_depth_stencil->dstSubpass          = 0;
    dependency_for_depth_stencil->srcStageMask =
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency_for_depth_stencil->srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency_for_depth_stencil->dstStageMask =
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency_for_depth_stencil->dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency_for_depth_stencil->dependencyFlags = 0u;
  }

  if (have_color_attachment) {
    VkSubpassDependency* dependency_for_color = &subpass_deps[nsubpass_deps++];
    dependency_for_color->srcSubpass          = VK_SUBPASS_EXTERNAL;
    dependency_for_color->dstSubpass          = 0u;
    dependency_for_color->srcStageMask        = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency_for_color->srcAccessMask       = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency_for_color->dstStageMask        = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency_for_color->dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency_for_color->dependencyFlags = 0u;
  }

  if (have_sampled_attachments) {
    VkSubpassDependency*          dependency_for_readers = &subpass_deps[nsubpass_deps++];
    const VkPipelineStageFlagBits depth_stages =
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    const VkAccessFlagBits depth_accesses =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependency_for_readers->srcSubpass   = 0u;
    dependency_for_readers->dstSubpass   = VK_SUBPASS_EXTERNAL;
    dependency_for_readers->srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                           (have_depth_stencil_attachment ? depth_stages : 0u);
    dependency_for_readers->srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                            (have_depth_stencil_attachment ? depth_accesses : 0u);
    dependency_for_readers->dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    dependency_for_readers->dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    dependency_for_readers->dependencyFlags = 0u;
  }

  const VkRenderPassCreateInfo renderpass_ci = {
      .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .pNext           = NULL,
      .flags           = 0u,
      .attachmentCount = nattachments,
      .pAttachments    = vk_attachment_descs,
      .subpassCount    = 1u,
      .pSubpasses      = &subpass_desc,
      .dependencyCount = nsubpass_deps,
      .pDependencies   = subpass_deps};

  return vkCreateRenderPass(_vk.device, &renderpass_ci, NULL, result);
}

// Returns a bitstring uniquely identifying the series of load/store op combos
// for each attachment.
static uint64_t ngfvk_renderpass_ops_key(
    const ngf_render_target        rt,
    const ngf_attachment_load_op*  load_ops,
    const ngf_attachment_store_op* store_ops) {
  const uint32_t num_rt_attachments = rt->nattachments;
  const uint32_t nattachments =
      rt->is_default ? (NGFI_MIN(2, num_rt_attachments)) : num_rt_attachments;
  assert(nattachments < (8u * sizeof(uint64_t) / 3u));
  uint64_t result = 0u;
  for (uint32_t i = 0u; i < nattachments; ++i) {
    const uint64_t load_op_bits  = (uint64_t)load_ops[i];
    const uint64_t store_op_bits = (uint64_t)store_ops[i];
    assert(load_op_bits <= 3);
    assert(store_op_bits <= 1);
    const uint64_t attachment_ops_combo = (load_op_bits << 1u) | store_op_bits;
    result |= attachment_ops_combo << (i * 3u);
  }
  // For default RT, the load/store ops of the resolve attachments are not
  // specified by the client code explicitly. We always treat them as
  // DONT_CARE / STORE.
  if (rt->is_default && nattachments < num_rt_attachments &&
      rt->attachment_compat_pass_descs[nattachments].is_resolve) {
    result = result | ((uint64_t)0x1u << (3u * nattachments));
  }
  return result;
}

// Macros for accessing load/store ops encoded in a renderpass ops key.
#define NGFVK_ATTACHMENT_OPS_COMBO(idx, ops_key) ((ops_key >> (3u * idx)) & 7u)
#define NGFVK_ATTACHMENT_LOAD_OP_FROM_KEY(idx, ops_key) \
  (get_vk_load_op((ngf_attachment_load_op)(NGFVK_ATTACHMENT_OPS_COMBO(idx, ops_key) >> 1u)))
#define NGFVK_ATTACHMENT_STORE_OP_FROM_KEY(idx, ops_key) \
  (get_vk_store_op((ngf_attachment_store_op)(NGFVK_ATTACHMENT_OPS_COMBO(idx, ops_key) & 1u)))

// Looks up a renderpass object from the current context's renderpass cache, and creates
// one if it doesn't exist.
static VkRenderPass ngfvk_lookup_renderpass(ngf_render_target rt, uint64_t ops_key) {
  VkRenderPass result = VK_NULL_HANDLE;
  NGFI_DARRAY_FOREACH(CURRENT_CONTEXT->renderpass_cache, r) {
    const ngfvk_renderpass_cache_entry* cache_entry =
        &NGFI_DARRAY_AT(CURRENT_CONTEXT->renderpass_cache, r);
    if (cache_entry->rt == rt && cache_entry->ops_key == ops_key) {
      result = cache_entry->renderpass;
      break;
    }
  }

  if (result == VK_NULL_HANDLE) {
    const uint32_t nattachments               = rt->nattachments;
    const size_t   attachment_pass_descs_size = sizeof(ngfvk_attachment_pass_desc) * nattachments;
    ngfvk_attachment_pass_desc* attachment_compat_pass_descs =
        ngfi_sa_alloc(ngfi_tmp_store(), attachment_pass_descs_size);
    const size_t rt_attachment_pass_descs_size =
        rt->nattachments * sizeof(ngfvk_attachment_pass_desc);
    memcpy(
        attachment_compat_pass_descs,
        rt->attachment_compat_pass_descs,
        rt_attachment_pass_descs_size);

    for (uint32_t i = 0; i < rt->nattachments; ++i) {
      attachment_compat_pass_descs[i].load_op  = NGFVK_ATTACHMENT_LOAD_OP_FROM_KEY(i, ops_key);
      attachment_compat_pass_descs[i].store_op = NGFVK_ATTACHMENT_STORE_OP_FROM_KEY(i, ops_key);
    }

    ngfvk_renderpass_from_attachment_descs(
        nattachments,
        rt->attachment_descs,
        attachment_compat_pass_descs,
        &result);
    const ngfvk_renderpass_cache_entry cache_entry = {
        .rt         = rt,
        .ops_key    = ops_key,
        .renderpass = result};
    NGFI_DARRAY_APPEND(CURRENT_CONTEXT->renderpass_cache, cache_entry);
  }

  return result;
}

static int ngfvk_binding_comparator(const void* a, const void* b) {
  const SpvReflectDescriptorBinding* a_binding = a;
  const SpvReflectDescriptorBinding* b_binding = b;
  if (a_binding->set < b_binding->set)
    return -1;
  else if (a_binding->set == b_binding->set) {
    if (a_binding->binding < b_binding->binding)
      return -1;
    else if (a_binding->binding == b_binding->binding)
      return 0;
  }
  return 1;
}

static ngf_descriptor_type
ngfvk_get_ngf_descriptor_type(SpvReflectDescriptorType spv_reflect_type) {
  switch (spv_reflect_type) {
  case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    return NGF_DESCRIPTOR_UNIFORM_BUFFER;
  case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    return NGF_DESCRIPTOR_IMAGE;
  case SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER:
    return NGF_DESCRIPTOR_SAMPLER;
  case SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
    return NGF_DESCRIPTOR_IMAGE_AND_SAMPLER;
  case SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    return NGF_DESCRIPTOR_TEXEL_BUFFER;
  case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER:
    return NGF_DESCRIPTOR_STORAGE_BUFFER;
  case SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    return NGF_DESCRIPTOR_STORAGE_IMAGE;
  default:
    return NGF_DESCRIPTOR_TYPE_COUNT;
  }
}

static void ngfvk_init_loader_if_necessary() {
  if (!vkGetInstanceProcAddr) {
    NGFI_DIAG_INFO("Initializing Vulkan loader.")
    if (!vkl_init_loader()) {
      // Initialize the vulkan loader if it wasn't initialized before.
      NGFI_DIAG_ERROR("Failed to initialize Vulkan loader.")
    }
    NGFI_DIAG_INFO("Vulkan loader initialized successfully.");
  }
}

static VkResult
ngfvk_create_instance(bool request_validation, VkInstance* instance_ptr, bool* validation_enabled) {
  const char* const ext_names[] = {// Names of instance-level extensions.
                                   "VK_KHR_surface",
                                   VK_SURFACE_EXT,
                                   VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
                                   VK_EXT_DEBUG_UTILS_EXTENSION_NAME};

  const VkApplicationInfo app_info = {// Application information.
                                      .sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                      .pNext            = NULL,
                                      .pApplicationName = NULL,  // TODO: allow specifying app name.
                                      .pEngineName      = "nicegraf",
                                      .engineVersion = VK_MAKE_VERSION(NGF_VER_MAJ, NGF_VER_MIN, 0),
                                      .apiVersion    = VK_MAKE_VERSION(1, 0, 9)};

  // Names of instance layers to enable.
  const char* validation_layer_name = "VK_LAYER_KHRONOS_validation";
  const char* enabled_layers[]      = {validation_layer_name};

  // Check if validation layers are supported.
  uint32_t nlayers = 0u;
  vkEnumerateInstanceLayerProperties(&nlayers, NULL);
  VkLayerProperties* layer_props =
      ngfi_sa_alloc(ngfi_tmp_store(), nlayers * sizeof(VkLayerProperties));
  vkEnumerateInstanceLayerProperties(&nlayers, layer_props);
  bool validation_supported = false;
  for (size_t l = 0u; !validation_supported && l < nlayers; ++l) {
    validation_supported = (strcmp(validation_layer_name, layer_props[l].layerName) == 0u);
  }

  // Enable validation only if detailed verbosity is requested.
  const bool enable_validation = validation_supported && request_validation;
  if (validation_enabled) { *validation_enabled = enable_validation; }

  // Create a Vulkan instance.
  const VkInstanceCreateInfo inst_info = {
      .sType                 = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext                 = NULL,
      .flags                 = 0u,
      .pApplicationInfo      = &app_info,
      .enabledLayerCount     = enable_validation ? 1u : 0u,
      .ppEnabledLayerNames   = enabled_layers,
      .enabledExtensionCount = (uint32_t)NGFI_ARRAYSIZE(ext_names) - (enable_validation ? 0u : 1u),
      .ppEnabledExtensionNames = ext_names};
  VkResult vk_err = vkCreateInstance(&inst_info, NULL, instance_ptr);
  if (vk_err != VK_SUCCESS) {
    NGFI_DIAG_ERROR("Failed to create a Vulkan instance, VK error %d.", vk_err);
    return vk_err;
  }

  return VK_SUCCESS;
}

static void ngfvk_populate_vk_spec_consts(
    const ngf_specialization_info* spec_info,
    VkSpecializationInfo*          vk_spec_info) {
  if (spec_info) {
    VkSpecializationMapEntry* spec_map_entries =
        NGFI_SALLOC(VkSpecializationMapEntry, spec_info->nspecializations);

    vk_spec_info->pData         = spec_info->value_buffer;
    vk_spec_info->mapEntryCount = spec_info->nspecializations;
    vk_spec_info->pMapEntries   = spec_map_entries;

    size_t total_data_size = 0u;
    for (size_t i = 0; i < spec_info->nspecializations; ++i) {
      VkSpecializationMapEntry*          vk_specialization = &spec_map_entries[i];
      const ngf_constant_specialization* specialization    = &spec_info->specializations[i];
      vk_specialization->constantID                        = specialization->constant_id;
      vk_specialization->offset                            = specialization->offset;
      size_t specialization_size                           = 0u;
      switch (specialization->type) {
      case NGF_TYPE_INT8:
      case NGF_TYPE_UINT8:
        specialization_size = 1u;
        break;
      case NGF_TYPE_INT16:
      case NGF_TYPE_UINT16:
      case NGF_TYPE_HALF_FLOAT:
        specialization_size = 2u;
        break;
      case NGF_TYPE_INT32:
      case NGF_TYPE_UINT32:
      case NGF_TYPE_FLOAT:
        specialization_size = 4u;
        break;
      case NGF_TYPE_DOUBLE:
        specialization_size = 8u;
        break;
      default:
        assert(false);
      }
      vk_specialization->size = specialization_size;
      total_data_size += specialization_size;
    }
    vk_spec_info->dataSize = total_data_size;
  }
}

void ngfvk_populate_vk_shader_stages(
    const ngf_shader_stage*          shader_stages,
    uint32_t                         nshader_stages,
    VkPipelineShaderStageCreateInfo* vk_shader_stages,
    const VkSpecializationInfo*      vk_spec_info) {
  for (uint32_t s = 0u; s < nshader_stages; ++s) {
    const ngf_shader_stage stage            = shader_stages[s];
    vk_shader_stages[s].sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vk_shader_stages[s].pNext               = NULL;
    vk_shader_stages[s].flags               = 0u;
    vk_shader_stages[s].stage               = stage->vk_stage_bits;
    vk_shader_stages[s].module              = stage->vk_module;
    vk_shader_stages[s].pName               = stage->entry_point_name,
    vk_shader_stages[s].pSpecializationInfo = vk_spec_info;
  }
}

ngf_error ngfvk_create_pipeline_layout(
    const ngf_shader_stage* shader_stages,
    uint32_t                nshader_stages,
    ngfvk_generic_pipeline* pipeline_data) {
  NGFI_DARRAY_RESET(pipeline_data->descriptor_set_layouts, 4);

  // Extract and dedupe all descriptor bindings.
  uint32_t ntotal_bindings = 0u;
  for (uint32_t i = 0u; i < nshader_stages; ++i) {
    ntotal_bindings += shader_stages[i]->spv_reflect_module.descriptor_binding_count;
  }
  SpvReflectDescriptorBinding* bindings = NGFI_SALLOC(SpvReflectDescriptorBinding, ntotal_bindings);

  uint32_t bindings_offset = 0u;
  for (uint32_t i = 0u; i < nshader_stages; ++i) {
    const SpvReflectShaderModule* spv_module    = &shader_stages[i]->spv_reflect_module;
    const uint32_t                binding_count = spv_module->descriptor_binding_count;
    memcpy(
        &bindings[bindings_offset],
        spv_module->descriptor_bindings,
        sizeof(SpvReflectDescriptorBinding) * binding_count);
    bindings_offset += binding_count;
  }
  qsort(bindings, ntotal_bindings, sizeof(SpvReflectDescriptorBinding), ngfvk_binding_comparator);
  uint32_t nunique_bindings = 0u;
  uint32_t max_set_id       = 0u;
  for (uint32_t cur = 0u; cur < ntotal_bindings; ++cur) {
    if (nunique_bindings == 0 ||
        (bindings[nunique_bindings - 1].set != bindings[cur].set ||
         bindings[nunique_bindings - 1].binding != bindings[cur].binding)) {
      bindings[nunique_bindings++] = bindings[cur];
      max_set_id                   = NGFI_MAX(max_set_id, bindings[cur].set);
    }
  }

  // Create descriptor set layouts.
  VkDescriptorSetLayout* vk_set_layouts =
      ngfi_sa_alloc(ngfi_tmp_store(), sizeof(VkDescriptorSetLayout) * (max_set_id + 1));
  uint32_t last_set_id = ~0u;
  for (uint32_t cur = 0u; cur < nunique_bindings;) {
    ngfvk_desc_set_layout set_layout;
    memset(&set_layout, 0, sizeof(set_layout));
    const uint32_t current_set_id = bindings[cur].set;
    if ((last_set_id > current_set_id) || (current_set_id - last_set_id > 1u)) {
      // there is a gap in descriptor sets, fill it in with empty layouts;
      for (uint32_t i = last_set_id + 1; i < current_set_id; ++i) {
        const VkDescriptorSetLayoutCreateInfo vk_ds_info = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext        = NULL,
            .flags        = 0u,
            .bindingCount = 0u,
            .pBindings    = NULL};
        vkCreateDescriptorSetLayout(_vk.device, &vk_ds_info, NULL, &set_layout.vk_handle);
        NGFI_DARRAY_APPEND(pipeline_data->descriptor_set_layouts, set_layout);
        vk_set_layouts[i] = set_layout.vk_handle;
      }
    }
    memset(&set_layout, 0, sizeof(set_layout));
    const uint32_t first_binding_in_set = cur;
    while (cur < nunique_bindings && current_set_id == bindings[cur].set) cur++;
    const uint32_t                nbindings_in_set = cur - first_binding_in_set;
    VkDescriptorSetLayoutBinding* vk_descriptor_bindings =
        ngfi_sa_alloc(ngfi_tmp_store(), sizeof(VkDescriptorSetLayoutBinding) * nbindings_in_set);
    for (uint32_t i = first_binding_in_set; i < cur; ++i) {
      VkDescriptorSetLayoutBinding*      vk_d = &vk_descriptor_bindings[i - first_binding_in_set];
      const SpvReflectDescriptorBinding* d    = &bindings[i];
      const ngf_descriptor_type ngf_desc_type = ngfvk_get_ngf_descriptor_type(d->descriptor_type);
      if (ngf_desc_type == NGF_DESCRIPTOR_TYPE_COUNT) { return NGF_ERROR_OBJECT_CREATION_FAILED; }
      vk_d->binding            = d->binding;
      vk_d->descriptorCount    = d->count;
      vk_d->descriptorType     = get_vk_descriptor_type(ngf_desc_type);
      vk_d->stageFlags         = VK_SHADER_STAGE_ALL;
      vk_d->pImmutableSamplers = NULL;
      set_layout.counts[ngf_desc_type]++;
    }
    const VkDescriptorSetLayoutCreateInfo vk_ds_info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext        = NULL,
        .flags        = 0u,
        .bindingCount = nbindings_in_set,
        .pBindings    = vk_descriptor_bindings};
    const VkResult vk_err =
        vkCreateDescriptorSetLayout(_vk.device, &vk_ds_info, NULL, &set_layout.vk_handle);
    NGFI_DARRAY_APPEND(pipeline_data->descriptor_set_layouts, set_layout);
    vk_set_layouts[current_set_id] = set_layout.vk_handle;
    if (vk_err != VK_SUCCESS) { return NGF_ERROR_OBJECT_CREATION_FAILED; }
    last_set_id = current_set_id;
  }

  // Pipeline layout.
  const uint32_t ndescriptor_sets = NGFI_DARRAY_SIZE(pipeline_data->descriptor_set_layouts);
  const VkPipelineLayoutCreateInfo vk_pipeline_layout_info = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext                  = NULL,
      .flags                  = 0u,
      .setLayoutCount         = ndescriptor_sets,
      .pSetLayouts            = vk_set_layouts,
      .pushConstantRangeCount = 0u,
      .pPushConstantRanges    = NULL};
  const VkResult vk_err = vkCreatePipelineLayout(
      _vk.device,
      &vk_pipeline_layout_info,
      NULL,
      &pipeline_data->vk_pipeline_layout);
  if (vk_err != VK_SUCCESS) { return NGF_ERROR_OBJECT_CREATION_FAILED; }
  return NGF_ERROR_OK;
}

static ngf_error ngfvk_initialize_generic_pipeline_data(
    ngfvk_generic_pipeline*          data,
    const ngf_specialization_info*   spec_info,
    VkPipelineShaderStageCreateInfo* vk_shader_stages,
    const ngf_shader_stage*          shader_stages,
    uint32_t                         nshader_stages) {
  // Build up Vulkan specialization structure, if necessary.
  ngfvk_populate_vk_spec_consts(spec_info, &data->vk_spec_info);

  // Prepare shader stages.
  ngfvk_populate_vk_shader_stages(
      shader_stages,
      nshader_stages,
      vk_shader_stages,
      spec_info ? &data->vk_spec_info : NULL);

  // Prepare pipeline layout.
  return ngfvk_create_pipeline_layout(shader_stages, nshader_stages, data);
}

static void
ngfi_destroy_generic_pipeline_data(ngfvk_frame_resources* res, ngfvk_generic_pipeline* data) {
  if (data->vk_pipeline != VK_NULL_HANDLE) {
    NGFI_DARRAY_APPEND(res->retire_pipelines, data->vk_pipeline);
  }
  if (data->vk_pipeline_layout != VK_NULL_HANDLE) {
    NGFI_DARRAY_APPEND(res->retire_pipeline_layouts, data->vk_pipeline_layout);
  }
  NGFI_DARRAY_FOREACH(data->descriptor_set_layouts, l) {
    VkDescriptorSetLayout set_layout = NGFI_DARRAY_AT(data->descriptor_set_layouts, l).vk_handle;
    NGFI_DARRAY_APPEND(res->retire_dset_layouts, set_layout);
  }
  NGFI_DARRAY_DESTROY(data->descriptor_set_layouts);
}

static void ngfvk_cmd_bind_resources(
    ngf_cmd_buffer              buf,
    const ngf_resource_bind_op* bind_operations,
    uint32_t                    nbind_operations) {
  for (uint32_t i = 0; i < nbind_operations; ++i) {
    const ngf_resource_bind_op* bind_op          = &bind_operations[i];
    ngfvk_bind_op_chunk_list*   pending_bind_ops = &buf->pending_bind_ops;
    if (!pending_bind_ops->last || pending_bind_ops->last->last_idx >= NGFVK_BIND_OP_CHUNK_SIZE) {
      ngfvk_bind_op_chunk* prev_last = pending_bind_ops->last;
      pending_bind_ops->last = ngfi_blkalloc_alloc(CURRENT_CONTEXT->bind_op_chunk_allocator);
      if (pending_bind_ops->last == NULL) {
        NGFI_DIAG_ERROR("failed memory allocation while binding resources");
        return;
      }
      pending_bind_ops->last->next     = NULL;
      pending_bind_ops->last->last_idx = 0;
      if (prev_last) {
        prev_last->next = pending_bind_ops->last;
      } else {
        pending_bind_ops->first = pending_bind_ops->last;
      }
    }
    pending_bind_ops->last->data[pending_bind_ops->last->last_idx++] = *bind_op;
    pending_bind_ops->size++;
  }
}

struct ngfvk_wait_events_params {
  VkEvent*               wait_events;
  VkImageMemoryBarrier*  image_memory_barriers;
  VkBufferMemoryBarrier* buffer_memory_barriers;
  uint32_t               max_image_memory_barriers;
  uint32_t               max_buffer_memory_barriers;
  uint32_t               max_wait_events;
  uint32_t               nimage_memory_barriers;
  uint32_t               nbuffer_memory_barriers;
  uint32_t               nwait_events;
};

union ngfvk_sync_resources {
  const ngf_sync_compute_resource* sync_compute_resources;
  const ngf_sync_render_resource*  sync_render_resources;
  const ngf_sync_xfer_resource*    sync_xfer_resources;
};

enum ngfvk_sync_encoder_type {
  NGFVK_SYNC_COMPUTE_ENCODER,
  NGFVK_SYNC_RENDER_ENCODER,
  NGFVK_SYNC_XFER_ENCODER
};

static void ngfvk_generate_sync_op_barriers(
    uint32_t                         nsync_resources,
    union ngfvk_sync_resources       sync_resources,
    enum ngfvk_sync_encoder_type     sync_encoder_type,
    VkAccessFlags                    possible_src_access_flag_bits,
    VkAccessFlags                    possible_dst_access_flag_bits,
    struct ngfvk_wait_events_params* temp_data) {
  if (nsync_resources <= 0u) { return; }
  for (uint32_t i = 0u; i < nsync_resources; ++i) {
    ngf_sync_resource_ref sync_resource = {
        .resource.buffer_slice = {.buffer = NULL, .offset = 0u, .range = 0u},
        .sync_resource_type    = NGF_SYNC_RESOURCE_BUFFER};
    VkEvent wait_event = VK_NULL_HANDLE;
    switch (sync_encoder_type) {
    case NGFVK_SYNC_COMPUTE_ENCODER:
      sync_resource = sync_resources.sync_compute_resources[i].resource;
      wait_event = (VkEvent)sync_resources.sync_compute_resources[i].encoder.pvt_data_donotuse.d1;
      break;
    case NGFVK_SYNC_RENDER_ENCODER:
      sync_resource = sync_resources.sync_render_resources[i].resource;
      wait_event    = (VkEvent)sync_resources.sync_render_resources[i].encoder.pvt_data_donotuse.d1;
      break;
    case NGFVK_SYNC_XFER_ENCODER:
      sync_resource = sync_resources.sync_xfer_resources[i].resource;
      wait_event    = (VkEvent)sync_resources.sync_xfer_resources[i].encoder.pvt_data_donotuse.d1;
      break;
    default:
      assert(false);
    }

    // TODO: use hashset to dedupe events.
    bool dupe_event = false;
    for (int32_t j = (int32_t)temp_data->nwait_events - 1;
         temp_data->nwait_events > 0 && j >= 0 && !dupe_event;
         --j) {
      dupe_event = (temp_data->wait_events[j] == wait_event);
    }
    if (!dupe_event) { temp_data->wait_events[temp_data->nwait_events++] = wait_event; }

    if (sync_resource.sync_resource_type == NGF_SYNC_RESOURCE_BUFFER) {
      const ngf_buffer_slice* buf_slice           = &sync_resource.resource.buffer_slice;
      const VkAccessFlags     buffer_access_flags = get_vk_buffer_access_flags(buf_slice->buffer);
      VkBufferMemoryBarrier*  barrier =
          &temp_data->buffer_memory_barriers[temp_data->nbuffer_memory_barriers++];
      barrier->sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      barrier->pNext               = NULL;
      barrier->srcAccessMask       = buffer_access_flags & possible_src_access_flag_bits;
      barrier->dstAccessMask       = buffer_access_flags & possible_dst_access_flag_bits;
      barrier->srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier->dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier->buffer              = (VkBuffer)buf_slice->buffer->alloc.obj_handle;
      barrier->offset              = buf_slice->offset;
      barrier->size                = buf_slice->range;
    } else if (sync_resource.sync_resource_type == NGF_SYNC_RESOURCE_IMAGE) {
      const ngf_image_ref*  img_ref            = &sync_resource.resource.image_ref;
      VkAccessFlags         image_access_flags = get_vk_image_access_flags(img_ref->image);
      VkImageMemoryBarrier* barrier =
          &temp_data->image_memory_barriers[temp_data->nimage_memory_barriers++];
      barrier->sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      barrier->pNext                           = NULL;
      barrier->srcAccessMask                   = image_access_flags & possible_src_access_flag_bits;
      barrier->dstAccessMask                   = image_access_flags & possible_dst_access_flag_bits;
      barrier->srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
      barrier->dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
      barrier->image                           = (VkImage)img_ref->image->alloc.obj_handle;
      barrier->subresourceRange.baseArrayLayer = img_ref->layer;
      barrier->subresourceRange.layerCount     = 1u;
      barrier->subresourceRange.baseMipLevel   = img_ref->mip_level;
      barrier->subresourceRange.levelCount     = 1u;

      const bool is_storage_image = img_ref->image->usage_flags & NGF_IMAGE_USAGE_STORAGE;

      barrier->oldLayout =
          is_storage_image ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      barrier->newLayout =
          is_storage_image ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      const VkFormat image_format = img_ref->image->vkformat;
      const bool     is_depth     = ngfvk_format_is_depth(image_format);
      const bool     is_stencil   = ngfvk_format_is_stencil(image_format);

      barrier->subresourceRange.aspectMask =
          is_depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | (is_stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0))
                   : VK_IMAGE_ASPECT_COLOR_BIT;
    } else {
      assert(false);
    }
  }
}

static ngf_error ngfvk_execute_sync_op(
    ngf_cmd_buffer                   cmd_buf,
    uint32_t                         nsync_compute_resources,
    const ngf_sync_compute_resource* sync_compute_resources,
    uint32_t                         nsync_render_resources,
    const ngf_sync_render_resource*  sync_render_resources,
    uint32_t                         nsync_xfer_resources,
    const ngf_sync_xfer_resource*    sync_xfer_resources,
    VkPipelineStageFlags             dst_stage_mask) {
  assert(cmd_buf != NULL);
  assert(sync_compute_resources != NULL || nsync_compute_resources == 0u);
  assert(sync_render_resources != NULL || nsync_render_resources == 0u);
  assert(sync_xfer_resources != NULL || nsync_xfer_resources == 0u);

  // Verify the command buffer state. Synchronization operations may not be recorded into a given
  // command buffer if it has an encoder active.
  if (cmd_buf->state != NGFI_CMD_BUFFER_READY &&
      cmd_buf->state != NGFI_CMD_BUFFER_AWAITING_SUBMIT) {
    return NGF_ERROR_INVALID_OPERATION;
  }

  // Holds pointers to temporarily allocated memory used to store parameters for vkCmdWaitEvents.
  struct ngfvk_wait_events_params temp_data;
  memset(&temp_data, 0, sizeof(temp_data));

  // Conservative estimate of necessary buffer size.
  const uint32_t max_buffer_size =
      nsync_compute_resources + nsync_render_resources + nsync_xfer_resources;

  // Early-out if no events to wait on have been specified.
  if (max_buffer_size <= 0u) { return NGF_ERROR_OK; }

  // Allocate memory for vkCmdWaitEvents parameters.
  temp_data.max_buffer_memory_barriers = max_buffer_size;
  temp_data.max_image_memory_barriers  = max_buffer_size;
  temp_data.max_wait_events            = max_buffer_size;
  if (temp_data.max_wait_events > 0u) {
    temp_data.wait_events =
        ngfi_sa_alloc(ngfi_tmp_store(), sizeof(VkEvent) * temp_data.max_wait_events);
    if (temp_data.wait_events == NULL) { return NGF_ERROR_OUT_OF_MEM; }
  }
  if (temp_data.max_image_memory_barriers > 0u) {
    temp_data.image_memory_barriers = ngfi_sa_alloc(
        ngfi_tmp_store(),
        sizeof(VkImageMemoryBarrier) * temp_data.max_image_memory_barriers);
    if (temp_data.image_memory_barriers == NULL) { return NGF_ERROR_OUT_OF_MEM; }
  }
  if (temp_data.max_buffer_memory_barriers > 0u) {
    temp_data.buffer_memory_barriers = ngfi_sa_alloc(
        ngfi_tmp_store(),
        sizeof(VkBufferMemoryBarrier) * temp_data.max_buffer_memory_barriers);
    if (temp_data.buffer_memory_barriers == NULL) { return NGF_ERROR_OUT_OF_MEM; }
  }

  // Compute source and destination access flags for memory barriers.
  const VkAccessFlagBits possible_compute_access_flag_bits =
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_UNIFORM_READ_BIT;

  const VkAccessFlagBits possible_xfer_access_flag_bits =
      VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;

  const VkAccessFlagBits possible_gfx_access_flag_bits =
      VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT |
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
      VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT;

  const VkAccessFlags possible_dst_access_flag_bits =
      ((dst_stage_mask & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) ? possible_compute_access_flag_bits
                                                               : 0u) |
      ((dst_stage_mask & NGFVK_GFX_PIPELINE_STAGE_MASK) ? possible_gfx_access_flag_bits : 0u) |
      ((dst_stage_mask & VK_PIPELINE_STAGE_TRANSFER_BIT) ? possible_xfer_access_flag_bits : 0u);

  // Generate barrier structs.
  union ngfvk_sync_resources sync_res_union = {.sync_compute_resources = sync_compute_resources};
  ngfvk_generate_sync_op_barriers(
      nsync_compute_resources,
      sync_res_union,
      NGFVK_SYNC_COMPUTE_ENCODER,
      possible_compute_access_flag_bits,
      possible_dst_access_flag_bits,
      &temp_data);

  // It only makes sense for compute encoders to sync with render / transfer encoders.
  const bool dst_is_compute = dst_stage_mask & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  if (dst_is_compute) {
    sync_res_union.sync_render_resources = sync_render_resources;
    ngfvk_generate_sync_op_barriers(
        nsync_render_resources,
        sync_res_union,
        NGFVK_SYNC_RENDER_ENCODER,
        possible_gfx_access_flag_bits,
        possible_dst_access_flag_bits,
        &temp_data);

    sync_res_union.sync_xfer_resources = sync_xfer_resources;
    ngfvk_generate_sync_op_barriers(
        nsync_xfer_resources,
        sync_res_union,
        NGFVK_SYNC_XFER_ENCODER,
        possible_xfer_access_flag_bits,
        possible_dst_access_flag_bits,
        &temp_data);
  }

  const VkPipelineStageFlags src_stage_mask =
      ((nsync_render_resources > 0u && dst_is_compute) ? NGFVK_GFX_PIPELINE_STAGE_MASK : 0u) |
      ((nsync_xfer_resources > 0u && dst_is_compute) ? VK_PIPELINE_STAGE_TRANSFER_BIT : 0u) |
      (nsync_compute_resources > 0u ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : 0u);

  if (temp_data.nbuffer_memory_barriers > 0u || temp_data.nimage_memory_barriers > 0u) {
    vkCmdWaitEvents(
        cmd_buf->vk_cmd_buffer,
        temp_data.nwait_events,
        temp_data.wait_events,
        src_stage_mask,
        dst_stage_mask,
        0u,
        NULL,
        temp_data.nbuffer_memory_barriers,
        temp_data.buffer_memory_barriers,
        temp_data.nimage_memory_barriers,
        temp_data.image_memory_barriers);
  }
  return NGF_ERROR_OK;
}

static bool ngfvk_phys_dev_extension_supported(const char* ext_name) {
  if (_vk.supported_phys_dev_exts == NULL || _vk.nsupported_phys_dev_exts == 0) {
    VkResult result;

    result = vkEnumerateDeviceExtensionProperties(
        _vk.phys_dev,
        NULL,
        &_vk.nsupported_phys_dev_exts,
        NULL);
    if (result != VK_SUCCESS) {
      NGFI_DIAG_WARNING("Failed to fetch physical device extensions count");
      return false;
    }

    _vk.supported_phys_dev_exts =
        malloc(sizeof(VkExtensionProperties) * _vk.nsupported_phys_dev_exts);
    if (_vk.supported_phys_dev_exts == NULL) {
      NGFI_DIAG_WARNING("Out of memory");
      return false;
    }

    result = vkEnumerateDeviceExtensionProperties(
        _vk.phys_dev,
        NULL,
        &_vk.nsupported_phys_dev_exts,
        _vk.supported_phys_dev_exts);
    if (result != VK_SUCCESS) {
      NGFI_DIAG_WARNING("Failed to fetch physical device extensions");
      return false;
    }
  }

  uint32_t supported_exts_idx;
  for (supported_exts_idx = 0; supported_exts_idx < _vk.nsupported_phys_dev_exts;
       supported_exts_idx++) {
    const VkExtensionProperties* supported_ext = &_vk.supported_phys_dev_exts[supported_exts_idx];
    if (strcmp(ext_name, supported_ext->extensionName) == 0) { return true; }
  }

  return false;
}

#pragma endregion

#pragma region external_funcs

ngf_error ngf_get_device_list(const ngf_device** devices, uint32_t* ndevices) {
  ngfvk_init_loader_if_necessary();
  if (NGFVK_DEVICE_LIST == NULL) {
    ngf_error  err          = NGF_ERROR_OK;
    VkInstance tmp_instance = VK_NULL_HANDLE;
    VkResult   vk_err       = ngfvk_create_instance(false, &tmp_instance, NULL);
    if (vk_err != VK_SUCCESS) { return NGF_ERROR_OBJECT_CREATION_FAILED; }
    PFN_vkEnumeratePhysicalDevices enumerate_vk_phys_devs = (PFN_vkEnumeratePhysicalDevices)
        vkGetInstanceProcAddr(tmp_instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties get_vk_phys_dev_properties =
        (PFN_vkGetPhysicalDeviceProperties)
            vkGetInstanceProcAddr(tmp_instance, "vkGetPhysicalDeviceProperties");
    PFN_vkGetPhysicalDeviceFeatures get_vk_phys_dev_features = (PFN_vkGetPhysicalDeviceFeatures)
        vkGetInstanceProcAddr(tmp_instance, "vkGetPhysicalDeviceFeatures");
    PFN_vkDestroyInstance destroy_vk_instance =
        (PFN_vkDestroyInstance)vkGetInstanceProcAddr(tmp_instance, "vkDestroyInstance");
    vk_err = enumerate_vk_phys_devs(tmp_instance, &NGFVK_DEVICE_COUNT, NULL);
    if (vk_err != VK_SUCCESS || NGFVK_DEVICE_COUNT == 0) {
      err = NGF_ERROR_OBJECT_CREATION_FAILED;
      goto ngf_enumerate_devices_cleanup;
    }
    NGFVK_DEVICE_LIST    = malloc(sizeof(ngf_device) * NGFVK_DEVICE_COUNT);
    NGFVK_DEVICE_ID_LIST = malloc(sizeof(ngfvk_device_id) * NGFVK_DEVICE_COUNT);
    VkPhysicalDevice* phys_devs =
        ngfi_sa_alloc(ngfi_tmp_store(), sizeof(VkPhysicalDevice) * NGFVK_DEVICE_COUNT);
    if (NGFVK_DEVICE_LIST == NULL || NGFVK_DEVICE_ID_LIST == NULL || phys_devs == NULL) {
      err = NGF_ERROR_OUT_OF_MEM;
      goto ngf_enumerate_devices_cleanup;
    }

    enumerate_vk_phys_devs(tmp_instance, &NGFVK_DEVICE_COUNT, phys_devs);
    for (size_t i = 0; i < NGFVK_DEVICE_COUNT; ++i) {
      VkPhysicalDeviceProperties dev_props;
      VkPhysicalDeviceFeatures   dev_features;
      get_vk_phys_dev_properties(phys_devs[i], &dev_props);
      get_vk_phys_dev_features(phys_devs[i], &dev_features);
      ngfvk_device_id* ngfdevid = &NGFVK_DEVICE_ID_LIST[i];
      ngfdevid->device_id       = dev_props.deviceID;
      ngfdevid->vendor_id       = dev_props.vendorID;
      ngf_device* ngfdev        = &NGFVK_DEVICE_LIST[i];
      ngfdev->handle            = (ngf_device_handle)i;
      switch (dev_props.deviceType) {
      case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        ngfdev->performance_tier = NGF_DEVICE_PERFORMANCE_TIER_HIGH;
        break;
      case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
      case VK_PHYSICAL_DEVICE_TYPE_CPU:
        ngfdev->performance_tier = NGF_DEVICE_PERFORMANCE_TIER_LOW;
        break;
      default:
        ngfdev->performance_tier = NGF_DEVICE_PERFORMANCE_TIER_UNKNOWN;
      }
      strncpy(
          ngfdev->name,
          dev_props.deviceName,
          NGFI_MIN(NGF_DEVICE_NAME_MAX_LENGTH, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE));
      ngf_device_capabilities*      devcaps     = &ngfdev->capabilities;
      const VkPhysicalDeviceLimits* vkdevlimits = &dev_props.limits;
      devcaps->clipspace_z_zero_to_one          = true;
      devcaps->uniform_buffer_offset_alignment =
          (size_t)vkdevlimits->minUniformBufferOffsetAlignment;
      devcaps->texel_buffer_offset_alignment = (size_t)vkdevlimits->minTexelBufferOffsetAlignment;
      devcaps->max_vertex_input_attributes_per_pipeline = vkdevlimits->maxVertexInputAttributes;
      devcaps->max_sampled_images_per_stage  = vkdevlimits->maxPerStageDescriptorSampledImages;
      devcaps->max_samplers_per_stage        = vkdevlimits->maxPerStageDescriptorSamplers;
      devcaps->max_fragment_input_components = vkdevlimits->maxFragmentInputComponents;
      devcaps->max_fragment_inputs =
          (devcaps->max_fragment_input_components) / 4; /* as per vk spec. */
      devcaps->max_1d_image_dimension          = vkdevlimits->maxImageDimension1D;
      devcaps->max_2d_image_dimension          = vkdevlimits->maxImageDimension2D;
      devcaps->max_3d_image_dimension          = vkdevlimits->maxImageDimension3D;
      devcaps->max_cube_image_dimension        = vkdevlimits->maxImageDimensionCube;
      devcaps->max_image_layers                = vkdevlimits->maxImageArrayLayers;
      devcaps->max_color_attachments_per_pass  = vkdevlimits->maxColorAttachments;
      devcaps->max_uniform_buffers_per_stage   = vkdevlimits->maxPerStageDescriptorUniformBuffers;
      devcaps->max_sampler_anisotropy          = vkdevlimits->maxSamplerAnisotropy;
      devcaps->max_uniform_buffer_range        = vkdevlimits->maxUniformBufferRange;
      devcaps->cubemap_arrays_supported        = dev_features.imageCubeArray;
      devcaps->framebuffer_color_sample_counts = vkdevlimits->framebufferColorSampleCounts;
      devcaps->framebuffer_depth_sample_counts = vkdevlimits->framebufferDepthSampleCounts;
      devcaps->texture_color_sample_counts     = vkdevlimits->sampledImageColorSampleCounts;
      devcaps->texture_depth_sample_counts     = vkdevlimits->sampledImageDepthSampleCounts;

      devcaps->max_supported_framebuffer_color_sample_count =
          ngfi_get_highest_sample_count(devcaps->framebuffer_color_sample_counts);
      devcaps->max_supported_framebuffer_depth_sample_count =
          ngfi_get_highest_sample_count(devcaps->framebuffer_depth_sample_counts);
      devcaps->max_supported_texture_color_sample_count =
          ngfi_get_highest_sample_count(devcaps->texture_color_sample_counts);
      devcaps->max_supported_texture_depth_sample_count =
          ngfi_get_highest_sample_count(devcaps->texture_depth_sample_counts);
    }
ngf_enumerate_devices_cleanup:
    if (tmp_instance != VK_NULL_HANDLE) { destroy_vk_instance(tmp_instance, NULL); }
    if (err != NGF_ERROR_OK) return err;
  }
  if (devices) { *devices = NGFVK_DEVICE_LIST; }
  if (ndevices) { *ndevices = NGFVK_DEVICE_COUNT; }
  return NGF_ERROR_OK;
}

ngf_error ngf_initialize(const ngf_init_info* init_info) {
  assert(init_info);

  if (init_info->renderdoc_info) {
    ModuleHandle ngf_renderdoc_mod = LoadLibraryA(init_info->renderdoc_info->renderdoc_lib_path);
    if (ngf_renderdoc_mod != NULL) {
      pRENDERDOC_GetAPI RENDERDOC_GetAPI =
          (pRENDERDOC_GetAPI)GetProcAddress(ngf_renderdoc_mod, "RENDERDOC_GetAPI");
      if (!RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_6_0, (void**)&_renderdoc.api)) {
        return NGF_ERROR_OBJECT_CREATION_FAILED;
      }
      if (init_info->renderdoc_info->renderdoc_destination_template) {
        _renderdoc.api->SetCaptureFilePathTemplate(
            init_info->renderdoc_info->renderdoc_destination_template);
      }
      _renderdoc.is_capturing_next_frame = false;
    }
  }

  // Sanity checks.
  if (!init_info) { return NGF_ERROR_INVALID_OPERATION; }
  if (_vk.instance != VK_NULL_HANDLE) {
    // Disallow double initialization.
    NGFI_DIAG_ERROR("double-initialization detected. `ngf_initialize` may only be called once.")
    return NGF_ERROR_INVALID_OPERATION;
  }

  // Install user-provided diagnostic callbacks and set preferred log verbosity.
  if (init_info->diag_info != NULL) {
    ngfi_diag_info = *init_info->diag_info;
  } else {
    ngfi_diag_info.callback  = NULL;
    ngfi_diag_info.userdata  = NULL;
    ngfi_diag_info.verbosity = NGF_DIAGNOSTICS_VERBOSITY_DEFAULT;
  }
  NGFI_DIAG_INFO("Initializing nicegraf.");

  // Install user-provided allocation callbacks.
  ngfi_set_allocation_callbacks(init_info->allocation_callbacks);

  // Load vk entrypoints.
  ngfvk_init_loader_if_necessary();

  // Create vk instance, attempting to enable api validation according to user preference.
  const bool request_validation = ngfi_diag_info.verbosity == NGF_DIAGNOSTICS_VERBOSITY_DETAILED;
  ngfvk_create_instance(request_validation, &_vk.instance, &_vk.validation_enabled);
  vkl_init_instance(
      _vk.instance);  // load instance-level Vulkan functions into the global namespace.

  // If validation was requested, and successfully enabled, install a debug callback to forward
  // vulkan debug messages to the user.
  if (_vk.validation_enabled) {
    NGFI_DIAG_INFO("vulkan validation layers enabled");
    const VkDebugUtilsMessengerCreateInfoEXT debug_callback_info = {
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .pNext           = NULL,
        .flags           = 0u,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT,

        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = ngfvk_debug_message_callback,
        .pUserData       = NULL};
    vkCreateDebugUtilsMessengerEXT(_vk.instance, &debug_callback_info, NULL, &_vk.debug_messenger);
  } else {
    NGFI_DIAG_INFO("vulkan validation is disabled");
  }

  // Obtain a list of available physical devices.
  uint32_t         nphysdev = NGFVK_MAX_PHYS_DEV;
  VkPhysicalDevice physdevs[NGFVK_MAX_PHYS_DEV];
  VkResult         vk_err = vkEnumeratePhysicalDevices(_vk.instance, &nphysdev, physdevs);
  if (vk_err != VK_SUCCESS) {
    NGFI_DIAG_ERROR("Failed to enumerate Vulkan physical devices, VK error %d.", vk_err);
    return NGF_ERROR_INVALID_OPERATION;
  }

  // sanity-check the device handle.
  const uint32_t device_idx = (uint32_t)init_info->device;
  if (device_idx >= NGFVK_DEVICE_COUNT) { return NGF_ERROR_INVALID_OPERATION; }

  // Pick a suitable physical device based on user's preference.
  uint32_t               vk_device_index = NGFVK_INVALID_IDX;
  const ngfvk_device_id* ngfdevid        = &NGFVK_DEVICE_ID_LIST[device_idx];
  for (uint32_t i = 0; i < nphysdev; ++i) {
    VkPhysicalDeviceProperties dev_props;
    vkGetPhysicalDeviceProperties(physdevs[i], &dev_props);
    if (dev_props.deviceID == ngfdevid->device_id && dev_props.vendorID == ngfdevid->vendor_id) {
      vk_device_index = i;
    }
  }
  if (vk_device_index == NGFVK_INVALID_IDX) {
    NGFI_DIAG_ERROR("Failed to find a suitable physical device.");
    return NGF_ERROR_INVALID_OPERATION;
  }
  _vk.phys_dev = physdevs[vk_device_index];
  VkPhysicalDeviceProperties phys_dev_properties;
  vkGetPhysicalDeviceProperties(_vk.phys_dev, &phys_dev_properties);

  // Obtain a list of queue family properties from the device.
  uint32_t num_queue_families = 0U;
  vkGetPhysicalDeviceQueueFamilyProperties(_vk.phys_dev, &num_queue_families, NULL);
  VkQueueFamilyProperties* queue_families =
      NGFI_ALLOCN(VkQueueFamilyProperties, num_queue_families);
  assert(queue_families);
  vkGetPhysicalDeviceQueueFamilyProperties(_vk.phys_dev, &num_queue_families, queue_families);

  // Pick suitable queue families for graphics and present, ensuring graphics also supports compute.
  uint32_t gfx_family_idx     = NGFVK_INVALID_IDX;
  uint32_t present_family_idx = NGFVK_INVALID_IDX;
  for (uint32_t q = 0; queue_families && q < num_queue_families; ++q) {
    const VkQueueFlags flags      = queue_families[q].queueFlags;
    const bool         is_gfx     = (flags & VK_QUEUE_GRAPHICS_BIT) != 0;
    const bool         is_present = ngfvk_query_presentation_support(_vk.phys_dev, q);
    const bool         is_compute = (flags & VK_QUEUE_COMPUTE_BIT) != 0;
    if (gfx_family_idx == NGFVK_INVALID_IDX && is_gfx && is_compute) { gfx_family_idx = q; }
    if (present_family_idx == NGFVK_INVALID_IDX && is_present) { present_family_idx = q; }
  }
  NGFI_FREEN(queue_families, num_queue_families);
  queue_families = NULL;
  if (gfx_family_idx == NGFVK_INVALID_IDX || present_family_idx == NGFVK_INVALID_IDX) {
    NGFI_DIAG_ERROR("Could not find a suitable queue family for graphics and/or presentation.");
    return NGF_ERROR_INVALID_OPERATION;
  }
  _vk.gfx_family_idx     = gfx_family_idx;
  _vk.present_family_idx = present_family_idx;

  // Create logical device.
  const float             queue_prio           = 1.0f;
  const bool              same_gfx_and_present = _vk.gfx_family_idx == _vk.present_family_idx;
  VkDeviceQueueCreateInfo queue_infos[]        = {
      {.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
              .pNext            = NULL,
              .flags            = 0,
              .queueFamilyIndex = _vk.present_family_idx,
              .queueCount       = 1,
              .pQueuePriorities = &queue_prio},
      {.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
              .pNext            = NULL,
              .flags            = 0,
              .queueFamilyIndex = _vk.gfx_family_idx,
              .queueCount       = 1,
              .pQueuePriorities = &queue_prio}};
  const uint32_t num_queue_infos = (same_gfx_and_present ? 1u : 2u);
  const char*    device_exts[]   = {
      "VK_KHR_maintenance1",
      "VK_KHR_swapchain",
      "VK_KHR_shader_float16_int8"};
  const uint32_t device_exts_count = sizeof(device_exts) / sizeof(const char*);
  const bool     shader_float16_int8_supported =
      ngfvk_phys_dev_extension_supported("VK_KHR_shader_float16_int8");

  const VkBool32 enable_cubemap_arrays =
      NGFVK_DEVICE_LIST[device_idx].capabilities.cubemap_arrays_supported ? VK_TRUE : VK_FALSE;
  const VkPhysicalDeviceFeatures required_features = {
      .samplerAnisotropy = VK_TRUE,
      .imageCubeArray    = enable_cubemap_arrays};
  VkPhysicalDeviceShaderFloat16Int8Features sf16_features = {
      .sType         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
      .pNext         = NULL,
      .shaderFloat16 = false,
      .shaderInt8    = false};

  if (vkGetPhysicalDeviceFeatures2KHR) {
    VkPhysicalDeviceFeatures2KHR phys_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &sf16_features};
    vkGetPhysicalDeviceFeatures2KHR(_vk.phys_dev, &phys_features);
  }

  const VkDeviceCreateInfo dev_info = {
      .sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext                = &sf16_features,
      .flags                = 0,
      .queueCreateInfoCount = num_queue_infos,
      .pQueueCreateInfos    = &queue_infos[same_gfx_and_present ? 1u : 0u],
      .enabledLayerCount    = 0,
      .ppEnabledLayerNames  = NULL,
      .pEnabledFeatures     = &required_features,
      .enabledExtensionCount =
          shader_float16_int8_supported ? device_exts_count : device_exts_count - 1,
      .ppEnabledExtensionNames = device_exts};
  vk_err = vkCreateDevice(_vk.phys_dev, &dev_info, NULL, &_vk.device);
  if (vk_err != VK_SUCCESS) {
    NGFI_DIAG_ERROR("Failed to create a Vulkan device, VK error %d.", vk_err);
    return NGF_ERROR_INVALID_OPERATION;
  }

  // Load device-level entry points.
  vkl_init_device(_vk.device);

  // Obtain queue handles.
  vkGetDeviceQueue(_vk.device, _vk.gfx_family_idx, 0, &_vk.gfx_queue);
  vkGetDeviceQueue(_vk.device, _vk.present_family_idx, 0, &_vk.present_queue);

  // Initialize pending image barrier queue.
  NGFI_DARRAY_RESET(NGFVK_PENDING_IMG_BARRIER_QUEUE.barriers, 10u);
  pthread_mutex_init(&NGFVK_PENDING_IMG_BARRIER_QUEUE.lock, 0);

  // Populate device capabilities.
  DEVICE_CAPS = NGFVK_DEVICE_LIST[init_info->device].capabilities;

  // Done!

  return NGF_ERROR_OK;
}

void ngf_shutdown(void) {
  NGFI_DIAG_INFO("Shutting down nicegraf.");

  if (CURRENT_CONTEXT != NULL) {
      NGFI_DIAG_ERROR("Context not destroyed before shutdown.")
  }

  vkDestroyDevice(_vk.device, NULL);
  if (_vk.validation_enabled) {
    vkDestroyDebugUtilsMessengerEXT(_vk.instance, _vk.debug_messenger, NULL);
  }
  vkDestroyInstance(_vk.instance, NULL);
}

const ngf_device_capabilities* ngf_get_device_capabilities(void) {
  return &DEVICE_CAPS;
}

#if defined(__APPLE__)
void* ngfvk_create_ca_metal_layer(const ngf_swapchain_info*);
#endif

ngf_error ngf_create_context(const ngf_context_info* info, ngf_context* result) {
  assert(info);
  assert(result);

  ngf_error                 err            = NGF_ERROR_OK;
  VkResult                  vk_err         = VK_SUCCESS;
  const ngf_swapchain_info* swapchain_info = info->swapchain_info;

  // Allocate space for context data.
  *result         = NGFI_ALLOC(struct ngf_context_t);
  ngf_context ctx = *result;
  if (ctx == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_create_context_cleanup;
  }
  memset(ctx, 0, sizeof(struct ngf_context_t));

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
      .vkDestroyImage                      = vkDestroyImage,
      .vkCmdCopyBuffer                     = vkCmdCopyBuffer,
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
      .pRecordSettings             = NULL,
      .instance                    = _vk.instance,
      .vulkanApiVersion            = 0};
  vk_err = vmaCreateAllocator(&vma_info, &ctx->allocator);

  // Create swapchain if necessary.
  if (swapchain_info != NULL) {
    // Begin by creating the window surface.
#if defined(_WIN32) || defined(_WIN64)
    const VkWin32SurfaceCreateInfoKHR surface_info = {
        .sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .pNext     = NULL,
        .flags     = 0,
        .hinstance = GetModuleHandle(NULL),
        .hwnd      = (HWND)swapchain_info->native_handle};
#elif defined(__ANDROID__)
    const VkAndroidSuraceCreateInfoKHR surface_info = {
        .sType  = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR,
        .pNext  = NULL,
        .flags  = 0,
        .window = swapchain_info->native_handle};
#elif defined(__APPLE__)
    const VkMetalSurfaceCreateInfoEXT surface_info = {
        .sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT,
        .pNext  = NULL,
        .flags  = 0,
        .pLayer = (const CAMetalLayer*)ngfvk_create_ca_metal_layer(swapchain_info)};
#else
    const VkXcbSurfaceCreateInfoKHR surface_info = {
        .sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .pNext      = NULL,
        .flags      = 0,
        .window     = (xcb_window_t)swapchain_info->native_handle,
        .connection = _vk.xcb_connection};
#endif
    vk_err = VK_CREATE_SURFACE_FN(_vk.instance, &surface_info, NULL, &ctx->surface);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_OBJECT_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }
    VkBool32 surface_supported = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(
        _vk.phys_dev,
        _vk.present_family_idx,
        ctx->surface,
        &surface_supported);
    if (!surface_supported) {
      err = NGF_ERROR_OBJECT_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }

    // Create the default rendertarget object.
    const bool default_rt_has_depth = swapchain_info->depth_format != NGF_IMAGE_FORMAT_UNDEFINED;
    const bool default_rt_is_multisampled = swapchain_info->sample_count > 1u;
    const bool default_rt_no_stencil = swapchain_info->depth_format == NGF_IMAGE_FORMAT_DEPTH32 ||
                                       swapchain_info->depth_format == NGF_IMAGE_FORMAT_DEPTH16;

    ctx->default_render_target = NGFI_ALLOC(struct ngf_render_target_t);
    if (ctx->default_render_target == NULL) {
      err = NGF_ERROR_OBJECT_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }
    const uint32_t nattachment_descs =
        1u + (default_rt_has_depth ? 1u : 0u) + (default_rt_is_multisampled ? 1u : 0u);

    ctx->default_render_target->is_default   = true;
    ctx->default_render_target->width        = swapchain_info->width;
    ctx->default_render_target->height       = swapchain_info->height;
    ctx->default_render_target->frame_buffer = VK_NULL_HANDLE;
    ctx->default_render_target->nattachments = nattachment_descs;
    ctx->default_render_target->attachment_descs =
        NGFI_ALLOCN(ngf_attachment_description, nattachment_descs);
    ctx->default_render_target->attachment_compat_pass_descs =
        NGFI_ALLOCN(ngfvk_attachment_pass_desc, nattachment_descs);
    ctx->default_render_target->attachment_image_views = NULL;

    uint32_t                    attachment_desc_idx = 0u;
    ngf_attachment_description* color_attachment_desc =
        &ctx->default_render_target->attachment_descs[attachment_desc_idx];
    color_attachment_desc->format       = swapchain_info->color_format;
    color_attachment_desc->is_sampled   = false;
    color_attachment_desc->sample_count = swapchain_info->sample_count;
    color_attachment_desc->type         = NGF_ATTACHMENT_COLOR;

    ngfvk_attachment_pass_desc* color_attachment_pass_desc =
        &ctx->default_render_target->attachment_compat_pass_descs[attachment_desc_idx];
    color_attachment_pass_desc->initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment_pass_desc->layout         = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment_pass_desc->final_layout   = default_rt_is_multisampled
                                                     ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                                     : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    color_attachment_pass_desc->is_resolve     = false;
    color_attachment_pass_desc->load_op        = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment_pass_desc->store_op       = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    if (default_rt_has_depth) {
      ++attachment_desc_idx;

      ngf_attachment_description* depth_attachment_desc =
          &ctx->default_render_target->attachment_descs[attachment_desc_idx];
      depth_attachment_desc->format       = swapchain_info->depth_format;
      depth_attachment_desc->is_sampled   = false;
      depth_attachment_desc->sample_count = swapchain_info->sample_count;
      depth_attachment_desc->type =
          default_rt_no_stencil ? NGF_ATTACHMENT_DEPTH : NGF_ATTACHMENT_DEPTH_STENCIL;

      ngfvk_attachment_pass_desc* depth_attachment_pass_desc =
          &ctx->default_render_target->attachment_compat_pass_descs[attachment_desc_idx];
      depth_attachment_pass_desc->initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
      depth_attachment_pass_desc->layout         = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      depth_attachment_pass_desc->final_layout   = depth_attachment_pass_desc->layout;
      depth_attachment_pass_desc->is_resolve     = false;
      depth_attachment_pass_desc->load_op        = VK_ATTACHMENT_LOAD_OP_CLEAR;
      depth_attachment_pass_desc->store_op       = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }

    if (default_rt_is_multisampled) {
      ++attachment_desc_idx;

      ngf_attachment_description* resolve_attachment_desc =
          &ctx->default_render_target->attachment_descs[attachment_desc_idx];
      resolve_attachment_desc->format       = swapchain_info->color_format;
      resolve_attachment_desc->is_sampled   = false;
      resolve_attachment_desc->sample_count = NGF_SAMPLE_COUNT_1;
      resolve_attachment_desc->type         = NGF_ATTACHMENT_COLOR;

      ngfvk_attachment_pass_desc* resolve_attachment_pass_desc =
          &ctx->default_render_target->attachment_compat_pass_descs[attachment_desc_idx];
      resolve_attachment_pass_desc->initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
      resolve_attachment_pass_desc->layout         = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      resolve_attachment_pass_desc->final_layout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      resolve_attachment_pass_desc->is_resolve     = true;
      resolve_attachment_pass_desc->load_op        = VK_ATTACHMENT_LOAD_OP_CLEAR;
      resolve_attachment_pass_desc->store_op       = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }

    ngfvk_renderpass_from_attachment_descs(
        nattachment_descs,
        ctx->default_render_target->attachment_descs,
        ctx->default_render_target->attachment_compat_pass_descs,
        &ctx->default_render_target->compat_render_pass);

    // Create the swapchain itself.
    ngf_context tmp = CURRENT_CONTEXT;
    CURRENT_CONTEXT = ctx;
    err             = ngfvk_create_swapchain(swapchain_info, ctx->surface, &ctx->swapchain);
    CURRENT_CONTEXT = tmp;
    if (err != NGF_ERROR_OK) goto ngf_create_context_cleanup;
    ctx->swapchain_info = *swapchain_info;
  } else {
    ctx->default_render_target = NULL;
  }

  // Create frame resource holders.
  const uint32_t max_inflight_frames = swapchain_info ? ctx->swapchain.num_images : 3u;
  ctx->max_inflight_frames           = max_inflight_frames;
  ctx->frame_res                     = NGFI_ALLOCN(ngfvk_frame_resources, max_inflight_frames);
  if (ctx->frame_res == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_create_context_cleanup;
  }
  for (uint32_t f = 0u; f < max_inflight_frames; ++f) {
    NGFI_DARRAY_RESET(ctx->frame_res[f].cmd_bufs, 8u);
    NGFI_DARRAY_RESET(ctx->frame_res[f].cmd_pools, 8u);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_pipelines, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_pipeline_layouts, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_dset_layouts, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_framebuffers, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_render_passes, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_samplers, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_image_views, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_buffer_views, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_events, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].real_retire_events, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_images, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_buffers, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].reset_desc_pools_lists, 8);

    ctx->frame_res[f].semaphore                = VK_NULL_HANDLE;
    const VkSemaphoreCreateInfo semaphore_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0u,
    };
    vk_err = vkCreateSemaphore(_vk.device, &semaphore_info, NULL, &ctx->frame_res[f].semaphore);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_OBJECT_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }

    const VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0u};
    ctx->frame_res[f].nwait_fences = 0;
    for (uint32_t i = 0u; i < sizeof(ctx->frame_res[f].fences) / sizeof(VkFence); ++i) {
      vk_err = vkCreateFence(_vk.device, &fence_info, NULL, &ctx->frame_res[f].fences[i]);
      if (vk_err != VK_SUCCESS) {
        err = NGF_ERROR_OBJECT_CREATION_FAILED;
        goto ngf_create_context_cleanup;
      }
    }
  }

  ctx->frame_id = 0u;

  // initialize bind op allocator.
  ctx->bind_op_chunk_allocator = ngfi_blkalloc_create(sizeof(ngfvk_bind_op_chunk), 256);
  if (ctx->bind_op_chunk_allocator == NULL) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngf_create_context_cleanup;
  }

  ctx->current_frame_token = ~0u;

  NGFI_DARRAY_RESET(ctx->command_superpools, 3);
  NGFI_DARRAY_RESET(ctx->desc_superpools, 3);
  NGFI_DARRAY_RESET(ctx->renderpass_cache, 8);

  ctx->cmd_buffer_counter = 0u;

ngf_create_context_cleanup:
  if (err != NGF_ERROR_OK) { ngf_destroy_context(ctx); }
  return err;
}

ngf_error ngf_resize_context(ngf_context ctx, uint32_t new_width, uint32_t new_height) {
  assert(ctx);
  if (ctx == NULL || ctx->default_render_target == NULL) { return NGF_ERROR_INVALID_OPERATION; }

  ngf_error err = NGF_ERROR_OK;
  ngfvk_destroy_swapchain(&ctx->swapchain);
  ctx->swapchain_info.width          = NGFI_MAX(1, new_width);
  ctx->swapchain_info.height         = NGFI_MAX(1, new_height);
  ctx->default_render_target->width  = ctx->swapchain_info.width;
  ctx->default_render_target->height = ctx->swapchain_info.height;
  err = ngfvk_create_swapchain(&ctx->swapchain_info, ctx->surface, &ctx->swapchain);
  return err;
}

void ngfvk_reset_renderpass_cache(ngf_context ctx) {
  NGFI_DARRAY_FOREACH(ctx->renderpass_cache, p) {
    vkDestroyRenderPass(_vk.device, NGFI_DARRAY_AT(ctx->renderpass_cache, p).renderpass, NULL);
  }
  NGFI_DARRAY_CLEAR(ctx->renderpass_cache);
}

void ngf_destroy_context(ngf_context ctx) {
  if (ctx != NULL) {
    vkDeviceWaitIdle(_vk.device);

    if (ctx->default_render_target) {
      ngfvk_destroy_swapchain(&ctx->swapchain);
      if (ctx->surface != VK_NULL_HANDLE) { vkDestroySurfaceKHR(_vk.instance, ctx->surface, NULL); }
      ngf_destroy_render_target(ctx->default_render_target);
    }

    for (uint32_t f = 0u; ctx->frame_res != NULL && f < ctx->max_inflight_frames; ++f) {
      ngfvk_retire_resources(&ctx->frame_res[f]);

      NGFI_DARRAY_FOREACH(ctx->frame_res[f].real_retire_events, s) {
        vkDestroyEvent(_vk.device, NGFI_DARRAY_AT(ctx->frame_res[f].real_retire_events, s), NULL);
      }

      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_pipelines);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_pipeline_layouts);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_dset_layouts);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_framebuffers);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_render_passes);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_samplers);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_image_views);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_buffer_views);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_events);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].real_retire_events);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_images);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_buffers);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].cmd_bufs);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].cmd_pools);
      for (uint32_t i = 0u; i < sizeof(ctx->frame_res[f].fences) / sizeof(VkFence); ++i) {
        vkDestroyFence(_vk.device, ctx->frame_res[f].fences[i], NULL);
      }
      if (ctx->frame_res[f].semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(_vk.device, ctx->frame_res[f].semaphore, NULL);
      }
    }

    NGFI_DARRAY_FOREACH(ctx->desc_superpools, p) {
      ngfvk_destroy_desc_superpool(&NGFI_DARRAY_AT(ctx->desc_superpools, p));
    }
    NGFI_DARRAY_DESTROY(ctx->desc_superpools);

    ngfvk_reset_renderpass_cache(ctx);
    NGFI_DARRAY_DESTROY(ctx->renderpass_cache);

    NGFI_DARRAY_FOREACH(ctx->command_superpools, i) {
      ngfvk_destroy_command_superpool(&ctx->command_superpools.data[i]);
    }
    NGFI_DARRAY_DESTROY(ctx->command_superpools);

    if (ctx->allocator != VK_NULL_HANDLE) { vmaDestroyAllocator(ctx->allocator); }
    if (ctx->frame_res != NULL) { NGFI_FREEN(ctx->frame_res, ctx->max_inflight_frames); }
    if (ctx->bind_op_chunk_allocator) { ngfi_blkalloc_destroy(ctx->bind_op_chunk_allocator); }

    if (CURRENT_CONTEXT == ctx) CURRENT_CONTEXT = NULL;
    NGFI_FREE(ctx);
  }
}

ngf_error ngf_set_context(ngf_context ctx) {
  CURRENT_CONTEXT = ctx;
  return NGF_ERROR_OK;
}

ngf_error ngf_create_cmd_buffer(const ngf_cmd_buffer_info* info, ngf_cmd_buffer* result) {
  assert(info);
  assert(result);
  NGFI_IGNORE_VAR(info);

  ngf_cmd_buffer cmd_buf = NGFI_ALLOC(ngf_cmd_buffer_t);
  if (cmd_buf == NULL) { return NGF_ERROR_OUT_OF_MEM; }
  *result                         = cmd_buf;
  cmd_buf->parent_frame           = ~0u;
  cmd_buf->state                  = NGFI_CMD_BUFFER_NEW;
  cmd_buf->active_gfx_pipe        = NULL;
  cmd_buf->active_compute_pipe    = NULL;
  cmd_buf->renderpass_active      = false;
  cmd_buf->compute_pass_active    = false;
  cmd_buf->active_rt              = NULL;
  cmd_buf->desc_pools_list        = NULL;
  cmd_buf->pending_bind_ops.first = NULL;
  cmd_buf->pending_bind_ops.last  = NULL;
  cmd_buf->pending_bind_ops.size  = 0u;
  cmd_buf->vk_cmd_buffer          = VK_NULL_HANDLE;
  cmd_buf->vk_cmd_pool            = VK_NULL_HANDLE;
  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_begin_render_pass_simple_with_sync(
    ngf_cmd_buffer                   cmd_buf,
    ngf_render_target                rt,
    float                            clear_color_r,
    float                            clear_color_g,
    float                            clear_color_b,
    float                            clear_color_a,
    float                            clear_depth,
    uint32_t                         clear_stencil,
    uint32_t                         nsync_compute_resources,
    const ngf_sync_compute_resource* sync_compute_resources,
    ngf_render_encoder*              enc) {
  ngfi_sa_reset(ngfi_tmp_store());
  ngf_attachment_load_op* load_ops =
      ngfi_sa_alloc(ngfi_tmp_store(), sizeof(ngf_attachment_load_op) * rt->nattachments);
  ngf_attachment_store_op* store_ops =
      ngfi_sa_alloc(ngfi_tmp_store(), sizeof(ngf_attachment_store_op) * rt->nattachments);
  ngf_clear* clears = ngfi_sa_alloc(ngfi_tmp_store(), sizeof(ngf_clear) * rt->nattachments);

  for (size_t i = 0u; i < rt->nattachments; ++i) {
    load_ops[i] = NGF_LOAD_OP_CLEAR;
    if (rt->attachment_descs[i].type == NGF_ATTACHMENT_COLOR) {
      clears[i].clear_color[0] = clear_color_r;
      clears[i].clear_color[1] = clear_color_g;
      clears[i].clear_color[2] = clear_color_b;
      clears[i].clear_color[3] = clear_color_a;
    } else if (rt->attachment_descs[i].type == NGF_ATTACHMENT_DEPTH) {
      clears[i].clear_depth_stencil.clear_depth   = clear_depth;
      clears[i].clear_depth_stencil.clear_stencil = clear_stencil;
    } else {
      assert(false);
    }
    const bool is_multisampled = (rt->attachment_descs[i].sample_count > 1u);
    store_ops[i]               = is_multisampled ? NGF_STORE_OP_DONTCARE : NGF_STORE_OP_STORE;
  }
  const ngf_render_pass_info pass_info = {
      .render_target          = rt,
      .load_ops               = load_ops,
      .store_ops              = store_ops,
      .clears                 = clears,
      .sync_compute_resources = {
          .nsync_resources = nsync_compute_resources,
          .sync_resources  = sync_compute_resources}};
  return ngf_cmd_begin_render_pass(cmd_buf, &pass_info, enc);
}

ngf_error ngf_cmd_begin_render_pass_simple(
    ngf_cmd_buffer      cmd_buf,
    ngf_render_target   rt,
    float               clear_color_r,
    float               clear_color_g,
    float               clear_color_b,
    float               clear_color_a,
    float               clear_depth,
    uint32_t            clear_stencil,
    ngf_render_encoder* enc) {
  return ngf_cmd_begin_render_pass_simple_with_sync(
      cmd_buf,
      rt,
      clear_color_r,
      clear_color_g,
      clear_color_b,
      clear_color_a,
      clear_depth,
      clear_stencil,
      0u,
      NULL,
      enc);
}

ngf_error ngf_cmd_begin_render_pass(
    ngf_cmd_buffer              cmd_buf,
    const ngf_render_pass_info* pass_info,
    ngf_render_encoder*         enc) {
  ngf_error          err         = NGF_ERROR_OK;
  const VkRenderPass render_pass = ngfvk_lookup_renderpass(
      pass_info->render_target,
      ngfvk_renderpass_ops_key(
          pass_info->render_target,
          pass_info->load_ops,
          pass_info->store_ops));

  const ngfvk_swapchain*  swapchain = &CURRENT_CONTEXT->swapchain;
  const ngf_render_target target    = pass_info->render_target;

  const VkFramebuffer fb =
      target->is_default ? swapchain->framebuffers[swapchain->image_idx] : target->frame_buffer;
  const VkExtent2D render_extent = {
      target->is_default ? CURRENT_CONTEXT->swapchain_info.width : target->width,
      target->is_default ? CURRENT_CONTEXT->swapchain_info.height : target->height};

  const uint32_t clear_value_count = pass_info->clears ? target->nattachments : 0u;
  VkClearValue*  vk_clears =
      clear_value_count > 0
           ? ngfi_sa_alloc(ngfi_tmp_store(), sizeof(VkClearValue) * clear_value_count)
           : NULL;
  if (clear_value_count > 0) {
    for (size_t i = 0; i < clear_value_count; ++i) {
      VkClearValue*    vk_clear_val = &vk_clears[i];
      const ngf_clear* clear        = &pass_info->clears[i];
      if (target->attachment_descs[i].format != NGF_IMAGE_FORMAT_DEPTH16 &&
          target->attachment_descs[i].format != NGF_IMAGE_FORMAT_DEPTH32 &&
          target->attachment_descs[i].format != NGF_IMAGE_FORMAT_DEPTH24_STENCIL8) {
        VkClearColorValue* clear_color_var = &vk_clear_val->color;
        clear_color_var->float32[0]        = clear->clear_color[0];
        clear_color_var->float32[1]        = clear->clear_color[1];
        clear_color_var->float32[2]        = clear->clear_color[2];
        clear_color_var->float32[3]        = clear->clear_color[3];
      } else {
        VkClearDepthStencilValue* clear_depth_stencil_val = &vk_clear_val->depthStencil;
        clear_depth_stencil_val->depth                    = clear->clear_depth_stencil.clear_depth;
        clear_depth_stencil_val->stencil = clear->clear_depth_stencil.clear_stencil;
      }
    }
  }
  const VkRenderPassBeginInfo begin_info = {
      .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .pNext           = NULL,
      .framebuffer     = fb,
      .clearValueCount = clear_value_count,
      .pClearValues    = vk_clears,
      .renderPass      = render_pass,
      .renderArea      = {.offset = {0u, 0u}, .extent = render_extent}};
  if (pass_info->sync_compute_resources.nsync_resources > 0u) {
    err = ngfvk_execute_sync_op(
        cmd_buf,
        pass_info->sync_compute_resources.nsync_resources,
        pass_info->sync_compute_resources.sync_resources,
        0u,
        NULL,
        0u,
        NULL,
        NGFVK_GFX_PIPELINE_STAGE_MASK);
    if (err != NGF_ERROR_OK) return err;
  }

  ngfvk_encoder_start(cmd_buf);
  if (err != NGF_ERROR_OK) return err;

  err = ngfvk_initialize_generic_encoder(cmd_buf, &enc->pvt_data_donotuse);
  if (err != NGF_ERROR_OK) { return err; }

  cmd_buf->active_rt         = target;
  cmd_buf->renderpass_active = true;
  vkCmdBeginRenderPass(cmd_buf->vk_cmd_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
  ngfi_sa_reset(ngfi_tmp_store());

  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_begin_xfer_pass(
    ngf_cmd_buffer            cmd_buf,
    const ngf_xfer_pass_info* pass_info,
    ngf_xfer_encoder*         enc) {
  ngf_error err;
  if (pass_info->sync_compute_resources.nsync_resources > 0u) {
    err = ngfvk_execute_sync_op(
        cmd_buf,
        pass_info->sync_compute_resources.nsync_resources,
        pass_info->sync_compute_resources.sync_resources,
        0u,
        NULL,
        0u,
        NULL,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    if (err != NGF_ERROR_OK) return err;
  }

  err = ngfvk_encoder_start(cmd_buf);
  if (err != NGF_ERROR_OK) return err;

  err = ngfvk_initialize_generic_encoder(cmd_buf, &enc->pvt_data_donotuse);
  if (err != NGF_ERROR_OK) { return err; }

  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_begin_compute_pass(
    ngf_cmd_buffer               cmd_buf,
    const ngf_compute_pass_info* pass_info,
    ngf_compute_encoder*         enc) {
  ngf_error err;
  err = ngfvk_execute_sync_op(
      cmd_buf,
      pass_info->sync_compute_resources.nsync_resources,
      pass_info->sync_compute_resources.sync_resources,
      pass_info->sync_render_resources.nsync_resources,
      pass_info->sync_render_resources.sync_resources,
      pass_info->sync_xfer_resources.nsync_resources,
      pass_info->sync_xfer_resources.sync_resources,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
  if (err != NGF_ERROR_OK) return err;

  err = ngfvk_encoder_start(cmd_buf);
  if (err != NGF_ERROR_OK) return err;

  err = ngfvk_initialize_generic_encoder(cmd_buf, &enc->pvt_data_donotuse);
  if (err != NGF_ERROR_OK) { return err; }

  cmd_buf->compute_pass_active = true;
  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_end_render_pass(ngf_render_encoder enc) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  vkCmdEndRenderPass(buf->vk_cmd_buffer);
  buf->renderpass_active = false;
  return ngfvk_encoder_end(buf, &enc.pvt_data_donotuse, NGFVK_GFX_PIPELINE_STAGE_MASK);
}

ngf_error ngf_cmd_end_xfer_pass(ngf_xfer_encoder enc) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  return ngfvk_encoder_end(buf, &enc.pvt_data_donotuse, VK_PIPELINE_STAGE_TRANSFER_BIT);
}

ngf_error ngf_cmd_end_compute_pass(ngf_compute_encoder enc) {
  ngf_cmd_buffer cmd_buf       = NGFVK_ENC2CMDBUF(enc);
  cmd_buf->compute_pass_active = false;
  return ngfvk_encoder_end(cmd_buf, &enc.pvt_data_donotuse, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

ngf_error ngf_start_cmd_buffer(ngf_cmd_buffer cmd_buf, ngf_frame_token token) {
  assert(cmd_buf);

  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_READY);

  cmd_buf->parent_frame    = token;
  cmd_buf->desc_pools_list = NULL;
  cmd_buf->active_rt       = NULL;
  return ngfvk_cmd_buffer_allocate_for_frame(token, &cmd_buf->vk_cmd_pool, &cmd_buf->vk_cmd_buffer);
}

void ngf_destroy_cmd_buffer(ngf_cmd_buffer buffer) {
  assert(buffer);
  if (buffer->vk_cmd_buffer != VK_NULL_HANDLE) {
    vkFreeCommandBuffers(_vk.device, buffer->vk_cmd_pool, 1u, &buffer->vk_cmd_buffer);
  }
  ngfvk_cleanup_pending_binds(buffer);
  NGFI_FREE(buffer);
}

ngf_error ngf_submit_cmd_buffers(uint32_t nbuffers, ngf_cmd_buffer* cmd_bufs) {
  assert(cmd_bufs);
  uint32_t               frame_id       = CURRENT_CONTEXT->frame_id;
  ngfvk_frame_resources* frame_res_data = &CURRENT_CONTEXT->frame_res[frame_id];
  for (uint32_t i = 0u; i < nbuffers; ++i) {
    ngf_cmd_buffer cmd_buf = cmd_bufs[i];
    if (cmd_buf->parent_frame != CURRENT_CONTEXT->current_frame_token) {
      NGFI_DIAG_ERROR("submitting a command buffer for the wrong frame");
      return NGF_ERROR_INVALID_OPERATION;
    }
    NGFI_TRANSITION_CMD_BUF(cmd_bufs[i], NGFI_CMD_BUFFER_SUBMITTED);
    if (cmd_buf->desc_pools_list) {
      NGFI_DARRAY_APPEND(frame_res_data->reset_desc_pools_lists, cmd_buf->desc_pools_list);
    }
    vkEndCommandBuffer(cmd_buf->vk_cmd_buffer);

    NGFI_DARRAY_APPEND(frame_res_data->cmd_bufs, cmd_buf->vk_cmd_buffer);
    NGFI_DARRAY_APPEND(frame_res_data->cmd_pools, cmd_buf->vk_cmd_pool);

    cmd_buf->active_gfx_pipe     = NULL;
    cmd_buf->active_compute_pipe = NULL;
    cmd_buf->active_rt           = NULL;
    cmd_buf->vk_cmd_buffer       = VK_NULL_HANDLE;
    cmd_buf->vk_cmd_pool         = VK_NULL_HANDLE;
    CURRENT_CONTEXT->cmd_buffer_counter++;
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_begin_frame(ngf_frame_token* token) {
  ngf_error      err = NGF_ERROR_OK;
  const uint32_t fi  = CURRENT_CONTEXT->frame_id;

  // setup frame capture
  if (_renderdoc.api && _renderdoc.is_capturing_next_frame) {
    _renderdoc.api->StartFrameCapture(
        RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(_vk.instance),
        (RENDERDOC_WindowHandle)CURRENT_CONTEXT->swapchain_info.native_handle);
  }

  // reset stack allocator.
  ngfi_sa_reset(ngfi_tmp_store());

  const bool needs_present = CURRENT_CONTEXT->swapchain.vk_swapchain != VK_NULL_HANDLE;

  if (needs_present) {
    const VkResult acquire_result = vkAcquireNextImageKHR(
        _vk.device,
        CURRENT_CONTEXT->swapchain.vk_swapchain,
        UINT64_MAX,
        CURRENT_CONTEXT->swapchain.image_semaphores[fi],
        VK_NULL_HANDLE,
        &CURRENT_CONTEXT->swapchain.image_idx);
    if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
      return NGF_ERROR_INVALID_OPERATION;
    }
  }

  // Retire resources.
  ngfvk_frame_resources* next_frame_res = &CURRENT_CONTEXT->frame_res[fi];
  ngfvk_retire_resources(next_frame_res);

  // Insert placeholders for deferred barriers.
  NGFI_DARRAY_APPEND(CURRENT_CONTEXT->frame_res[fi].cmd_bufs, VK_NULL_HANDLE);
  NGFI_DARRAY_APPEND(CURRENT_CONTEXT->frame_res[fi].cmd_pools, VK_NULL_HANDLE);

  CURRENT_CONTEXT->current_frame_token = ngfi_encode_frame_token(
      (uint16_t)((uintptr_t)CURRENT_CONTEXT & 0xffff),
      (uint8_t)CURRENT_CONTEXT->max_inflight_frames,
      (uint8_t)CURRENT_CONTEXT->frame_id);

  *token = CURRENT_CONTEXT->current_frame_token;

  return err;
}

ngf_error ngf_end_frame(ngf_frame_token token) {
  if (token != CURRENT_CONTEXT->current_frame_token) {
    NGFI_DIAG_ERROR("ending a frame with an unexpected frame token");
    return NGF_ERROR_INVALID_OPERATION;
  }

  ngf_error err = NGF_ERROR_OK;

  // Obtain the current frame resource structure and increment frame number.
  const uint32_t fi                = CURRENT_CONTEXT->frame_id;
  const uint32_t next_fi           = (fi + 1u) % CURRENT_CONTEXT->max_inflight_frames;
  CURRENT_CONTEXT->frame_id        = next_fi;
  ngfvk_frame_resources* frame_res = &CURRENT_CONTEXT->frame_res[fi];

  frame_res->nwait_fences = 0u;

  // Prep a command buffer for pending image barriers if necessary.
  pthread_mutex_lock(&NGFVK_PENDING_IMG_BARRIER_QUEUE.lock);
  const uint32_t npending_barriers = NGFI_DARRAY_SIZE(NGFVK_PENDING_IMG_BARRIER_QUEUE.barriers);
  const bool     have_deferred_barriers = npending_barriers > 0;
  if (have_deferred_barriers) {
    VkCommandPool   pool;
    VkCommandBuffer buffer;
    ngfvk_cmd_buffer_allocate_for_frame(token, &pool, &buffer);
    vkCmdPipelineBarrier(
        buffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        NULL,
        0,
        NULL,
        npending_barriers,
        NGFVK_PENDING_IMG_BARRIER_QUEUE.barriers.data);
    vkEndCommandBuffer(buffer);
    NGFI_DARRAY_AT(frame_res->cmd_bufs, 0)  = buffer;
    NGFI_DARRAY_AT(frame_res->cmd_pools, 0) = pool;
    NGFI_DARRAY_CLEAR(NGFVK_PENDING_IMG_BARRIER_QUEUE.barriers);
  }
  pthread_mutex_unlock(&NGFVK_PENDING_IMG_BARRIER_QUEUE.lock);

  // Submit pending commands & present.
  VkSemaphore image_semaphore = VK_NULL_HANDLE;
  const bool  needs_present   = CURRENT_CONTEXT->swapchain.vk_swapchain != VK_NULL_HANDLE;
  if (needs_present) { image_semaphore = CURRENT_CONTEXT->swapchain.image_semaphores[fi]; }

  const VkPipelineStageFlags wait_masks[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  const uint32_t ncmd_bufs = have_deferred_barriers ? NGFI_DARRAY_SIZE(frame_res->cmd_bufs)
                                                    : NGFI_DARRAY_SIZE(frame_res->cmd_bufs) - 1u;

  const VkSubmitInfo submit_info = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext = NULL,
      .pCommandBuffers =
          have_deferred_barriers ? (frame_res->cmd_bufs.data) : &(frame_res->cmd_bufs.data[1]),
      .commandBufferCount   = ncmd_bufs,
      .pWaitDstStageMask    = wait_masks,
      .pWaitSemaphores      = needs_present ? &image_semaphore : NULL,
      .waitSemaphoreCount   = needs_present ? 1u : 0u,
      .pSignalSemaphores    = &(frame_res->semaphore),
      .signalSemaphoreCount = 1u};

  const VkResult submit_result =
      vkQueueSubmit(_vk.gfx_queue, 1, &submit_info, frame_res->fences[frame_res->nwait_fences++]);
  if (submit_result != VK_SUCCESS) err = NGF_ERROR_INVALID_OPERATION;

  // Present if necessary.
  if (submit_result == VK_SUCCESS && needs_present) {
    const VkPresentInfoKHR present_info = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext              = NULL,
        .waitSemaphoreCount = 1u,
        .pWaitSemaphores    = &frame_res->semaphore,
        .swapchainCount     = 1,
        .pSwapchains        = &CURRENT_CONTEXT->swapchain.vk_swapchain,
        .pImageIndices      = &CURRENT_CONTEXT->swapchain.image_idx,
        .pResults           = NULL};
    const VkResult present_result = vkQueuePresentKHR(_vk.present_queue, &present_info);
    if (present_result != VK_SUCCESS) err = NGF_ERROR_INVALID_OPERATION;
  }

  // end frame capture
  if (_renderdoc.api && _renderdoc.is_capturing_next_frame) {
    _renderdoc.api->EndFrameCapture(
        RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(_vk.instance),
        (RENDERDOC_WindowHandle)CURRENT_CONTEXT->swapchain_info.native_handle);
    _renderdoc.is_capturing_next_frame = false;
  }

  return err;
}

ngf_error ngf_create_shader_stage(const ngf_shader_stage_info* info, ngf_shader_stage* result) {
  assert(info);
  assert(result);

  *result                = NGFI_ALLOC(ngf_shader_stage_t);
  ngf_shader_stage stage = *result;
  if (stage == NULL) { return NGF_ERROR_OUT_OF_MEM; }

  VkShaderModuleCreateInfo vk_sm_info = {
      .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .pNext    = NULL,
      .flags    = 0u,
      .pCode    = (uint32_t*)info->content,
      .codeSize = (info->content_length)};
  VkResult vkerr = vkCreateShaderModule(_vk.device, &vk_sm_info, NULL, &stage->vk_module);
  const SpvReflectResult spverr =
      spvReflectCreateShaderModule(info->content_length, info->content, &stage->spv_reflect_module);
  if (vkerr != VK_SUCCESS || spverr != SPV_REFLECT_RESULT_SUCCESS) {
    NGFI_FREE(stage);
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }
  stage->vk_stage_bits           = get_vk_shader_stage(info->type);
  size_t entry_point_name_length = strlen(info->entry_point_name) + 1u;
  stage->entry_point_name        = NGFI_ALLOCN(char, entry_point_name_length);
  strncpy(stage->entry_point_name, info->entry_point_name, entry_point_name_length);

  return NGF_ERROR_OK;
}

void ngf_destroy_shader_stage(ngf_shader_stage stage) {
  if (stage) {
    vkDestroyShaderModule(_vk.device, stage->vk_module, NULL);
    spvReflectDestroyShaderModule(&stage->spv_reflect_module);
    NGFI_FREEN(stage->entry_point_name, strlen(stage->entry_point_name) + 1u);
    NGFI_FREE(stage);
  }
}

ngf_error ngf_create_graphics_pipeline(
    const ngf_graphics_pipeline_info* info,
    ngf_graphics_pipeline*            result) {
  assert(info);
  assert(result);
  ngfi_sa_reset(ngfi_tmp_store());
  ngf_error err    = NGF_ERROR_OK;
  VkResult  vk_err = VK_SUCCESS;

  // Allocate space for the pipeline object.
  *result                        = NGFI_ALLOC(ngf_graphics_pipeline_t);
  ngf_graphics_pipeline pipeline = *result;
  if (pipeline == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_create_graphics_pipeline_cleanup;
  }

  VkPipelineShaderStageCreateInfo vk_shader_stages[5];
  err = ngfvk_initialize_generic_pipeline_data(
      &pipeline->generic_pipeline,
      info->spec_info,
      vk_shader_stages,
      info->shader_stages,
      info->nshader_stages);
  if (err != NGF_ERROR_OK) { goto ngf_create_graphics_pipeline_cleanup; }

  // Prepare vertex input.
  VkVertexInputBindingDescription* vk_binding_descs =
      NGFI_SALLOC(VkVertexInputBindingDescription, info->input_info->nvert_buf_bindings);
  VkVertexInputAttributeDescription* vk_attrib_descs =
      NGFI_SALLOC(VkVertexInputAttributeDescription, info->input_info->nattribs);

  if (vk_binding_descs == NULL || vk_attrib_descs == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_create_graphics_pipeline_cleanup;
  }

  for (uint32_t i = 0u; i < info->input_info->nvert_buf_bindings; ++i) {
    VkVertexInputBindingDescription*   vk_binding_desc = &vk_binding_descs[i];
    const ngf_vertex_buf_binding_desc* binding_desc    = &info->input_info->vert_buf_bindings[i];
    vk_binding_desc->binding                           = binding_desc->binding;
    vk_binding_descs->stride                           = binding_desc->stride;
    vk_binding_descs->inputRate = get_vk_input_rate(binding_desc->input_rate);
  }

  for (uint32_t i = 0u; i < info->input_info->nattribs; ++i) {
    VkVertexInputAttributeDescription* vk_attrib_desc = &vk_attrib_descs[i];
    const ngf_vertex_attrib_desc*      attrib_desc    = &info->input_info->attribs[i];
    vk_attrib_desc->location                          = attrib_desc->location;
    vk_attrib_desc->binding                           = attrib_desc->binding;
    vk_attrib_desc->offset                            = attrib_desc->offset;
    vk_attrib_desc->format =
        get_vk_vertex_format(attrib_desc->type, attrib_desc->size, attrib_desc->normalized);
  }

  VkPipelineVertexInputStateCreateInfo vertex_input = {
      .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .pNext                           = NULL,
      .flags                           = 0u,
      .vertexBindingDescriptionCount   = info->input_info->nvert_buf_bindings,
      .pVertexBindingDescriptions      = vk_binding_descs,
      .vertexAttributeDescriptionCount = info->input_info->nattribs,
      .pVertexAttributeDescriptions    = vk_attrib_descs};

  // Prepare input assembly.
  VkPipelineInputAssemblyStateCreateInfo input_assembly = {
      .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .pNext    = NULL,
      .flags    = 0u,
      .topology = get_vk_primitive_type(info->input_assembly_info->primitive_topology),
      .primitiveRestartEnable = info->input_assembly_info->enable_primitive_restart};

  // Prepare tessellation state.
  VkPipelineTessellationStateCreateInfo tess = {
      .sType              = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
      .pNext              = NULL,
      .flags              = 0u,
      .patchControlPoints = 1u};

  // Prepare viewport/scissor state.
  const VkViewport dummy_viewport =
      {.x = .0f, .y = .0f, .width = .0f, .height = .0f, .minDepth = .0f, .maxDepth = .0f};
  const VkRect2D dummy_scissor = {.offset = {.x = 0, .y = 0}, .extent = {.width = 0, .height = 0}};
  VkPipelineViewportStateCreateInfo viewport_state = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .pNext         = NULL,
      .flags         = 0u,
      .viewportCount = 1u,
      .scissorCount  = 1u,
      .pViewports    = &dummy_viewport,
      .pScissors     = &dummy_scissor};

  // Prepare rasterization state.
  VkPipelineRasterizationStateCreateInfo rasterization = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .pNext                   = NULL,
      .flags                   = 0u,
      .depthClampEnable        = VK_FALSE,
      .rasterizerDiscardEnable = info->rasterization->discard,
      .polygonMode             = get_vk_polygon_mode(info->rasterization->polygon_mode),
      .cullMode                = get_vk_cull_mode(info->rasterization->cull_mode),
      .frontFace               = get_vk_front_face(info->rasterization->front_face),
      .depthBiasEnable         = VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 0.0f};

  // Prepare multisampling.
  VkPipelineMultisampleStateCreateInfo multisampling = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .pNext                 = NULL,
      .flags                 = 0u,
      .rasterizationSamples  = get_vk_sample_count(info->multisample->sample_count),
      .sampleShadingEnable   = VK_FALSE,
      .minSampleShading      = 0.0f,
      .pSampleMask           = NULL,
      .alphaToCoverageEnable = info->multisample->alpha_to_coverage ? VK_TRUE : VK_FALSE,
      .alphaToOneEnable      = VK_FALSE};

  // Prepare depth/stencil.
  VkPipelineDepthStencilStateCreateInfo depth_stencil = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .pNext                 = NULL,
      .flags                 = 0u,
      .depthTestEnable       = info->depth_stencil->depth_test,
      .depthWriteEnable      = info->depth_stencil->depth_write,
      .depthCompareOp        = get_vk_compare_op(info->depth_stencil->depth_compare),
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable     = info->depth_stencil->stencil_test,
      .front =
          {.failOp      = get_vk_stencil_op(info->depth_stencil->front_stencil.fail_op),
           .passOp      = get_vk_stencil_op(info->depth_stencil->front_stencil.pass_op),
           .depthFailOp = get_vk_stencil_op(info->depth_stencil->front_stencil.depth_fail_op),
           .compareOp   = get_vk_compare_op(info->depth_stencil->front_stencil.compare_op),
           .compareMask = info->depth_stencil->front_stencil.compare_mask,
           .writeMask   = info->depth_stencil->front_stencil.write_mask,
           .reference   = info->depth_stencil->front_stencil.reference},
      .back =
          {.failOp      = get_vk_stencil_op(info->depth_stencil->back_stencil.fail_op),
           .passOp      = get_vk_stencil_op(info->depth_stencil->back_stencil.pass_op),
           .depthFailOp = get_vk_stencil_op(info->depth_stencil->back_stencil.depth_fail_op),
           .compareOp   = get_vk_compare_op(info->depth_stencil->back_stencil.compare_op),
           .compareMask = info->depth_stencil->back_stencil.compare_mask,
           .writeMask   = info->depth_stencil->back_stencil.write_mask,
           .reference   = info->depth_stencil->back_stencil.reference},
      .minDepthBounds = 0.0f,
      .maxDepthBounds = 1.0f};

  uint32_t ncolor_attachments = 0u;
  for (uint32_t i = 0; i < info->compatible_rt_attachment_descs->ndescs; ++i) {
    if (info->compatible_rt_attachment_descs->descs[i].type == NGF_ATTACHMENT_COLOR)
      ++ncolor_attachments;
  }

  // Prepare blend state.
  VkPipelineColorBlendAttachmentState blend_states[16];
  memset(blend_states, 0, sizeof(blend_states));
  for (size_t i = 0u; i < ncolor_attachments; ++i) {
    if (info->color_attachment_blend_states) {
      const ngf_blend_info* blend = &info->color_attachment_blend_states[i];

      const VkPipelineColorBlendAttachmentState attachment_blend_state = {
          .blendEnable         = blend->enable,
          .srcColorBlendFactor = blend->enable ? get_vk_blend_factor(blend->src_color_blend_factor)
                                               : VK_BLEND_FACTOR_ONE,
          .dstColorBlendFactor = blend->enable ? get_vk_blend_factor(blend->dst_color_blend_factor)
                                               : VK_BLEND_FACTOR_ZERO,
          .colorBlendOp = blend->enable ? get_vk_blend_op(blend->blend_op_color) : VK_BLEND_OP_ADD,
          .srcAlphaBlendFactor = blend->enable ? get_vk_blend_factor(blend->src_alpha_blend_factor)
                                               : VK_BLEND_FACTOR_ONE,
          .dstAlphaBlendFactor = blend->enable ? get_vk_blend_factor(blend->dst_alpha_blend_factor)
                                               : VK_BLEND_FACTOR_ZERO,
          .alphaBlendOp = blend->enable ? get_vk_blend_op(blend->blend_op_alpha) : VK_BLEND_OP_ADD,
          .colorWriteMask =
              ((blend->color_write_mask & NGF_COLOR_MASK_WRITE_BIT_R) ? VK_COLOR_COMPONENT_R_BIT
                                                                      : 0) |
              ((blend->color_write_mask & NGF_COLOR_MASK_WRITE_BIT_G) ? VK_COLOR_COMPONENT_G_BIT
                                                                      : 0) |
              ((blend->color_write_mask & NGF_COLOR_MASK_WRITE_BIT_B) ? VK_COLOR_COMPONENT_B_BIT
                                                                      : 0) |
              ((blend->color_write_mask & NGF_COLOR_MASK_WRITE_BIT_A) ? VK_COLOR_COMPONENT_A_BIT
                                                                      : 0)};
      blend_states[i] = attachment_blend_state;
    } else {
      blend_states[i].blendEnable    = VK_FALSE;
      blend_states[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }
  }

  if (ncolor_attachments >= NGFI_ARRAYSIZE(blend_states)) {
    NGFI_DIAG_ERROR("too many attachments specified");
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngf_create_graphics_pipeline_cleanup;
  }

  VkPipelineColorBlendStateCreateInfo color_blend = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .pNext           = NULL,
      .flags           = 0u,
      .logicOpEnable   = VK_FALSE,
      .logicOp         = VK_LOGIC_OP_SET,
      .attachmentCount = ncolor_attachments,
      .pAttachments    = blend_states,
      .blendConstants  = {
          info->blend_consts[0],
          info->blend_consts[1],
          info->blend_consts[2],
          info->blend_consts[3]}};

  // Dynamic state.
  const VkDynamicState dynamic_states[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
      VK_DYNAMIC_STATE_LINE_WIDTH,
      VK_DYNAMIC_STATE_DEPTH_BOUNDS};
  const uint32_t                   ndynamic_states = NGFI_ARRAYSIZE(dynamic_states);
  VkPipelineDynamicStateCreateInfo dynamic_state   = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .pNext             = NULL,
        .flags             = 0u,
        .dynamicStateCount = ndynamic_states,
        .pDynamicStates    = dynamic_states};

  // Create a compatible render pass object.
  ngfvk_attachment_pass_desc* attachment_compat_pass_descs = ngfi_sa_alloc(
      ngfi_tmp_store(),
      sizeof(ngfvk_attachment_pass_desc) * info->compatible_rt_attachment_descs->ndescs);
  for (uint32_t i = 0u; i < info->compatible_rt_attachment_descs->ndescs; ++i) {
    attachment_compat_pass_descs[i].load_op        = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment_compat_pass_descs[i].store_op       = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment_compat_pass_descs[i].final_layout   = VK_IMAGE_LAYOUT_GENERAL;
    attachment_compat_pass_descs[i].initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment_compat_pass_descs[i].is_resolve     = false;
    attachment_compat_pass_descs[i].layout         = VK_IMAGE_LAYOUT_GENERAL;
  }

  vk_err = ngfvk_renderpass_from_attachment_descs(
      info->compatible_rt_attachment_descs->ndescs,
      info->compatible_rt_attachment_descs->descs,
      attachment_compat_pass_descs,
      &pipeline->compatible_render_pass);

  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngf_create_graphics_pipeline_cleanup;
  }

  // Create required pipeline.
  const VkGraphicsPipelineCreateInfo vk_pipeline_info = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext               = NULL,
      .flags               = 0u,
      .stageCount          = info->nshader_stages,
      .pStages             = vk_shader_stages,
      .pVertexInputState   = &vertex_input,
      .pInputAssemblyState = &input_assembly,
      .pTessellationState  = &tess,
      .pViewportState      = &viewport_state,
      .pRasterizationState = &rasterization,
      .pMultisampleState   = &multisampling,
      .pDepthStencilState  = &depth_stencil,
      .pColorBlendState    = &color_blend,
      .pDynamicState       = &dynamic_state,
      .layout              = pipeline->generic_pipeline.vk_pipeline_layout,
      .renderPass          = pipeline->compatible_render_pass,
      .subpass             = 0u,
      .basePipelineHandle  = VK_NULL_HANDLE,
      .basePipelineIndex   = -1};
  vk_err = vkCreateGraphicsPipelines(
      _vk.device,
      VK_NULL_HANDLE,
      1u,
      &vk_pipeline_info,
      NULL,
      &pipeline->generic_pipeline.vk_pipeline);
  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngf_create_graphics_pipeline_cleanup;
  }

ngf_create_graphics_pipeline_cleanup:
  if (err != NGF_ERROR_OK) { ngf_destroy_graphics_pipeline(pipeline); }
  return err;
}

void ngf_destroy_graphics_pipeline(ngf_graphics_pipeline p) {
  if (p != NULL) {
    ngfvk_frame_resources* res = &CURRENT_CONTEXT->frame_res[CURRENT_CONTEXT->frame_id];
    NGFI_DARRAY_APPEND(res->retire_render_passes, p->compatible_render_pass);
    ngfi_destroy_generic_pipeline_data(res, &p->generic_pipeline);
    NGFI_FREE(p);
  }
}

ngf_error
ngf_create_compute_pipeline(const ngf_compute_pipeline_info* info, ngf_compute_pipeline* result) {
  assert(info);
  assert(result);
  ngfi_sa_reset(ngfi_tmp_store());
  ngf_error err = NGF_ERROR_OK;

  // Allocate space for the pipeline object.
  *result                       = NGFI_ALLOC(ngf_compute_pipeline_t);
  ngf_compute_pipeline pipeline = *result;
  if (pipeline == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_create_compute_pipeline_cleanup;
  }
  VkPipelineShaderStageCreateInfo vk_shader_stage;
  err = ngfvk_initialize_generic_pipeline_data(
      &pipeline->generic_pipeline,
      info->spec_info,
      &vk_shader_stage,
      &info->shader_stage,
      1);

  const VkComputePipelineCreateInfo vk_pipeline_ci = {
      .sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .pNext              = NULL,
      .flags              = 0,
      .stage              = vk_shader_stage,
      .layout             = pipeline->generic_pipeline.vk_pipeline_layout,
      .basePipelineHandle = VK_NULL_HANDLE,
      .basePipelineIndex  = -1};
  VkResult vk_err = vkCreateComputePipelines(
      _vk.device,
      VK_NULL_HANDLE,
      1,
      &vk_pipeline_ci,
      NULL,
      &pipeline->generic_pipeline.vk_pipeline);
  if (vk_err != VK_SUCCESS) { err = NGF_ERROR_OBJECT_CREATION_FAILED; }
ngf_create_compute_pipeline_cleanup:
  if (err != NGF_ERROR_OK) { ngf_destroy_compute_pipeline(pipeline); }
  return err;
}

void ngf_destroy_compute_pipeline(ngf_compute_pipeline p) {
  if (p != NULL) {
    ngfvk_frame_resources* res = &CURRENT_CONTEXT->frame_res[CURRENT_CONTEXT->frame_id];
    ngfi_destroy_generic_pipeline_data(res, &p->generic_pipeline);
    NGFI_FREE(p);
  }
}

ngf_render_target ngf_default_render_target() {
  if (CURRENT_CONTEXT) {
    return CURRENT_CONTEXT->default_render_target;
  } else {
    return NULL;
  }
}

const ngf_attachment_descriptions* ngf_default_render_target_attachment_descs() {
  if (CURRENT_CONTEXT->default_render_target) {
    CURRENT_CONTEXT->default_attachment_descriptions_list.ndescs =
        CURRENT_CONTEXT->swapchain_info.depth_format != NGF_IMAGE_FORMAT_UNDEFINED ? 2u : 1u;
    CURRENT_CONTEXT->default_attachment_descriptions_list.descs =
        CURRENT_CONTEXT->default_render_target->attachment_descs;
    return &CURRENT_CONTEXT->default_attachment_descriptions_list;
  } else {
    return NULL;
  }
}

ngf_error ngf_create_render_target(const ngf_render_target_info* info, ngf_render_target* result) {
  ngf_render_target rt = NGFI_ALLOC(ngf_render_target_t);
  if (rt == NULL) { return NGF_ERROR_OUT_OF_MEM; }
  memset(rt, 0, sizeof(ngf_render_target_t));
  *result          = rt;
  ngf_error err    = NGF_ERROR_OK;
  VkResult  vk_err = VK_SUCCESS;

  ngfvk_attachment_pass_desc* vk_attachment_pass_descs =
      NGFI_ALLOCN(ngfvk_attachment_pass_desc, info->attachment_descriptions->ndescs);

  VkImageView* attachment_views = NGFI_ALLOCN(VkImageView, info->attachment_descriptions->ndescs);
  if (vk_attachment_pass_descs == NULL || attachment_views == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_create_render_target_cleanup;
  }
  uint32_t ncolor_attachments = 0u;
  for (uint32_t a = 0u; a < info->attachment_descriptions->ndescs; ++a) {
    const ngf_attachment_description* ngf_attachment_desc =
        &info->attachment_descriptions->descs[a];
    ngfvk_attachment_pass_desc* attachment_pass_desc = &vk_attachment_pass_descs[a];
    const ngf_attachment_type   attachment_type      = ngf_attachment_desc->type;
    switch (attachment_type) {
    case NGF_ATTACHMENT_COLOR:
      ++ncolor_attachments;
      attachment_pass_desc->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      break;
    case NGF_ATTACHMENT_DEPTH:
    case NGF_ATTACHMENT_DEPTH_STENCIL:
      attachment_pass_desc->layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      break;
    default:
      assert(false);
    }

    const ngf_image_ref* attachment_img_ref = &info->attachment_image_refs[a];
    const ngf_image      attachment_img     = attachment_img_ref->image;
    const bool is_attachment_sampled = attachment_img->usage_flags & NGF_IMAGE_USAGE_SAMPLE_FROM;
    const bool is_attachment_storage = attachment_img->usage_flags & NGF_IMAGE_USAGE_STORAGE;
    attachment_pass_desc->is_resolve = false;
    attachment_pass_desc->initial_layout =
        is_attachment_storage ? VK_IMAGE_LAYOUT_GENERAL
                              : (is_attachment_sampled ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                       : attachment_pass_desc->layout);
    attachment_pass_desc->final_layout =
        is_attachment_storage ? VK_IMAGE_LAYOUT_GENERAL
                              : (is_attachment_sampled ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                       : attachment_pass_desc->layout);
    const VkImageAspectFlags subresource_aspect_flags =
        (attachment_type == NGF_ATTACHMENT_COLOR ? VK_IMAGE_ASPECT_COLOR_BIT : 0u) |
        (attachment_type == NGF_ATTACHMENT_DEPTH ? VK_IMAGE_ASPECT_DEPTH_BIT : 0u) |
        (attachment_type == NGF_ATTACHMENT_DEPTH_STENCIL
             ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
             : 0);
    const VkImageViewCreateInfo image_view_create_info = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext    = NULL,
        .flags    = 0u,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .image    = (VkImage)attachment_img->alloc.obj_handle,
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .format           = attachment_img->vkformat,
        .subresourceRange = {
            .baseArrayLayer = attachment_img_ref->layer,
            .baseMipLevel   = attachment_img_ref->mip_level,
            .layerCount     = 1u,
            .levelCount     = 1u,
            .aspectMask     = subresource_aspect_flags}};
    vk_err = vkCreateImageView(_vk.device, &image_view_create_info, NULL, &attachment_views[a]);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_OBJECT_CREATION_FAILED;
      goto ngf_create_render_target_cleanup;
    }
  }
  rt->attachment_image_views = attachment_views;

  const ngf_attachment_load_op  load_op  = NGF_LOAD_OP_CLEAR;
  const ngf_attachment_store_op store_op = NGF_STORE_OP_STORE;

  for (uint32_t a = 0u; a < info->attachment_descriptions->ndescs; ++a) {
    ngfvk_attachment_pass_desc* attachment_pass_desc = &vk_attachment_pass_descs[a];
    attachment_pass_desc->load_op                    = get_vk_load_op(load_op);
    attachment_pass_desc->store_op                   = get_vk_store_op(store_op);
  }
  const VkResult renderpass_create_result = ngfvk_renderpass_from_attachment_descs(
      info->attachment_descriptions->ndescs,
      info->attachment_descriptions->descs,
      vk_attachment_pass_descs,
      &rt->compat_render_pass);
  if (renderpass_create_result != VK_SUCCESS) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngf_create_render_target_cleanup;
  }

  rt->width                        = info->attachment_image_refs[0].image->extent.width;
  rt->height                       = info->attachment_image_refs[0].image->extent.height;
  rt->nattachments                 = info->attachment_descriptions->ndescs;
  rt->attachment_descs             = NGFI_ALLOCN(ngf_attachment_description, rt->nattachments);
  rt->attachment_compat_pass_descs = vk_attachment_pass_descs;

  memcpy(
      rt->attachment_descs,
      info->attachment_descriptions->descs,
      sizeof(ngf_attachment_description) * info->attachment_descriptions->ndescs);

  // Create a framebuffer.
  const VkFramebufferCreateInfo fb_info = {
      .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
      .pNext           = NULL,
      .flags           = 0u,
      .renderPass      = rt->compat_render_pass,
      .attachmentCount = info->attachment_descriptions->ndescs,
      .pAttachments    = attachment_views,
      .width           = rt->width,
      .height          = rt->height,
      .layers          = 1u};
  vk_err = vkCreateFramebuffer(_vk.device, &fb_info, NULL, &rt->frame_buffer);
  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngf_create_render_target_cleanup;
  }

ngf_create_render_target_cleanup:
  if (err != NGF_ERROR_OK) { ngf_destroy_render_target(rt); }
  return err;
}

void ngf_destroy_render_target(ngf_render_target target) {
  if (target) {
    ngfvk_frame_resources* res = &CURRENT_CONTEXT->frame_res[CURRENT_CONTEXT->frame_id];
    if (!target->is_default) {
      if (target->frame_buffer != VK_NULL_HANDLE) {
        NGFI_DARRAY_APPEND(res->retire_framebuffers, target->frame_buffer);
      }
    }
    if (target->compat_render_pass != VK_NULL_HANDLE) {
      NGFI_DARRAY_APPEND(res->retire_render_passes, target->compat_render_pass);
    }
    if (target->attachment_image_views) {
      for (size_t i = 0; i < target->nattachments; ++i) {
        NGFI_DARRAY_APPEND(res->retire_image_views, target->attachment_image_views[i]);
      }
      NGFI_FREEN(target->attachment_image_views, target->nattachments);
    }
    NGFI_FREEN(target->attachment_descs, target->nattachments);
    NGFI_FREEN(target->attachment_compat_pass_descs, target->nattachments);
    NGFI_FREE(target);

    // clear out the entire renderpass cache to make sure the entries associated
    // with this target don't stick around.
    // TODO: clear out all caches across all contexts.
    ngfvk_reset_renderpass_cache(CURRENT_CONTEXT);
  }
}

void ngf_cmd_dispatch(
    ngf_compute_encoder enc,
    uint32_t            x_threadgroups,
    uint32_t            y_threadgroups,
    uint32_t            z_threadgroups) {
  ngf_cmd_buffer cmd_buf = NGFVK_ENC2CMDBUF(enc);
  // Allocate and write descriptor sets.
  ngfvk_execute_pending_binds(cmd_buf);

  vkCmdDispatch(cmd_buf->vk_cmd_buffer, x_threadgroups, y_threadgroups, z_threadgroups);
}

void ngf_cmd_draw(
    ngf_render_encoder enc,
    bool               indexed,
    uint32_t           first_element,
    uint32_t           nelements,
    uint32_t           ninstances) {
  ngf_cmd_buffer cmd_buf = NGFVK_ENC2CMDBUF(enc);

  // Allocate and write descriptor sets.
  ngfvk_execute_pending_binds(cmd_buf);

  // With all resources bound, we may perform the draw operation.
  if (indexed) {
    vkCmdDrawIndexed(cmd_buf->vk_cmd_buffer, nelements, ninstances, first_element, 0u, 0u);
  } else {
    vkCmdDraw(cmd_buf->vk_cmd_buffer, nelements, ninstances, first_element, 0u);
  }
}

void ngf_cmd_bind_gfx_pipeline(ngf_render_encoder enc, const ngf_graphics_pipeline pipeline) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);

  // If we had a pipeline bound for which there have been resources bound, but no draw call
  // executed, commit those resources to actual descriptor sets and bind them so that the next
  // pipeline is able to "see" those resources, provided that it's compatible.
  if (buf->active_gfx_pipe && buf->pending_bind_ops.size > 0u) { ngfvk_execute_pending_binds(buf); }

  buf->active_gfx_pipe = pipeline;
  vkCmdBindPipeline(
      buf->vk_cmd_buffer,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      pipeline->generic_pipeline.vk_pipeline);
}

void ngf_cmd_bind_resources(
    ngf_render_encoder          enc,
    const ngf_resource_bind_op* bind_operations,
    uint32_t                    nbind_operations) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  ngfvk_cmd_bind_resources(buf, bind_operations, nbind_operations);
}

void ngf_cmd_bind_compute_resources(
    ngf_compute_encoder         enc,
    const ngf_resource_bind_op* bind_operations,
    uint32_t                    nbind_operations) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  ngfvk_cmd_bind_resources(buf, bind_operations, nbind_operations);
}

void ngf_cmd_bind_compute_pipeline(ngf_compute_encoder enc, const ngf_compute_pipeline pipeline) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  if (buf->active_compute_pipe && buf->pending_bind_ops.size > 0u) {
    ngfvk_execute_pending_binds(buf);
  }

  buf->active_compute_pipe = pipeline;
  vkCmdBindPipeline(
      buf->vk_cmd_buffer,
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeline->generic_pipeline.vk_pipeline);
}

void ngf_cmd_viewport(ngf_render_encoder enc, const ngf_irect2d* r) {
  ngf_cmd_buffer   buf      = NGFVK_ENC2CMDBUF(enc);
  const VkViewport viewport = {
      .x        = (float)r->x,
      .y        = (float)r->y,
      .width    = NGFI_MAX(1, (float)r->width),
      .height   = NGFI_MAX(1, (float)r->height),
      .minDepth = 0.0f,
      .maxDepth = 1.0f};
  vkCmdSetViewport(buf->vk_cmd_buffer, 0u, 1u, &viewport);
}

void ngf_cmd_scissor(ngf_render_encoder enc, const ngf_irect2d* r) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  assert(buf->active_rt);
  const VkRect2D scissor_rect = {.offset = {r->x, r->y}, .extent = {r->width, r->height}};
  vkCmdSetScissor(buf->vk_cmd_buffer, 0u, 1u, &scissor_rect);
}

void ngf_cmd_stencil_reference(ngf_render_encoder enc, uint32_t front, uint32_t back) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  vkCmdSetStencilReference(buf->vk_cmd_buffer, VK_STENCIL_FACE_FRONT_BIT, front);
  vkCmdSetStencilReference(buf->vk_cmd_buffer, VK_STENCIL_FACE_BACK_BIT, back);
}

void ngf_cmd_stencil_compare_mask(ngf_render_encoder enc, uint32_t front, uint32_t back) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  vkCmdSetStencilCompareMask(buf->vk_cmd_buffer, VK_STENCIL_FACE_FRONT_BIT, front);
  vkCmdSetStencilCompareMask(buf->vk_cmd_buffer, VK_STENCIL_FACE_BACK_BIT, back);
}

void ngf_cmd_stencil_write_mask(ngf_render_encoder enc, uint32_t front, uint32_t back) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  vkCmdSetStencilWriteMask(buf->vk_cmd_buffer, VK_STENCIL_FACE_FRONT_BIT, front);
  vkCmdSetStencilWriteMask(buf->vk_cmd_buffer, VK_STENCIL_FACE_BACK_BIT, back);
}

void ngf_cmd_bind_attrib_buffer(
    ngf_render_encoder enc,
    const ngf_buffer   abuf,
    uint32_t           binding,
    uint32_t           offset) {
  ngf_cmd_buffer buf      = NGFVK_ENC2CMDBUF(enc);
  VkDeviceSize   vkoffset = offset;
  vkCmdBindVertexBuffers(
      buf->vk_cmd_buffer,
      binding,
      1,
      (VkBuffer*)&abuf->alloc.obj_handle,
      &vkoffset);
}

void ngf_cmd_bind_index_buffer(
    ngf_render_encoder enc,
    const ngf_buffer   ibuf,
    uint32_t           offset,
    ngf_type           index_type) {
  ngf_cmd_buffer    buf      = NGFVK_ENC2CMDBUF(enc);
  const VkIndexType idx_type = get_vk_index_type(index_type);
  assert(idx_type == VK_INDEX_TYPE_UINT16 || idx_type == VK_INDEX_TYPE_UINT32);
  vkCmdBindIndexBuffer(buf->vk_cmd_buffer, (VkBuffer)ibuf->alloc.obj_handle, offset, idx_type);
}

void ngf_cmd_copy_buffer(
    ngf_xfer_encoder enc,
    const ngf_buffer src,
    ngf_buffer       dst,
    size_t           size,
    size_t           src_offset,
    size_t           dst_offset) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  assert(buf);
  ngfvk_cmd_copy_buffer(
      buf->vk_cmd_buffer,
      (VkBuffer)src->alloc.obj_handle,
      (VkBuffer)dst->alloc.obj_handle,
      size,
      src_offset,
      dst_offset,
      get_vk_buffer_access_flags(dst),
      get_vk_buffer_pipeline_stage_flags(dst));
}

void ngf_cmd_write_image(
    ngf_xfer_encoder enc,
    const ngf_buffer src,
    size_t           src_offset,
    ngf_image_ref    dst,
    ngf_offset3d     offset,
    ngf_extent3d     extent,
    uint32_t         nlayers) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  assert(buf);
  const uint32_t dst_layer =
      dst.image->type == NGF_IMAGE_TYPE_CUBE ? 6u * dst.layer + dst.cubemap_face : dst.layer;
  const VkAccessFlags        image_access_flags = get_vk_image_access_flags(dst.image);
  const VkImageMemoryBarrier pre_xfer_barrier   = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = NULL,
        .srcAccessMask       = image_access_flags ^ VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = (VkImage)dst.image->alloc.obj_handle,
        .subresourceRange    = {
               .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
               .baseMipLevel   = dst.mip_level,
               .levelCount     = 1u,
               .baseArrayLayer = dst_layer,
               .layerCount     = nlayers}};
  vkCmdPipelineBarrier(
      buf->vk_cmd_buffer,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
          VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      0u,
      0u,
      NULL,
      0u,
      NULL,
      1u,
      &pre_xfer_barrier);
  const VkBufferImageCopy copy_op = {
      .bufferOffset      = src_offset,
      .bufferRowLength   = 0u,
      .bufferImageHeight = 0u,
      .imageSubresource =
          {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
           .mipLevel       = dst.mip_level,
           .baseArrayLayer = dst_layer,
           .layerCount     = nlayers},
      .imageOffset = {.x = offset.x, .y = offset.y, .z = offset.z},
      .imageExtent = {.width = extent.width, .height = extent.height, .depth = extent.depth}};
  vkCmdCopyBufferToImage(
      buf->vk_cmd_buffer,
      (VkBuffer)src->alloc.obj_handle,
      (VkImage)dst.image->alloc.obj_handle,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1u,
      &copy_op);
  const VkImageLayout        target_layout     = (dst.image->usage_flags & NGF_IMAGE_USAGE_STORAGE)
                                                     ? VK_IMAGE_LAYOUT_GENERAL
                                                     : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  const VkImageMemoryBarrier post_xfer_barrier = {
      .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext               = NULL,
      .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask       = image_access_flags ^ VK_ACCESS_TRANSFER_WRITE_BIT,
      .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .newLayout           = target_layout,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image               = (VkImage)dst.image->alloc.obj_handle,
      .subresourceRange    = {
             .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
             .baseMipLevel   = dst.mip_level,
             .levelCount     = 1u,
             .baseArrayLayer = dst_layer,
             .layerCount     = nlayers}};
  vkCmdPipelineBarrier(
      buf->vk_cmd_buffer,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
          VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
      0u,
      0u,
      NULL,
      0u,
      NULL,
      1u,
      &post_xfer_barrier);
}

void ngf_cmd_copy_image_to_buffer(
    ngf_xfer_encoder enc,
    const ngf_image_ref src,
    ngf_offset3d src_offset,
    ngf_extent3d src_extent,
    uint32_t nlayers,
    ngf_buffer dst,
    size_t dst_offset) 
{
    ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
    assert(buf);
    const uint32_t src_layer =
        src.image->type == NGF_IMAGE_TYPE_CUBE ? 6u * src.layer + src.cubemap_face : src.layer;
    const VkImageLayout        src_layout = (src.image->usage_flags & NGF_IMAGE_USAGE_STORAGE)
                                                   ? VK_IMAGE_LAYOUT_GENERAL
                                                   : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkAccessFlags        image_access_flags = get_vk_image_access_flags(src.image);
    const VkImageMemoryBarrier pre_xfer_barrier   = {
          .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext               = NULL,
          .srcAccessMask       = image_access_flags ^ VK_ACCESS_TRANSFER_READ_BIT,
          .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
          .oldLayout           = src_layout,
          .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .image               = (VkImage)src.image->alloc.obj_handle,
          .subresourceRange    = {
                 .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                 .baseMipLevel   = src.mip_level,
                 .levelCount     = 1u,
                 .baseArrayLayer = src_layer,
                 .layerCount     = nlayers}};

    vkCmdPipelineBarrier(
        buf->vk_cmd_buffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0u,
        0u,
        NULL,
        0u,
        NULL,
        1u,
        &pre_xfer_barrier);

    const VkBufferImageCopy copy_op = {
        .bufferOffset      = dst_offset,
        .bufferRowLength   = 0u,
        .bufferImageHeight = 0u,
        .imageSubresource =
            {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
             .mipLevel       = src.mip_level,
             .baseArrayLayer = src_layer,
             .layerCount     = nlayers},
        .imageOffset = {.x = src_offset.x, .y = src_offset.y, .z = src_offset.z},
        .imageExtent = {.width = src_extent.width, .height = src_extent.height, .depth = src_extent.depth}
    };

    vkCmdCopyImageToBuffer(
        buf->vk_cmd_buffer,
        (VkImage)src.image->alloc.obj_handle,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        (VkBuffer)dst->alloc.obj_handle,
        1u,
        &copy_op);

    const VkImageMemoryBarrier post_xfer_barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = NULL,
        .srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask       = image_access_flags ^ VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout           = src_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = (VkImage)src.image->alloc.obj_handle,
        .subresourceRange    = {
               .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
               .baseMipLevel   = src.mip_level,
               .levelCount     = 1u,
               .baseArrayLayer = src_layer,
               .layerCount     = nlayers}};

    vkCmdPipelineBarrier(
        buf->vk_cmd_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0u,
        0u,
        NULL,
        0u,
        NULL,
        1u,
        &post_xfer_barrier);
}

ngf_error ngf_cmd_generate_mipmaps(ngf_xfer_encoder xfenc, ngf_image img) {
  if (!(img->usage_flags & NGF_IMAGE_USAGE_MIPMAP_GENERATION)) {
    NGFI_DIAG_ERROR("mipmap generation was requested for an image that was created without "
                    "the NGF_IMAGE_USAGE_MIPMAP_GENERATION usage flag.");
    return NGF_ERROR_INVALID_OPERATION;
  }

  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(xfenc);
  assert(buf);

  // TODO: ensure the pixel format is valid for mip generation.

  uint32_t src_w = img->extent.width, src_h = img->extent.height, src_d = img->extent.depth,
           dst_w = 0, dst_h = 0, dst_d = 0;
  const uint32_t nlayers = img->nlayers;

  for (uint32_t src_level = 0u; src_level < img->nlevels - 1; ++src_level) {
    const uint32_t dst_level                       = src_level + 1u;
    dst_w                                          = src_w > 1u ? (src_w >> 1u) : 1u;
    dst_h                                          = src_h > 1u ? (src_h >> 1u) : 1u;
    dst_d                                          = src_d > 1u ? (src_d >> 1u) : 1u;
    const VkImageMemoryBarrier pre_blit_barriers[] = {
        {.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .pNext               = NULL,
         .srcAccessMask       = VK_ACCESS_SHADER_READ_BIT,
         .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
         .oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
         .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image               = (VkImage)img->alloc.obj_handle,
         .subresourceRange =
             {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseMipLevel   = src_level,
              .levelCount     = 1u,
              .baseArrayLayer = 0u,
              .layerCount     = nlayers}},
        {.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .pNext               = NULL,
         .srcAccessMask       = VK_ACCESS_SHADER_READ_BIT,
         .dstAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
         .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
         .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image               = (VkImage)img->alloc.obj_handle,
         .subresourceRange    = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = dst_level,
                .levelCount     = 1u,
                .baseArrayLayer = 0u,
                .layerCount     = nlayers}}};
    vkCmdPipelineBarrier(
        buf->vk_cmd_buffer,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0u,
        0u,
        NULL,
        0u,
        NULL,
        2u,
        pre_blit_barriers);
    const VkImageBlit blit_region = {
        .srcSubresource =
            {.mipLevel       = src_level,
             .baseArrayLayer = 0u,
             .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
             .layerCount     = nlayers},
        .dstSubresource =
            {.mipLevel       = dst_level,
             .baseArrayLayer = 0u,
             .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
             .layerCount     = nlayers},
        .srcOffsets = {{0, 0, 0}, {(int32_t)src_w, (int32_t)src_h, (int32_t)src_d}},
        .dstOffsets = {{0, 0, 0}, {(int32_t)dst_w, (int32_t)dst_h, (int32_t)dst_d}}};
    vkCmdBlitImage(
        buf->vk_cmd_buffer,
        (VkImage)img->alloc.obj_handle,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        (VkImage)img->alloc.obj_handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &blit_region,
        VK_FILTER_LINEAR);
    const VkImageMemoryBarrier post_blit_barriers[] = {
        {.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .pNext               = NULL,
         .srcAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
         .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
         .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
         .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image               = (VkImage)img->alloc.obj_handle,
         .subresourceRange =
             {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
              .baseMipLevel   = src_level,
              .levelCount     = 1u,
              .baseArrayLayer = 0u,
              .layerCount     = nlayers}},
        {.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .pNext               = NULL,
         .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
         .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
         .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
         .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .image               = (VkImage)img->alloc.obj_handle,
         .subresourceRange    = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = dst_level,
                .levelCount     = 1u,
                .baseArrayLayer = 0u,
                .layerCount     = nlayers}}};
    vkCmdPipelineBarrier(
        buf->vk_cmd_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        0u,
        0u,
        NULL,
        0u,
        NULL,
        2u,
        post_blit_barriers);
    src_w = dst_w;
    src_h = dst_h;
    src_d = dst_d;
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_create_texel_buffer_view(
    const ngf_texel_buffer_view_info* info,
    ngf_texel_buffer_view*            result) {
  assert(info);
  assert(result);

  ngf_texel_buffer_view buf_view = NGFI_ALLOC(ngf_texel_buffer_view_t);
  *result                        = buf_view;
  if (buf_view == NULL) return NGF_ERROR_OUT_OF_MEM;

  const VkBufferViewCreateInfo vk_buf_view_ci = {
      .sType  = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
      .pNext  = NULL,
      .flags  = 0u,
      .offset = info->offset,
      .range  = info->size,
      .format = get_vk_image_format(info->texel_format),
      .buffer = (VkBuffer)info->buffer->alloc.obj_handle};
  const VkResult vk_result =
      vkCreateBufferView(_vk.device, &vk_buf_view_ci, NULL, &buf_view->vk_buf_view);
  if (vk_result != VK_SUCCESS) {
    NGFI_FREE(buf_view);
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }
  return NGF_ERROR_OK;
}

void ngf_destroy_texel_buffer_view(ngf_texel_buffer_view buf_view) {
  if (buf_view) {
    const uint32_t fi = CURRENT_CONTEXT->frame_id;
    NGFI_DARRAY_APPEND(CURRENT_CONTEXT->frame_res[fi].retire_buffer_views, buf_view->vk_buf_view);
    NGFI_FREE(buf_view);
  }
}

ngf_error ngf_create_buffer(const ngf_buffer_info* info, ngf_buffer* result) {
  assert(info);
  assert(result);
  if (info->buffer_usage == 0u) {
    NGFI_DIAG_ERROR("Buffer usage not specified.");
    return NGF_ERROR_INVALID_OPERATION;
  }

  ngf_buffer buf = NGFI_ALLOC(ngf_buffer_t);
  *result        = buf;
  if (buf == NULL) return NGF_ERROR_OUT_OF_MEM;

  const VkBufferUsageFlags    vk_usage_flags = get_vk_buffer_usage(info->buffer_usage);
  const VkMemoryPropertyFlags vk_mem_flags   = get_vk_memory_flags(info->storage_type);
  const bool     vk_mem_is_host_visible      = vk_mem_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  const uint32_t vma_usage_flags             = info->storage_type == NGF_BUFFER_STORAGE_PRIVATE
                                                   ? VMA_MEMORY_USAGE_GPU_ONLY
                                                   : VMA_MEMORY_USAGE_CPU_ONLY;
  ngf_error      err                         = NGF_ERROR_OK;
  ngfvk_alloc*   alloc                       = &buf->alloc;

  const VkBufferCreateInfo buf_vk_info = {
      .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext                 = NULL,
      .flags                 = 0u,
      .size                  = info->size,
      .usage                 = vk_usage_flags,
      .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices   = NULL};

  const VmaAllocationCreateInfo buf_alloc_info = {
      .flags          = vk_mem_is_host_visible ? VMA_ALLOCATION_CREATE_MAPPED_BIT : 0u,
      .usage          = vma_usage_flags,
      .requiredFlags  = vk_mem_flags,
      .preferredFlags = 0u,
      .memoryTypeBits = 0u,
      .pool           = VK_NULL_HANDLE,
      .pUserData      = NULL};

  VmaAllocationInfo alloc_info;
  alloc_info.pMappedData = NULL;
  VkResult vkresult      = vmaCreateBuffer(
      CURRENT_CONTEXT->allocator,
      &buf_vk_info,
      &buf_alloc_info,
      (VkBuffer*)&alloc->obj_handle,
      &alloc->vma_alloc,
      &alloc_info);
  alloc->parent_allocator = CURRENT_CONTEXT->allocator;
  alloc->mapped_data      = vk_mem_is_host_visible ? alloc_info.pMappedData : NULL;
  err                     = (vkresult == VK_SUCCESS) ? NGF_ERROR_OK : NGF_ERROR_INVALID_OPERATION;

  if (err != NGF_ERROR_OK) {
    NGFI_FREE(buf);
  } else {
    buf->size = info->size;
  }

  buf->storage_type = info->storage_type;
  buf->usage_flags  = info->buffer_usage;

  return err;
}

void ngf_destroy_buffer(ngf_buffer buffer) {
  if (buffer) {
    const uint32_t fi = CURRENT_CONTEXT->frame_id;
    NGFI_DARRAY_APPEND(CURRENT_CONTEXT->frame_res[fi].retire_buffers, buffer->alloc);
    NGFI_FREE(buffer);
  }
}

void* ngf_buffer_map_range(ngf_buffer buf, size_t offset, size_t size) {
  NGFI_IGNORE_VAR(size);
  buf->mapped_offset = offset;
  return (uint8_t*)buf->alloc.mapped_data + buf->mapped_offset;
}

void ngf_buffer_flush_range(ngf_buffer buf, size_t offset, size_t size) {
  vmaFlushAllocation(
      CURRENT_CONTEXT->allocator,
      buf->alloc.vma_alloc,
      buf->mapped_offset + offset,
      size);
}

void ngf_buffer_unmap(ngf_buffer buf) {
  // vk buffers are persistently mapped.
  NGFI_IGNORE_VAR(buf);
}

ngf_error ngf_create_image(const ngf_image_info* info, ngf_image* result) {
  assert(info);
  assert(result);

  const bool is_sampled_from  = info->usage_hint & NGF_IMAGE_USAGE_SAMPLE_FROM;
  const bool is_storage       = info->usage_hint & NGF_IMAGE_USAGE_STORAGE;
  const bool is_xfer_dst      = info->usage_hint & NGF_IMAGE_USAGE_XFER_DST;
  const bool is_xfer_src      = info->usage_hint & NGF_IMAGE_USAGE_XFER_SRC;
  const bool is_attachment    = info->usage_hint & NGF_IMAGE_USAGE_ATTACHMENT;
  const bool enable_auto_mips = info->usage_hint & NGF_IMAGE_USAGE_MIPMAP_GENERATION;
  const bool is_transient     = info->usage_hint & NGFVK_IMAGE_USAGE_TRANSIENT_ATTACHMENT;
  const bool is_depth_stencil = info->format == NGF_IMAGE_FORMAT_DEPTH16 ||
                                info->format == NGF_IMAGE_FORMAT_DEPTH32 ||
                                info->format == NGF_IMAGE_FORMAT_DEPTH24_STENCIL8;
  const bool is_depth_only =
      info->format == NGF_IMAGE_FORMAT_DEPTH32 || info->format == NGF_IMAGE_FORMAT_DEPTH16;

  const VkImageUsageFlagBits attachment_usage_bits =
      is_depth_stencil ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                       : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  const VkImageUsageFlagBits usage_flags =
      (is_sampled_from ? VK_IMAGE_USAGE_SAMPLED_BIT : 0u) |
      (is_storage ? VK_IMAGE_USAGE_STORAGE_BIT : 0u) |
      (is_attachment ? attachment_usage_bits : 0u) |
      (is_transient ? VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : 0) |
      (is_xfer_dst ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0u) |
      (is_xfer_src ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0u) |
      (enable_auto_mips ? (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT) : 0u);

  ngf_error err = NGF_ERROR_OK;
  *result       = NGFI_ALLOC(ngf_image_t);
  ngf_image img = *result;
  if (img == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_create_image_cleanup;
  }

  img->vkformat      = get_vk_image_format(info->format);
  img->extent        = info->extent;
  img->usage_flags   = info->usage_hint;
  img->extent.depth  = NGFI_MAX(1, img->extent.depth);
  img->extent.width  = NGFI_MAX(1, img->extent.width);
  img->extent.height = NGFI_MAX(1, img->extent.height);

  const bool              is_cubemap    = info->type == NGF_IMAGE_TYPE_CUBE;
  const VkImageCreateInfo vk_image_info = {
      .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext     = NULL,
      .flags     = is_cubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u,
      .imageType = get_vk_image_type(info->type),
      .extent =
          {.width = info->extent.width, .height = info->extent.height, .depth = info->extent.depth},
      .format                = img->vkformat,
      .mipLevels             = info->nmips,
      .arrayLayers           = info->nlayers * (!is_cubemap ? 1u : 6u),
      .samples               = get_vk_sample_count(info->sample_count),
      .usage                 = usage_flags,
      .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .tiling                = VK_IMAGE_TILING_OPTIMAL,
      .pQueueFamilyIndices   = NULL,
      .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED};

  VmaAllocationCreateInfo vma_alloc_info = {
      .flags          = 0u,
      .usage          = VMA_MEMORY_USAGE_GPU_ONLY,
      .requiredFlags  = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      .preferredFlags = 0u,
      .memoryTypeBits = 0u,
      .pool           = VK_NULL_HANDLE,
      .pUserData      = NULL};

  const VkResult create_image_vkerr = vmaCreateImage(
      CURRENT_CONTEXT->allocator,
      &vk_image_info,
      &vma_alloc_info,
      (VkImage*)&img->alloc.obj_handle,
      &img->alloc.vma_alloc,
      NULL);
  img->alloc.parent_allocator = CURRENT_CONTEXT->allocator;
  img->type                   = info->type;

  if (create_image_vkerr != VK_SUCCESS) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngf_create_image_cleanup;
  }
  err = ngfvk_create_vk_image_view(
      (VkImage)img->alloc.obj_handle,
      get_vk_image_view_type(info->type, info->nlayers),
      vk_image_info.format,
      vk_image_info.mipLevels,
      vk_image_info.arrayLayers,
      &img->vkview);

  if (err != NGF_ERROR_OK) { goto ngf_create_image_cleanup; }

  const VkImageMemoryBarrier barrier = {
      .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext         = NULL,
      .srcAccessMask = 0u,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                       VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT |
                       VK_ACCESS_MEMORY_WRITE_BIT,
      .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout           = is_storage
                                 ? VK_IMAGE_LAYOUT_GENERAL
                                 : (is_sampled_from
                                        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                        : (is_depth_stencil ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                           : is_attachment ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                                           : VK_IMAGE_LAYOUT_GENERAL)),
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image               = (VkImage)img->alloc.obj_handle,
      .subresourceRange    = {
             .aspectMask     = is_depth_stencil ? (VK_IMAGE_ASPECT_DEPTH_BIT |
                                            (is_depth_only ? 0 : VK_IMAGE_ASPECT_STENCIL_BIT))
                                                : VK_IMAGE_ASPECT_COLOR_BIT,
             .baseMipLevel   = 0u,
             .levelCount     = vk_image_info.mipLevels,
             .baseArrayLayer = 0u,
             .layerCount     = vk_image_info.arrayLayers}};

  img->nlevels = info->nmips;
  img->nlayers = vk_image_info.arrayLayers;

  pthread_mutex_lock(&NGFVK_PENDING_IMG_BARRIER_QUEUE.lock);
  NGFI_DARRAY_APPEND(NGFVK_PENDING_IMG_BARRIER_QUEUE.barriers, barrier);
  pthread_mutex_unlock(&NGFVK_PENDING_IMG_BARRIER_QUEUE.lock);

ngf_create_image_cleanup:
  if (err != NGF_ERROR_OK) { ngf_destroy_image(img); }
  return err;
}

void ngf_destroy_image(ngf_image img) {
  if (img != NULL) {
    if (img->alloc.obj_handle != (uintptr_t)VK_NULL_HANDLE) {
      const uint32_t fi = CURRENT_CONTEXT->frame_id;
      NGFI_DARRAY_APPEND(CURRENT_CONTEXT->frame_res[fi].retire_images, img->alloc);
      NGFI_DARRAY_APPEND(CURRENT_CONTEXT->frame_res[fi].retire_image_views, img->vkview);
      NGFI_FREE(img);
    }
  }
}

ngf_error ngf_create_sampler(const ngf_sampler_info* info, ngf_sampler* result) {
  assert(info);
  assert(result);
  ngf_sampler sampler = NGFI_ALLOC(ngf_sampler_t);
  *result             = sampler;

  if (sampler == NULL) return NGF_ERROR_OUT_OF_MEM;

  const VkSamplerCreateInfo vk_sampler_info = {
      .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .pNext                   = NULL,
      .flags                   = 0u,
      .magFilter               = get_vk_filter(info->mag_filter),
      .minFilter               = get_vk_filter(info->min_filter),
      .mipmapMode              = get_vk_mipmode(info->mip_filter),
      .addressModeU            = get_vk_address_mode(info->wrap_u),
      .addressModeV            = get_vk_address_mode(info->wrap_v),
      .addressModeW            = get_vk_address_mode(info->wrap_w),
      .mipLodBias              = info->lod_bias,
      .anisotropyEnable        = info->enable_anisotropy ? VK_TRUE : VK_FALSE,
      .maxAnisotropy           = info->max_anisotropy,
      .compareEnable           = VK_FALSE,
      .compareOp               = VK_COMPARE_OP_ALWAYS,
      .minLod                  = info->lod_min,
      .maxLod                  = info->lod_max,
      .borderColor             = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK,
      .unnormalizedCoordinates = VK_FALSE};
  const VkResult vk_sampler_create_result =
      vkCreateSampler(_vk.device, &vk_sampler_info, NULL, &sampler->vksampler);
  if (vk_sampler_create_result != VK_SUCCESS) {
    NGFI_FREE(sampler);
    return NGF_ERROR_INVALID_OPERATION;
  } else {
    return NGF_ERROR_OK;
  }
}

void ngf_destroy_sampler(ngf_sampler sampler) {
  if (sampler) {
    const uint32_t fi = CURRENT_CONTEXT->frame_id;
    NGFI_DARRAY_APPEND(CURRENT_CONTEXT->frame_res[fi].retire_samplers, sampler->vksampler);
    NGFI_FREE(sampler);
  }
}

void ngf_finish(void) {
  vkDeviceWaitIdle(_vk.device);
}

void ngf_renderdoc_capture_next_frame() {
  if (_renderdoc.api) _renderdoc.is_capturing_next_frame = true;
}

void ngf_renderdoc_capture_begin() {
  if (_renderdoc.api && !_renderdoc.api->IsFrameCapturing()) {
    _renderdoc.api->StartFrameCapture(
        RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(_vk.instance),
        (RENDERDOC_WindowHandle)CURRENT_CONTEXT->swapchain_info.native_handle);
  }
}

void ngf_renderdoc_capture_end() {
  if (_renderdoc.api && _renderdoc.api->IsFrameCapturing()) {
    _renderdoc.api->EndFrameCapture(
        RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(_vk.instance),
        (RENDERDOC_WindowHandle)CURRENT_CONTEXT->swapchain_info.native_handle);
  }
}

#pragma endregion

#if defined(NGFVK_TEST_MODE)
#include "../tests/vk-backend-tests.c"
#endif
