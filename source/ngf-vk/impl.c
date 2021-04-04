/**
 * Copyright (c) 2021 nicegraf contributors
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
#include "ngf-common/block_alloc.h"
#include "ngf-common/cmdbuf_state.h"
#include "ngf-common/dynamic_array.h"
#include "ngf-common/frame_token.h"
#include "ngf-common/macros.h"
#include "ngf-common/stack_alloc.h"
#include "nicegraf.h"
#include "vk_10.h"

#include <assert.h>
#include <string.h>
#include <vk_mem_alloc.h>

#pragma region constants

#define NGFVK_INVALID_IDX                      (~0u)
#define NGFVK_MAX_PHYS_DEV                     (64u)  // 64 GPUs oughta be enough for everybody.
#define NGFVK_BIND_OP_CHUNK_SIZE               (10u)
#define NGFVK_MAX_COLOR_ATTACHMENTS            16u
#define NGFVK_IMAGE_USAGE_TRANSIENT_ATTACHMENT (1u << 31u)

#pragma endregion

#pragma region internal_struct_definitions
// Singleton for holding vulkan instance, device and
// queue handles.
// This is shared by all contexts.
struct {
  VkInstance       instance;
  VkPhysicalDevice phys_dev;
  VkDevice         device;
  VkQueue          gfx_queue;
  VkQueue          present_queue;
  uint32_t         gfx_family_idx;
  uint32_t         present_family_idx;
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

// A "command bundle" consists of a command buffer, a semaphore that is
// signaled on its completion and a reference to the command buffer's parent
// pool.
typedef struct {
  VkCommandBuffer vkcmdbuf;
  VkSemaphore     vksem;
  VkCommandPool   vkpool;
} ngfvk_cmd_bundle;

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

typedef struct {
  VmaAllocator  parent_allocator;
  uint64_t      obj_handle;
  VmaAllocation vma_alloc;
} ngfvk_alloc;

typedef struct {
  ngfvk_alloc alloc;
  size_t      size;
  size_t      mapped_offset;
} ngfvk_buffer;

// Vulkan resources associated with a given frame.
typedef struct ngfvk_frame_resources {
  // Command buffers submitted to the graphics queue, their
  // associated semaphores and parent command pool.
  NGFI_DARRAY_OF(VkCommandBuffer) cmd_bufs;
  NGFI_DARRAY_OF(VkCommandPool) cmd_pools;
  NGFI_DARRAY_OF(VkSemaphore) cmd_buf_sems;

  // Resources that should be disposed of at some point after this
  // frame's completion.
  NGFI_DARRAY_OF(VkPipeline) retire_pipelines;
  NGFI_DARRAY_OF(VkPipelineLayout) retire_pipeline_layouts;
  NGFI_DARRAY_OF(VkDescriptorSetLayout) retire_dset_layouts;
  NGFI_DARRAY_OF(VkFramebuffer) retire_framebuffers;
  NGFI_DARRAY_OF(VkRenderPass) retire_render_passes;
  NGFI_DARRAY_OF(VkSampler) retire_samplers;
  NGFI_DARRAY_OF(VkImageView) retire_image_views;
  NGFI_DARRAY_OF(ngfvk_alloc) retire_images;
  NGFI_DARRAY_OF(ngfvk_alloc) retire_buffers;
  NGFI_DARRAY_OF(ngfvk_desc_pools_list*) reset_desc_pools_lists;

  // Fences that will be signaled at the end of the frame.
  // Theoretically there could be multiple if there are multiple queue submissions
  // per frame, but currently we limit ourselves to 1 submission.
  VkFence fences[1];

  // Number of fences to wait on to complete all submissions related to this
  // frame.
  uint32_t nwait_fences;
} ngfvk_frame_resources;

// Vulkan needs to render-to-texture upside-down to maintain a semblance of
// a consistent coordinate system across different backends. This requires to
// flip polygon winding order as well. Since winding/culling are baked into
// pipeline state, we need two flavors for every pipeline...
// TODO: use VK_EXT_extended_dynamic_state to set polygon winding dynamically
// when that extension is more widely supported.
typedef enum {
  NGFVK_PIPELINE_FLAVOR_VANILLA = 0u,
  NGFVK_PIPELINE_FLAVOR_RENDER_TO_TEXTURE,
  NGFVK_PIPELINE_FLAVOR_COUNT
} ngfvk_pipeline_flavor;

typedef struct {
  uint16_t       ctx_id;
  uint8_t        num_pools;
  VkCommandPool* pools;
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

#pragma endregion

#pragma region external_struct_definitions

typedef struct ngf_cmd_buffer_t {
  ngf_frame_token          parent_frame;   // < The frame this cmd buffer is associated with.
  ngfi_cmd_buffer_state    state;          // < State of the cmd buffer (i.e. new/recording/etc.)
  ngfvk_cmd_bundle         active_bundle;  // < The current bundle.
  ngf_graphics_pipeline    active_pipe;    // < The bound pipeline.
  ngf_render_target        active_rt;      // < Active render target.
  bool                     renderpass_active;  // < Has an active renderpass.
  ngfvk_bind_op_chunk_list pending_bind_ops;   // < Resource binds that need to be performed
                                               //   before the next draw.
  ngfvk_desc_pools_list*
      desc_pools_list;  // < List of descriptor pools used in this buffer's frame.
} ngf_cmd_buffer_t;

typedef struct ngf_attrib_buffer_t {
  ngfvk_buffer data;
} ngf_attrib_buffer_t;

typedef struct ngf_index_buffer_t {
  ngfvk_buffer data;
} ngf_index_buffer_t;

typedef struct ngf_uniform_buffer_t {
  ngfvk_buffer data;
} ngf_uniform_buffer_t;

typedef struct ngf_pixel_buffer_t {
  ngfvk_buffer data;
} ngf_pixel_buffer_t;

typedef struct ngf_sampler_t {
  VkSampler vksampler;
} ngf_sampler_t;

typedef struct ngf_image_t {
  ngfvk_alloc    alloc;
  ngf_image_type type;
  VkImageView    vkview;
  VkFormat       vkformat;
  ngf_extent3d   extent;
  uint32_t       usage_flags;
} ngf_image_t;

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
  ngf_attachment_description  default_attachment_descs[2];
  ngf_attachment_descriptions default_attachment_descriptions_list;
  ngf_render_target           default_render_target;
  NGFI_DARRAY_OF(ngfvk_command_superpool) command_superpools;
  NGFI_DARRAY_OF(ngfvk_desc_superpool) desc_superpools;
  NGFI_DARRAY_OF(ngfvk_renderpass_cache_entry) renderpass_cache;
} ngf_context_t;

typedef struct ngf_shader_stage_t {
  VkShaderModule        vk_module;
  VkShaderStageFlagBits vk_stage_bits;
  char*                 entry_point_name;
} ngf_shader_stage_t;

typedef struct ngf_graphics_pipeline_t {
  VkPipeline vk_pipeline_flavors[NGFVK_PIPELINE_FLAVOR_COUNT];
  NGFI_DARRAY_OF(ngfvk_desc_set_layout) descriptor_set_layouts;
  VkPipelineLayout vk_pipeline_layout;
  VkRenderPass     compatible_render_pass;
} ngf_graphics_pipeline_t;

typedef struct ngf_render_target_t {
  VkFramebuffer               frame_buffer;
  VkRenderPass                compat_render_pass;
  uint32_t                    nattachments;
  ngf_attachment_description* attachment_descs;
  ngfvk_attachment_pass_desc* attachment_pass_descs;
  bool                        is_default;
  uint32_t                    width;
  uint32_t                    height;
} ngf_render_target_t;

#pragma endregion

#pragma region global_vars
NGFI_THREADLOCAL ngf_context CURRENT_CONTEXT = NULL;

static struct {
  pthread_mutex_t lock;
  NGFI_DARRAY_OF(VkImageMemoryBarrier) barriers;
} NGFVK_PENDING_IMG_BARRIER_QUEUE;

#pragma endregion

#pragma region vk_enum_maps

static VkFilter get_vk_filter(ngf_sampler_filter filter) {
  static const VkFilter vkfilters[NGF_FILTER_COUNT] = {VK_FILTER_NEAREST, VK_FILTER_LINEAR};
  return vkfilters[filter];
}

static VkSamplerAddressMode get_vk_address_mode(ngf_sampler_wrap_mode mode) {
  static const VkSamplerAddressMode vkmodes[NGF_WRAP_MODE_COUNT] = {
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
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

static VkSampleCountFlagBits get_vk_sample_count(uint32_t sample_count) {
  switch (sample_count) {
  case 0u:
  case 1u:
    return VK_SAMPLE_COUNT_1_BIT;
  case 2u:
    return VK_SAMPLE_COUNT_2_BIT;
  case 4u:
    return VK_SAMPLE_COUNT_4_BIT;
  case 8u:
    return VK_SAMPLE_COUNT_8_BIT;
  case 16u:
    return VK_SAMPLE_COUNT_16_BIT;
  case 32u:
    return VK_SAMPLE_COUNT_32_BIT;
  case 64u:
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
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER};
  return types[type];
}

static VkShaderStageFlags get_vk_stage_flags(uint32_t flags) {
  VkShaderStageFlags result = 0x0u;
  if (flags & NGF_DESCRIPTOR_FRAGMENT_STAGE_BIT) result |= VK_SHADER_STAGE_FRAGMENT_BIT;
  if (flags & NGF_DESCRIPTOR_VERTEX_STAGE_BIT) result |= VK_SHADER_STAGE_VERTEX_BIT;
  return result;
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
      VK_FORMAT_R32_SFLOAT,
      VK_FORMAT_R32G32_SFLOAT,
      VK_FORMAT_R32G32B32_SFLOAT,
      VK_FORMAT_R32G32B32A32_SFLOAT,
      VK_FORMAT_R16_SFLOAT,
      VK_FORMAT_R16G16_SFLOAT,
      VK_FORMAT_R16G16B16_SFLOAT,
      VK_FORMAT_R16G16B16A16_SFLOAT,
      VK_FORMAT_B10G11R11_UFLOAT_PACK32,
      VK_FORMAT_R16_UNORM,
      VK_FORMAT_R16_SNORM,
      VK_FORMAT_R16_UINT,
      VK_FORMAT_R16_SINT,
      VK_FORMAT_R16G16_UINT,
      VK_FORMAT_R16G16B16_UINT,
      VK_FORMAT_R16G16B16A16_UINT,
      VK_FORMAT_R32_UINT,
      VK_FORMAT_R32G32_UINT,
      VK_FORMAT_R32G32B32_UINT,
      VK_FORMAT_R32G32B32A32_UINT,
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

static VkPrimitiveTopology get_vk_primitive_type(ngf_primitive_type p) {
  static const VkPrimitiveTopology topos[NGF_PRIMITIVE_TYPE_COUNT] = {
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
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

static VkVertexInputRate get_vk_input_rate(ngf_input_rate r) {
  static const VkVertexInputRate rates[NGF_INPUT_RATE_COUNT] = {
      VK_VERTEX_INPUT_RATE_VERTEX,
      VK_VERTEX_INPUT_RATE_INSTANCE};
  return rates[r];
}

static VkShaderStageFlagBits get_vk_shader_stage(ngf_stage_type s) {
  static const VkShaderStageFlagBits stages[NGF_STAGE_COUNT] = {
      VK_SHADER_STAGE_VERTEX_BIT,
      VK_SHADER_STAGE_FRAGMENT_BIT};
  return stages[s];
}

static VkBufferUsageFlags get_vk_buffer_usage(uint32_t usage) {
  VkBufferUsageFlags flags = 0u;
  if (usage & NGF_BUFFER_USAGE_XFER_DST) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  if (usage & NGF_BUFFER_USAGE_XFER_SRC) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
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

#pragma endregion

#pragma region internal_funcs

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
  const bool is_depth =
      image_format == VK_FORMAT_D16_UNORM || image_format == VK_FORMAT_D16_UNORM_S8_UINT ||
      image_format == VK_FORMAT_D24_UNORM_S8_UINT || image_format == VK_FORMAT_D32_SFLOAT ||
      image_format == VK_FORMAT_D32_SFLOAT_S8_UINT;
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
          .aspectMask     = is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
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
        .type   = NGF_IMAGE_TYPE_IMAGE_2D,
        .extent = {.width = swapchain_info->width, .height = swapchain_info->height, .depth = 1u},
        .nmips  = 1u,
        .format = swapchain_info->color_format,
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
        .type   = NGF_IMAGE_TYPE_IMAGE_2D,
        .extent = {.width = swapchain_info->width, .height = swapchain_info->height, .depth = 1u},
        .nmips  = 1u,
        .sample_count = (ngf_sample_count)get_vk_sample_count(swapchain_info->sample_count),
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
  const uint32_t nattachments =
      CURRENT_CONTEXT->default_render_target->nattachments + (have_resolve_attachment ? 1u : 0u);
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
  if (frame_res->nwait_fences > 0 && frame_res->nwait_fences > 0u) {
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

  NGFI_DARRAY_FOREACH(frame_res->cmd_bufs, s) {
    vkDestroySemaphore(_vk.device, NGFI_DARRAY_AT(frame_res->cmd_buf_sems, s), NULL);
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

  NGFI_DARRAY_CLEAR(frame_res->cmd_bufs);
  NGFI_DARRAY_CLEAR(frame_res->cmd_pools);
  NGFI_DARRAY_CLEAR(frame_res->cmd_buf_sems);
  NGFI_DARRAY_CLEAR(frame_res->retire_pipelines);
  NGFI_DARRAY_CLEAR(frame_res->retire_dset_layouts);
  NGFI_DARRAY_CLEAR(frame_res->retire_framebuffers);
  NGFI_DARRAY_CLEAR(frame_res->retire_render_passes);
  NGFI_DARRAY_CLEAR(frame_res->retire_samplers);
  NGFI_DARRAY_CLEAR(frame_res->retire_image_views);
  NGFI_DARRAY_CLEAR(frame_res->retire_images);
  NGFI_DARRAY_CLEAR(frame_res->retire_pipeline_layouts);
  NGFI_DARRAY_CLEAR(frame_res->retire_buffers);
  NGFI_DARRAY_CLEAR(frame_res->reset_desc_pools_lists);
}

static ngf_error ngfvk_cmd_bundle_create(VkCommandPool pool, ngfvk_cmd_bundle* bundle) {
  VkCommandBufferAllocateInfo vk_cmdbuf_info = {
      .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .pNext              = NULL,
      .commandPool        = pool,
      .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1u};
  VkResult vk_err = vkAllocateCommandBuffers(_vk.device, &vk_cmdbuf_info, &bundle->vkcmdbuf);
  if (vk_err != VK_SUCCESS) {
    NGFI_DIAG_ERROR("Failed to allocate cmd buffer, VK error: %d", vk_err);
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }
  bundle->vkpool                         = pool;
  VkCommandBufferBeginInfo cmd_buf_begin = {
      .sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .pNext            = NULL,
      .flags            = 0,
      .pInheritanceInfo = NULL};
  vkBeginCommandBuffer(bundle->vkcmdbuf, &cmd_buf_begin);

  // Create semaphore.
  VkSemaphoreCreateInfo vk_sem_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0u};
  vk_err = vkCreateSemaphore(_vk.device, &vk_sem_info, NULL, &bundle->vksem);
  if (vk_err != VK_SUCCESS) { return NGF_ERROR_OBJECT_CREATION_FAILED; }
  return NGF_ERROR_OK;
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

static ngf_error ngfvk_encoder_end(ngf_cmd_buffer cmd_buf) {
  ngfvk_cleanup_pending_binds(cmd_buf);
  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_AWAITING_SUBMIT);
  return NGF_ERROR_OK;
}

static void ngfvk_submit_commands(
    VkQueue                     queue,
    const VkCommandBuffer*      cmd_bufs,
    uint32_t                    ncmd_bufs,
    const VkPipelineStageFlags* wait_stage_flags,
    const VkSemaphore*          wait_sems,
    uint32_t                    nwait_sems,
    const VkSemaphore*          signal_sems,
    uint32_t                    nsignal_sems,
    VkFence                     fence) {
  const VkSubmitInfo submit_info = {
      .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .pNext                = NULL,
      .waitSemaphoreCount   = nwait_sems,
      .pWaitSemaphores      = wait_sems,
      .pWaitDstStageMask    = wait_stage_flags,
      .commandBufferCount   = ncmd_bufs,
      .pCommandBuffers      = cmd_bufs,
      .signalSemaphoreCount = nsignal_sems,
      .pSignalSemaphores    = signal_sems};
  vkQueueSubmit(queue, 1, &submit_info, fence);
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
  NGFI_IGNORE_VAR(usage_access_mask)
  NGFI_IGNORE_VAR(usage_stage_mask);
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

static void* ngfvk_map_buffer(ngfvk_buffer* buf, size_t offset) {
  void*    result   = NULL;
  VkResult vkresult = vmaMapMemory(CURRENT_CONTEXT->allocator, buf->alloc.vma_alloc, &result);
  if (vkresult == VK_SUCCESS) { buf->mapped_offset = offset; }
  return vkresult == VK_SUCCESS ? ((uint8_t*)result + offset) : NULL;
}

static void ngfvk_flush_buffer(ngfvk_buffer* buf, size_t offset, size_t size) {
  vmaFlushAllocation(
      CURRENT_CONTEXT->allocator,
      buf->alloc.vma_alloc,
      buf->mapped_offset + offset,
      size);
}

static void ngfvk_unmap_buffer(ngfvk_buffer* buf) {
  vmaUnmapMemory(CURRENT_CONTEXT->allocator, buf->alloc.vma_alloc);
}

static void ngfvk_buffer_retire(ngfvk_buffer buf) {
  const uint32_t fi = CURRENT_CONTEXT->frame_id;
  NGFI_DARRAY_APPEND(CURRENT_CONTEXT->frame_res[fi].retire_buffers, buf.alloc);
}

static ngf_error ngfvk_create_command_superpool(
    ngfvk_command_superpool* superpool,
    uint8_t                  npools,
    uint16_t                 ctx_id) {
  superpool->ctx_id    = ctx_id;
  superpool->num_pools = npools;
  superpool->pools     = NGFI_ALLOCN(VkCommandPool, npools);
  if (superpool->pools == NULL) { return NGF_ERROR_OUT_OF_MEM; }

  for (size_t i = 0; i < npools; ++i) {
    const VkCommandPoolCreateInfo command_pool_ci = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext            = NULL,
        .queueFamilyIndex = _vk.gfx_family_idx,
        .flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT};
    if (vkCreateCommandPool(_vk.device, &command_pool_ci, NULL, &superpool->pools[i]) !=
        VK_SUCCESS) {
      return NGF_ERROR_OBJECT_CREATION_FAILED;
    }
  }

  return NGF_ERROR_OK;
}

static void ngfvk_destroy_command_superpool(ngfvk_command_superpool* superpool) {
  for (size_t i = 0; i < superpool->num_pools; ++i) {
    vkDestroyCommandPool(_vk.device, superpool->pools[i], NULL);
  }
  NGFI_FREEN(superpool->pools, superpool->num_pools);
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
    ngfvk_create_command_superpool(
        NGFI_DARRAY_BACKPTR(CURRENT_CONTEXT->command_superpools),
        nframes,
        ctx_id);
    result = NGFI_DARRAY_BACKPTR(CURRENT_CONTEXT->command_superpools);
  }

  return result;
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

void ngfvk_execute_pending_binds(ngf_cmd_buffer cmd_buf) {
  // Binding resources requires an active pipeline.
  ngf_graphics_pipeline active_pipe = cmd_buf->active_pipe;
  assert(active_pipe);

  // Get the number of active descriptor set layouts in the pipeline.
  const uint32_t ndesc_set_layouts = NGFI_DARRAY_SIZE(active_pipe->descriptor_set_layouts);

  // Reset temp. storage to make sure we have all of it available.
  ngfi_sa_reset(ngfi_tmp_store());

  // Allocate an array of descriptor set handles from temporary storage and
  // set them all to null. As we process bind operations, we'll allocate
  // descriptor sets and put them into the array as necessary.
  const size_t     vk_desc_sets_size_bytes = sizeof(VkDescriptorSet) * ndesc_set_layouts;
  VkDescriptorSet* vk_desc_sets = ngfi_sa_alloc(ngfi_tmp_store(), vk_desc_sets_size_bytes);
  memset(vk_desc_sets, VK_NULL_HANDLE, vk_desc_sets_size_bytes);

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
            &NGFI_DARRAY_AT(cmd_buf->active_pipe->descriptor_set_layouts, bind_op->target_set);
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
      case NGF_DESCRIPTOR_UNIFORM_BUFFER: {
        const ngf_uniform_buffer_bind_info* bind_info = &bind_op->info.uniform_buffer;
        VkDescriptorBufferInfo*             vk_bind_info =
            ngfi_sa_alloc(ngfi_tmp_store(), sizeof(VkDescriptorBufferInfo));

        vk_bind_info->buffer = (VkBuffer)bind_info->buffer->data.alloc.obj_handle;
        vk_bind_info->offset = bind_info->offset;
        vk_bind_info->range  = bind_info->range;

        vk_write->pBufferInfo = vk_bind_info;
        break;
      }
      case NGF_DESCRIPTOR_TEXTURE:
      case NGF_DESCRIPTOR_SAMPLER:
      case NGF_DESCRIPTOR_TEXTURE_AND_SAMPLER: {
        const ngf_image_sampler_bind_info* bind_info = &bind_op->info.image_sampler;
        VkDescriptorImageInfo*             vk_bind_info =
            ngfi_sa_alloc(ngfi_tmp_store(), sizeof(VkDescriptorImageInfo));
        vk_bind_info->imageView   = VK_NULL_HANDLE;
        vk_bind_info->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vk_bind_info->sampler     = VK_NULL_HANDLE;
        if (bind_op->type == NGF_DESCRIPTOR_TEXTURE ||
            bind_op->type == NGF_DESCRIPTOR_TEXTURE_AND_SAMPLER) {
          vk_bind_info->imageView   = bind_info->image_subresource.image->vkview;
          vk_bind_info->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        if (bind_op->type == NGF_DESCRIPTOR_SAMPLER ||
            bind_op->type == NGF_DESCRIPTOR_TEXTURE_AND_SAMPLER) {
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
          cmd_buf->active_bundle.vkcmdbuf,
          VK_PIPELINE_BIND_POINT_GRAPHICS,
          active_pipe->vk_pipeline_layout,
          s,
          1,
          &vk_desc_sets[s],
          0,
          NULL);
    }
  }
}

VkResult ngfvk_renderpass_from_attachment_descs(
    uint32_t                          nattachments,
    const ngf_attachment_description* attachment_descs,
    const ngfvk_attachment_pass_desc* attachment_pass_descs,
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

  for (uint32_t a = 0u; a < nattachments; ++a) {
    const ngf_attachment_description* ngf_attachment_desc  = &attachment_descs[a];
    const ngfvk_attachment_pass_desc* attachment_pass_desc = &attachment_pass_descs[a];
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

  const VkPipelineStageFlags source_stage_flags =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
      (have_depth_stencil_attachment ? VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT : 0u);
  const VkAccessFlags source_access_flags =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
      (have_depth_stencil_attachment ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0u);

  // Specify subpass dependencies.
  VkSubpassDependency subpass_dep = {
      .srcSubpass    = 0u,
      .dstSubpass    = VK_SUBPASS_EXTERNAL,
      .srcStageMask  = source_stage_flags,
      .dstStageMask  = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      .srcAccessMask = source_access_flags,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};

  const VkRenderPassCreateInfo renderpass_ci = {
      .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .pNext           = NULL,
      .flags           = 0u,
      .attachmentCount = nattachments,
      .pAttachments    = vk_attachment_descs,
      .subpassCount    = 1u,
      .pSubpasses      = &subpass_desc,
      .dependencyCount = have_sampled_attachments ? 1u : 0u,
      .pDependencies   = have_sampled_attachments ? &subpass_dep : NULL};

  return vkCreateRenderPass(_vk.device, &renderpass_ci, NULL, result);
}

// Returns a bitstring uniquely identifying the series of load/store op combos
// for each attachment.
static uint64_t ngfvk_renderpass_ops_key(
    uint32_t                       nattachments,
    const ngf_attachment_load_op*  load_ops,
    const ngf_attachment_store_op* store_ops) {
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
    // Default render target in MSAA mode has a resolve attachment that is hidden from the user.
    // However, we need to specify it when creating a renderpass for the default rendertarget.
    const bool add_resolve_attachment_desc =
        rt->is_default && (rt->attachment_descs[0].sample_count > 1u);
    const uint32_t nattachments = rt->nattachments + (add_resolve_attachment_desc ? 1u : 0u);
    const uint32_t resolve_attachment_idx = add_resolve_attachment_desc ? nattachments - 1u : (~0u);
    const uint32_t attachment_pass_descs_size = sizeof(ngfvk_attachment_pass_desc) * nattachments;
    ngfvk_attachment_pass_desc* attachment_pass_descs =
        ngfi_sa_alloc(ngfi_tmp_store(), attachment_pass_descs_size);
    const uint32_t rt_attachment_pass_descs_size =
        rt->nattachments * sizeof(ngfvk_attachment_pass_desc);
    memcpy(attachment_pass_descs, rt->attachment_pass_descs, rt_attachment_pass_descs_size);

    for (uint32_t i = 0; i < rt->nattachments; ++i) {
      attachment_pass_descs[i].load_op  = NGFVK_ATTACHMENT_LOAD_OP_FROM_KEY(i, ops_key);
      attachment_pass_descs[i].store_op = NGFVK_ATTACHMENT_STORE_OP_FROM_KEY(i, ops_key);
    }
    if (add_resolve_attachment_desc) {  // initialize pass description for the resolve attachment.
      ngfvk_attachment_pass_desc* resolve_attachment_pass_desc =
          &attachment_pass_descs[resolve_attachment_idx];
      resolve_attachment_pass_desc->initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
      resolve_attachment_pass_desc->layout         = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      resolve_attachment_pass_desc->final_layout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      resolve_attachment_pass_desc->load_op        = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      resolve_attachment_pass_desc->store_op       = VK_ATTACHMENT_STORE_OP_STORE;
      resolve_attachment_pass_desc->is_resolve     = true;
    }

    const uint32_t attachment_descs_size = sizeof(ngf_attachment_description) * nattachments;
    ngf_attachment_description* attachment_descs =
        ngfi_sa_alloc(ngfi_tmp_store(), attachment_descs_size);
    const uint32_t rt_attachment_descs_size = sizeof(ngf_attachment_description) * rt->nattachments;
    memcpy(attachment_descs, rt->attachment_descs, rt_attachment_descs_size);
    if (add_resolve_attachment_desc) {
      ngf_attachment_description* resolve_attachment_description =
          &attachment_descs[resolve_attachment_idx];
      resolve_attachment_description->format       = rt->attachment_descs[0].format;
      resolve_attachment_description->is_sampled   = false;
      resolve_attachment_description->sample_count = 1u;
      resolve_attachment_description->type         = NGF_ATTACHMENT_COLOR;
    }

    ngfvk_renderpass_from_attachment_descs(
        nattachments,
        attachment_descs,
        attachment_pass_descs,
        &result);
    const ngfvk_renderpass_cache_entry cache_entry = {
        .rt         = rt,
        .ops_key    = ops_key,
        .renderpass = result};
    NGFI_DARRAY_APPEND(CURRENT_CONTEXT->renderpass_cache, cache_entry);
  }

  return result;
}

#pragma endregion

#pragma region external_funcs

ngf_device_capabilities DEVICE_CAPS;

ngf_error ngf_initialize(const ngf_init_info* init_info) {
  assert(init_info);
  if (!init_info) { return NGF_ERROR_INVALID_OPERATION; }
  ngfi_diag_info = init_info->diag_info;

  if (_vk.instance == VK_NULL_HANDLE) {  // Vulkan not initialized yet.
    vkl_init_loader();                   // Initialize the vulkan loader.

    const char* const ext_names[] = {// Names of instance-level extensions.
                                     "VK_KHR_surface",
                                     VK_SURFACE_EXT,
                                     VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
                                     VK_EXT_DEBUG_UTILS_EXTENSION_NAME};

    const VkApplicationInfo app_info = {
        // Application information.
        .sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext            = NULL,
        .pApplicationName = NULL,  // TODO: allow specifying app name.
        .pEngineName      = "nicegraf",
        .engineVersion    = VK_MAKE_VERSION(NGF_VER_MAJ, NGF_VER_MIN, 0),
        .apiVersion       = VK_MAKE_VERSION(1, 0, 9)};

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
    const bool enable_validation =
        validation_supported && (ngfi_diag_info.verbosity == NGF_DIAGNOSTICS_VERBOSITY_DETAILED);

    // Create a Vulkan instance.
    const VkInstanceCreateInfo inst_info = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = NULL,
        .flags                   = 0u,
        .pApplicationInfo        = &app_info,
        .enabledLayerCount       = enable_validation ? 1u : 0u,
        .ppEnabledLayerNames     = enabled_layers,
        .enabledExtensionCount   = NGFI_ARRAYSIZE(ext_names) - (enable_validation ? 0u : 1u),
        .ppEnabledExtensionNames = ext_names};
    VkResult vk_err = vkCreateInstance(&inst_info, NULL, &_vk.instance);
    if (vk_err != VK_SUCCESS) {
      NGFI_DIAG_ERROR("Failed to create a Vulkan instance, VK error %d.", vk_err);
      return NGF_ERROR_OBJECT_CREATION_FAILED;
    }

    vkl_init_instance(_vk.instance);  // load instance-level Vulkan functions.

    if (enable_validation) {
      // Install a debug callback to forward vulkan debug messages to the user.
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
      VkDebugUtilsMessengerEXT vk_debug_messenger;
      vkCreateDebugUtilsMessengerEXT(_vk.instance, &debug_callback_info, NULL, &vk_debug_messenger);
    }

    // Obtain a list of available physical devices.
    uint32_t         nphysdev = NGFVK_MAX_PHYS_DEV;
    VkPhysicalDevice physdevs[NGFVK_MAX_PHYS_DEV];
    vk_err = vkEnumeratePhysicalDevices(_vk.instance, &nphysdev, physdevs);
    if (vk_err != VK_SUCCESS) {
      NGFI_DIAG_ERROR("Failed to enumerate Vulkan physical devices, VK error %d.", vk_err);
      return NGF_ERROR_INVALID_OPERATION;
    }

    // Pick a suitable physical device based on user's preference.
    uint32_t                    best_device_score = 0U;
    uint32_t                    best_device_index = NGFVK_INVALID_IDX;
    const ngf_device_preference pref              = init_info->device_pref;
    for (uint32_t i = 0; i < nphysdev; ++i) {
      VkPhysicalDeviceProperties dev_props;
      vkGetPhysicalDeviceProperties(physdevs[i], &dev_props);
      uint32_t score = 0U;
      switch (dev_props.deviceType) {
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
      default:
        score += 10U;
      }
      if (score > best_device_score) {
        best_device_index = i;
        best_device_score = score;
      }
    }
    if (best_device_index == NGFVK_INVALID_IDX) {
      NGFI_DIAG_ERROR("Failed to find a suitable physical device.");
      return NGF_ERROR_INVALID_OPERATION;
    }
    _vk.phys_dev = physdevs[best_device_index];
    VkPhysicalDeviceProperties phys_dev_properties;
    vkGetPhysicalDeviceProperties(_vk.phys_dev, &phys_dev_properties);

    // Obtain a list of queue family properties from the device.
    uint32_t num_queue_families = 0U;
    vkGetPhysicalDeviceQueueFamilyProperties(_vk.phys_dev, &num_queue_families, NULL);
    VkQueueFamilyProperties* queue_families =
        NGFI_ALLOCN(VkQueueFamilyProperties, num_queue_families);
    assert(queue_families);
    vkGetPhysicalDeviceQueueFamilyProperties(_vk.phys_dev, &num_queue_families, queue_families);

    // Pick suitable queue families for graphics and present.
    uint32_t gfx_family_idx     = NGFVK_INVALID_IDX;
    uint32_t present_family_idx = NGFVK_INVALID_IDX;
    for (uint32_t q = 0; queue_families && q < num_queue_families; ++q) {
      const VkQueueFlags flags      = queue_families[q].queueFlags;
      const VkBool32     is_gfx     = (flags & VK_QUEUE_GRAPHICS_BIT) != 0;
      const VkBool32     is_present = ngfvk_query_presentation_support(_vk.phys_dev, q);
      if (gfx_family_idx == NGFVK_INVALID_IDX && is_gfx) { gfx_family_idx = q; }
      if (present_family_idx == NGFVK_INVALID_IDX && is_present == VK_TRUE) {
        present_family_idx = q;
      }
    }
    NGFI_FREEN(queue_families, num_queue_families);
    queue_families = NULL;
    if (gfx_family_idx == NGFVK_INVALID_IDX || present_family_idx == NGFVK_INVALID_IDX) {
      NGFI_DIAG_ERROR("Could not find a suitable queue family.");
      return NGF_ERROR_INVALID_OPERATION;
    }
    _vk.gfx_family_idx     = gfx_family_idx;
    _vk.present_family_idx = present_family_idx;

    // Create logical device.
    const float                   queue_prio     = 1.0f;
    const VkDeviceQueueCreateInfo gfx_queue_info = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext            = NULL,
        .flags            = 0,
        .queueFamilyIndex = _vk.gfx_family_idx,
        .queueCount       = 1,
        .pQueuePriorities = &queue_prio};
    const VkDeviceQueueCreateInfo present_queue_info = {
        .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext            = NULL,
        .flags            = 0,
        .queueFamilyIndex = _vk.present_family_idx,
        .queueCount       = 1,
        .pQueuePriorities = &queue_prio};
    const bool same_gfx_and_present               = _vk.gfx_family_idx == _vk.present_family_idx;
    const VkDeviceQueueCreateInfo queue_infos[]   = {gfx_queue_info, present_queue_info};
    const uint32_t                num_queue_infos = 1u + (same_gfx_and_present ? 0u : 1u);
    const char*                   device_exts[]   = {
        "VK_KHR_maintenance1",
        "VK_KHR_swapchain",
        "VK_KHR_shader_float16_int8"};
    const VkPhysicalDeviceFeatures required_features = {.samplerAnisotropy = VK_TRUE};

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
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &sf16_features,
        .flags                   = 0,
        .queueCreateInfoCount    = num_queue_infos,
        .pQueueCreateInfos       = queue_infos,
        .enabledLayerCount       = 0,
        .ppEnabledLayerNames     = NULL,
        .pEnabledFeatures        = &required_features,
        .enabledExtensionCount   = sizeof(device_exts) / sizeof(const char*),
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
    DEVICE_CAPS.clipspace_z_zero_to_one = true;
    DEVICE_CAPS.uniform_buffer_offset_alignment =
        phys_dev_properties.limits.minUniformBufferOffsetAlignment;

    // Done!
  }

  return NGF_ERROR_OK;
}

const ngf_device_capabilities* ngf_get_device_capabilities(void) {
  return &DEVICE_CAPS;
}

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
      .vkDestroyImage                      = vkDestroyImage};
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
      .pRecordSettings             = NULL};
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
    ctx->default_attachment_descs[0].format       = swapchain_info->color_format;
    ctx->default_attachment_descs[0].is_sampled   = false;
    ctx->default_attachment_descs[0].sample_count = swapchain_info->sample_count;
    ctx->default_attachment_descs[0].type         = NGF_ATTACHMENT_COLOR;
    const bool has_depth       = swapchain_info->depth_format != NGF_IMAGE_FORMAT_UNDEFINED;
    const bool is_multisampled = swapchain_info->sample_count > 1u;
    const bool is_depth_only =
        swapchain_info->depth_format == NGF_IMAGE_FORMAT_DEPTH32 || NGF_IMAGE_FORMAT_DEPTH16;
    if (has_depth) {
      ctx->default_attachment_descs[1].format       = swapchain_info->depth_format;
      ctx->default_attachment_descs[1].is_sampled   = false;
      ctx->default_attachment_descs[1].sample_count = swapchain_info->sample_count;
      ctx->default_attachment_descs[1].type =
          is_depth_only ? NGF_ATTACHMENT_DEPTH : NGF_ATTACHMENT_DEPTH_STENCIL;
    }
    ctx->default_render_target = NGFI_ALLOC(struct ngf_render_target_t);
    if (ctx->default_render_target == NULL) {
      err = NGF_ERROR_OBJECT_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }
    const uint32_t nattachment_descs = 1u + (has_depth ? 1u : 0u);

    ctx->default_render_target->is_default   = true;
    ctx->default_render_target->width        = swapchain_info->width;
    ctx->default_render_target->height       = swapchain_info->height;
    ctx->default_render_target->frame_buffer = VK_NULL_HANDLE;
    ctx->default_render_target->nattachments = nattachment_descs;
    ctx->default_render_target->attachment_descs =
        NGFI_ALLOCN(ngf_attachment_description, nattachment_descs);
    ctx->default_render_target->attachment_pass_descs =
        NGFI_ALLOCN(ngfvk_attachment_pass_desc, nattachment_descs);

    uint32_t attachment_desc_idx = 0u;
    ctx->default_render_target->attachment_descs[attachment_desc_idx] =
        ctx->default_attachment_descs[0];
    ngfvk_attachment_pass_desc* color_attachment_pass_desc =
        &ctx->default_render_target->attachment_pass_descs[attachment_desc_idx];
    color_attachment_pass_desc->initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment_pass_desc->layout         = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment_pass_desc->final_layout   = is_multisampled
                                                     ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                                     : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    color_attachment_pass_desc->is_resolve     = false;
    color_attachment_pass_desc->load_op        = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment_pass_desc->store_op       = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    if (has_depth) {
      ++attachment_desc_idx;
      ctx->default_render_target->attachment_descs[attachment_desc_idx] =
          ctx->default_attachment_descs[1];
      ngfvk_attachment_pass_desc* depth_attachment_pass_desc =
          &ctx->default_render_target->attachment_pass_descs[attachment_desc_idx];
      depth_attachment_pass_desc->initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
      depth_attachment_pass_desc->layout         = is_depth_only
                                                       ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
                                                       : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      depth_attachment_pass_desc->final_layout   = depth_attachment_pass_desc->layout;
      depth_attachment_pass_desc->is_resolve     = false;
      depth_attachment_pass_desc->load_op        = VK_ATTACHMENT_LOAD_OP_CLEAR;
      depth_attachment_pass_desc->store_op       = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }

    ngfvk_renderpass_from_attachment_descs(
        nattachment_descs,
        ctx->default_render_target->attachment_descs,
        ctx->default_render_target->attachment_pass_descs,
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
    NGFI_DARRAY_RESET(ctx->frame_res[f].cmd_bufs, max_inflight_frames);
    NGFI_DARRAY_RESET(ctx->frame_res[f].cmd_pools, max_inflight_frames);
    NGFI_DARRAY_RESET(ctx->frame_res[f].cmd_buf_sems, max_inflight_frames);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_pipelines, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_pipeline_layouts, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_dset_layouts, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_framebuffers, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_render_passes, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_samplers, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_image_views, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_images, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].retire_buffers, 8);
    NGFI_DARRAY_RESET(ctx->frame_res[f].reset_desc_pools_lists, 8);

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

ngf_create_context_cleanup:
  if (err != NGF_ERROR_OK) { ngf_destroy_context(ctx); }
  return err;
}

ngf_error ngf_resize_context(ngf_context ctx, uint32_t new_width, uint32_t new_height) {
  assert(ctx);
  if (new_width == 0u || new_height == 0u || ctx == NULL || ctx->default_render_target == NULL) {
    return NGF_ERROR_INVALID_OPERATION;
  }

  ngf_error err = NGF_ERROR_OK;
  ngfvk_destroy_swapchain(&ctx->swapchain);
  ctx->swapchain_info.width          = NGFI_MAX(1, new_width);
  ctx->swapchain_info.height         = NGFI_MAX(1, new_height);
  ctx->default_render_target->width  = ctx->swapchain_info.width;
  ctx->default_render_target->height = ctx->swapchain_info.height;
  err = ngfvk_create_swapchain(&ctx->swapchain_info, ctx->surface, &ctx->swapchain);
  return err;
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
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].cmd_bufs);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].cmd_pools);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].cmd_buf_sems);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_pipelines);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_pipeline_layouts);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_dset_layouts);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_framebuffers);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_render_passes);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_samplers);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_image_views);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].retire_images);
      for (uint32_t i = 0u; i < ctx->frame_res[f].nwait_fences; ++i) {
        vkDestroyFence(_vk.device, ctx->frame_res[f].fences[i], NULL);
      }
    }

    NGFI_DARRAY_FOREACH(ctx->desc_superpools, p) {
      ngfvk_destroy_desc_superpool(&NGFI_DARRAY_AT(ctx->desc_superpools, p));
    }
    NGFI_DARRAY_DESTROY(ctx->desc_superpools);

    NGFI_DARRAY_FOREACH(ctx->renderpass_cache, r) {
      vkDestroyRenderPass(_vk.device, NGFI_DARRAY_AT(ctx->renderpass_cache, r).renderpass, NULL);
    }
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
  cmd_buf->active_pipe            = NULL;
  cmd_buf->renderpass_active      = false;
  cmd_buf->active_rt              = NULL;
  cmd_buf->desc_pools_list        = NULL;
  cmd_buf->pending_bind_ops.first = NULL;
  cmd_buf->pending_bind_ops.last  = NULL;
  cmd_buf->pending_bind_ops.size  = 0u;
  cmd_buf->active_bundle.vkcmdbuf = VK_NULL_HANDLE;
  cmd_buf->active_bundle.vkpool   = VK_NULL_HANDLE;
  cmd_buf->active_bundle.vksem    = VK_NULL_HANDLE;
  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_buffer_start_render(ngf_cmd_buffer cmd_buf, ngf_render_encoder* enc) {
  enc->__handle = (uintptr_t)((void*)cmd_buf);
  return ngfvk_encoder_start(cmd_buf);
}

ngf_error ngf_cmd_buffer_start_xfer(ngf_cmd_buffer cmd_buf, ngf_xfer_encoder* enc) {
  enc->__handle = (uintptr_t)((void*)cmd_buf);
  return ngfvk_encoder_start(cmd_buf);
}

ngf_error ngf_render_encoder_end(ngf_render_encoder enc) {
  return ngfvk_encoder_end((ngf_cmd_buffer)((void*)enc.__handle));
}

ngf_error ngf_xfer_encoder_end(ngf_xfer_encoder enc) {
  return ngfvk_encoder_end((ngf_cmd_buffer)((void*)enc.__handle));
}

ngf_error ngf_start_cmd_buffer(ngf_cmd_buffer cmd_buf, ngf_frame_token token) {
  assert(cmd_buf);

  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_READY);

  cmd_buf->parent_frame    = token;
  cmd_buf->desc_pools_list = NULL;
  cmd_buf->active_rt       = NULL;

  const ngfvk_command_superpool* superpool =
      ngfvk_find_command_superpool(ngfi_frame_ctx_id(token), ngfi_frame_max_inflight_frames(token));
  const VkCommandPool pool = superpool->pools[ngfi_frame_id(token)];

  return ngfvk_cmd_bundle_create(pool, &cmd_buf->active_bundle);
}

void ngf_destroy_cmd_buffer(ngf_cmd_buffer buffer) {
  assert(buffer);
  if (buffer->active_bundle.vkcmdbuf != VK_NULL_HANDLE) {
    vkFreeCommandBuffers(
        _vk.device,
        buffer->active_bundle.vkpool,
        1u,
        &buffer->active_bundle.vkcmdbuf);
  }
  if (buffer->active_bundle.vksem != VK_NULL_HANDLE) {
    vkDestroySemaphore(_vk.device, buffer->active_bundle.vksem, NULL);
  }
  ngfvk_cleanup_pending_binds(buffer);
  NGFI_FREE(buffer);
}

ngf_error ngf_submit_cmd_buffers(uint32_t nbuffers, ngf_cmd_buffer* cmd_bufs) {
  assert(cmd_bufs);
  uint32_t               fi             = CURRENT_CONTEXT->frame_id;
  ngfvk_frame_resources* frame_res_data = &CURRENT_CONTEXT->frame_res[fi];
  for (uint32_t i = 0u; i < nbuffers; ++i) {
    if (cmd_bufs[i]->parent_frame != CURRENT_CONTEXT->current_frame_token) {
      NGFI_DIAG_ERROR("submitting a command buffer for the wrong frame");
      return NGF_ERROR_INVALID_OPERATION;
    }
    NGFI_TRANSITION_CMD_BUF(cmd_bufs[i], NGFI_CMD_BUFFER_SUBMITTED);
    if (cmd_bufs[i]->desc_pools_list) {
      NGFI_DARRAY_APPEND(frame_res_data->reset_desc_pools_lists, cmd_bufs[i]->desc_pools_list);
    }
    vkEndCommandBuffer(cmd_bufs[i]->active_bundle.vkcmdbuf);
    NGFI_DARRAY_APPEND(frame_res_data->cmd_bufs, cmd_bufs[i]->active_bundle.vkcmdbuf);
    NGFI_DARRAY_APPEND(frame_res_data->cmd_pools, cmd_bufs[i]->active_bundle.vkpool);
    NGFI_DARRAY_APPEND(frame_res_data->cmd_buf_sems, cmd_bufs[i]->active_bundle.vksem);
    cmd_bufs[i]->active_pipe = VK_NULL_HANDLE;
    cmd_bufs[i]->active_rt   = NULL;
    memset(&cmd_bufs[i]->active_bundle, 0, sizeof(cmd_bufs[i]->active_bundle));
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_begin_frame(ngf_frame_token* token) {
  ngf_error      err = NGF_ERROR_OK;
  const uint32_t fi  = CURRENT_CONTEXT->frame_id;

  NGFI_DARRAY_CLEAR(CURRENT_CONTEXT->frame_res[fi].cmd_bufs);
  NGFI_DARRAY_CLEAR(CURRENT_CONTEXT->frame_res[fi].cmd_pools);
  NGFI_DARRAY_CLEAR(CURRENT_CONTEXT->frame_res[fi].cmd_buf_sems);
  // Insert placeholders for deferred barriers.
  NGFI_DARRAY_APPEND(CURRENT_CONTEXT->frame_res[fi].cmd_bufs, VK_NULL_HANDLE);
  NGFI_DARRAY_APPEND(CURRENT_CONTEXT->frame_res[fi].cmd_pools, VK_NULL_HANDLE);
  NGFI_DARRAY_APPEND(CURRENT_CONTEXT->frame_res[fi].cmd_buf_sems, VK_NULL_HANDLE);

  // reset stack allocator.
  ngfi_sa_reset(ngfi_tmp_store());

  const bool needs_present = CURRENT_CONTEXT->swapchain.vk_swapchain != VK_NULL_HANDLE;

  if (needs_present) {
    vkAcquireNextImageKHR(
        _vk.device,
        CURRENT_CONTEXT->swapchain.vk_swapchain,
        UINT64_MAX,
        CURRENT_CONTEXT->swapchain.image_semaphores[fi],
        VK_NULL_HANDLE,
        &CURRENT_CONTEXT->swapchain.image_idx);
  }

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

  // Obtain the current frame sync structure and increment frame number.
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
    const ngfvk_command_superpool* superpool = ngfvk_find_command_superpool(
        ngfi_frame_ctx_id(token),
        ngfi_frame_max_inflight_frames(token));
    const VkCommandPool pool = superpool->pools[ngfi_frame_id(token)];
    ngfvk_cmd_bundle    bundle;
    ngfvk_cmd_bundle_create(pool, &bundle);
    vkCmdPipelineBarrier(
        bundle.vkcmdbuf,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
        0,
        0,
        NULL,
        0,
        NULL,
        npending_barriers,
        NGFVK_PENDING_IMG_BARRIER_QUEUE.barriers.data);
    vkEndCommandBuffer(bundle.vkcmdbuf);
    frame_res->cmd_bufs.data[0]     = bundle.vkcmdbuf;
    frame_res->cmd_pools.data[0]    = bundle.vkpool;
    frame_res->cmd_buf_sems.data[0] = bundle.vksem;
    NGFI_DARRAY_CLEAR(NGFVK_PENDING_IMG_BARRIER_QUEUE.barriers);
  }
  pthread_mutex_unlock(&NGFVK_PENDING_IMG_BARRIER_QUEUE.lock);

  // Submit pending gfx commands & present.
  const uint32_t nsubmitted_gfx_cmdbuffers =
      NGFI_DARRAY_SIZE(frame_res->cmd_bufs) - (have_deferred_barriers ? 0 : 1);
  const bool             needs_present = CURRENT_CONTEXT->swapchain.vk_swapchain != VK_NULL_HANDLE;
  const bool             have_pending_gfx_cmds = nsubmitted_gfx_cmdbuffers > 0;
  const VkCommandBuffer* cmd_bufs =
      have_pending_gfx_cmds
          ? (have_deferred_barriers ? frame_res->cmd_bufs.data : &frame_res->cmd_bufs.data[1])
          : NULL;
  const VkSemaphore* cmd_buf_sems =
      have_pending_gfx_cmds ? (have_deferred_barriers ? frame_res->cmd_buf_sems.data
                                                      : &frame_res->cmd_buf_sems.data[1])
                            : NULL;

  // Submit pending graphics commands.
  const VkPipelineStageFlags color_attachment_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  uint32_t                   wait_sem_count         = 0u;
  VkSemaphore*               wait_sems              = NULL;
  const VkPipelineStageFlags* wait_stage_flags      = NULL;
  if (CURRENT_CONTEXT->swapchain.vk_swapchain != VK_NULL_HANDLE) {
    wait_sem_count   = 1u;
    wait_sems        = &CURRENT_CONTEXT->swapchain.image_semaphores[fi];
    wait_stage_flags = &color_attachment_stage;
  }

  ngfvk_submit_commands(
      _vk.gfx_queue,
      cmd_bufs,
      nsubmitted_gfx_cmdbuffers,
      wait_stage_flags,
      wait_sems,
      wait_sem_count,
      cmd_buf_sems,
      nsubmitted_gfx_cmdbuffers,
      frame_res->fences[frame_res->nwait_fences++]);

  // Present if necessary.
  if (needs_present) {
    const VkPresentInfoKHR present_info = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext              = NULL,
        .waitSemaphoreCount = nsubmitted_gfx_cmdbuffers,
        .pWaitSemaphores    = cmd_buf_sems,
        .swapchainCount     = 1,
        .pSwapchains        = &CURRENT_CONTEXT->swapchain.vk_swapchain,
        .pImageIndices      = &CURRENT_CONTEXT->swapchain.image_idx,
        .pResults           = NULL};
    const VkResult present_result = vkQueuePresentKHR(_vk.present_queue, &present_info);
    if (present_result != VK_SUCCESS) err = NGF_ERROR_INVALID_OPERATION;
  }

  // Retire resources.
  ngfvk_frame_resources* next_frame_res = &CURRENT_CONTEXT->frame_res[next_fi];
  ngfvk_retire_resources(next_frame_res);
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
  if (vkerr != VK_SUCCESS) {
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
    NGFI_FREEN(stage->entry_point_name, strlen(stage->entry_point_name) + 1u);
    NGFI_FREE(stage);
  }
}

ngf_error ngf_create_graphics_pipeline(
    const ngf_graphics_pipeline_info* info,
    ngf_graphics_pipeline*            result) {
  assert(info);
  assert(result);
  VkVertexInputBindingDescription*   vk_binding_descs = NULL;
  VkVertexInputAttributeDescription* vk_attrib_descs  = NULL;
  ngf_error                          err              = NGF_ERROR_OK;
  VkResult                           vk_err           = VK_SUCCESS;

  // Allocate space for the pipeline object.
  *result                        = NGFI_ALLOC(ngf_graphics_pipeline_t);
  ngf_graphics_pipeline pipeline = *result;
  if (pipeline == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_create_graphics_pipeline_cleanup;
  }

  // Build up Vulkan specialization structure, if necessary.
  VkSpecializationInfo           vk_spec_info;
  const ngf_specialization_info* spec_info = info->spec_info;
  if (info->spec_info) {
    VkSpecializationMapEntry* spec_map_entries = ngfi_sa_alloc(
        ngfi_tmp_store(),
        info->spec_info->nspecializations * sizeof(VkSpecializationMapEntry));

    vk_spec_info.pData         = spec_info->value_buffer;
    vk_spec_info.mapEntryCount = spec_info->nspecializations;
    vk_spec_info.pMapEntries   = spec_map_entries;

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
    vk_spec_info.dataSize = total_data_size;
  }

  // Prepare shader stages.
  VkPipelineShaderStageCreateInfo vk_shader_stages[5];
  assert(NGFI_ARRAYSIZE(vk_shader_stages) == NGFI_ARRAYSIZE(info->shader_stages));
  if (info->nshader_stages >= NGFI_ARRAYSIZE(vk_shader_stages)) {
    err = NGF_ERROR_OUT_OF_BOUNDS;
    goto ngf_create_graphics_pipeline_cleanup;
  }
  for (uint32_t s = 0u; s < info->nshader_stages; ++s) {
    const ngf_shader_stage stage            = info->shader_stages[s];
    vk_shader_stages[s].sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vk_shader_stages[s].pNext               = NULL;
    vk_shader_stages[s].flags               = 0u;
    vk_shader_stages[s].stage               = stage->vk_stage_bits;
    vk_shader_stages[s].module              = stage->vk_module;
    vk_shader_stages[s].pName               = stage->entry_point_name,
    vk_shader_stages[s].pSpecializationInfo = spec_info ? &vk_spec_info : NULL;
  }

  // Prepare vertex input.
  vk_binding_descs =
      NGFI_ALLOCN(VkVertexInputBindingDescription, info->input_info->nvert_buf_bindings);
  vk_attrib_descs = NGFI_ALLOCN(VkVertexInputAttributeDescription, info->input_info->nattribs);

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
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .pNext                  = NULL,
      .flags                  = 0u,
      .topology               = get_vk_primitive_type(info->primitive_type),
      .primitiveRestartEnable = VK_FALSE};

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
  // TODO: use specified alpha-to-coverage
  VkPipelineMultisampleStateCreateInfo multisampling = {
      .sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .pNext                 = NULL,
      .flags                 = 0u,
      .rasterizationSamples  = get_vk_sample_count(info->multisample->sample_count),
      .sampleShadingEnable   = VK_FALSE,
      .minSampleShading      = 0.0f,
      .pSampleMask           = NULL,
      .alphaToCoverageEnable = VK_FALSE,
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

  // Prepare blend state.
  const VkPipelineColorBlendAttachmentState attachment_blend_state = {
      .blendEnable         = info->blend->enable,
      .srcColorBlendFactor = info->blend->enable
                                 ? get_vk_blend_factor(info->blend->src_color_blend_factor)
                                 : VK_BLEND_FACTOR_ONE,
      .dstColorBlendFactor = info->blend->enable
                                 ? get_vk_blend_factor(info->blend->dst_color_blend_factor)
                                 : VK_BLEND_FACTOR_ZERO,
      .colorBlendOp =
          info->blend->enable ? get_vk_blend_op(info->blend->blend_op_color) : VK_BLEND_OP_ADD,
      .srcAlphaBlendFactor = info->blend->enable
                                 ? get_vk_blend_factor(info->blend->src_alpha_blend_factor)
                                 : VK_BLEND_FACTOR_ONE,
      .dstAlphaBlendFactor = info->blend->enable
                                 ? get_vk_blend_factor(info->blend->dst_alpha_blend_factor)
                                 : VK_BLEND_FACTOR_ZERO,
      .alphaBlendOp =
          info->blend->enable ? get_vk_blend_op(info->blend->blend_op_alpha) : VK_BLEND_OP_ADD,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |  // TODO: set color write mask
                        VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                        VK_COLOR_COMPONENT_A_BIT};

  uint32_t ncolor_attachments = 0u;
  for (uint32_t i = 0; i < info->compatible_rt_attachment_descs->ndescs; ++i) {
    if (info->compatible_rt_attachment_descs->descs[i].type == NGF_ATTACHMENT_COLOR)
      ++ncolor_attachments;
  }
  VkPipelineColorBlendAttachmentState blend_states[16];
  for (size_t i = 0u; i < 16u; ++i) { blend_states[i] = attachment_blend_state; }

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
      .blendConstants  = {0.0f, .0f, .0f, .0f}};

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

  // Descriptor set layouts.
  NGFI_DARRAY_RESET(pipeline->descriptor_set_layouts, info->layout->ndescriptor_set_layouts);
  VkDescriptorSetLayout* vk_set_layouts = ngfi_sa_alloc(
      ngfi_tmp_store(),
      sizeof(VkDescriptorSetLayout) * info->layout->ndescriptor_set_layouts);
  for (uint32_t s = 0u; s < info->layout->ndescriptor_set_layouts; ++s) {
    VkDescriptorSetLayoutBinding* vk_descriptor_bindings =
        NGFI_ALLOCN(  // TODO: use temp storage here
            VkDescriptorSetLayoutBinding,
            info->layout->descriptor_set_layouts[s].ndescriptors);
    ngfvk_desc_set_layout set_layout;
    memset(&set_layout, 0, sizeof(set_layout));
    for (uint32_t b = 0u; b < info->layout->descriptor_set_layouts[s].ndescriptors; ++b) {
      VkDescriptorSetLayoutBinding* vk_d = &vk_descriptor_bindings[b];
      const ngf_descriptor_info*    d    = &info->layout->descriptor_set_layouts[s].descriptors[b];
      vk_d->binding                      = d->id;
      vk_d->descriptorCount              = 1u;
      vk_d->descriptorType               = get_vk_descriptor_type(d->type);
      vk_d->descriptorCount              = 1u;
      vk_d->stageFlags                   = get_vk_stage_flags(d->stage_flags);
      vk_d->pImmutableSamplers           = NULL;
      set_layout.counts[d->type]++;
    }
    const VkDescriptorSetLayoutCreateInfo vk_ds_info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext        = NULL,
        .flags        = 0u,
        .bindingCount = info->layout->descriptor_set_layouts[s].ndescriptors,
        .pBindings    = vk_descriptor_bindings};
    vk_err = vkCreateDescriptorSetLayout(_vk.device, &vk_ds_info, NULL, &set_layout.vk_handle);
    NGFI_DARRAY_APPEND(pipeline->descriptor_set_layouts, set_layout);
    vk_set_layouts[s] = set_layout.vk_handle;
    NGFI_FREEN(vk_descriptor_bindings, info->layout->descriptor_set_layouts[s].ndescriptors);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_OBJECT_CREATION_FAILED;
      goto ngf_create_graphics_pipeline_cleanup;
    }
  }

  // Pipeline layout.
  const uint32_t ndescriptor_sets = NGFI_DARRAY_SIZE(pipeline->descriptor_set_layouts);
  const VkPipelineLayoutCreateInfo vk_pipeline_layout_info = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pNext                  = NULL,
      .flags                  = 0u,
      .setLayoutCount         = ndescriptor_sets,
      .pSetLayouts            = vk_set_layouts,
      .pushConstantRangeCount = 0u,
      .pPushConstantRanges    = NULL};
  vk_err = vkCreatePipelineLayout(
      _vk.device,
      &vk_pipeline_layout_info,
      NULL,
      &pipeline->vk_pipeline_layout);
  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngf_create_graphics_pipeline_cleanup;
  }

  // Create a compatible render pass object.
  ngfvk_attachment_pass_desc* attachment_pass_descs = ngfi_sa_alloc(
      ngfi_tmp_store(),
      sizeof(ngfvk_attachment_pass_desc) * info->compatible_rt_attachment_descs->ndescs);
  for (uint32_t i = 0u; i < info->compatible_rt_attachment_descs->ndescs; ++i) {
    attachment_pass_descs[i].load_op        = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment_pass_descs[i].store_op       = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment_pass_descs[i].final_layout   = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment_pass_descs[i].initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment_pass_descs[i].is_resolve     = false;
    attachment_pass_descs[i].layout         = VK_IMAGE_LAYOUT_UNDEFINED;
  }

  vk_err = ngfvk_renderpass_from_attachment_descs(
      info->compatible_rt_attachment_descs->ndescs,
      info->compatible_rt_attachment_descs->descs,
      attachment_pass_descs,
      &pipeline->compatible_render_pass);

  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngf_create_graphics_pipeline_cleanup;
  }

  // Create all the required pipeline flavors.
  for (size_t f = 0u; f < NGFVK_PIPELINE_FLAVOR_COUNT; ++f) {
    VkPipelineRasterizationStateCreateInfo actual_rasterization = rasterization;
    if (f == NGFVK_PIPELINE_FLAVOR_RENDER_TO_TEXTURE) {
      // flip winding when rendering to texture.
      actual_rasterization.frontFace = (actual_rasterization.frontFace == VK_FRONT_FACE_CLOCKWISE)
                                           ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                           : VK_FRONT_FACE_CLOCKWISE;
    }

    VkGraphicsPipelineCreateInfo vk_pipeline_info = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext               = NULL,
        .flags               = 0u,
        .stageCount          = info->nshader_stages,
        .pStages             = vk_shader_stages,
        .pVertexInputState   = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pTessellationState  = &tess,
        .pViewportState      = &viewport_state,
        .pRasterizationState = &actual_rasterization,
        .pMultisampleState   = &multisampling,
        .pDepthStencilState  = &depth_stencil,
        .pColorBlendState    = &color_blend,
        .pDynamicState       = &dynamic_state,
        .layout              = pipeline->vk_pipeline_layout,
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
        &pipeline->vk_pipeline_flavors[f]);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_OBJECT_CREATION_FAILED;
      goto ngf_create_graphics_pipeline_cleanup;
    }
  }

ngf_create_graphics_pipeline_cleanup:
  if (err != NGF_ERROR_OK) { ngf_destroy_graphics_pipeline(pipeline); }
  NGFI_FREE(vk_binding_descs);
  NGFI_FREE(vk_attrib_descs);
  return err;
}

void ngf_destroy_graphics_pipeline(ngf_graphics_pipeline p) {
  if (p != NULL) {
    ngfvk_frame_resources* res = &CURRENT_CONTEXT->frame_res[CURRENT_CONTEXT->frame_id];
    NGFI_DARRAY_APPEND(res->retire_render_passes, p->compatible_render_pass);
    for (size_t f = 0u; f < NGFVK_PIPELINE_FLAVOR_COUNT; ++f) {
      if (p->vk_pipeline_flavors[f] != VK_NULL_HANDLE) {
        NGFI_DARRAY_APPEND(res->retire_pipelines, p->vk_pipeline_flavors[f]);
      }
    }
    if (p->vk_pipeline_layout != VK_NULL_HANDLE) {
      NGFI_DARRAY_APPEND(res->retire_pipeline_layouts, p->vk_pipeline_layout);
    }
    NGFI_DARRAY_FOREACH(p->descriptor_set_layouts, l) {
      VkDescriptorSetLayout set_layout = NGFI_DARRAY_AT(p->descriptor_set_layouts, l).vk_handle;
      NGFI_DARRAY_APPEND(res->retire_dset_layouts, set_layout);
    }
    NGFI_DARRAY_DESTROY(p->descriptor_set_layouts);
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
        CURRENT_CONTEXT->default_attachment_descs;
    return &CURRENT_CONTEXT->default_attachment_descriptions_list;
  } else {
    return NULL;
  }
}

ngf_error ngf_create_render_target(const ngf_render_target_info* info, ngf_render_target* result) {
  ngf_render_target rt = NGFI_ALLOC(ngf_render_target_t);
  if (rt == NULL) { return NGF_ERROR_OUT_OF_MEM; }
  memset(rt, 0, sizeof(ngf_render_target_t));
  *result       = rt;
  ngf_error err = NGF_ERROR_OK;

  ngfvk_attachment_pass_desc* vk_attachment_pass_descs =
      NGFI_ALLOCN(ngfvk_attachment_pass_desc, info->attachment_descriptions->ndescs);

  VkImageView* attachment_views =
      ngfi_sa_alloc(ngfi_tmp_store(), info->attachment_descriptions->ndescs * sizeof(VkImageView));
  if (vk_attachment_pass_descs == NULL || attachment_views == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_create_render_target_cleanup;
  }
  uint32_t ncolor_attachments = 0u;
  for (uint32_t a = 0u; a < info->attachment_descriptions->ndescs; ++a) {
    const ngf_attachment_description* ngf_attachment_desc =
        &info->attachment_descriptions->descs[a];
    ngfvk_attachment_pass_desc* attachment_pass_desc = &vk_attachment_pass_descs[a];
    switch (ngf_attachment_desc->type) {
    case NGF_ATTACHMENT_COLOR:
      ++ncolor_attachments;
      attachment_pass_desc->layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      break;
    case NGF_ATTACHMENT_DEPTH:
      attachment_pass_desc->layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
      break;
    case NGF_ATTACHMENT_DEPTH_STENCIL:
      attachment_pass_desc->layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      break;
    default:
      assert(false);
    }

    const ngf_image attachment_img   = info->attachment_image_refs->image;
    const bool is_attachment_sampled = attachment_img->usage_flags | NGF_IMAGE_USAGE_SAMPLE_FROM;
    attachment_pass_desc->is_resolve = false;
    attachment_pass_desc->initial_layout = is_attachment_sampled
                                               ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                               : attachment_pass_desc->layout;
    attachment_pass_desc->final_layout   = is_attachment_sampled
                                               ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                               : attachment_pass_desc->layout;
    attachment_views[a] =
        info->attachment_image_refs->image->vkview;  // TODO: use the specified subresource.
  }

  const ngf_attachment_load_op  load_op  = NGF_LOAD_OP_CLEAR;
  const ngf_attachment_store_op store_op = NGF_STORE_OP_STORE;

  for (uint32_t a = 0u; a < info->attachment_descriptions->ndescs; ++a) {
    ngfvk_attachment_pass_desc* attachment_pass_desc = &vk_attachment_pass_descs[a];
    attachment_pass_desc->load_op                    = get_vk_load_op(load_op);
    attachment_pass_desc->store_op                   = get_vk_load_op(store_op);
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

  rt->width                 = info->attachment_image_refs[0].image->extent.width;
  rt->height                = info->attachment_image_refs[0].image->extent.height;
  rt->nattachments          = info->attachment_descriptions->ndescs;
  rt->attachment_descs      = NGFI_ALLOCN(ngf_attachment_description, rt->nattachments);
  rt->attachment_pass_descs = vk_attachment_pass_descs;

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
  const VkResult vk_err = vkCreateFramebuffer(_vk.device, &fb_info, NULL, &rt->frame_buffer);
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
    NGFI_FREEN(target->attachment_descs, target->nattachments);
    NGFI_FREEN(target->attachment_pass_descs, target->nattachments);
    NGFI_FREE(target);
  }
}

#define NGFVK_ENC2CMDBUF(enc) ((ngf_cmd_buffer)((void*)enc.__handle))

void ngf_cmd_begin_pass(ngf_render_encoder enc, const ngf_pass_info* pass_info) {
  ngfi_sa_reset(ngfi_tmp_store());
  ngf_cmd_buffer          buf       = NGFVK_ENC2CMDBUF(enc);
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
  const VkRenderPass render_pass = ngfvk_lookup_renderpass(
      pass_info->render_target,
      ngfvk_renderpass_ops_key(
          pass_info->render_target->nattachments,
          pass_info->load_ops,
          pass_info->store_ops));
  const VkRenderPassBeginInfo begin_info = {
      .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      .pNext           = NULL,
      .framebuffer     = fb,
      .clearValueCount = clear_value_count,
      .pClearValues    = vk_clears,
      .renderPass      = render_pass,
      .renderArea      = {.offset = {0u, 0u}, .extent = render_extent}};
  buf->active_rt         = target;
  buf->renderpass_active = true;
  vkCmdBeginRenderPass(buf->active_bundle.vkcmdbuf, &begin_info, VK_SUBPASS_CONTENTS_INLINE);
}

void ngf_cmd_end_pass(ngf_render_encoder enc) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  vkCmdEndRenderPass(buf->active_bundle.vkcmdbuf);
  buf->renderpass_active = false;
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
    vkCmdDrawIndexed(cmd_buf->active_bundle.vkcmdbuf, nelements, ninstances, first_element, 0u, 0u);
  } else {
    vkCmdDraw(cmd_buf->active_bundle.vkcmdbuf, nelements, ninstances, first_element, 0u);
  }
}

void ngf_cmd_bind_gfx_pipeline(ngf_render_encoder enc, const ngf_graphics_pipeline pipeline) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);

  // If we had a pipeline bound for which there have been resources bound, but no draw call
  // executed, commit those resources to actual descriptor sets and bind them so that the next
  // pipeline is able to "see" those resources, provided that it's compatible.
  if (buf->active_pipe && buf->pending_bind_ops.size > 0u) { ngfvk_execute_pending_binds(buf); }

  buf->active_pipe = pipeline;
  vkCmdBindPipeline(
      buf->active_bundle.vkcmdbuf,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      buf->active_rt->is_default
          ? pipeline->vk_pipeline_flavors[NGFVK_PIPELINE_FLAVOR_VANILLA]
          : pipeline->vk_pipeline_flavors[NGFVK_PIPELINE_FLAVOR_RENDER_TO_TEXTURE]);
}

void ngf_cmd_bind_gfx_resources(
    ngf_render_encoder          enc,
    const ngf_resource_bind_op* bind_operations,
    uint32_t                    nbind_operations) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);

  for (uint32_t i = 0; i < nbind_operations; ++i) {
    ngfvk_bind_op_chunk_list* pending_bind_ops = &buf->pending_bind_ops;
    if (!pending_bind_ops->last || pending_bind_ops->last->last_idx >= NGFVK_BIND_OP_CHUNK_SIZE) {
      ngfvk_bind_op_chunk* prev_last = pending_bind_ops->last;
      pending_bind_ops->last       = ngfi_blkalloc_alloc(CURRENT_CONTEXT->bind_op_chunk_allocator);
      pending_bind_ops->last->next = NULL;
      pending_bind_ops->last->last_idx = 0;
      if (prev_last) {
        prev_last->next = pending_bind_ops->last;
      } else {
        pending_bind_ops->first = pending_bind_ops->last;
      }
    }
    pending_bind_ops->last->data[pending_bind_ops->last->last_idx++] = bind_operations[i];
    pending_bind_ops->size++;
  }
}

void ngf_cmd_viewport(ngf_render_encoder enc, const ngf_irect2d* r) {
  ngf_cmd_buffer   buf           = NGFVK_ENC2CMDBUF(enc);
  const bool       is_default_rt = buf->active_rt ? (buf->active_rt->is_default) : false;
  const VkViewport viewport      = {
      .x        = (float)r->x,
      .y        = is_default_rt ? (float)r->y + (float)r->height : (float)r->y,
      .width    = NGFI_MAX(1, (float)r->width),
      .height   = (is_default_rt ? -1.0f : 1.0f) * NGFI_MAX(1, (float)r->height),
      .minDepth = 0.0f,  // TODO: add depth parameter
      .maxDepth = 1.0f   // TODO: add max depth parameter
  };
  vkCmdSetViewport(buf->active_bundle.vkcmdbuf, 0u, 1u, &viewport);
}

void ngf_cmd_scissor(ngf_render_encoder enc, const ngf_irect2d* r) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  assert(buf->active_rt);
  const uint32_t target_height = (buf->active_rt->is_default)
                                     ? CURRENT_CONTEXT->swapchain_info.height
                                     : buf->active_rt->height;
  const VkRect2D scissor_rect  = {
      .offset = {r->x, ((int32_t)target_height - r->y) - (int32_t)r->height},
      .extent = {r->width, r->height}};
  vkCmdSetScissor(buf->active_bundle.vkcmdbuf, 0u, 1u, &scissor_rect);
}

void ngf_cmd_bind_attrib_buffer(
    ngf_render_encoder      enc,
    const ngf_attrib_buffer abuf,
    uint32_t                binding,
    uint32_t                offset) {
  ngf_cmd_buffer buf      = NGFVK_ENC2CMDBUF(enc);
  VkDeviceSize   vkoffset = offset;
  vkCmdBindVertexBuffers(
      buf->active_bundle.vkcmdbuf,
      binding,
      1,
      (VkBuffer*)&abuf->data.alloc.obj_handle,
      &vkoffset);
}

void ngf_cmd_bind_index_buffer(
    ngf_render_encoder     enc,
    const ngf_index_buffer ibuf,
    ngf_type               index_type) {
  ngf_cmd_buffer    buf      = NGFVK_ENC2CMDBUF(enc);
  const VkIndexType idx_type = get_vk_index_type(index_type);
  assert(idx_type == VK_INDEX_TYPE_UINT16 || idx_type == VK_INDEX_TYPE_UINT32);
  vkCmdBindIndexBuffer(
      buf->active_bundle.vkcmdbuf,
      (VkBuffer)ibuf->data.alloc.obj_handle,
      0u,
      idx_type);
}

void ngf_cmd_copy_attrib_buffer(
    ngf_xfer_encoder        enc,
    const ngf_attrib_buffer src,
    ngf_attrib_buffer       dst,
    size_t                  size,
    size_t                  src_offset,
    size_t                  dst_offset) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  assert(buf);
  ngfvk_cmd_copy_buffer(
      buf->active_bundle.vkcmdbuf,
      (VkBuffer)src->data.alloc.obj_handle,
      (VkBuffer)dst->data.alloc.obj_handle,
      size,
      src_offset,
      dst_offset,
      VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
}

void ngf_cmd_copy_index_buffer(
    ngf_xfer_encoder       enc,
    const ngf_index_buffer src,
    ngf_index_buffer       dst,
    size_t                 size,
    size_t                 src_offset,
    size_t                 dst_offset) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  assert(buf);
  ngfvk_cmd_copy_buffer(
      buf->active_bundle.vkcmdbuf,
      (VkBuffer)src->data.alloc.obj_handle,
      (VkBuffer)dst->data.alloc.obj_handle,
      size,
      src_offset,
      dst_offset,
      VK_ACCESS_INDEX_READ_BIT,
      VK_PIPELINE_STAGE_VERTEX_INPUT_BIT);
}

void ngf_cmd_copy_uniform_buffer(
    ngf_xfer_encoder         enc,
    const ngf_uniform_buffer src,
    ngf_uniform_buffer       dst,
    size_t                   size,
    size_t                   src_offset,
    size_t                   dst_offset) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  assert(buf);
  ngfvk_cmd_copy_buffer(
      buf->active_bundle.vkcmdbuf,
      (VkBuffer)src->data.alloc.obj_handle,
      (VkBuffer)dst->data.alloc.obj_handle,
      size,
      src_offset,
      dst_offset,
      VK_ACCESS_UNIFORM_READ_BIT,
      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
}

void ngf_cmd_write_image(
    ngf_xfer_encoder       enc,
    const ngf_pixel_buffer src,
    size_t                 src_offset,
    ngf_image_ref          dst,
    const ngf_offset3d*    offset,
    const ngf_extent3d*    extent) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  assert(buf);
  const uint32_t dst_layer =
      dst.image->type == NGF_IMAGE_TYPE_CUBE ? 6u * dst.layer + dst.cubemap_face : dst.layer;
  const VkImageMemoryBarrier pre_xfer_barrier = {
      .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext               = NULL,
      .srcAccessMask       = VK_ACCESS_SHADER_READ_BIT,
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
          .layerCount     = 1u}};
  vkCmdPipelineBarrier(
      buf->active_bundle.vkcmdbuf,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
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
           .layerCount     = 1u},
      .imageOffset = {.x = offset->x, .y = offset->y, .z = offset->z},
      .imageExtent = {.width = extent->width, .height = extent->height, .depth = extent->depth}};
  vkCmdCopyBufferToImage(
      buf->active_bundle.vkcmdbuf,
      (VkBuffer)src->data.alloc.obj_handle,
      (VkImage)dst.image->alloc.obj_handle,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      1u,
      &copy_op);
  const VkImageMemoryBarrier post_xfer_barrier = {
      .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext               = NULL,
      .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask       = VK_ACCESS_SHADER_READ_BIT,
      .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image               = (VkImage)dst.image->alloc.obj_handle,
      .subresourceRange    = {
          .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel   = dst.mip_level,
          .levelCount     = 1u,
          .baseArrayLayer = dst_layer,
          .layerCount     = 1u}};
  vkCmdPipelineBarrier(
      buf->active_bundle.vkcmdbuf,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
      0u,
      0u,
      NULL,
      0u,
      NULL,
      1u,
      &post_xfer_barrier);
}

static ngf_error ngfvk_create_buffer(
    size_t                size,
    VkBufferUsageFlags    vk_usage_flags,
    uint32_t              vma_usage_flags,
    VkMemoryPropertyFlags vk_mem_flags,
    ngfvk_alloc*          alloc) {
  const VkBufferCreateInfo buf_vk_info = {
      .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext                 = NULL,
      .flags                 = 0u,
      .size                  = size,
      .usage                 = vk_usage_flags,
      .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices   = NULL};

  const VmaAllocationCreateInfo buf_alloc_info = {
      .flags          = 0u,
      .usage          = vma_usage_flags,
      .requiredFlags  = vk_mem_flags,
      .preferredFlags = 0u,
      .memoryTypeBits = 0u,
      .pool           = VK_NULL_HANDLE,
      .pUserData      = NULL};

  VkResult vkresult = vmaCreateBuffer(
      CURRENT_CONTEXT->allocator,
      &buf_vk_info,
      &buf_alloc_info,
      (VkBuffer*)&alloc->obj_handle,
      &alloc->vma_alloc,
      NULL);
  alloc->parent_allocator = CURRENT_CONTEXT->allocator;
  return (vkresult == VK_SUCCESS) ? NGF_ERROR_OK : NGF_ERROR_INVALID_OPERATION;
}

ngf_error ngf_create_attrib_buffer(const ngf_attrib_buffer_info* info, ngf_attrib_buffer* result) {
  assert(info);
  assert(result);
  ngf_attrib_buffer buf = NGFI_ALLOC(ngf_attrib_buffer_t);
  *result               = buf;

  if (buf == NULL) return NGF_ERROR_OUT_OF_MEM;

  const VkBufferUsageFlags vk_usage_flags =
      get_vk_buffer_usage(info->buffer_usage) | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  const VkMemoryPropertyFlags vk_mem_flags    = get_vk_memory_flags(info->storage_type);
  const uint32_t              vma_usage_flags = info->storage_type == NGF_BUFFER_STORAGE_PRIVATE
                                                    ? VMA_MEMORY_USAGE_GPU_ONLY
                                                    : VMA_MEMORY_USAGE_CPU_ONLY;

  ngf_error err = ngfvk_create_buffer(
      info->size,
      vk_usage_flags,
      vma_usage_flags,
      vk_mem_flags,
      &buf->data.alloc);

  if (err != NGF_ERROR_OK) {
    NGFI_FREE(buf);
  } else {
    buf->data.size = info->size;
  }

  return err;
}

void ngf_destroy_attrib_buffer(ngf_attrib_buffer buffer) {
  if (buffer) {
    ngfvk_buffer_retire(buffer->data);
    NGFI_FREE(buffer);
  }
}

void* ngf_attrib_buffer_map_range(
    ngf_attrib_buffer buf,
    size_t            offset,
    size_t            size,
    uint32_t          flags) {
  NGFI_IGNORE_VAR(size);
  NGFI_IGNORE_VAR(flags);
  return ngfvk_map_buffer(&buf->data, offset);
}

void ngf_attrib_buffer_flush_range(ngf_attrib_buffer buf, size_t offset, size_t size) {
  ngfvk_flush_buffer(&buf->data, offset, size);
}

void ngf_attrib_buffer_unmap(ngf_attrib_buffer buf) {
  ngfvk_unmap_buffer(&buf->data);
}

ngf_error ngf_create_index_buffer(const ngf_index_buffer_info* info, ngf_index_buffer* result) {
  assert(info);
  assert(result);
  ngf_index_buffer buf = NGFI_ALLOC(ngf_index_buffer_t);
  *result              = buf;

  if (buf == NULL) return NGF_ERROR_OUT_OF_MEM;

  const VkBufferUsageFlags vk_usage_flags =
      get_vk_buffer_usage(info->buffer_usage) | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  const VkMemoryPropertyFlags vk_mem_flags    = get_vk_memory_flags(info->storage_type);
  const uint32_t              vma_usage_flags = info->storage_type == NGF_BUFFER_STORAGE_PRIVATE
                                                    ? VMA_MEMORY_USAGE_GPU_ONLY
                                                    : VMA_MEMORY_USAGE_CPU_ONLY;

  ngf_error err = ngfvk_create_buffer(
      info->size,
      vk_usage_flags,
      vma_usage_flags,
      vk_mem_flags,
      &buf->data.alloc);

  if (err != NGF_ERROR_OK) {
    NGFI_FREE(buf);
  } else {
    buf->data.size = info->size;
  }
  return err;
}

void ngf_destroy_index_buffer(ngf_index_buffer buffer) {
  if (buffer) {
    ngfvk_buffer_retire(buffer->data);
    NGFI_FREE(buffer);
  }
}

void* ngf_index_buffer_map_range(ngf_index_buffer buf, size_t offset, size_t size, uint32_t flags) {
  NGFI_IGNORE_VAR(size);
  NGFI_IGNORE_VAR(flags);
  return ngfvk_map_buffer(&buf->data, offset);
}

void ngf_index_buffer_flush_range(ngf_index_buffer buf, size_t offset, size_t size) {
  ngfvk_flush_buffer(&buf->data, offset, size);
}

void ngf_index_buffer_unmap(ngf_index_buffer buf) {
  ngfvk_unmap_buffer(&buf->data);
}

ngf_error
ngf_create_uniform_buffer(const ngf_uniform_buffer_info* info, ngf_uniform_buffer* result) {
  assert(info);
  assert(result);
  ngf_uniform_buffer buf = NGFI_ALLOC(ngf_uniform_buffer_t);
  *result                = buf;

  if (buf == NULL) return NGF_ERROR_OUT_OF_MEM;

  const VkBufferUsageFlags vk_usage_flags =
      get_vk_buffer_usage(info->buffer_usage) | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  const VkMemoryPropertyFlags vk_mem_flags    = get_vk_memory_flags(info->storage_type);
  const uint32_t              vma_usage_flags = info->storage_type == NGF_BUFFER_STORAGE_PRIVATE
                                                    ? VMA_MEMORY_USAGE_GPU_ONLY
                                                    : VMA_MEMORY_USAGE_CPU_ONLY;

  ngf_error err = ngfvk_create_buffer(
      info->size,
      vk_usage_flags,
      vma_usage_flags,
      vk_mem_flags,
      &buf->data.alloc);

  if (err != NGF_ERROR_OK) {
    NGFI_FREE(buf);
  } else {
    buf->data.size = info->size;
  }
  return err;
}

void ngf_destroy_uniform_buffer(ngf_uniform_buffer buffer) {
  if (buffer) {
    ngfvk_buffer_retire(buffer->data);
    NGFI_FREE(buffer);
  }
}

void* ngf_uniform_buffer_map_range(
    ngf_uniform_buffer buf,
    size_t             offset,
    size_t             size,
    uint32_t           flags) {
  NGFI_IGNORE_VAR(size);
  NGFI_IGNORE_VAR(flags);
  return ngfvk_map_buffer(&buf->data, offset);
}

void ngf_uniform_buffer_flush_range(ngf_uniform_buffer buf, size_t offset, size_t size) {
  ngfvk_flush_buffer(&buf->data, offset, size);
}

void ngf_uniform_buffer_unmap(ngf_uniform_buffer buf) {
  ngfvk_unmap_buffer(&buf->data);
}

ngf_error ngf_create_pixel_buffer(const ngf_pixel_buffer_info* info, ngf_pixel_buffer* result) {
  assert(info);
  assert(result);
  ngf_pixel_buffer buf = NGFI_ALLOC(ngf_pixel_buffer_t);
  *result              = buf;

  if (buf == NULL) return NGF_ERROR_OUT_OF_MEM;

  ngf_error err = ngfvk_create_buffer(
      info->size,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VMA_MEMORY_USAGE_CPU_ONLY,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
      &buf->data.alloc);

  if (err != NGF_ERROR_OK) {
    NGFI_FREE(buf);
  } else {
    buf->data.size = info->size;
  }
  return err;
}

void ngf_destroy_pixel_buffer(ngf_pixel_buffer buffer) {
  if (buffer) {
    ngfvk_buffer_retire(buffer->data);
    NGFI_FREE(buffer);
  }
}

void* ngf_pixel_buffer_map_range(ngf_pixel_buffer buf, size_t offset, size_t size, uint32_t flags) {
  NGFI_IGNORE_VAR(size);
  NGFI_IGNORE_VAR(flags);
  return ngfvk_map_buffer(&buf->data, offset);
}

void ngf_pixel_buffer_flush_range(ngf_pixel_buffer buf, size_t offset, size_t size) {
  ngfvk_flush_buffer(&buf->data, offset, size);
}

void ngf_pixel_buffer_unmap(ngf_pixel_buffer buf) {
  ngfvk_unmap_buffer(&buf->data);
}

ngf_error ngf_create_image(const ngf_image_info* info, ngf_image* result) {
  assert(info);
  assert(result);

  const bool is_sampled_from  = info->usage_hint & NGF_IMAGE_USAGE_SAMPLE_FROM;
  const bool is_xfer_dst      = info->usage_hint & NGF_IMAGE_USAGE_XFER_DST;
  const bool is_attachment    = info->usage_hint & NGF_IMAGE_USAGE_ATTACHMENT;
  const bool is_transient     = info->usage_hint & NGFVK_IMAGE_USAGE_TRANSIENT_ATTACHMENT;
  const bool is_depth_stencil = info->format == NGF_IMAGE_FORMAT_DEPTH24_STENCIL8;
  const bool is_depth_only =
      info->format == NGF_IMAGE_FORMAT_DEPTH32 || info->format == NGF_IMAGE_FORMAT_DEPTH16;

  const VkImageUsageFlagBits attachment_usage_bits =
      is_depth_only || is_depth_stencil ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                                        : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  const VkImageUsageFlagBits usage_flags =
      (is_sampled_from ? VK_IMAGE_USAGE_SAMPLED_BIT : 0u) |
      (is_attachment ? attachment_usage_bits : 0u) |
      (is_transient ? VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT : 0) |
      (is_xfer_dst ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0u);

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
      .arrayLayers           = !is_cubemap ? 1u : 6u,  // TODO: layered images
      .samples               = get_vk_sample_count(info->sample_count),
      .usage                 = usage_flags,
      .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
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
      get_vk_image_view_type(info->type, info->extent.depth),
      vk_image_info.format,
      vk_image_info.mipLevels,
      vk_image_info.arrayLayers,
      &img->vkview);

  if (err != NGF_ERROR_OK) { goto ngf_create_image_cleanup; }

  const VkImageMemoryBarrier barrier = {
      .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext         = NULL,
      .srcAccessMask = 0u,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      .oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout           = is_sampled_from
                                 ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                 : (is_depth_only
                                        ? VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
                                        : (is_depth_stencil ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                                                            : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)),
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image               = (VkImage)img->alloc.obj_handle,
      .subresourceRange    = {
          .aspectMask     = is_depth_only ? VK_IMAGE_ASPECT_DEPTH_BIT
                                             : (is_depth_stencil ? (VK_IMAGE_ASPECT_DEPTH_BIT |
                                                             VK_IMAGE_ASPECT_STENCIL_BIT)
                                                                 : VK_IMAGE_ASPECT_COLOR_BIT),
          .baseMipLevel   = 0u,
          .levelCount     = vk_image_info.mipLevels,
          .baseArrayLayer = 0u,
          .layerCount     = vk_image_info.arrayLayers}};

  pthread_mutex_lock(&NGFVK_PENDING_IMG_BARRIER_QUEUE.lock);
  NGFI_DARRAY_APPEND(NGFVK_PENDING_IMG_BARRIER_QUEUE.barriers, barrier);
  pthread_mutex_unlock(&NGFVK_PENDING_IMG_BARRIER_QUEUE.lock);

ngf_create_image_cleanup:
  if (err != NGF_ERROR_OK) { ngf_destroy_image(img); }
  return err;
}

void ngf_destroy_image(ngf_image img) {
  if (img != NULL) {
    if (img->alloc.obj_handle != VK_NULL_HANDLE) {
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
      .addressModeU            = get_vk_address_mode(info->wrap_s),
      .addressModeV            = get_vk_address_mode(info->wrap_t),
      .addressModeW            = get_vk_address_mode(info->wrap_r),
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

#pragma endregion
