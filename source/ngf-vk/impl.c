/**
 * Copyright (c) 2025 nicegraf contributors
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

#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef __clang__
#pragma clang diagnostic ignored "-Wnullability-completeness"
#if __has_warning("-Wcast-function-type-mismatch")
#pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
#endif
#endif

#include "ngf-common/block-alloc.h"
#include "ngf-common/chunk-list.h"
#include "ngf-common/cmdbuf-state.h"
#include "ngf-common/dict.h"
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
#define NGFVK_RENDER_CMD_CHUNK_SIZE            (128u)
#define NGFVK_MAX_COLOR_ATTACHMENTS            (16u)
#define NGFVK_IMAGE_USAGE_TRANSIENT_ATTACHMENT (1u << 31u)

#pragma endregion

#pragma region internal_struct_definitions

// Type of object in the Vulkan object retirement queue.
typedef enum ngfvk_retire_obj {
  NGFVK_RETIRE_OBJ_PIPELINE = 0,
  NGFVK_RETIRE_OBJ_PIPELINE_LAYOUT,
  NGFVK_RETIRE_OBJ_DSET_LAYOUT,
  NGFVK_RETIRE_OBJ_CMDBUF_WITH_POOL,
  NGFVK_RETIRE_OBJ_FRAMEBUFFER,
  NGFVK_RETIRE_OBJ_RENDER_PASS,
  NGFVK_RETIRE_OBJ_SAMPLER,
  NGFVK_RETIRE_OBJ_IMG_VIEW,
  NGFVK_RETIRE_OBJ_BUF_VIEW,
  NGFVK_RETIRE_OBJ_IMG,
  NGFVK_RETIRE_OBJ_BUF,
  NGFVK_RETIRE_OBJ_DESC_POOL_LIST,
  NGFVK_RETIRE_OBJ_COUNT
} ngfvk_retire_obj;

// Function pointer for Vulkan object destructors.
typedef void (*ngfvk_retire_obj_dtor)(ngfi_chnk_hdr*);

typedef struct ngfvk_dummy_resources {
  ngf_image              img;
  ngf_image              cube;
  ngf_buffer             buf;
  ngf_texel_buffer_view  tbuf;
  ngf_sampler            samp;
  VkAccelerationStructureKHR dummy_accel_struct;
  VkDescriptorImageInfo  img_info;
  VkDescriptorImageInfo  cube_info;
  VkDescriptorImageInfo  img_arr_info;
  VkDescriptorImageInfo  cube_arr_info;
  VkDescriptorImageInfo  samp_info;
  VkDescriptorImageInfo  imgsamp_info;
  VkDescriptorImageInfo  imgsamp_arr_info;
  VkDescriptorBufferInfo buf_info;
  pthread_mutex_t        img_mu;
  bool                   image_transitioned;
} ngfvk_dummy_resources;

// Singleton for holding vulkan instance, device and queue handles.
// This is shared by all contexts.
struct {
  VkInstance               instance;
  VkPhysicalDevice         phys_dev;
  VkDevice                 device;
  VmaAllocator             allocator;
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
  ngfvk_retire_obj_dtor retire_obj_dtors[NGFVK_RETIRE_OBJ_COUNT];
  ngfvk_dummy_resources dummy_res;
  bool                     ray_tracing_enabled;
} _vk;

// Singleton for holding on to RenderDoc API
struct {
  RENDERDOC_API_1_6_0* api;
  bool                 capture_next;
  bool                 is_capturing;
} _renderdoc;

// Swapchain state.
typedef struct ngfvk_swapchain {
  VkSwapchainKHR   vk_swapchain;
  VkImage*         imgs;
  ngf_image*       wrapper_imgs;
  ngf_image*       multisample_imgs;
  VkImageView*     img_views;
  VkImageView*     multisample_img_views;
  VkSemaphore*     img_sems;
  VkFramebuffer*   framebufs;
  VkPresentModeKHR present_mode;
  ngf_image        depth_img;
  uint32_t         nimgs;      // < Total number of images in the swapchain.
  uint32_t         image_idx;  // < The index of currently acquired image.
  uint32_t         width;
  uint32_t         height;
} ngfvk_swapchain;

typedef struct {
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

typedef struct ngfvk_desc_binding {
  VkDescriptorType     type;
  VkPipelineStageFlags stage_accessors;
  bool                 readonly;
  bool                 is_multilayered_image;
  bool                 is_cubemap;
  uint32_t             ndescs_in_binding;
} ngfvk_desc_binding;

typedef struct ngfvk_desc_set_layout {
  VkDescriptorSetLayout vk_handle;
  ngfvk_desc_count      counts;
  uint32_t              nall_bindings;  // < Number of ALL bindings (incl. unused ones).
  uint32_t              nall_descs; // < Total number of descriptors across all bindings.
  ngfvk_desc_binding*   binding_properties;
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

// Vulkan resources associated with a given frame.
typedef struct ngfvk_frame_resources {
  NGFI_DARRAY_OF(ngf_cmd_buffer) submitted_cmd_bufs;  // < Submitted ngf command buffers.
  VkSemaphore semaphore;  // < Signalled when the last cmd buffer finishes.

  // Resources that should be disposed of at some point after this
  // frame's completion.
  ngfi_chnklist retire_objs[NGFVK_RETIRE_OBJ_COUNT];

  // Fences that will be signaled at the end of the frame.
  VkFence fences[2];

  // Number of fences to wait on to complete all submissions related to this
  // frame.
  uint32_t nwait_fences;
} ngfvk_frame_resources;

typedef struct {
  VkCommandPool* cmd_pools;
  uint16_t       ctx_id;
  uint8_t        num_pools;
} ngfvk_command_superpool;

typedef struct ngfvk_cmd_buf_with_pool {
  VkCommandBuffer cmd_buf;
  VkCommandPool   cmd_pool;
} ngfvk_cmd_buf_with_pool;

typedef struct ngfvk_attachment_pass_desc {
  VkImageLayout       layout;
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

// Describes how a resource is accessed within a synchronization scope.
typedef struct ngfvk_sync_barrier_masks {
  VkAccessFlags        access_mask;  // < Ways in which the resource is accessed.
  VkPipelineStageFlags stage_mask;   // < Pipeline stages that have access to the resource.
} ngfvk_sync_barrier_masks;

// Synchronization request, that describes the intent to access a resource.
typedef struct ngfvk_sync_req {
  ngfvk_sync_barrier_masks barrier_masks;  // < Access/stage masks.
  VkImageLayout            layout;         // < For image resources only, current layout.
} ngfvk_sync_req;

// Synchronization state of a resource within the context of a single command buffer.
typedef struct ngfvk_sync_state {
  ngfvk_sync_barrier_masks last_writer_masks;
  ngfvk_sync_barrier_masks active_readers_masks;
  uint32_t                 per_stage_readers_mask;
  VkImageLayout            layout;
} ngfvk_sync_state;

// Type of synchronized resource.
typedef enum ngfvk_sync_res_type {
  NGFVK_SYNC_RES_BUFFER,
  NGFVK_SYNC_RES_IMAGE,
  NGFVK_SYNC_RES_COUNT
} ngfvk_sync_res_type;

// Tagged union for passing around handles to synchronized GPU resources in a generic way.
typedef struct ngfvk_sync_res {
  union {
    ngf_image  img;
    ngf_buffer buf;
  } data;
  ngfvk_sync_res_type type;
  uint64_t            hash;
} ngfvk_sync_res;

// Data associated with a particular synchronized resource within the context of a single cmd
// buffer.
typedef struct ngfvk_sync_res_data {
  ngfvk_sync_req      expected_sync_req;  // < Expected sync state.
  ngfvk_sync_state    sync_state;         // < Latest synchronization state.
  uint32_t            pending_sync_req_idx;
  ngfvk_sync_res_type res_type;
  uintptr_t           res_handle;
  bool                had_barrier;
} ngfvk_sync_res_data;

typedef struct ngfvk_sync_req_batch {
  ngfi_dict_keyhash* sync_res_data_keys;
  ngfvk_sync_req*    pending_sync_reqs;
  bool*              freshness;
  uint32_t           npending_sync_reqs;
  uint32_t           nbuffer_sync_reqs;
  uint32_t           nimage_sync_reqs;
} ngfvk_sync_req_batch;

typedef enum ngfvk_render_cmd_type {
  NGFVK_RENDER_CMD_BIND_PIPELINE,
  NGFVK_RENDER_CMD_SET_VIEWPORT,
  NGFVK_RENDER_CMD_SET_SCISSOR,
  NGFVK_RENDER_CMD_SET_STENCIL_REFERENCE,
  NGFVK_RENDER_CMD_SET_STENCIL_COMPARE_MASK,
  NGFVK_RENDER_CMD_SET_STENCIL_WRITE_MASK,
  NGFVK_RENDER_CMD_BIND_RESOURCE,
  NGFVK_RENDER_CMD_BIND_ATTRIB_BUFFER,
  NGFVK_RENDER_CMD_BIND_INDEX_BUFFER,
  NGFVK_RENDER_CMD_SET_DEPTH_BIAS,
  NGFVK_RENDER_CMD_DRAW,
} ngfvk_render_cmd_type;

typedef struct ngfvk_barrier_data {
  VkAccessFlags        src_access_mask;
  VkAccessFlags        dst_access_mask;
  VkPipelineStageFlags src_stage_mask;
  VkPipelineStageFlags dst_stage_mask;
  VkImageLayout        src_layout;
  VkImageLayout        dst_layout;
  ngfvk_sync_res       res;
} ngfvk_barrier_data;

typedef struct ngfvk_render_cmd {
  union {
    ngf_graphics_pipeline pipeline;
    ngf_irect2d           rect;
    struct {
      uint32_t front;
      uint32_t back;
    } stencil_values;
    ngf_resource_bind_op bind_resource;
    struct {
      ngf_buffer buffer;
      uint32_t   binding;
      size_t     offset;
    } bind_attrib_buffer;
    struct {
      ngf_buffer buffer;
      size_t     offset;
      ngf_type   type;
    } bind_index_buffer;
    struct {
      uint32_t first_element;
      uint32_t nelements;
      uint32_t ninstances;
      bool     indexed;
    } draw;
    struct {
        float const_factor;
        float slope_factor;
        float clamp;
    } depth_bias;
  } data;
  ngfvk_render_cmd_type type : 8;
} ngfvk_render_cmd;

typedef struct ngfvk_pending_barrier_list {
  ngfi_chnklist chnklist;
  uint32_t      npending_img_bars;
  uint32_t      npending_buf_bars;
} ngfvk_pending_barrier_list;

typedef struct ngfvk_reflect_binding_and_stage_mask {
  SpvReflectDescriptorBinding binding_data;
  VkPipelineStageFlags        mask;
} ngfvk_reflect_binding_and_stage_mask;

#pragma endregion

#pragma region external_struct_definitions

typedef struct ngf_cmd_buffer_t {
  ngf_frame_token        parent_frame;         // < The frame this cmd buffer is associated with.
  VkCommandBuffer        vk_cmd_buffer;        // < Active vulkan command buffer.
  VkCommandPool          vk_cmd_pool;          // < Active vulkan command pool.
  ngf_graphics_pipeline  active_gfx_pipe;      // < The bound graphics pipeline.
  ngf_compute_pipeline   active_compute_pipe;  // < The bound compute pipeline.
  ngf_render_target      active_rt;            // < Active render target.
  ngf_buffer             active_attr_buf;
  ngf_buffer             active_idx_buf;
  ngfvk_desc_pools_list* desc_pools_list;  // < List of descriptor pools used in the buffer's frame.
  ngfi_chnklist          pending_bind_ops;  // < Bind ops to be performed before the next draw.
  ngfi_chnklist          in_pass_cmd_chnks;
  ngfi_chnklist          virt_bind_ops_ranges;
  ngfvk_pending_barrier_list pending_barriers;
  ngfi_dict                  local_res_states;
  ngf_render_pass_info       pending_render_pass_info;  // < describes the active render pass
  uint32_t                   npending_bind_ops;
  uint16_t                   pending_clear_value_count;
  ngfi_cmd_buffer_state      state;  // < State of the cmd buffer (i.e. new/recording/etc.)
  bool                       renderpass_active : 1;    // < Has an active renderpass.
  bool                       compute_pass_active : 1;  // < Has an active compute pass.
  bool                       destroy_on_submit : 1;    // < Destroy after submitting.
} ngf_cmd_buffer_t;

typedef struct ngf_sampler_t {
  VkSampler vksampler;
} ngf_sampler_t;

typedef struct ngf_buffer_t {
  ngfvk_alloc             alloc;
  size_t                  size;
  size_t                  mapped_offset;
  ngfvk_sync_state        sync_state;
  uint64_t                hash;
  uint32_t                usage_flags;
  ngf_buffer_storage_type storage_type;
} ngf_buffer_t;

typedef struct ngf_texel_buffer_view_t {
  VkBufferView vk_buf_view;
  ngf_buffer   buffer;
} ngf_texel_buffer_view_t;

typedef struct ngf_image_t {
  ngfvk_alloc      alloc;
  VkImageView      vkview;
  VkImageView      vkview_arrayed;
  VkFormat         vk_fmt;
  ngf_extent3d     extent;
  ngf_image_type   type;
  ngfvk_sync_state sync_state;
  uint64_t         hash;
  uint32_t         usage_flags;
  uint32_t         nlevels;
  uint32_t         nlayers;
  bool             owns_backing_resource;
} ngf_image_t;

typedef struct ngf_image_view_t {
  VkImageView vk_view;
  ngf_image   src;
} ngf_image_view_t;

typedef struct ngf_context_t {
  ngfvk_frame_resources*      frame_res;
  ngfvk_swapchain             swapchain;
  ngf_swapchain_info          swapchain_info;
  VkSurfaceKHR                surface;
  uint32_t                    frame_id;
  uint32_t                    max_inflight_frames;
  ngfi_block_allocator*       blkalloc;
  ngf_frame_token             current_frame_token;
  ngf_attachment_descriptions default_attachment_descriptions_list;
  ngf_render_target           default_render_target;
  uint64_t                    cmd_buffer_counter;
  NGFI_DARRAY_OF(ngfvk_command_superpool) command_superpools;
  NGFI_DARRAY_OF(ngfvk_desc_superpool) desc_superpools;
  NGFI_DARRAY_OF(ngfvk_renderpass_cache_entry) renderpass_cache;
  bool being_destroyed;
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
  ngf_image*                  attachment_images;      /* unused in default RT, set to NULL. */
  ngfvk_attachment_pass_desc* attachment_compat_pass_descs;
  bool                        is_default;
  bool                        have_resolve_attachments;
  uint32_t                    width;
  uint32_t                    height;
} ngf_render_target_t;

#pragma endregion

#pragma region global_vars

NGFI_THREADLOCAL ngf_context CURRENT_CONTEXT = NULL;

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
      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR
  };
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
      VK_ATTACHMENT_STORE_OP_DONT_CARE,
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
      VK_FORMAT_R8G8_SNORM,
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
      VK_FORMAT_BC6H_SFLOAT_BLOCK,
      VK_FORMAT_BC6H_UFLOAT_BLOCK,
      VK_FORMAT_BC5_UNORM_BLOCK,
      VK_FORMAT_BC5_SNORM_BLOCK,
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
  if (usage & NGF_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  if (usage & NGF_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT) flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;
  if (usage & NGF_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT) flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
  return flags;
}

static VkMemoryPropertyFlags get_vk_memory_flags(ngf_buffer_storage_type s) {
  switch (s) {
  case NGF_BUFFER_STORAGE_HOST_READABLE:
    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  case NGF_BUFFER_STORAGE_HOST_WRITEABLE:
  case NGF_BUFFER_STORAGE_HOST_READABLE_WRITEABLE:
    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  case NGF_BUFFER_STORAGE_DEVICE_LOCAL:
    return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  case NGF_BUFFER_STORAGE_DEVICE_LOCAL_HOST_WRITEABLE:
    return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  case NGF_BUFFER_STORAGE_DEVICE_LOCAL_HOST_READABLE_WRITEABLE:
    return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
           VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  }
  return 0;
}

static VmaAllocatorCreateFlags ngfvk_get_vma_alloc_flags(ngf_buffer_storage_type storage_type) {
  switch (storage_type) {
  case NGF_BUFFER_STORAGE_HOST_WRITEABLE:
    return VMA_ALLOCATION_CREATE_MAPPED_BIT |
           VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
  case NGF_BUFFER_STORAGE_HOST_READABLE:
  case NGF_BUFFER_STORAGE_HOST_READABLE_WRITEABLE:
    return VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
  case NGF_BUFFER_STORAGE_DEVICE_LOCAL:
    return 0;
  case NGF_BUFFER_STORAGE_DEVICE_LOCAL_HOST_WRITEABLE:
    return VMA_ALLOCATION_CREATE_MAPPED_BIT |
           VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
           VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
  case NGF_BUFFER_STORAGE_DEVICE_LOCAL_HOST_READABLE_WRITEABLE:
    return VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
           VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
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

static VkColorSpaceKHR get_vk_color_space(ngf_colorspace colorspace) {
  static VkColorSpaceKHR color_spaces[NGF_COLORSPACE_COUNT] = {
      VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
      VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT,
      VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,
      VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT,
      VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT,
      VK_COLOR_SPACE_DCI_P3_NONLINEAR_EXT,
      VK_COLOR_SPACE_BT2020_LINEAR_EXT,
      VK_COLOR_SPACE_HDR10_ST2084_EXT};
  return color_spaces[colorspace];
}

#pragma endregion  // vk_enum_maps

#pragma region internal_funcs

#define NGFVK_RETIRE_OBJECT(frame_res, obj_type_idx, obj) \
  ngfi_chnklist_append(&((frame_res)->retire_objs[(obj_type_idx)]), &(obj), sizeof(obj))

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

static inline uint64_t ngfvk_ptr_hash(void* data) {
  uint64_t mmh3_out[2] = {0, 0};
  ngfi_mmh3_x64_128((uintptr_t)data, 0x9e3779b9, mmh3_out);
  return mmh3_out[0] ^ mmh3_out[1];
}

static ngf_error ngfvk_create_image(
    const ngf_image_info* info,
    const ngfvk_alloc*    backing_resource_alloc,
    bool                  owns_backing_resource,
    ngf_image*            result) {
  ngf_error err = NGF_ERROR_OK;

  *result = NGFI_ALLOC(ngf_image_t);
  if (*result == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngfvk_create_image_exit;
  }

  const bool is_cubemap            = info->type == NGF_IMAGE_TYPE_CUBE;
  (*result)->alloc                 = *backing_resource_alloc;
  (*result)->extent.width          = NGFI_MAX(1, info->extent.width);
  (*result)->extent.height         = NGFI_MAX(1, info->extent.height);
  (*result)->extent.depth          = NGFI_MAX(1, info->extent.depth);
  (*result)->nlayers               = info->nlayers * (is_cubemap ? 6u : 1u);
  (*result)->nlevels               = info->nmips;
  (*result)->type                  = info->type;
  (*result)->usage_flags           = info->usage_hint;
  (*result)->vk_fmt                = get_vk_image_format(info->format);
  (*result)->owns_backing_resource = owns_backing_resource;
  memset(&(*result)->sync_state, 0, sizeof((*result)->sync_state));
  (*result)->sync_state.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  (*result)->hash              = ngfvk_ptr_hash(*result);

  if (owns_backing_resource) {
    err = ngfvk_create_vk_image_view(
        (VkImage)(*result)->alloc.obj_handle,
        get_vk_image_view_type(info->type, info->nlayers),
        (*result)->vk_fmt,
        (*result)->nlevels,
        (*result)->nlayers,
        &(*result)->vkview);
    err = ngfvk_create_vk_image_view(
        (VkImage)(*result)->alloc.obj_handle,
        get_vk_image_view_type(info->type, 2u), // force _ARRAY type view
        (*result)->vk_fmt,
        (*result)->nlevels,
        (*result)->nlayers,
        &(*result)->vkview_arrayed);
  }

  if (err != NGF_ERROR_OK) { goto ngfvk_create_image_exit; }

ngfvk_create_image_exit:
  return err;
}

static void ngfvk_destroy_swapchain(ngfvk_swapchain* swapchain) {
  if (swapchain) {
    vkDeviceWaitIdle(_vk.device);

    for (uint32_t s = 0u; swapchain->img_sems != NULL && s < swapchain->nimgs; ++s) {
      if (swapchain->img_sems[s] != VK_NULL_HANDLE) {
        vkDestroySemaphore(_vk.device, swapchain->img_sems[s], NULL);
      }
    }
    if (swapchain->img_sems != NULL) { NGFI_FREEN(swapchain->img_sems, swapchain->nimgs); }

    for (uint32_t f = 0u; swapchain->framebufs && f < swapchain->nimgs; ++f) {
      vkDestroyFramebuffer(_vk.device, swapchain->framebufs[f], NULL);
    }
    if (swapchain->framebufs != NULL) { NGFI_FREEN(swapchain->framebufs, swapchain->nimgs); }

    for (uint32_t v = 0u; swapchain->img_views != NULL && v < swapchain->nimgs; ++v) {
      vkDestroyImageView(_vk.device, swapchain->img_views[v], NULL);
    }
    if (swapchain->img_views) { NGFI_FREEN(swapchain->img_views, swapchain->nimgs); }

    for (uint32_t v = 0u; swapchain->multisample_img_views != NULL && v < swapchain->nimgs; ++v) {
      vkDestroyImageView(_vk.device, swapchain->multisample_img_views[v], NULL);
    }
    if (swapchain->multisample_img_views) {
      NGFI_FREEN(swapchain->multisample_img_views, swapchain->nimgs);
    }

    for (uint32_t i = 0u; swapchain->multisample_imgs && i < swapchain->nimgs; ++i) {
      ngf_destroy_image(swapchain->multisample_imgs[i]);
    }
    if (swapchain->multisample_imgs) { NGFI_FREEN(swapchain->multisample_imgs, swapchain->nimgs); }

    if (swapchain->vk_swapchain != VK_NULL_HANDLE) {
      vkDestroySwapchainKHR(_vk.device, swapchain->vk_swapchain, NULL);
    }

    if (swapchain->imgs) { NGFI_FREEN(swapchain->imgs, swapchain->nimgs); }
    if (swapchain->wrapper_imgs) { NGFI_FREEN(swapchain->wrapper_imgs, swapchain->nimgs); }

    if (swapchain->depth_img) { ngf_destroy_image(swapchain->depth_img); }
  }
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
      .imageColorSpace = get_vk_color_space(swapchain_info->colorspace),
      .imageExtent =
          {.width = NGFI_MIN(
               max_surface_extent.width,
               NGFI_MAX(min_surface_extent.width, swapchain_info->width)),
           .height = NGFI_MIN(
               max_surface_extent.height,
               NGFI_MAX(min_surface_extent.height, swapchain_info->height))},
      .imageArrayLayers      = 1,
      .imageUsage            = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (swapchain_info->enable_compute_access ? VK_IMAGE_USAGE_STORAGE_BIT : 0),
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
  vk_err = vkGetSwapchainImagesKHR(_vk.device, swapchain->vk_swapchain, &swapchain->nimgs, NULL);
  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngfvk_create_swapchain_cleanup;
  }
  swapchain->imgs = NGFI_ALLOCN(VkImage, swapchain->nimgs);
  if (swapchain->imgs == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngfvk_create_swapchain_cleanup;
  }
  vk_err = vkGetSwapchainImagesKHR(
      _vk.device,
      swapchain->vk_swapchain,
      &swapchain->nimgs,
      swapchain->imgs);
  if (vk_err != VK_SUCCESS) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngfvk_create_swapchain_cleanup;
  }

  swapchain->wrapper_imgs = NGFI_ALLOCN(ngf_image, swapchain->nimgs);
  if (swapchain->wrapper_imgs == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngfvk_create_swapchain_cleanup;
  }

  const ngf_image_info wrapper_image_info = {
      .extent  = {.width = swapchain_info->width, .height = swapchain_info->height, .depth = 1},
      .format  = swapchain_info->color_format,
      .nlayers = 1u,
      .nmips   = 1u,
      .sample_count = NGF_SAMPLE_COUNT_1,
      .type         = NGF_IMAGE_TYPE_IMAGE_2D,
      .usage_hint   = NGF_IMAGE_USAGE_ATTACHMENT};
  for (size_t i = 0u; i < swapchain->nimgs; ++i) {
    ngfvk_alloc wrapper_alloc;
    memset(&wrapper_alloc, 0, sizeof(wrapper_alloc));
    wrapper_alloc.obj_handle = (uintptr_t)swapchain->imgs[i];
    ngfvk_create_image(&wrapper_image_info, &wrapper_alloc, false, &swapchain->wrapper_imgs[i]);
  }

  const bool is_multisampled = (unsigned int)swapchain_info->sample_count > 1u;

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
    swapchain->multisample_imgs = NGFI_ALLOCN(ngf_image, swapchain->nimgs);
    if (swapchain->multisample_imgs == NULL) {
      err = NGF_ERROR_OUT_OF_MEM;
      goto ngfvk_create_swapchain_cleanup;
    }
    for (size_t i = 0u; i < swapchain->nimgs; ++i) {
      const ngf_error img_create_error =
          ngf_create_image(&ms_image_info, &swapchain->multisample_imgs[i]);
      if (img_create_error != NGF_ERROR_OK) {
        err = img_create_error;
        goto ngfvk_create_swapchain_cleanup;
      }
    }
    // Create image views for multisample images.
    swapchain->multisample_img_views = NGFI_ALLOCN(VkImageView, swapchain->nimgs);
    if (swapchain->multisample_img_views == NULL) {
      err = NGF_ERROR_OUT_OF_MEM;
      goto ngfvk_create_swapchain_cleanup;
    }
    for (uint32_t i = 0u; i < swapchain->nimgs; ++i) {
      err = ngfvk_create_vk_image_view(
          (VkImage)swapchain->multisample_imgs[i]->alloc.obj_handle,
          VK_IMAGE_VIEW_TYPE_2D,
          requested_format,
          1u,
          1u,
          &swapchain->multisample_img_views[i]);
      if (err != NGF_ERROR_OK) { goto ngfvk_create_swapchain_cleanup; }
    }

  } else {
    swapchain->multisample_imgs      = NULL;
    swapchain->multisample_img_views = NULL;
  }

  // Create image views for swapchain images.
  swapchain->img_views = NGFI_ALLOCN(VkImageView, swapchain->nimgs);
  if (swapchain->img_views == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngfvk_create_swapchain_cleanup;
  }
  for (uint32_t i = 0u; i < swapchain->nimgs; ++i) {
    err = ngfvk_create_vk_image_view(
        swapchain->imgs[i],
        VK_IMAGE_VIEW_TYPE_2D,
        requested_format,
        1u,
        1u,
        &swapchain->img_views[i]);
    if (err != NGF_ERROR_OK) { goto ngfvk_create_swapchain_cleanup; }
    swapchain->wrapper_imgs[i]->vkview = swapchain->img_views[i];
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
    err = ngf_create_image(&depth_image_info, &swapchain->depth_img);
    if (err != NGF_ERROR_OK) { goto ngfvk_create_swapchain_cleanup; }
  } else {
    swapchain->depth_img = NULL;
  }

  // Create framebuffers for swapchain images.
  swapchain->framebufs = NGFI_ALLOCN(VkFramebuffer, swapchain->nimgs);
  if (swapchain->framebufs == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngfvk_create_swapchain_cleanup;
  }
  const bool     have_resolve_attachment      = (unsigned int)swapchain_info->sample_count > 1u;
  const uint32_t depth_stencil_attachment_idx = swapchain->depth_img ? 1u : VK_ATTACHMENT_UNUSED;
  const uint32_t resolve_attachment_idx =
      have_resolve_attachment ? (swapchain->depth_img ? 2u : 1u) : VK_ATTACHMENT_UNUSED;
  const uint32_t nattachments = CURRENT_CONTEXT->default_render_target->nattachments;
  for (uint32_t f = 0u; f < swapchain->nimgs; ++f) {
    VkImageView attachment_views[3];
    attachment_views[0] =
        is_multisampled ? swapchain->multisample_img_views[f] : swapchain->img_views[f];
    if (depth_stencil_attachment_idx != VK_ATTACHMENT_UNUSED) {
      attachment_views[depth_stencil_attachment_idx] = swapchain->depth_img->vkview;
    }
    if (resolve_attachment_idx != VK_ATTACHMENT_UNUSED) {
      attachment_views[resolve_attachment_idx] = swapchain->img_views[f];
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
    vk_err = vkCreateFramebuffer(_vk.device, &fb_info, NULL, &swapchain->framebufs[f]);
    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_OBJECT_CREATION_FAILED;
      goto ngfvk_create_swapchain_cleanup;
    }
  }

  // Create semaphores to be signaled when a swapchain image becomes available.
  swapchain->img_sems = NGFI_ALLOCN(VkSemaphore, swapchain->nimgs);
  if (swapchain->img_sems == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngfvk_create_swapchain_cleanup;
  }
  memset(swapchain->img_sems, 0, sizeof(VkSemaphore) * swapchain->nimgs);
  for (uint32_t s = 0u; s < swapchain->nimgs; ++s) {
    const VkSemaphoreCreateInfo sem_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0};
    vk_err = vkCreateSemaphore(_vk.device, &sem_info, NULL, &swapchain->img_sems[s]);
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

  for (size_t i = 0u; i < (size_t)NGFVK_RETIRE_OBJ_COUNT; ++i) {
    ngfi_chnklist* retire_objs_chnklist  = &frame_res->retire_objs[i];
    ngfi_chnk_hdr* retire_objs_firstchnk = retire_objs_chnklist->firstchnk;
    if (retire_objs_firstchnk) {
      NGFI_LIST_FOR_EACH(&(retire_objs_firstchnk->clnode), n) {
        _vk.retire_obj_dtors[i](NGFI_CHNK_FROM_NODE(n));
      }
    }
    ngfi_chnklist_clear(retire_objs_chnklist);
  }
}

static void ngfvk_cleanup_pending_binds(ngf_cmd_buffer cmd_buf) {
  ngfi_chnklist_clear(&cmd_buf->pending_bind_ops);
  cmd_buf->npending_bind_ops = 0u;
}

static ngf_error ngfvk_encoder_start(ngf_cmd_buffer cmd_buf) {
  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_RECORDING);
  return NGF_ERROR_OK;
}

static ngf_error
ngfvk_initialize_generic_encoder(ngf_cmd_buffer cmd_buf, struct ngfi_private_encoder_data* enc) {
  enc->d0 = (uintptr_t)cmd_buf;
  return NGF_ERROR_OK;
}

static ngf_error
ngfvk_encoder_end(ngf_cmd_buffer cmd_buf, struct ngfi_private_encoder_data* generic_enc) {
  (void)generic_enc;
  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_READY_TO_SUBMIT);
  return NGF_ERROR_OK;
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

  // Bind dummy resources.
  VkWriteDescriptorSet* dummy_writes = (VkWriteDescriptorSet*)ngfi_sa_alloc(
      ngfi_tmp_store(),
      sizeof(VkWriteDescriptorSet) * set_layout->nall_descs);
  uint32_t num_writes = 0u;
  for (uint32_t b = 0u; b < set_layout->nall_bindings; ++b) {
    if (set_layout->binding_properties[b].type == VK_DESCRIPTOR_TYPE_MAX_ENUM) continue;
    for (uint32_t array_idx = 0u; array_idx < set_layout->binding_properties[b].ndescs_in_binding; ++array_idx) {
      VkWriteDescriptorSet* desc_w = &dummy_writes[num_writes++];
      desc_w->sType                = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      desc_w->pNext                = NULL;
      desc_w->descriptorCount      = 1u;
      desc_w->descriptorType       = set_layout->binding_properties[b].type;
      desc_w->dstArrayElement      = array_idx;
      desc_w->dstBinding           = b;
      desc_w->dstSet               = result;

      const bool is_multilayered_image = set_layout->binding_properties[b].is_multilayered_image;
      const bool is_cubemap = set_layout->binding_properties[b].is_cubemap;

      switch (desc_w->descriptorType) {
      case VK_DESCRIPTOR_TYPE_SAMPLER:
        desc_w->pImageInfo = &_vk.dummy_res.samp_info;
        break;
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        desc_w->pImageInfo =
            is_multilayered_image ? &_vk.dummy_res.imgsamp_arr_info : &_vk.dummy_res.imgsamp_info;
        break;
      case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        if (!is_cubemap) {
          desc_w->pImageInfo =
              is_multilayered_image ? &_vk.dummy_res.img_arr_info : &_vk.dummy_res.img_info;
        } else {
          desc_w->pImageInfo =
              is_multilayered_image ? &_vk.dummy_res.cube_arr_info : &_vk.dummy_res.cube_info;
        }
        break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        desc_w->pBufferInfo = &_vk.dummy_res.buf_info;
        break;
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        desc_w->pTexelBufferView = &_vk.dummy_res.tbuf->vk_buf_view;
        break;
      case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR: {
        VkWriteDescriptorSetAccelerationStructureKHR* dummy_accel_info =
            ngfi_sa_alloc(ngfi_tmp_store(), sizeof(VkWriteDescriptorSetAccelerationStructureKHR));
        dummy_accel_info->sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
        dummy_accel_info->pNext                      = NULL;
        dummy_accel_info->accelerationStructureCount = 1;
        dummy_accel_info->pAccelerationStructures    = &_vk.dummy_res.dummy_accel_struct;
        desc_w->pNext                                = dummy_accel_info;
        break;
      }
      default:
        assert(false);
      }
    }
  }
  vkUpdateDescriptorSets(_vk.device, num_writes, dummy_writes, 0, NULL);

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

  // Allocate an array of vulkan descriptor set writes from temp storage, one write per
  // pending bind op.
  VkWriteDescriptorSet* vk_writes =
      ngfi_sa_alloc(ngfi_tmp_store(), cmd_buf->npending_bind_ops * sizeof(VkWriteDescriptorSet));

  // Find a descriptor pools list to allocate from.
  ngfvk_desc_pools_list* pools = ngfvk_find_desc_pools_list(cmd_buf->parent_frame);
  cmd_buf->desc_pools_list     = pools;

  // Process each bind operation, constructing a corresponding
  // vulkan descriptor set write operation.
  uint32_t descriptor_write_idx = 0u;
  NGFI_CHNKLIST_FOR_EACH(cmd_buf->pending_bind_ops, const ngf_resource_bind_op, bind_op) {
    // Ensure that a valid descriptor set is referenced by this
    // bind operation.
    if (bind_op->target_set >= ndesc_set_layouts) {
      NGFI_DIAG_WARNING(
          "invalid descriptor set %d referenced by bind operation (pipeline has "
          "%d sets) - ignoring",
          bind_op->target_set,
          ndesc_set_layouts);
      continue;
    }
    // Find the corresponding descriptor set layout.
    const ngfvk_desc_set_layout* set_layout =
          &NGFI_DARRAY_AT(pipeline_data->descriptor_set_layouts, bind_op->target_set);
    // Ensure that a valid binding is referenced by this bind operation.
    if (bind_op->target_binding >= set_layout->nall_bindings) {
      NGFI_DIAG_WARNING(
          "invalid binding %d referenced by bind operation (descriptor set has %d bindings) - ignoring",
          bind_op->target_binding,
          set_layout->nall_bindings);
      continue;
    }

    if (set_layout->binding_properties[bind_op->target_binding].type != get_vk_descriptor_type(bind_op->type)) {
        NGFI_DIAG_WARNING("attempting to bind descriptor with unmatching type (set %d binding %d) - ignoring",
          bind_op->target_set,
          bind_op->target_binding);
      continue;
    }

    // Allocate a new descriptor set if necessary.
    const bool need_new_desc_set = vk_desc_sets[bind_op->target_set] == VK_NULL_HANDLE;
    if (need_new_desc_set) {
      VkDescriptorSet set = ngfvk_desc_pools_list_allocate_set(pools, set_layout);
      if (set == VK_NULL_HANDLE) {
        NGFI_DIAG_ERROR("Failed to bind graphics resources - could not allocate descriptor set");
        return;
      }
      vk_desc_sets[bind_op->target_set] = set;
    }

    // At this point, we have a valid descriptor set in the `vk_sets` array.
    // We'll use it in the write operation corresponding to the current bind_op.
    VkDescriptorSet set = vk_desc_sets[bind_op->target_set];

    // Construct a vulkan descriptor set write corresponding to this bind
    // operation.
    VkWriteDescriptorSet* vk_write = &vk_writes[descriptor_write_idx];

    vk_write->sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    vk_write->pNext           = NULL;
    vk_write->dstSet          = set;
    vk_write->dstBinding      = bind_op->target_binding;
    vk_write->descriptorCount = 1u;
    vk_write->dstArrayElement = bind_op->array_index;
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
        NGFI_DIAG_WARNING("Binding storage images to non-compute shader is currently unsupported.");
        continue;
      }
    /* break omitted intentionally */
    case NGF_DESCRIPTOR_IMAGE:
    case NGF_DESCRIPTOR_SAMPLER:
    case NGF_DESCRIPTOR_IMAGE_AND_SAMPLER: {
      const ngf_image_sampler_bind_info* bind_info = &bind_op->info.image_sampler;
      const bool                         is_multilayered_image =
          set_layout->binding_properties[bind_op->target_binding].is_multilayered_image;
      VkImageView image_view = VK_NULL_HANDLE;
      if (bind_op->type == NGF_DESCRIPTOR_IMAGE ||
          bind_op->type == NGF_DESCRIPTOR_STORAGE_IMAGE ||
          bind_op->type == NGF_DESCRIPTOR_IMAGE_AND_SAMPLER) {
        image_view = bind_info->is_image_view
                         ? bind_info->resource.view->vk_view
                         : (is_multilayered_image ? bind_info->resource.image->vkview_arrayed
                                                  : bind_info->resource.image->vkview);
      }
      VkDescriptorImageInfo* vk_bind_info =
          ngfi_sa_alloc(ngfi_tmp_store(), sizeof(VkDescriptorImageInfo));
      vk_bind_info->imageView   = VK_NULL_HANDLE;
      vk_bind_info->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      vk_bind_info->sampler     = VK_NULL_HANDLE;
      if (bind_op->type == NGF_DESCRIPTOR_IMAGE ||
          bind_op->type == NGF_DESCRIPTOR_IMAGE_AND_SAMPLER) {
        vk_bind_info->imageView   = image_view;
        vk_bind_info->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      } else if (bind_op->type == NGF_DESCRIPTOR_STORAGE_IMAGE) {
        vk_bind_info->imageView   = image_view;
        vk_bind_info->imageLayout = VK_IMAGE_LAYOUT_GENERAL;
      } else if (
          bind_op->type == NGF_DESCRIPTOR_SAMPLER ||
          bind_op->type == NGF_DESCRIPTOR_IMAGE_AND_SAMPLER) {
        vk_bind_info->sampler = bind_info->sampler->vksampler;
      }
      vk_write->pImageInfo = vk_bind_info;
      break;
    }
    case NGF_DESCRIPTOR_ACCELERATION_STRUCTURE: {
      VkWriteDescriptorSetAccelerationStructureKHR* accel_struct_info =
          ngfi_sa_alloc(ngfi_tmp_store(), sizeof(VkWriteDescriptorSetAccelerationStructureKHR));
      accel_struct_info->sType                      = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
      accel_struct_info->pNext                      = NULL;
      accel_struct_info->accelerationStructureCount = 1u;
      accel_struct_info->pAccelerationStructures    = (const VkAccelerationStructureKHR*)&bind_op->info.acceleration_structure;
      vk_write->pNext                               = accel_struct_info;
      break;
    }

    default:
      assert(false);
    }
    ++descriptor_write_idx;
  }
  // perform all the vulkan descriptor set write operations to populate the
  // newly allocated descriptor sets.
  vkUpdateDescriptorSets(_vk.device, descriptor_write_idx, vk_writes, 0, NULL);

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
  ngfvk_cleanup_pending_binds(cmd_buf);
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

  for (uint32_t a = 0u; a < nattachments; ++a) {
    const ngf_attachment_description* ngf_attachment_desc  = &attachment_descs[a];
    const ngfvk_attachment_pass_desc* attachment_pass_desc = &attachment_compat_pass_descs[a];
    VkAttachmentDescription*          vk_attachment_desc   = &vk_attachment_descs[a];

    vk_attachment_desc->flags   = 0u;
    vk_attachment_desc->format  = get_vk_image_format(ngf_attachment_desc->format);
    vk_attachment_desc->samples = get_vk_sample_count(ngf_attachment_desc->sample_count);
    vk_attachment_desc->loadOp  = attachment_pass_desc->load_op;
    vk_attachment_desc->storeOp = attachment_pass_desc->store_op;
    const bool has_stencil = ngf_attachment_desc->type == NGF_ATTACHMENT_DEPTH_STENCIL;
    vk_attachment_desc->stencilLoadOp =
        has_stencil ? attachment_pass_desc->load_op : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    vk_attachment_desc->stencilStoreOp =
        has_stencil ? attachment_pass_desc->store_op : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    vk_attachment_desc->initialLayout = attachment_pass_desc->layout;
    vk_attachment_desc->finalLayout   = attachment_pass_desc->layout;

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

  const VkRenderPassCreateInfo renderpass_ci = {
      .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
      .pNext           = NULL,
      .flags           = 0u,
      .attachmentCount = nattachments,
      .pAttachments    = vk_attachment_descs,
      .subpassCount    = 1u,
      .pSubpasses      = &subpass_desc,
      .dependencyCount = 0u,
      .pDependencies   = NULL};

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
  assert(nattachments < (8u * sizeof(uint64_t) / 4u));
  uint64_t result = 0u;
  for (uint32_t i = 0u; i < nattachments; ++i) {
    const uint64_t load_op_bits  = (uint64_t)load_ops[i];
    const uint64_t store_op_bits = (uint64_t)store_ops[i];
    assert(load_op_bits <= 3);
    assert(store_op_bits <= 2);
    const uint64_t attachment_ops_combo = (load_op_bits << 2u) | store_op_bits;
    result |= attachment_ops_combo << (i * 4u);
  }
  // For default RT, the load/store ops of the resolve attachments are not
  // specified by the client code explicitly. We always treat them as
  // DONT_CARE / STORE.
  if (rt->is_default && nattachments < num_rt_attachments &&
      rt->attachment_compat_pass_descs[nattachments].is_resolve) {
    result = result | ((uint64_t)0x1u << (4u * nattachments));
  }
  return result;
}

// Macros for accessing load/store ops encoded in a renderpass ops key.
#define NGFVK_ATTACHMENT_OPS_COMBO(idx, ops_key) ((ops_key >> (4u * idx)) & 15u)
#define NGFVK_ATTACHMENT_LOAD_OP_FROM_KEY(idx, ops_key) \
  (get_vk_load_op((ngf_attachment_load_op)(NGFVK_ATTACHMENT_OPS_COMBO(idx, ops_key) >> 2u)))
#define NGFVK_ATTACHMENT_STORE_OP_FROM_KEY(idx, ops_key) \
  (get_vk_store_op((ngf_attachment_store_op)(NGFVK_ATTACHMENT_OPS_COMBO(idx, ops_key) & 3u)))

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
  const ngfvk_reflect_binding_and_stage_mask* a_binding = a;
  const ngfvk_reflect_binding_and_stage_mask* b_binding = b;
  if (a_binding->binding_data.set < b_binding->binding_data.set)
    return -1;
  else if (a_binding->binding_data.set == b_binding->binding_data.set) {
    if (a_binding->binding_data.binding < b_binding->binding_data.binding)
      return -1;
    else if (a_binding->binding_data.binding == b_binding->binding_data.binding)
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
  case SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
    return NGF_DESCRIPTOR_ACCELERATION_STRUCTURE;
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

static VkResult ngfvk_create_instance(
    bool        request_validation,
    bool        request_debug_groups,
    VkInstance* instance_ptr,
    bool*       validation_enabled) {
  // Scan through the list of instance-level extensions, determine which are supported.
  bool     swapchain_colorspace_supported = false;
  uint32_t ninst_exts                     = 0u;
  vkEnumerateInstanceExtensionProperties(NULL, &ninst_exts, NULL);
  VkExtensionProperties* ext_props = malloc(sizeof(VkExtensionProperties) * ninst_exts);
  if (ext_props == NULL) { return VK_ERROR_OUT_OF_HOST_MEMORY; }
  vkEnumerateInstanceExtensionProperties(NULL, &ninst_exts, ext_props);
  for (size_t i = 0; i < ninst_exts && !swapchain_colorspace_supported; ++i) {
    swapchain_colorspace_supported =
        (strcmp(VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME, ext_props[i].extensionName) == 0u);
  }
  free(ext_props);

  // Query the supported instance version.
  uint32_t instance_version = VK_API_VERSION_1_0;
  if (vkEnumerateInstanceVersion) {
    vkEnumerateInstanceVersion(&instance_version);
  }

  // nicegraf requires Vulkan 1.1+
  if (instance_version < VK_API_VERSION_1_1) {
    return VK_ERROR_INCOMPATIBLE_DRIVER;
  }

  // Use the highest supported version up to 1.2.
  const uint32_t api_version = NGFI_MIN(instance_version, VK_API_VERSION_1_2);

  // Names of instance-level extensions.
  const char* ext_names[] = {
      "VK_KHR_surface",
      VK_SURFACE_EXT,
      VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
      NULL,
      NULL};
  const uint32_t max_optional_exts  = 2u;
  uint32_t       optional_ext_count = 0u;
  const uint32_t nmandatory_exts    = NGFI_ARRAYSIZE(ext_names) - max_optional_exts;
  if (swapchain_colorspace_supported) {
    ext_names[nmandatory_exts + optional_ext_count++] = VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME;
  }
  if (request_validation || request_debug_groups) {
    ext_names[nmandatory_exts + optional_ext_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
  }
  assert(max_optional_exts >= optional_ext_count);

  const VkApplicationInfo app_info = {// Application information.
                                      .sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                      .pNext            = NULL,
                                      .pApplicationName = NULL,  // TODO: allow specifying app name.
                                      .pEngineName      = "nicegraf",
                                      .engineVersion = VK_MAKE_VERSION(NGF_VER_MAJ, NGF_VER_MIN, 0),
                                      .apiVersion    = api_version};

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
  const uint32_t             nunused_exts = (max_optional_exts - optional_ext_count);
  const VkInstanceCreateInfo inst_info    = {
         .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
         .pNext                   = NULL,
         .flags                   = 0u,
         .pApplicationInfo        = &app_info,
         .enabledLayerCount       = enable_validation ? 1u : 0u,
         .ppEnabledLayerNames     = enabled_layers,
         .enabledExtensionCount   = (uint32_t)NGFI_ARRAYSIZE(ext_names) - nunused_exts,
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
  ngfvk_reflect_binding_and_stage_mask* bindings =
      NGFI_SALLOC(ngfvk_reflect_binding_and_stage_mask, ntotal_bindings);

  uint32_t bindings_offset = 0u;
  for (uint32_t i = 0u; i < nshader_stages; ++i) {
    const SpvReflectShaderModule* spv_module    = &shader_stages[i]->spv_reflect_module;
    const uint32_t                binding_count = spv_module->descriptor_binding_count;
    for (size_t j = bindings_offset; j < bindings_offset + binding_count; ++j) {
      bindings[j].binding_data = spv_module->descriptor_bindings[j - bindings_offset];
      switch (spv_module->entry_points[0].shader_stage) {
      case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT:
        bindings[j].mask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
        break;
      case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT:
        bindings[j].mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        break;
      case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT:
        bindings[j].mask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        break;
      default:
        assert(false);
        break;
      }
    }
    bindings_offset += binding_count;
  }
  qsort(
      bindings,
      ntotal_bindings,
      sizeof(ngfvk_reflect_binding_and_stage_mask),
      ngfvk_binding_comparator);
  const uint32_t last_binding_idx = ntotal_bindings > 0 ? ntotal_bindings - 1u : 0u;
  const uint32_t max_set_id = ntotal_bindings > 0 ? bindings[last_binding_idx].binding_data.set : 0u;
  const uint32_t nall_sets = ntotal_bindings > 0 ? max_set_id + 1u : 0u;
  const uint32_t binding_counter_buf_size = sizeof(uint32_t) * nall_sets;
  uint32_t* nall_bindings_per_set = ngfi_sa_alloc(ngfi_tmp_store(), binding_counter_buf_size);
  memset(nall_bindings_per_set, 0, binding_counter_buf_size);
  uint32_t  nunique_bindings      = 0u;
  for (uint32_t cur = 0u; cur < ntotal_bindings; ++cur) {
    const ngfvk_reflect_binding_and_stage_mask* cur_binding = &bindings[cur];
    ngfvk_reflect_binding_and_stage_mask*       last_unique_binding =
        nunique_bindings == 0 ? NULL : &bindings[nunique_bindings - 1];
    const SpvReflectDescriptorBinding* last_unique_binding_data =
        !last_unique_binding ? NULL : &last_unique_binding->binding_data;
    const SpvReflectDescriptorBinding*          cur_binding_data = &cur_binding->binding_data;
    if (!last_unique_binding_data ||
        (last_unique_binding_data->set != cur_binding_data->set ||
         last_unique_binding_data->binding != cur_binding_data->binding)) {
      bindings[nunique_bindings++] = *cur_binding;
      nall_bindings_per_set[cur_binding_data->set] =
          NGFI_MAX(nall_bindings_per_set[cur_binding_data->set], cur_binding_data->binding + 1u);
    } else {
     last_unique_binding->mask |= cur_binding->mask;
    }
  }

  // Create descriptor set layouts.
  VkDescriptorSetLayout* vk_set_layouts =
      ngfi_sa_alloc(ngfi_tmp_store(), sizeof(VkDescriptorSetLayout) * (max_set_id + 1));
  uint32_t last_set_id = ~0u;
  for (uint32_t cur = 0u; cur < nunique_bindings;) {
    ngfvk_desc_set_layout set_layout;
    memset(&set_layout, 0, sizeof(set_layout));
    const uint32_t current_set_id = bindings[cur].binding_data.set;
    if (last_set_id == ~0u || current_set_id - last_set_id > 1u) {
      // there is a gap in descriptor sets, fill it in with empty layouts;
      for (uint32_t i = last_set_id == ~0u ? 0u : last_set_id + 1; i < current_set_id; ++i) {
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
    set_layout.nall_bindings = nall_bindings_per_set[bindings[cur].binding_data.set];
    if (set_layout.nall_bindings > 0u) {
      set_layout.binding_properties = NGFI_ALLOCN(ngfvk_desc_binding, set_layout.nall_bindings);
      for (size_t i = 0u; i < set_layout.nall_bindings; ++i) { set_layout.binding_properties[i].type = VK_DESCRIPTOR_TYPE_MAX_ENUM; }
      memset(set_layout.binding_properties, 0, sizeof(ngfvk_desc_binding) * set_layout.nall_bindings);
    }
    const uint32_t first_binding_in_set = cur;
    while (cur < nunique_bindings && current_set_id == bindings[cur].binding_data.set) cur++;
    const uint32_t                nbindings_in_set = cur - first_binding_in_set;
    VkDescriptorSetLayoutBinding* vk_descriptor_bindings =
        ngfi_sa_alloc(ngfi_tmp_store(), sizeof(VkDescriptorSetLayoutBinding) * nbindings_in_set);
    for (uint32_t i = first_binding_in_set; i < cur; ++i) {
      VkDescriptorSetLayoutBinding*      vk_d = &vk_descriptor_bindings[i - first_binding_in_set];
      const SpvReflectDescriptorBinding* d    = &bindings[i].binding_data;
      const ngf_descriptor_type ngf_desc_type = ngfvk_get_ngf_descriptor_type(d->descriptor_type);
      if (ngf_desc_type == NGF_DESCRIPTOR_TYPE_COUNT) { return NGF_ERROR_OBJECT_CREATION_FAILED; }
      vk_d->binding            = d->binding;
      vk_d->descriptorCount    = d->count;
      vk_d->descriptorType     = get_vk_descriptor_type(ngf_desc_type);
      vk_d->stageFlags         = VK_SHADER_STAGE_ALL;
      vk_d->pImmutableSamplers = NULL;
      const ngfvk_desc_binding binding_properties = {
          .type              = vk_d->descriptorType,
          .stage_accessors   = bindings[i].mask,
          .readonly          = (d->block.decoration_flags & SPV_REFLECT_DECORATION_NON_WRITABLE),
          .ndescs_in_binding = vk_d->descriptorCount,
          .is_cubemap        = (d->image.dim == SpvDimCube),
          .is_multilayered_image = (d->image.arrayed != 0)};
      set_layout.binding_properties[d->binding] = binding_properties;
      set_layout.counts[ngf_desc_type]++;
      set_layout.nall_descs += vk_d->descriptorCount;
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
    NGFVK_RETIRE_OBJECT(res, NGFVK_RETIRE_OBJ_PIPELINE, data->vk_pipeline);
  }
  if (data->vk_pipeline_layout != VK_NULL_HANDLE) {
    NGFVK_RETIRE_OBJECT(res, NGFVK_RETIRE_OBJ_PIPELINE_LAYOUT, data->vk_pipeline_layout);
  }
  NGFI_DARRAY_FOREACH(data->descriptor_set_layouts, l) {
    ngfvk_desc_set_layout* layout    = &NGFI_DARRAY_AT(data->descriptor_set_layouts, l);
    VkDescriptorSetLayout  vk_layout = layout->vk_handle;
    NGFVK_RETIRE_OBJECT(res, NGFVK_RETIRE_OBJ_DSET_LAYOUT, vk_layout);
    if (layout->nall_bindings > 0u) {
      if (layout->binding_properties) { NGFI_FREEN(layout->binding_properties, layout->nall_bindings); }
    }
  }
  NGFI_DARRAY_DESTROY(data->descriptor_set_layouts);
}

static void ngfvk_cmd_bind_resources(
    ngf_cmd_buffer              buf,
    const ngf_resource_bind_op* bind_operations,
    uint32_t                    nbind_operations) {
  for (uint32_t i = 0; i < nbind_operations; ++i) {
    ngfi_chnklist_append(&buf->pending_bind_ops, &bind_operations[i], sizeof(bind_operations[i]));
    ++buf->npending_bind_ops;
  }
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

static void ngfvk_reset_renderpass_cache(ngf_context ctx) {
  NGFI_DARRAY_FOREACH(ctx->renderpass_cache, p) {
    NGFVK_RETIRE_OBJECT(
        &(ctx->frame_res[ctx->frame_id]),
        NGFVK_RETIRE_OBJ_RENDER_PASS,
        NGFI_DARRAY_AT(ctx->renderpass_cache, p).renderpass);
  }
  NGFI_DARRAY_CLEAR(ctx->renderpass_cache);
}

static void ngfvk_cmd_buf_reset_render_cmds(ngf_cmd_buffer cmd_buf) {
  ngfi_chnklist_clear(&cmd_buf->in_pass_cmd_chnks);
}

static void ngfvk_cmd_buf_add_render_cmd(
    ngf_cmd_buffer          cmd_buf,
    const ngfvk_render_cmd* cmd,
    bool                    in_renderpass) {
  if (in_renderpass) {
    ngfi_chnklist_append(&cmd_buf->in_pass_cmd_chnks, cmd, sizeof(*cmd));
  } else {
    assert(false);
  }
}

static void ngfvk_cmd_buf_reset_res_states(ngf_cmd_buffer cmd_buf) {
  ngfi_dict_clear(cmd_buf->local_res_states);
}

static inline ngfvk_sync_res ngfvk_sync_res_from_buf(ngf_buffer buf) {
  ngfvk_sync_res sync_res = {
      .data = {.buf = buf},
      .type = NGFVK_SYNC_RES_BUFFER,
      .hash = buf->hash};
  return sync_res;
}

static inline ngfvk_sync_res ngfvk_sync_res_from_img(ngf_image img) {
  ngfvk_sync_res sync_res = {.data = {.img = img}, .type = NGFVK_SYNC_RES_IMAGE, .hash = img->hash};
  return sync_res;
}

static uintptr_t ngfvk_handle_from_sync_res(const ngfvk_sync_res* res) {
  return res->type == NGFVK_SYNC_RES_BUFFER ? (uintptr_t)res->data.img : (uintptr_t)res->data.buf;
}

// Look up resource state in a given cmd buffer.
// If an entry corresponding to the resource doesn't already exist, it gets created.
static bool ngfvk_cmd_buf_lookup_sync_res(
    ngf_cmd_buffer        cmd_buf,
    const ngfvk_sync_res* sync_res,
    ngfvk_sync_res_data** sync_res_data_out) {
  ngfvk_sync_res_data     new_res_state;
  bool                    new_res = false;
  const ngfi_dict_keyhash keyhash = {ngfvk_handle_from_sync_res(sync_res), sync_res->hash};

  *sync_res_data_out =
      ngfi_dict_get_prehashed(&cmd_buf->local_res_states, &keyhash, &new_res_state, &new_res);

  if (new_res) {
    ngfvk_sync_res_data* sync_res_data = *sync_res_data_out;
    memset(sync_res_data, 0, sizeof(new_res_state));
    sync_res_data->expected_sync_req.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    sync_res_data->res_handle               = ngfvk_handle_from_sync_res(sync_res);
    sync_res_data->res_type                 = sync_res->type;
    sync_res_data->pending_sync_req_idx     = ~0u;
  }

  return new_res;
}

static inline uint32_t ngfvk_next_nonzero_bit(uint32_t* mask) {
  const uint32_t old_mask = *mask;
  return (*mask = old_mask & (old_mask - 1), *mask ^ old_mask);
}

static inline uint32_t ngfvk_stage_idx(VkPipelineStageFlagBits bit) {
  switch (bit) {
  case VK_PIPELINE_STAGE_VERTEX_INPUT_BIT:
    return 0;
  case VK_PIPELINE_STAGE_VERTEX_SHADER_BIT:
    return 1;
  case VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:
    return 2;
  case VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT:
    return 3;
  case VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT:
    return 4;
  case VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT:
    return 5;
  case VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT:
    return 6;
  case VK_PIPELINE_STAGE_TRANSFER_BIT:
    return 7;
  default:
    assert(false);
  }
  return ~0u;
}

static inline uint32_t ngfvk_access_idx(VkAccessFlagBits bit) {
  switch (bit) {
  case VK_ACCESS_SHADER_READ_BIT:
    return 0u;
  case VK_ACCESS_SHADER_WRITE_BIT:
    return 1u;
  case VK_ACCESS_UNIFORM_READ_BIT:
    return 2u;
  case VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT:
    return 0u;
  case VK_ACCESS_INDEX_READ_BIT:
    return 1u;
  case VK_ACCESS_COLOR_ATTACHMENT_READ_BIT:
    return 0u;
  case VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT:
    return 1u;
  case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT:
    return 0u;
  case VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT:
    return 1u;
  case VK_ACCESS_TRANSFER_READ_BIT:
    return 0u;
  case VK_ACCESS_TRANSFER_WRITE_BIT:
    return 1u;
  default:
    assert(false);
  }
  return ~0u;
}

static uint32_t ngfvk_per_stage_access_mask(const ngfvk_sync_barrier_masks* barrier_masks) {
  static const VkAccessFlags valid_access_flags[] = {
      VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT,  // VERTEX_INPUT
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT,          // VERTEX_SHADER
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT,          // FRAGMENT_SHADER
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT |
          VK_ACCESS_SHADER_WRITE_BIT,  // COMPUTE_SHADER
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,  // EARLY_FRAGMENT_TESTS
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,  // LATE_FRAGMENT_TESTS
      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,                   // COLOR_ATTACHMENT_OUTPUT
      VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT  // TRANSFER
  };
  static const uint32_t bits_per_stage = 3u;

  uint32_t stage_mask = (uint32_t)barrier_masks->stage_mask;
  uint32_t result     = 0u;

  while (stage_mask) {
    const VkPipelineStageFlagBits stage_bit =
        (VkPipelineStageFlagBits)ngfvk_next_nonzero_bit(&stage_mask);
    const uint32_t stg_idx     = ngfvk_stage_idx(stage_bit);
    uint32_t       access_mask = (uint32_t)barrier_masks->access_mask;
    while (access_mask) {
      const VkAccessFlagBits access_bit = (VkAccessFlagBits)ngfvk_next_nonzero_bit(&access_mask);
      if (valid_access_flags[stg_idx] & access_bit) {
        const uint32_t acc_idx = ngfvk_access_idx(access_bit);
        result |= (1 << (bits_per_stage * stg_idx + acc_idx));
      }
    }
  }
  return result;
}

// Checks whether a barrier is needed before performing an operation on a resource, given its
// sync state.
// If a barrier is not needed, returns false. Otherwise, populates the barrier data appropriately
// and returns true.
static bool ngfvk_sync_barrier(
    ngfvk_sync_state*     sync_state,
    const ngfvk_sync_req* sync_req,
    ngfvk_barrier_data*   barrier) {
  const VkPipelineStageFlags dst_stage_mask  = sync_req->barrier_masks.stage_mask;
  const VkAccessFlags        dst_access_mask = sync_req->barrier_masks.access_mask;
  const VkImageLayout        dst_layout      = sync_req->layout;

  // Mask of all accesses we care about, that perform writes.
  static const VkAccessFlags all_write_accesses_mask =
      VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  // Reset all barrier data.
  memset(barrier, 0, sizeof(*barrier));

  // Decide if the requested operation necessitates a write to the resource.
  // Layout transitions are read-modify-write operations, thus if a layout transition is required
  // for the operation, we _always_ need a write, even if the actual requested access type
  // specified in `dst_access_mask` is read-only.
  const bool need_layout_transition = dst_layout != sync_state->layout;
  const bool dst_stages_want_write  = (all_write_accesses_mask & dst_access_mask);
  const bool need_write             = dst_stages_want_write || need_layout_transition;

  if (!need_write) {
    // Case for read-only operations.
    // Those can run concurrently with other read-only operations, and only need to wait for
    // any outstanding writes to complete.

    const uint32_t per_stg_acc_mask = ngfvk_per_stage_access_mask(&sync_req->barrier_masks);
    const bool     accesses_seen_write =
        ((sync_state->per_stage_readers_mask & per_stg_acc_mask) == per_stg_acc_mask);

    if (sync_state->last_writer_masks.stage_mask != VK_PIPELINE_STAGE_NONE &&
        !accesses_seen_write) {
      // If there was a preceding write, and the stage requesting the read-only operation
      // hasn't consumed it yet, a barrier is necessary.
      barrier->src_stage_mask |= sync_state->last_writer_masks.stage_mask;
      barrier->src_access_mask |=
          sync_state->last_writer_masks.access_mask & all_write_accesses_mask;
    }
    // Add the requested operation to the mask of ongoing reads.
    sync_state->active_readers_masks.stage_mask |= dst_stage_mask;
    sync_state->active_readers_masks.access_mask |= dst_access_mask;
    sync_state->per_stage_readers_mask |= per_stg_acc_mask;
  } else {
    // Case for modifying operations.
    // No more than a single modifying operation may be in progress at a given time.
    // Modifying operations have to wait for all outstanding reads and writes to complete.

    // Add any outstanding readers to the barrier's source mask.
    barrier->src_stage_mask |= sync_state->active_readers_masks.stage_mask;
    barrier->src_access_mask |= sync_state->active_readers_masks.access_mask;

    // No active readers remain after a modifying op, so zero out their corresponding masks.
    sync_state->active_readers_masks.stage_mask  = 0u;
    sync_state->active_readers_masks.access_mask = 0u;
    sync_state->per_stage_readers_mask           = 0u;

    // If there is an outstanding write, emit a barrier for it.
    // Note that we skip this if there were any outsdtanding reads, those already depend on the
    // write to finish, so it's sufficient to just depend on them.
    if (barrier->src_stage_mask == 0 &&
        sync_state->last_writer_masks.stage_mask != VK_PIPELINE_STAGE_NONE) {
      barrier->src_stage_mask |= sync_state->last_writer_masks.stage_mask;
      barrier->src_access_mask |= sync_state->last_writer_masks.access_mask;
    }

    // Update last writer stage and access mask.
    sync_state->last_writer_masks.stage_mask  = dst_stage_mask;
    sync_state->last_writer_masks.access_mask = dst_access_mask;

    // If the requested access was actually readonly, mark it as synced with the last write
    // since in that context the last write is made by the layout transition, the results of which
    // are made available and visible to the destination stage automatically.
    if ((dst_access_mask & all_write_accesses_mask) == 0u) {
      sync_state->active_readers_masks.stage_mask |= dst_stage_mask;
      sync_state->active_readers_masks.access_mask |= dst_access_mask;
      sync_state->per_stage_readers_mask |= ngfvk_per_stage_access_mask(&sync_req->barrier_masks);
    }
  }

  // We need a barrier if we found any source stages to wait on, or if a layout transition was
  // necessary.
  const bool need_barrier = barrier->src_stage_mask != 0u || need_layout_transition;

  if (need_barrier) {
    barrier->dst_access_mask = dst_access_mask;
    barrier->dst_stage_mask  = dst_stage_mask;
    barrier->src_stage_mask =
        barrier->src_stage_mask ? barrier->src_stage_mask : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    barrier->src_layout = sync_state->layout;
    barrier->dst_layout = dst_layout;
  }

  // Update the layout in synchronization state.
  sync_state->layout = dst_layout;

  return need_barrier;
}

static void ngfvk_sync_req_batch_init(uint32_t nmax_sync_reqs, ngfvk_sync_req_batch* result) {
  memset(result, 0, sizeof(*result));
  result->pending_sync_reqs  = NGFI_SALLOC(ngfvk_sync_req, nmax_sync_reqs);
  result->sync_res_data_keys = NGFI_SALLOC(ngfi_dict_keyhash, nmax_sync_reqs);
  result->freshness          = NGFI_SALLOC(bool, nmax_sync_reqs);
  memset(result->freshness, 0, sizeof(bool) * nmax_sync_reqs);
}

// Merges a given sync request with the resource's already pending sync request. Returns `false` and
// does nothing if the operation requested by the given sync request is incompatible with the
// pending sync request.
static bool ngfvk_sync_req_merge(ngfvk_sync_req* dst_sync_req, const ngfvk_sync_req* sync_req) {
  static const VkAccessFlags NGFVK_RENDER_ACCESSES_MASK =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  static const VkAccessFlags NGFVK_WRITE_ACCESSES_MASK =
      VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
  const bool render_requested =
      ((sync_req->barrier_masks.access_mask & NGFVK_RENDER_ACCESSES_MASK) != 0);
  const bool write_requested =
      ((sync_req->barrier_masks.access_mask & NGFVK_WRITE_ACCESSES_MASK) != 0);
  const bool render_pending =
      ((dst_sync_req->barrier_masks.access_mask & NGFVK_RENDER_ACCESSES_MASK) != 0);
  const bool write_pending =
      ((dst_sync_req->barrier_masks.access_mask & NGFVK_WRITE_ACCESSES_MASK) != 0);
  const bool read_requested      = !write_requested && (sync_req->barrier_masks.access_mask != 0);
  const bool read_pending        = !write_pending && (dst_sync_req->barrier_masks.access_mask != 0);
  const bool layout_incompatible = dst_sync_req->layout != VK_IMAGE_LAYOUT_UNDEFINED &&
                                   dst_sync_req->layout != VK_IMAGE_LAYOUT_GENERAL &&
                                   sync_req->layout != VK_IMAGE_LAYOUT_GENERAL &&
                                   dst_sync_req->layout != sync_req->layout;
  // Using a resource as a render target is not compatible with any other type of access.
  // Using a resource in a manner that requires it to be simultaneously in two incompatible layouts
  // results in transitioning to the GENERAL layout which is compatible with all kinds of accesses.
  // Merging modifying and non-modifying sync requests is allowed because the same resource might
  // be accessed with different descriptors in a GPU program (e.g. an image can be accessed both
  // as a sampled texture and as a storage image).
  if ((render_requested && (write_pending || read_pending || render_pending)) ||
      (render_pending && (write_requested || read_requested))) {
    NGFI_DIAG_ERROR("Attempt to use a resource with incompatible accesses within a single "
                    "draw/dispatch. Ignoring.");
    return false;
  }

  dst_sync_req->barrier_masks.access_mask |= sync_req->barrier_masks.access_mask;
  dst_sync_req->barrier_masks.stage_mask |= sync_req->barrier_masks.stage_mask;
  const bool preserve_general_layout =
      (dst_sync_req->layout == VK_IMAGE_LAYOUT_GENERAL ||
       sync_req->layout == VK_IMAGE_LAYOUT_GENERAL);
  dst_sync_req->layout = (preserve_general_layout || layout_incompatible) ? VK_IMAGE_LAYOUT_GENERAL : sync_req->layout;
  return true;
}

static bool ngfvk_sync_req_batch_add(
    ngfvk_sync_req_batch* batch,
    ngfi_dict_key         key,
    uint64_t              hash,
    ngfvk_sync_res_data*  sync_res_data,
    bool                  fresh,
    const ngfvk_sync_req* sync_req) {
  if (sync_res_data->pending_sync_req_idx == ~0u) {
    sync_res_data->pending_sync_req_idx = batch->npending_sync_reqs++;
    if (sync_res_data->res_type == NGFVK_SYNC_RES_BUFFER) {
      batch->nbuffer_sync_reqs++;
    } else if (sync_res_data->res_type == NGFVK_SYNC_RES_IMAGE) {
      batch->nimage_sync_reqs++;
    }
    memset(
        &batch->pending_sync_reqs[sync_res_data->pending_sync_req_idx],
        0,
        sizeof(batch->pending_sync_reqs[0]));
    batch->pending_sync_reqs[sync_res_data->pending_sync_req_idx].layout =
        VK_IMAGE_LAYOUT_UNDEFINED;
    batch->sync_res_data_keys[sync_res_data->pending_sync_req_idx].key  = key;
    batch->sync_res_data_keys[sync_res_data->pending_sync_req_idx].hash = hash;
  }
  if (fresh && sync_res_data->pending_sync_req_idx < batch->npending_sync_reqs) {
    batch->freshness[sync_res_data->pending_sync_req_idx] = true;
  }
  return ngfvk_sync_req_merge(
      &batch->pending_sync_reqs[sync_res_data->pending_sync_req_idx],
      sync_req);
}

static bool ngfvk_sync_req_batch_add_with_lookup(
    ngfvk_sync_req_batch* batch,
    ngf_cmd_buffer        cmd_buf,
    const ngfvk_sync_res* res,
    const ngfvk_sync_req* sync_req) {
  ngfvk_sync_res_data* sync_res_data;

  const bool fresh = ngfvk_cmd_buf_lookup_sync_res(cmd_buf, res, &sync_res_data);

  return ngfvk_sync_req_batch_add(
      batch,
      ngfvk_handle_from_sync_res(res),
      res->hash,
      sync_res_data,
      fresh,
      sync_req);
}

static void ngfvk_sync_commit_pending_barriers_legacy(
    ngfvk_pending_barrier_list* pending_bars,
    VkCommandBuffer             cmd_buf) {
  VkImageMemoryBarrier* img_bars =
      NGFI_SALLOC(VkImageMemoryBarrier, pending_bars->npending_img_bars);
  VkBufferMemoryBarrier* buf_bars =
      NGFI_SALLOC(VkBufferMemoryBarrier, pending_bars->npending_buf_bars);
  VkPipelineStageFlags src_stage_mask = 0u;
  VkPipelineStageFlags dst_stage_mask = 0u;
  uint32_t             nimg_bars      = 0u;
  uint32_t             nbuf_bars      = 0u;

  NGFI_CHNKLIST_FOR_EACH(pending_bars->chnklist, ngfvk_barrier_data, barrier) {
    src_stage_mask |= barrier->src_stage_mask;
    dst_stage_mask |= barrier->dst_stage_mask;
    switch (barrier->res.type) {
    case NGFVK_SYNC_RES_IMAGE: {
      const ngf_image       img                      = barrier->res.data.img;
      VkImageMemoryBarrier* image_barrier            = &img_bars[nimg_bars++];
      image_barrier->sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
      image_barrier->pNext                           = NULL;
      image_barrier->srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
      image_barrier->dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
      image_barrier->srcAccessMask                   = barrier->src_access_mask;
      image_barrier->dstAccessMask                   = barrier->dst_access_mask;
      image_barrier->oldLayout                       = barrier->src_layout;
      image_barrier->newLayout                       = barrier->dst_layout;
      image_barrier->image                           = (VkImage)img->alloc.obj_handle;
      image_barrier->subresourceRange.baseArrayLayer = 0u;
      image_barrier->subresourceRange.baseMipLevel   = 0u;
      image_barrier->subresourceRange.layerCount     = img->nlayers;
      image_barrier->subresourceRange.levelCount     = img->nlevels;
      const bool is_depth                            = ngfvk_format_is_depth(img->vk_fmt);
      const bool is_stencil                          = ngfvk_format_is_stencil(img->vk_fmt);
      image_barrier->subresourceRange.aspectMask =
          (is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : 0u) |
          (is_stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0u) |
          ((!is_depth && !is_stencil) ? VK_IMAGE_ASPECT_COLOR_BIT : 0u);
      break;
    }
    case NGFVK_SYNC_RES_BUFFER: {
      const ngf_buffer       buf            = barrier->res.data.buf;
      VkBufferMemoryBarrier* buffer_barrier = &buf_bars[nbuf_bars++];
      buffer_barrier->sType                 = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      buffer_barrier->pNext                 = NULL;
      buffer_barrier->srcQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
      buffer_barrier->dstQueueFamilyIndex   = VK_QUEUE_FAMILY_IGNORED;
      buffer_barrier->srcAccessMask         = barrier->src_access_mask;
      buffer_barrier->dstAccessMask         = barrier->dst_access_mask;
      buffer_barrier->offset                = 0u;
      buffer_barrier->buffer                = (VkBuffer)buf->alloc.obj_handle;
      buffer_barrier->size                  = buf->size;
      break;
    default:
      assert(false);
      break;
    }
    }
  }
  ngfi_chnklist_clear(&pending_bars->chnklist);
  pending_bars->npending_buf_bars = 0u;
  pending_bars->npending_img_bars = 0u;
  if (nbuf_bars > 0 || nimg_bars > 0) {
    vkCmdPipelineBarrier(
        cmd_buf,
        src_stage_mask,
        dst_stage_mask,
        0u,
        0u,
        NULL,
        nbuf_bars,
        buf_bars,
        nimg_bars,
        img_bars);
  }
}

static void ngfvk_sync_commit_pending_barriers_sync2(
    ngfvk_pending_barrier_list* pending_bars,
    VkCommandBuffer             cmd_buf) {
  VkImageMemoryBarrier2* img_bars =
      NGFI_SALLOC(VkImageMemoryBarrier2, pending_bars->npending_img_bars);
  VkBufferMemoryBarrier2* buf_bars =
      NGFI_SALLOC(VkBufferMemoryBarrier2, pending_bars->npending_buf_bars);
  uint32_t nimg_bars = 0u;
  uint32_t nbuf_bars = 0u;
  NGFI_CHNKLIST_FOR_EACH(pending_bars->chnklist, ngfvk_barrier_data, barrier) {
    switch (barrier->res.type) {
    case NGFVK_SYNC_RES_IMAGE: {
      const ngf_image        img                     = barrier->res.data.img;
      VkImageMemoryBarrier2* image_barrier           = &img_bars[nimg_bars++];
      image_barrier->sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
      image_barrier->pNext                           = NULL;
      image_barrier->srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
      image_barrier->dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
      image_barrier->srcStageMask                    = barrier->src_stage_mask;
      image_barrier->dstStageMask                    = barrier->dst_stage_mask;
      image_barrier->srcAccessMask                   = barrier->src_access_mask;
      image_barrier->dstAccessMask                   = barrier->dst_access_mask;
      image_barrier->oldLayout                       = barrier->src_layout;
      image_barrier->newLayout                       = barrier->dst_layout;
      image_barrier->image                           = (VkImage)img->alloc.obj_handle;
      image_barrier->subresourceRange.baseArrayLayer = 0u;
      image_barrier->subresourceRange.baseMipLevel   = 0u;
      image_barrier->subresourceRange.layerCount     = img->nlayers;
      image_barrier->subresourceRange.levelCount     = img->nlevels;
      const bool is_depth                            = ngfvk_format_is_depth(img->vk_fmt);
      const bool is_stencil                          = ngfvk_format_is_stencil(img->vk_fmt);
      image_barrier->subresourceRange.aspectMask =
          (is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : 0u) |
          (is_stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0u) |
          ((!is_depth && !is_stencil) ? VK_IMAGE_ASPECT_COLOR_BIT : 0u);
      break;
    }
    case NGFVK_SYNC_RES_BUFFER: {
      const ngf_buffer        buf            = barrier->res.data.buf;
      VkBufferMemoryBarrier2* buffer_barrier = &buf_bars[nbuf_bars++];
      buffer_barrier->sType                  = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
      buffer_barrier->pNext                  = NULL;
      buffer_barrier->srcStageMask           = barrier->src_stage_mask;
      buffer_barrier->dstStageMask           = barrier->dst_stage_mask;
      buffer_barrier->srcQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
      buffer_barrier->dstQueueFamilyIndex    = VK_QUEUE_FAMILY_IGNORED;
      buffer_barrier->srcAccessMask          = barrier->src_access_mask;
      buffer_barrier->dstAccessMask          = barrier->dst_access_mask;
      buffer_barrier->offset                 = 0u;
      buffer_barrier->buffer                 = (VkBuffer)buf->alloc.obj_handle;
      buffer_barrier->size                   = buf->size;
      break;
    }
    default:
      assert(false);
      break;
    }
  }
  ngfi_chnklist_clear(&pending_bars->chnklist);
  pending_bars->npending_buf_bars = 0u;
  pending_bars->npending_img_bars = 0u;
  if (nbuf_bars > 0 || nimg_bars > 0) {
    const VkDependencyInfo dep_info = {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext                    = NULL,
        .dependencyFlags          = 0u,
        .bufferMemoryBarrierCount = nbuf_bars,
        .imageMemoryBarrierCount  = nimg_bars,
        .memoryBarrierCount       = 0u,
        .pBufferMemoryBarriers    = buf_bars,
        .pImageMemoryBarriers     = img_bars,
        .pMemoryBarriers          = NULL};
    vkCmdPipelineBarrier2(cmd_buf, &dep_info);
  }
}

static void ngfvk_sync_commit_pending_barriers(
    ngfvk_pending_barrier_list* pending_bars,
    VkCommandBuffer             cmd_buf) {
  if (vkCmdPipelineBarrier2) {
    ngfvk_sync_commit_pending_barriers_sync2(pending_bars, cmd_buf);
  } else {
    ngfvk_sync_commit_pending_barriers_legacy(pending_bars, cmd_buf);
  }
}

static void ngfvk_sync_req_batch_process(ngfvk_sync_req_batch* batch, ngf_cmd_buffer cmd_buf) {
  for (size_t i = 0u; i < batch->npending_sync_reqs; ++i) {
    ngfvk_sync_res_data* sync_res_data = ngfi_dict_get_prehashed(
        &cmd_buf->local_res_states,
        &batch->sync_res_data_keys[i],
        NULL,
        NULL);
    if (!sync_res_data) {
      NGFI_DIAG_WARNING(
          "Internal error - resource missing from cmd buffer's synchronization table?");
      assert(false);
    }

    const ngfvk_sync_req* sync_req = &batch->pending_sync_reqs[i];
    const bool            fresh    = batch->freshness[i];
    ngfvk_barrier_data    barrier_data;
    const bool            barrier_needed =
        ngfvk_sync_barrier(&sync_res_data->sync_state, sync_req, &barrier_data);
    if (barrier_needed && !fresh) {
      barrier_data.res.type = sync_res_data->res_type;
      if (barrier_data.res.type == NGFVK_SYNC_RES_IMAGE) {
        barrier_data.res.data.img = (ngf_image)sync_res_data->res_handle;
        ++cmd_buf->pending_barriers.npending_img_bars;
      } else {
        barrier_data.res.data.buf = (ngf_buffer)sync_res_data->res_handle;
        ++cmd_buf->pending_barriers.npending_buf_bars;
      }
      ngfi_chnklist_append(
          &cmd_buf->pending_barriers.chnklist,
          &barrier_data,
          sizeof(barrier_data));
      sync_res_data->had_barrier = true;
    }
    sync_res_data->pending_sync_req_idx = ~0u;

    if (!sync_res_data->had_barrier) {
      sync_res_data->expected_sync_req.barrier_masks.stage_mask |=
          sync_req->barrier_masks.stage_mask;
      sync_res_data->expected_sync_req.barrier_masks.access_mask |=
          sync_req->barrier_masks.access_mask;
      // Make note of the initial layout with which the resource is expected to be used.
      if (sync_res_data->expected_sync_req.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        sync_res_data->expected_sync_req.layout = sync_req->layout;
      }
    }
  }
}

static void ngfvk_sync_req_batch_commit(ngfvk_sync_req_batch* batch, ngf_cmd_buffer cmd_buf) {
  ngfvk_sync_req_batch_process(batch, cmd_buf);
  ngfvk_sync_commit_pending_barriers(&cmd_buf->pending_barriers, cmd_buf->vk_cmd_buffer);
}

static void ngfvk_handle_single_sync_req(
    ngf_cmd_buffer        cmd_buf,
    const ngfvk_sync_res* res,
    const ngfvk_sync_req* sync_req) {
  bool              fresh = false;
  ngfi_dict_keyhash sync_res_data_key;
  ngfvk_sync_req empty_sync_req = {.barrier_masks = {0u, 0u}, .layout = VK_IMAGE_LAYOUT_UNDEFINED};

  ngfvk_sync_req_batch batch = {
      .sync_res_data_keys = &sync_res_data_key,
      .pending_sync_reqs  = &empty_sync_req,
      .freshness          = &fresh,
      .npending_sync_reqs = 0,
      .nbuffer_sync_reqs  = 0,
      .nimage_sync_reqs   = 0};

  ngfvk_sync_req_batch_add_with_lookup(&batch, cmd_buf, res, sync_req);
  ngfvk_sync_req_batch_commit(&batch, cmd_buf);
}

static ngfvk_sync_res ngfvk_sync_res_from_bind_op(const ngf_resource_bind_op* bind_op) {
  switch (bind_op->type) {
  case NGF_DESCRIPTOR_IMAGE:
  case NGF_DESCRIPTOR_IMAGE_AND_SAMPLER:
  case NGF_DESCRIPTOR_STORAGE_IMAGE:
    return ngfvk_sync_res_from_img(
        bind_op->info.image_sampler.is_image_view ? bind_op->info.image_sampler.resource.view->src
                                                  : bind_op->info.image_sampler.resource.image);
    break;
  case NGF_DESCRIPTOR_STORAGE_BUFFER:
  case NGF_DESCRIPTOR_UNIFORM_BUFFER:
    return ngfvk_sync_res_from_buf(bind_op->info.buffer.buffer);
    break;
  case NGF_DESCRIPTOR_TEXEL_BUFFER:
    return ngfvk_sync_res_from_buf(bind_op->info.texel_buffer_view->buffer);
    break;
  default:
    break;
  }
  const ngfvk_sync_res none = {.type = NGFVK_SYNC_RES_COUNT, .data = {.buf = NULL}};
  return none;
}

// Returns a sync request corresponding to the given bind operation.
static ngfvk_sync_req ngfvk_sync_req_for_bind_op(
    const ngf_resource_bind_op*   bind_op,
    const ngfvk_generic_pipeline* pipeline) {
  ngfvk_sync_req sync_req;
  memset(&sync_req, 0, sizeof(sync_req));
  sync_req.layout = VK_IMAGE_LAYOUT_UNDEFINED;

  // Bind ops that target non-existent sets/bindings should be disregarded.
  if (bind_op->target_set >= NGFI_DARRAY_SIZE(pipeline->descriptor_set_layouts)) return sync_req;
  const ngfvk_desc_set_layout* layout =
      &NGFI_DARRAY_AT(pipeline->descriptor_set_layouts, bind_op->target_set);
  if (bind_op->target_binding >= layout->nall_bindings) return sync_req;

  const bool is_read_only = layout->binding_properties[bind_op->target_binding].readonly;

  sync_req.barrier_masks.stage_mask =
      NGFI_DARRAY_AT(pipeline->descriptor_set_layouts, bind_op->target_set)
          .binding_properties[bind_op->target_binding].stage_accessors;

  switch (bind_op->type) {
  case NGF_DESCRIPTOR_UNIFORM_BUFFER: {
    sync_req.barrier_masks.access_mask = VK_ACCESS_UNIFORM_READ_BIT;
    break;
  }
  case NGF_DESCRIPTOR_IMAGE:
  case NGF_DESCRIPTOR_IMAGE_AND_SAMPLER: {
    sync_req.barrier_masks.access_mask = VK_ACCESS_SHADER_READ_BIT;
    sync_req.layout                    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    break;
  }
  case NGF_DESCRIPTOR_STORAGE_BUFFER: {
    sync_req.barrier_masks.access_mask =
        VK_ACCESS_SHADER_READ_BIT | (is_read_only ? 0u : VK_ACCESS_SHADER_WRITE_BIT);
    break;
  }
  case NGF_DESCRIPTOR_STORAGE_IMAGE: {
    sync_req.barrier_masks.access_mask =
        VK_ACCESS_SHADER_READ_BIT | (is_read_only ? 0u : VK_ACCESS_SHADER_WRITE_BIT);
    sync_req.layout = VK_IMAGE_LAYOUT_GENERAL;
    break;
  }
  case NGF_DESCRIPTOR_TEXEL_BUFFER: {
    sync_req.barrier_masks.access_mask = VK_ACCESS_SHADER_READ_BIT;
    break;
  }
  case NGF_DESCRIPTOR_SAMPLER:
    sync_req.barrier_masks.stage_mask = 0u;
    break;
  case NGF_DESCRIPTOR_ACCELERATION_STRUCTURE:
    sync_req.barrier_masks.stage_mask = 0u;
    break;
  default:
    assert(0);
  }
  return sync_req;
}

// Actually records renderpass commands into a command buffer.
static void ngfvk_cmd_buf_record_render_cmds(ngf_cmd_buffer buf, const ngfi_chnklist* cmd_list) {
  ngfi_sa_reset(ngfi_tmp_store());

  NGFI_CHNKLIST_FOR_EACH((*cmd_list), ngfvk_render_cmd, cmd) {
    switch (cmd->type) {
    case NGFVK_RENDER_CMD_BIND_PIPELINE: {
      buf->active_gfx_pipe = cmd->data.pipeline;
      // If we had a pipeline bound for which there have been resources bound, but no draw call
      // executed, commit those resources to actual descriptor sets and bind them so that the next
      // pipeline is able to "see" those resources, provided that it's compatible.
      if (buf->active_gfx_pipe && buf->npending_bind_ops > 0u) { ngfvk_execute_pending_binds(buf); }
      vkCmdBindPipeline(
          buf->vk_cmd_buffer,
          VK_PIPELINE_BIND_POINT_GRAPHICS,
          cmd->data.pipeline->generic_pipeline.vk_pipeline);
      break;
    }
    case NGFVK_RENDER_CMD_SET_VIEWPORT: {
      const VkViewport viewport = {
          .x        = (float)cmd->data.rect.x,
          .y        = (float)cmd->data.rect.y,
          .width    = NGFI_MAX(1, (float)cmd->data.rect.width),
          .height   = NGFI_MAX(1, (float)cmd->data.rect.height),
          .minDepth = 0.0f,
          .maxDepth = 1.0f};
      vkCmdSetViewport(buf->vk_cmd_buffer, 0u, 1u, &viewport);
      break;
    }
    case NGFVK_RENDER_CMD_SET_SCISSOR: {
      const ngf_irect2d* r            = &cmd->data.rect;
      const VkRect2D     scissor_rect = {.offset = {r->x, r->y}, .extent = {r->width, r->height}};
      vkCmdSetScissor(buf->vk_cmd_buffer, 0u, 1u, &scissor_rect);
      break;
    }
    case NGFVK_RENDER_CMD_SET_STENCIL_REFERENCE: {
      vkCmdSetStencilReference(
          buf->vk_cmd_buffer,
          VK_STENCIL_FACE_FRONT_BIT,
          cmd->data.stencil_values.front);
      vkCmdSetStencilReference(
          buf->vk_cmd_buffer,
          VK_STENCIL_FACE_BACK_BIT,
          cmd->data.stencil_values.back);
      break;
    }
    case NGFVK_RENDER_CMD_SET_STENCIL_COMPARE_MASK: {
      vkCmdSetStencilCompareMask(
          buf->vk_cmd_buffer,
          VK_STENCIL_FACE_FRONT_BIT,
          cmd->data.stencil_values.front);
      vkCmdSetStencilCompareMask(
          buf->vk_cmd_buffer,
          VK_STENCIL_FACE_BACK_BIT,
          cmd->data.stencil_values.back);
      break;
    }
    case NGFVK_RENDER_CMD_SET_STENCIL_WRITE_MASK: {
      vkCmdSetStencilWriteMask(
          buf->vk_cmd_buffer,
          VK_STENCIL_FACE_FRONT_BIT,
          cmd->data.stencil_values.front);
      vkCmdSetStencilWriteMask(
          buf->vk_cmd_buffer,
          VK_STENCIL_FACE_BACK_BIT,
          cmd->data.stencil_values.back);
      break;
    }
    case NGFVK_RENDER_CMD_SET_DEPTH_BIAS: {
      vkCmdSetDepthBias(
          buf->vk_cmd_buffer,
          cmd->data.depth_bias.const_factor,
          cmd->data.depth_bias.clamp,
          cmd->data.depth_bias.slope_factor);
      break;
    }
    case NGFVK_RENDER_CMD_BIND_RESOURCE: {
      ngfvk_cmd_bind_resources(buf, &cmd->data.bind_resource, 1u);
      break;
    }
    case NGFVK_RENDER_CMD_BIND_ATTRIB_BUFFER: {
      VkDeviceSize vkoffset = cmd->data.bind_attrib_buffer.offset;
      vkCmdBindVertexBuffers(
          buf->vk_cmd_buffer,
          cmd->data.bind_attrib_buffer.binding,
          1,
          (VkBuffer*)&cmd->data.bind_attrib_buffer.buffer->alloc.obj_handle,
          &vkoffset);
      break;
    }
    case NGFVK_RENDER_CMD_BIND_INDEX_BUFFER: {
      const VkIndexType idx_type = get_vk_index_type(cmd->data.bind_index_buffer.type);
      assert(idx_type == VK_INDEX_TYPE_UINT16 || idx_type == VK_INDEX_TYPE_UINT32);
      vkCmdBindIndexBuffer(
          buf->vk_cmd_buffer,
          (VkBuffer)cmd->data.bind_index_buffer.buffer->alloc.obj_handle,
          cmd->data.bind_index_buffer.offset,
          idx_type);
      break;
    }
    case NGFVK_RENDER_CMD_DRAW: {
      // Allocate and write descriptor sets.
      ngfvk_execute_pending_binds(buf);

      // With all resources bound, we may perform the draw operation.
      if (cmd->data.draw.indexed) {
        vkCmdDrawIndexed(
            buf->vk_cmd_buffer,
            cmd->data.draw.nelements,
            cmd->data.draw.ninstances,
            cmd->data.draw.first_element,
            0u,
            0u);
      } else {
        vkCmdDraw(
            buf->vk_cmd_buffer,
            cmd->data.draw.nelements,
            cmd->data.draw.ninstances,
            cmd->data.draw.first_element,
            0u);
      }
      break;
    }
    default:
      assert(false);
    }
  }
  ngfi_sa_reset(ngfi_tmp_store());
}

static void ngfvk_debug_label_begin(VkCommandBuffer b, const char* name) {
  if (vkCmdBeginDebugUtilsLabelEXT) {
    const VkDebugUtilsLabelEXT label = {
        .color      = {0.f, 0.f, 0.f, 0.f},
        .pLabelName = name,
        .pNext      = NULL,
        .sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
    vkCmdBeginDebugUtilsLabelEXT(b, &label);
  }
}

static void ngfvk_debug_label_end(VkCommandBuffer b) {
  if (vkCmdEndDebugUtilsLabelEXT) { vkCmdEndDebugUtilsLabelEXT(b); }
}

// Submits all pending command buffers for the current frame.
static ngf_error ngfvk_submit_pending_cmd_buffers(
    ngfvk_frame_resources* frame_res,
    VkSemaphore            wait_semaphore,
    VkFence                signal_fence) {

  ngf_error        err       = NGF_ERROR_OK;
  const uint32_t   ncmd_bufs = NGFI_DARRAY_SIZE(frame_res->submitted_cmd_bufs);
  VkCommandBuffer* submitted_cmd_buf_handles =
      ngfi_sa_alloc(ngfi_frame_store(), sizeof(VkCommandBuffer) * ncmd_bufs * 2u + 2u);
  uint32_t submitted_cmd_buf_handles_idx = 0u;

  {
    // Check if dummy image needs to be transitioned from UNDEFINED to GENERAL layout,
    // submit and aux command buffer with the appropriate barrier if so.
    pthread_mutex_lock(&_vk.dummy_res.img_mu);
    if (!_vk.dummy_res.image_transitioned) {
      _vk.dummy_res.image_transitioned = true;
      VkCommandBuffer aux_cmd_buf;
      VkCommandPool   aux_cmd_pool;
      ngfvk_cmd_buffer_allocate_for_frame(
          CURRENT_CONTEXT->current_frame_token,
          &aux_cmd_pool,
          &aux_cmd_buf);
      const VkImageMemoryBarrier bar[] = {
          {.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
           .pNext         = NULL,
           .image         = (VkImage)_vk.dummy_res.img->alloc.obj_handle,
           .oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
           .newLayout     = VK_IMAGE_LAYOUT_GENERAL,
           .srcAccessMask = 0,
           .dstAccessMask = 0,
           .subresourceRange =
               {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseArrayLayer = 0u,
                .baseMipLevel   = 0u,
                .layerCount     = 1u,
                .levelCount     = 1u},
           .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
           .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED},
          {.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
           .pNext         = NULL,
           .image         = (VkImage)_vk.dummy_res.cube->alloc.obj_handle,
           .oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED,
           .newLayout     = VK_IMAGE_LAYOUT_GENERAL,
           .srcAccessMask = 0,
           .dstAccessMask = 0,
           .subresourceRange =
               {.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseArrayLayer = 0u,
                .baseMipLevel   = 0u,
                .layerCount     = 6u,
                .levelCount     = 1u},
           .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
           .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED}};
      vkCmdPipelineBarrier(aux_cmd_buf, 0, 0, 0, 0, NULL, 0, NULL, 2, bar);
      vkEndCommandBuffer(aux_cmd_buf);
      submitted_cmd_buf_handles[submitted_cmd_buf_handles_idx++] = aux_cmd_buf;
      ngfvk_cmd_buf_with_pool aux_buf_pool = {aux_cmd_buf, aux_cmd_pool};
      NGFVK_RETIRE_OBJECT(frame_res, NGFVK_RETIRE_OBJ_CMDBUF_WITH_POOL, aux_buf_pool);
    }
    pthread_mutex_unlock(&_vk.dummy_res.img_mu);
  }

  ngfvk_pending_barrier_list pending_patch_barriers;
  memset(&pending_patch_barriers, 0, sizeof(pending_patch_barriers));
  pending_patch_barriers.chnklist.blkalloc = CURRENT_CONTEXT->blkalloc;

  NGFI_DARRAY_FOREACH(frame_res->submitted_cmd_bufs, c) {
    ngf_cmd_buffer cmd_buf = NGFI_DARRAY_AT(frame_res->submitted_cmd_bufs, c);
    ngfi_sa_reset(ngfi_tmp_store());

    NGFI_DICT_FOREACH(cmd_buf->local_res_states, r_it) {
      ngfvk_sync_res_data* cmd_buf_res_state =
          (ngfvk_sync_res_data*)ngfi_dict_itval(cmd_buf->local_res_states, r_it);
      ngfvk_sync_state* global_sync_state =
          cmd_buf_res_state->res_type == NGFVK_SYNC_RES_IMAGE
              ? &(((ngf_image)cmd_buf_res_state->res_handle)->sync_state)
              : &(((ngf_buffer)cmd_buf_res_state->res_handle)->sync_state);
      ngfvk_barrier_data patch_barrier_data;
      if (ngfvk_sync_barrier(
              global_sync_state,
              &cmd_buf_res_state->expected_sync_req,
              &patch_barrier_data)) {
        patch_barrier_data.res.type = cmd_buf_res_state->res_type;
        if (patch_barrier_data.res.type == NGFVK_SYNC_RES_IMAGE) {
          patch_barrier_data.res.data.img = (ngf_image)cmd_buf_res_state->res_handle;
          pending_patch_barriers.npending_img_bars++;
        } else {
          patch_barrier_data.res.data.buf = (ngf_buffer)cmd_buf_res_state->res_handle;
          pending_patch_barriers.npending_buf_bars++;
        }
        ngfi_chnklist_append(
            &pending_patch_barriers.chnklist,
            &patch_barrier_data,
            sizeof(patch_barrier_data));
      }
      if (cmd_buf_res_state->sync_state.last_writer_masks.access_mask != 0) {
        *global_sync_state = cmd_buf_res_state->sync_state;
      } else {
        global_sync_state->active_readers_masks.access_mask |=
            cmd_buf_res_state->sync_state.active_readers_masks.access_mask;
        global_sync_state->per_stage_readers_mask |=
            cmd_buf_res_state->sync_state.per_stage_readers_mask;
      }
    }
    if (pending_patch_barriers.npending_buf_bars + pending_patch_barriers.npending_img_bars > 0u) {
      VkCommandBuffer aux_cmd_buf;
      VkCommandPool   aux_cmd_pool;
      ngfvk_cmd_buffer_allocate_for_frame(
          CURRENT_CONTEXT->current_frame_token,
          &aux_cmd_pool,
          &aux_cmd_buf);
      ngfvk_debug_label_begin(aux_cmd_buf, "ngf - patch barrier cmd buffer");
      ngfvk_sync_commit_pending_barriers(&pending_patch_barriers, aux_cmd_buf);
      ngfvk_debug_label_end(aux_cmd_buf);
      vkEndCommandBuffer(aux_cmd_buf);
      submitted_cmd_buf_handles[submitted_cmd_buf_handles_idx++] = aux_cmd_buf;

      ngfvk_cmd_buf_with_pool aux_buf_pool = {aux_cmd_buf, aux_cmd_pool};
      NGFVK_RETIRE_OBJECT(frame_res, NGFVK_RETIRE_OBJ_CMDBUF_WITH_POOL, aux_buf_pool);
    }
    ngfi_chnklist_clear(&pending_patch_barriers.chnklist);
    submitted_cmd_buf_handles[submitted_cmd_buf_handles_idx++] = cmd_buf->vk_cmd_buffer;
    NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_SUBMITTED);
    cmd_buf->active_gfx_pipe     = NULL;
    cmd_buf->active_compute_pipe = NULL;
    cmd_buf->active_rt           = NULL;
    ngfvk_cmd_buf_reset_res_states(cmd_buf);
    ngfvk_cmd_buf_with_pool buf_pool = {cmd_buf->vk_cmd_buffer, cmd_buf->vk_cmd_pool};
    NGFVK_RETIRE_OBJECT(frame_res, NGFVK_RETIRE_OBJ_CMDBUF_WITH_POOL, buf_pool);

    cmd_buf->vk_cmd_buffer = VK_NULL_HANDLE;
    cmd_buf->vk_cmd_pool   = VK_NULL_HANDLE;
    if (cmd_buf->destroy_on_submit) { ngf_destroy_cmd_buffer(cmd_buf); }
  }
  NGFI_DARRAY_CLEAR(frame_res->submitted_cmd_bufs);

  // Transition the swapchain image to VK_IMAGE_LAYOUT_PRESENT_SRC if necessary.
  const bool needs_present = wait_semaphore != VK_NULL_HANDLE;
  if (needs_present) {
    ngf_image swapchain_image =
        CURRENT_CONTEXT->swapchain.wrapper_imgs[CURRENT_CONTEXT->swapchain.image_idx];
    if (swapchain_image->sync_state.layout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
      VkCommandBuffer aux_cmd_buf;
      VkCommandPool   aux_cmd_pool;
      ngfvk_cmd_buffer_allocate_for_frame(
          CURRENT_CONTEXT->current_frame_token,
          &aux_cmd_pool,
          &aux_cmd_buf);
      const VkImageMemoryBarrier swapchain_mem_bar = {
          .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext               = NULL,
          .srcAccessMask       = swapchain_image->sync_state.last_writer_masks.access_mask,
          .dstAccessMask       = 0u,
          .oldLayout           = swapchain_image->sync_state.layout,
          .newLayout           = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
          .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
          .image               = (VkImage)swapchain_image->alloc.obj_handle,
          .subresourceRange    = {
                 .baseMipLevel   = 0u,
                 .baseArrayLayer = 0u,
                 .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                 .layerCount     = 1u,
                 .levelCount     = 1u}};
      vkCmdPipelineBarrier(
          aux_cmd_buf,
          swapchain_image->sync_state.last_writer_masks.stage_mask,
          VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
          0u,
          0u,
          NULL,
          0u,
          NULL,
          1u,
          &swapchain_mem_bar);
      vkEndCommandBuffer(aux_cmd_buf);
      memset(&swapchain_image->sync_state, 0, sizeof(swapchain_image->sync_state));
      swapchain_image->sync_state.layout                         = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
      submitted_cmd_buf_handles[submitted_cmd_buf_handles_idx++] = aux_cmd_buf;
      ngfvk_cmd_buf_with_pool aux_buf_pool                       = {aux_cmd_buf, aux_cmd_pool};
      NGFVK_RETIRE_OBJECT(frame_res, NGFVK_RETIRE_OBJ_CMDBUF_WITH_POOL, aux_buf_pool);
    }
  }

  const VkPipelineStageFlags wait_masks[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  const VkSubmitInfo         submit_info  = {
               .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
               .pNext                = NULL,
               .pCommandBuffers      = submitted_cmd_buf_handles,
               .commandBufferCount   = submitted_cmd_buf_handles_idx,
               .pWaitDstStageMask    = wait_masks,
               .pWaitSemaphores      = needs_present ? &wait_semaphore : NULL,
               .waitSemaphoreCount   = needs_present ? 1u : 0u,
               .pSignalSemaphores    = needs_present ? &(frame_res->semaphore) : NULL,
               .signalSemaphoreCount = needs_present ? 1u : 0u};

  VkResult submit_result = vkQueueSubmit(_vk.gfx_queue, 1, &submit_info, signal_fence);

  if (submit_result != VK_SUCCESS) err = NGF_ERROR_INVALID_OPERATION;
  return err;
}

#define NGFVK_VK_OBJECT_DTOR(obj_type)                                                        \
  static void ngfvk_Vk##obj_type##_dtor(ngfi_chnk_hdr* chunk) {                               \
    NGFI_CHNK_FOR_EACH(chunk, Vk##obj_type, obj) vkDestroy##obj_type(_vk.device, *obj, NULL); \
  }

NGFVK_VK_OBJECT_DTOR(Pipeline);
NGFVK_VK_OBJECT_DTOR(PipelineLayout);
NGFVK_VK_OBJECT_DTOR(DescriptorSetLayout);
NGFVK_VK_OBJECT_DTOR(Framebuffer);
NGFVK_VK_OBJECT_DTOR(RenderPass);
NGFVK_VK_OBJECT_DTOR(ImageView);
NGFVK_VK_OBJECT_DTOR(BufferView);
NGFVK_VK_OBJECT_DTOR(Sampler);

static void ngfvk_cmd_buf_dtor(ngfi_chnk_hdr* chunk) {
  NGFI_CHNK_FOR_EACH(chunk, ngfvk_cmd_buf_with_pool, obj) {
    vkFreeCommandBuffers(_vk.device, obj->cmd_pool, 1u, &obj->cmd_buf);
  }
  NGFI_CHNK_FOR_EACH(chunk, ngfvk_cmd_buf_with_pool, obj) {
    vkResetCommandPool(_vk.device, obj->cmd_pool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);
  }
}

static void ngfvk_destroy_image(ngf_image img) {
  if (img->vkview) { vkDestroyImageView(_vk.device, img->vkview, NULL); }
  if (img->vkview_arrayed) { vkDestroyImageView(_vk.device, img->vkview_arrayed, NULL); }
  if (img->owns_backing_resource && img->alloc.obj_handle != (uintptr_t)VK_NULL_HANDLE) {
    vmaDestroyImage(_vk.allocator, (VkImage)img->alloc.obj_handle, img->alloc.vma_alloc);
  }
  NGFI_FREE(img);
}

static void ngfvk_img_dtor(ngfi_chnk_hdr* chunk) {
  NGFI_CHNK_FOR_EACH(chunk, ngf_image, img_ptr) {
    ngfvk_destroy_image(*img_ptr);
  }
}

static void ngfvk_destroy_buffer(ngf_buffer buf) {
  vmaDestroyBuffer(_vk.allocator, (VkBuffer)buf->alloc.obj_handle, buf->alloc.vma_alloc);
  NGFI_FREE(buf);
}

static void ngfvk_buf_dtor(ngfi_chnk_hdr* chunk) {
  NGFI_CHNK_FOR_EACH(chunk, ngf_buffer, buf_ptr) {
    ngfvk_destroy_buffer(*buf_ptr);
  }
}

static void ngfvk_desc_pools_list_dtor(ngfi_chnk_hdr* chunk) {
  NGFI_CHNK_FOR_EACH(chunk, ngfvk_desc_pools_list*, superpool_ptr) {
    ngfvk_desc_pools_list* superpool = *superpool_ptr;
    for (ngfvk_desc_pool* pool = superpool->list; pool; pool = pool->next) {
      vkResetDescriptorPool(_vk.device, pool->vk_pool, 0u);
      memset(&pool->utilization, 0, sizeof(pool->utilization));
    }
    superpool->active_pool = superpool->list;
  }
}

#pragma endregion

#pragma region external_funcs

ngf_error ngf_get_device_list(const ngf_device** devices, uint32_t* ndevices) {
  ngfvk_init_loader_if_necessary();
  if (NGFVK_DEVICE_LIST == NULL) {
    ngf_error  err          = NGF_ERROR_OK;
    VkInstance tmp_instance = VK_NULL_HANDLE;
    VkResult   vk_err       = ngfvk_create_instance(false, false, &tmp_instance, NULL);
    if (vk_err != VK_SUCCESS) { return NGF_ERROR_OBJECT_CREATION_FAILED; }
    PFN_vkEnumeratePhysicalDevices enumerate_vk_phys_devs = (PFN_vkEnumeratePhysicalDevices)
        vkGetInstanceProcAddr(tmp_instance, "vkEnumeratePhysicalDevices");
    PFN_vkGetPhysicalDeviceProperties get_vk_phys_dev_properties =
        (PFN_vkGetPhysicalDeviceProperties)
            vkGetInstanceProcAddr(tmp_instance, "vkGetPhysicalDeviceProperties");
    PFN_vkGetPhysicalDeviceFeatures get_vk_phys_dev_features = (PFN_vkGetPhysicalDeviceFeatures)
        vkGetInstanceProcAddr(tmp_instance, "vkGetPhysicalDeviceFeatures");
    PFN_vkGetPhysicalDeviceMemoryProperties get_vk_phys_dev_mem_props =
        (PFN_vkGetPhysicalDeviceMemoryProperties)
            vkGetInstanceProcAddr(tmp_instance, "vkGetPhysicalDeviceMemoryProperties");

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
      VkPhysicalDeviceProperties       dev_props;
      VkPhysicalDeviceFeatures         dev_features;
      VkPhysicalDeviceMemoryProperties mem_props;
      get_vk_phys_dev_properties(phys_devs[i], &dev_props);
      get_vk_phys_dev_features(phys_devs[i], &dev_features);
      get_vk_phys_dev_mem_props(phys_devs[i], &mem_props);
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
      devcaps->storage_buffer_offset_alignment =
          (size_t)vkdevlimits->minStorageBufferOffsetAlignment;
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

      // Detect device-local host-visible memory.
      devcaps->device_local_memory_is_host_visible = false;
      for (size_t mem_type_idx = 0u; !devcaps->device_local_memory_is_host_visible &&
                                     mem_type_idx < mem_props.memoryTypeCount;
           ++mem_type_idx) {
        const VkMemoryType*         mem_type = &mem_props.memoryTypes[mem_type_idx];
        const VkMemoryPropertyFlags local_visible =
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        if ((mem_type->propertyFlags & local_visible) == local_visible) {
          // Some systems only expose <= 256M device-local host-visible memory, we don't want that.
          // Only set the cap flag if a large region of device-local memory is also host-visible.
          devcaps->device_local_memory_is_host_visible =
              mem_props.memoryHeaps[mem_type->heapIndex].size > (256u * 1024u * 1024u);
        }
      }
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
    ngfi_module_handle ngf_renderdoc_mod =
        LoadLibraryA(init_info->renderdoc_info->renderdoc_lib_path);
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
      _renderdoc.is_capturing = false;
      _renderdoc.capture_next = false;
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
  const VkResult instance_create_result = ngfvk_create_instance(
      request_validation,
      ngfi_diag_info.enable_debug_groups,
      &_vk.instance,
      &_vk.validation_enabled);
  if (instance_create_result != VK_SUCCESS) {
    NGFI_DIAG_INFO("Failed to set up a new vulkan instance.");
    return NGF_ERROR_INVALID_OPERATION;
  }
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
  const uint32_t num_queue_infos          = (same_gfx_and_present ? 1u : 2u);

  NGFI_DARRAY_OF(const char*) device_exts;
  NGFI_DARRAY_RESET(device_exts, 4);
  NGFI_DARRAY_APPEND(device_exts, "VK_KHR_maintenance1");
  NGFI_DARRAY_APPEND(device_exts, "VK_KHR_swapchain");

  const bool shader_float16_int8_supported =
      ngfvk_phys_dev_extension_supported("VK_KHR_shader_float16_int8");
  const bool sync2_supported = ngfvk_phys_dev_extension_supported("VK_KHR_synchronization2");
  if (shader_float16_int8_supported) {
    NGFI_DARRAY_APPEND(device_exts, "VK_KHR_shader_float16_int8");
  }
  if (sync2_supported) {
    NGFI_DIAG_INFO("VK sync2 support enabled");
    NGFI_DARRAY_APPEND(device_exts, "VK_KHR_synchronization2");
  }

  const bool inline_ray_tracing_supported =
      ngfvk_phys_dev_extension_supported("VK_KHR_acceleration_structure") &&
      ngfvk_phys_dev_extension_supported("VK_KHR_buffer_device_address") &&
      ngfvk_phys_dev_extension_supported("VK_KHR_deferred_host_operations") &&
      ngfvk_phys_dev_extension_supported("VK_KHR_spirv_1_4") &&
      ngfvk_phys_dev_extension_supported("VK_KHR_shader_float_controls") &&
      ngfvk_phys_dev_extension_supported("VK_KHR_ray_query") &&
      ngfvk_phys_dev_extension_supported("VK_EXT_descriptor_indexing");
  if (inline_ray_tracing_supported) {
    NGFI_DIAG_INFO("VK inline ray tracing (ray query) support enabled");
    NGFI_DARRAY_APPEND(device_exts, "VK_KHR_acceleration_structure");
    NGFI_DARRAY_APPEND(device_exts, "VK_KHR_buffer_device_address");
    NGFI_DARRAY_APPEND(device_exts, "VK_KHR_deferred_host_operations");
    NGFI_DARRAY_APPEND(device_exts, "VK_KHR_spirv_1_4");
    NGFI_DARRAY_APPEND(device_exts, "VK_KHR_shader_float_controls");
    NGFI_DARRAY_APPEND(device_exts, "VK_KHR_ray_query");
    NGFI_DARRAY_APPEND(device_exts, "VK_EXT_descriptor_indexing");
    _vk.ray_tracing_enabled = true;
  }

  const VkBool32 enable_cubemap_arrays =
      NGFVK_DEVICE_LIST[device_idx].capabilities.cubemap_arrays_supported ? VK_TRUE : VK_FALSE;
  const VkPhysicalDeviceFeatures required_features = {
      .depthBiasClamp                       = VK_TRUE,
      .independentBlend                     = VK_TRUE,
      .samplerAnisotropy                    = VK_TRUE,
      .shaderStorageImageReadWithoutFormat  = VK_TRUE,
      .shaderStorageImageWriteWithoutFormat = VK_TRUE,
      .imageCubeArray                       = enable_cubemap_arrays};
  VkPhysicalDeviceShaderFloat16Int8Features sf16_features = {
      .sType         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
      .pNext         = NULL,
      .shaderFloat16 = false,
      .shaderInt8    = false};
  VkPhysicalDeviceSynchronization2Features sync2_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
      .pNext = &sf16_features};

  void* features_structs = NULL;
  if (shader_float16_int8_supported) {
    sf16_features.pNext = features_structs;
    features_structs    = &sf16_features;
  }
  if (sync2_supported) {
    sync2_features.pNext = features_structs;
    features_structs     = &sync2_features;
  }

  VkPhysicalDeviceBufferDeviceAddressFeaturesKHR buffer_device_address_features = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR,
    .pNext = NULL,
    .bufferDeviceAddress = VK_TRUE};

  VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features = {
    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
    .pNext = NULL,
    .accelerationStructure = VK_TRUE};

  VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features = {
    .sType    = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
    .pNext    = NULL,
    .rayQuery = VK_TRUE};

  if (inline_ray_tracing_supported) {
    buffer_device_address_features.pNext = features_structs;
    features_structs = &buffer_device_address_features;

    acceleration_structure_features.pNext = features_structs;
    features_structs = &acceleration_structure_features;

    ray_query_features.pNext = features_structs;
    features_structs = &ray_query_features;
  }

  if (vkGetPhysicalDeviceFeatures2KHR) {
    VkPhysicalDeviceFeatures2KHR phys_features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = features_structs};
    vkGetPhysicalDeviceFeatures2KHR(_vk.phys_dev, &phys_features);
  }

  const VkDeviceCreateInfo dev_info = {
      .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext                   = features_structs,
      .flags                   = 0,
      .queueCreateInfoCount    = num_queue_infos,
      .pQueueCreateInfos       = &queue_infos[same_gfx_and_present ? 1u : 0u],
      .enabledLayerCount       = 0,
      .ppEnabledLayerNames     = NULL,
      .pEnabledFeatures        = &required_features,
      .enabledExtensionCount   = NGFI_DARRAY_SIZE(device_exts),
      .ppEnabledExtensionNames = device_exts.data};
  vk_err = vkCreateDevice(_vk.phys_dev, &dev_info, NULL, &_vk.device);

  if (vk_err != VK_SUCCESS) {
    NGFI_DIAG_ERROR("Failed to create a Vulkan device, VK error %d.", vk_err);
    NGFI_DARRAY_DESTROY(device_exts);
    return NGF_ERROR_INVALID_OPERATION;
  }

  // Load device-level entry points.
  NGFI_DARRAY_DESTROY(device_exts);
  vkl_init_device(_vk.device, sync2_supported);

  // Set up VMA.
  VmaVulkanFunctions vma_vk_fns = {
      .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
      .vkGetDeviceProcAddr   = vkGetDeviceProcAddr,
  };
  VmaAllocatorCreateInfo vma_info = {
      .flags                       = _vk.ray_tracing_enabled ? VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT : 0,
      .physicalDevice              = _vk.phys_dev,
      .device                      = _vk.device,
      .preferredLargeHeapBlockSize = 0u,
      .pAllocationCallbacks        = NULL,
      .pDeviceMemoryCallbacks      = NULL,
      .pHeapSizeLimit              = NULL,
      .pVulkanFunctions            = &vma_vk_fns,
      .instance                    = _vk.instance,
      .vulkanApiVersion            = 0};
  vk_err = vmaCreateAllocator(&vma_info, &_vk.allocator);

  // Obtain queue handles.
  vkGetDeviceQueue(_vk.device, _vk.gfx_family_idx, 0, &_vk.gfx_queue);
  vkGetDeviceQueue(_vk.device, _vk.present_family_idx, 0, &_vk.present_queue);

  // Populate device capabilities.
  DEVICE_CAPS = NGFVK_DEVICE_LIST[init_info->device].capabilities;

  // Set up object destructor table.
  memset(_vk.retire_obj_dtors, 0, sizeof(_vk.retire_obj_dtors));
  _vk.retire_obj_dtors[NGFVK_RETIRE_OBJ_PIPELINE]         = ngfvk_VkPipeline_dtor;
  _vk.retire_obj_dtors[NGFVK_RETIRE_OBJ_PIPELINE_LAYOUT]  = ngfvk_VkPipelineLayout_dtor;
  _vk.retire_obj_dtors[NGFVK_RETIRE_OBJ_DSET_LAYOUT]      = ngfvk_VkDescriptorSetLayout_dtor;
  _vk.retire_obj_dtors[NGFVK_RETIRE_OBJ_CMDBUF_WITH_POOL] = ngfvk_cmd_buf_dtor;
  _vk.retire_obj_dtors[NGFVK_RETIRE_OBJ_FRAMEBUFFER]      = ngfvk_VkFramebuffer_dtor;
  _vk.retire_obj_dtors[NGFVK_RETIRE_OBJ_RENDER_PASS]      = ngfvk_VkRenderPass_dtor;
  _vk.retire_obj_dtors[NGFVK_RETIRE_OBJ_SAMPLER]          = ngfvk_VkSampler_dtor;
  _vk.retire_obj_dtors[NGFVK_RETIRE_OBJ_IMG_VIEW]         = ngfvk_VkImageView_dtor;
  _vk.retire_obj_dtors[NGFVK_RETIRE_OBJ_BUF_VIEW]         = ngfvk_VkBufferView_dtor;
  _vk.retire_obj_dtors[NGFVK_RETIRE_OBJ_IMG]              = ngfvk_img_dtor;
  _vk.retire_obj_dtors[NGFVK_RETIRE_OBJ_BUF]              = ngfvk_buf_dtor;
  _vk.retire_obj_dtors[NGFVK_RETIRE_OBJ_DESC_POOL_LIST]   = ngfvk_desc_pools_list_dtor;

  // Create dummy objects to pre-bind in fresh descriptor sets.
  const ngf_image_info dummy_img_info = {
      .extent       = {1u, 1u, 1u},
      .format       = NGF_IMAGE_FORMAT_R8,
      .nlayers      = 1u,
      .nmips        = 1u,
      .sample_count = NGF_SAMPLE_COUNT_1,
      .type         = NGF_IMAGE_TYPE_IMAGE_2D,
      .usage_hint   = NGF_IMAGE_USAGE_SAMPLE_FROM | NGF_IMAGE_USAGE_STORAGE};
  const ngf_image_info dummy_cube_info = {
      .extent       = {1u, 1u, 1u},
      .format       = NGF_IMAGE_FORMAT_R8,
      .nlayers      = 1u,
      .nmips        = 1u,
      .sample_count = NGF_SAMPLE_COUNT_1,
      .type         = NGF_IMAGE_TYPE_CUBE,
      .usage_hint   = NGF_IMAGE_USAGE_SAMPLE_FROM | NGF_IMAGE_USAGE_STORAGE};
  const ngf_buffer_info dummy_buf_info = {
      .buffer_usage = NGF_BUFFER_USAGE_STORAGE_BUFFER | NGF_BUFFER_USAGE_UNIFORM_BUFFER |
                      NGF_BUFFER_USAGE_TEXEL_BUFFER,
      .size         = 1u,
      .storage_type = NGF_BUFFER_STORAGE_DEVICE_LOCAL};
  ngf_sampler_info dummy_samp_info;
  memset(&dummy_samp_info, 0, sizeof(dummy_samp_info));
  ngf_create_image(&dummy_img_info, &_vk.dummy_res.img);
  ngf_create_image(&dummy_cube_info, &_vk.dummy_res.cube);
  ngf_create_buffer(&dummy_buf_info, &_vk.dummy_res.buf);
  ngf_create_sampler(&dummy_samp_info, &_vk.dummy_res.samp);
  const ngf_texel_buffer_view_info tbuf_info =
      {.buffer = _vk.dummy_res.buf, .offset = 0u, .size = 1u, .texel_format = NGF_IMAGE_FORMAT_R8};
  ngf_create_texel_buffer_view(&tbuf_info, &_vk.dummy_res.tbuf);
  _vk.dummy_res.buf_info.buffer            = (VkBuffer)_vk.dummy_res.buf->alloc.obj_handle;
  _vk.dummy_res.buf_info.offset            = 0u;
  _vk.dummy_res.buf_info.range             = 1u;
  _vk.dummy_res.img_info.imageLayout       = VK_IMAGE_LAYOUT_GENERAL;
  _vk.dummy_res.img_info.imageView         = _vk.dummy_res.img->vkview;
  _vk.dummy_res.img_info.sampler           = VK_NULL_HANDLE;
  _vk.dummy_res.cube_info.imageLayout      = VK_IMAGE_LAYOUT_GENERAL;
  _vk.dummy_res.cube_info.imageView        = _vk.dummy_res.cube->vkview;
  _vk.dummy_res.cube_info.sampler          = VK_NULL_HANDLE;
  _vk.dummy_res.img_arr_info               = _vk.dummy_res.img_info;
  _vk.dummy_res.img_arr_info.imageView     = _vk.dummy_res.img->vkview_arrayed;
  _vk.dummy_res.cube_arr_info              = _vk.dummy_res.cube_info;
  _vk.dummy_res.cube_arr_info.imageView    = _vk.dummy_res.cube->vkview_arrayed;
  _vk.dummy_res.samp_info.imageLayout      = VK_IMAGE_LAYOUT_GENERAL;
  _vk.dummy_res.samp_info.imageView        = VK_NULL_HANDLE;
  _vk.dummy_res.samp_info.sampler          = _vk.dummy_res.samp->vksampler;
  _vk.dummy_res.imgsamp_info.imageLayout   = VK_IMAGE_LAYOUT_GENERAL;
  _vk.dummy_res.imgsamp_info.imageView     = _vk.dummy_res.img->vkview;
  _vk.dummy_res.imgsamp_info.sampler       = _vk.dummy_res.samp->vksampler;
  _vk.dummy_res.imgsamp_arr_info           = _vk.dummy_res.imgsamp_info;
  _vk.dummy_res.imgsamp_arr_info.imageView = _vk.dummy_res.img->vkview_arrayed;
  _vk.dummy_res.dummy_accel_struct         = VK_NULL_HANDLE;
  _vk.dummy_res.image_transitioned         = false;
  pthread_mutex_init(&_vk.dummy_res.img_mu, NULL);

  // Done!

  return NGF_ERROR_OK;
}

void ngf_shutdown(void) {
  NGFI_DIAG_INFO("Shutting down nicegraf.");

  if (CURRENT_CONTEXT != NULL) { NGFI_DIAG_ERROR("Context not destroyed before shutdown.") }
  vkDestroyBufferView(_vk.device, _vk.dummy_res.tbuf->vk_buf_view, NULL);
  NGFI_FREE(_vk.dummy_res.tbuf);
  ngfvk_destroy_image(_vk.dummy_res.img);
  ngfvk_destroy_image(_vk.dummy_res.cube);
  ngfvk_destroy_buffer(_vk.dummy_res.buf);
  vkDestroySampler(_vk.device, _vk.dummy_res.samp->vksampler, NULL);

  if (_vk.allocator != VK_NULL_HANDLE) { vmaDestroyAllocator(_vk.allocator); }

  if (_vk.device != VK_NULL_HANDLE) { vkDestroyDevice(_vk.device, NULL); }
  if (_vk.validation_enabled) {
    vkDestroyDebugUtilsMessengerEXT(_vk.instance, _vk.debug_messenger, NULL);
  }
  if (_vk.instance != VK_NULL_HANDLE) { vkDestroyInstance(_vk.instance, NULL); }
  _vk.instance = VK_NULL_HANDLE;
  if (NGFVK_DEVICE_LIST) {
    free(NGFVK_DEVICE_LIST);
    NGFVK_DEVICE_LIST = NULL;
  }
  if (NGFVK_DEVICE_ID_LIST) {
    free(NGFVK_DEVICE_ID_LIST);
    NGFVK_DEVICE_ID_LIST = NULL;
  }
#if defined(__linux__)
  if (_vk.xcb_connection) {
    xcb_disconnect(_vk.xcb_connection);
    _vk.xcb_visualid   = 0;
    _vk.xcb_connection = NULL;
  }
#endif
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
    const bool default_rt_is_multisampled = (unsigned int)swapchain_info->sample_count > 1u;
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
    ctx->default_render_target->attachment_images      = NULL;

    uint32_t                    attachment_desc_idx = 0u;
    ngf_attachment_description* color_attachment_desc =
        &ctx->default_render_target->attachment_descs[attachment_desc_idx];
    color_attachment_desc->format       = swapchain_info->color_format;
    color_attachment_desc->sample_count = swapchain_info->sample_count;
    color_attachment_desc->type         = NGF_ATTACHMENT_COLOR;
    color_attachment_desc->is_resolve   = false;

    ngfvk_attachment_pass_desc* color_attachment_pass_desc =
        &ctx->default_render_target->attachment_compat_pass_descs[attachment_desc_idx];
    color_attachment_pass_desc->layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color_attachment_pass_desc->is_resolve = false;
    color_attachment_pass_desc->load_op    = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment_pass_desc->store_op   = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    if (default_rt_has_depth) {
      ++attachment_desc_idx;

      ngf_attachment_description* depth_attachment_desc =
          &ctx->default_render_target->attachment_descs[attachment_desc_idx];
      depth_attachment_desc->format       = swapchain_info->depth_format;
      depth_attachment_desc->sample_count = swapchain_info->sample_count;
      depth_attachment_desc->type =
          default_rt_no_stencil ? NGF_ATTACHMENT_DEPTH : NGF_ATTACHMENT_DEPTH_STENCIL;
      depth_attachment_desc->is_resolve = false;

      ngfvk_attachment_pass_desc* depth_attachment_pass_desc =
          &ctx->default_render_target->attachment_compat_pass_descs[attachment_desc_idx];
      depth_attachment_pass_desc->layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      depth_attachment_pass_desc->is_resolve = false;
      depth_attachment_pass_desc->load_op    = VK_ATTACHMENT_LOAD_OP_CLEAR;
      depth_attachment_pass_desc->store_op   = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }

    if (default_rt_is_multisampled) {
      ++attachment_desc_idx;

      ngf_attachment_description* resolve_attachment_desc =
          &ctx->default_render_target->attachment_descs[attachment_desc_idx];
      resolve_attachment_desc->format       = swapchain_info->color_format;
      resolve_attachment_desc->sample_count = NGF_SAMPLE_COUNT_1;
      resolve_attachment_desc->type         = NGF_ATTACHMENT_COLOR;
      resolve_attachment_desc->is_resolve   = true;

      ngfvk_attachment_pass_desc* resolve_attachment_pass_desc =
          &ctx->default_render_target->attachment_compat_pass_descs[attachment_desc_idx];
      resolve_attachment_pass_desc->layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      resolve_attachment_pass_desc->is_resolve = true;
      resolve_attachment_pass_desc->load_op    = VK_ATTACHMENT_LOAD_OP_CLEAR;
      resolve_attachment_pass_desc->store_op   = VK_ATTACHMENT_STORE_OP_DONT_CARE;

      ctx->default_render_target->have_resolve_attachments = true;
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

  // Initialize block allocator for this context.
  ctx->blkalloc = ngfi_blkalloc_create(
      1024,  // 1K per block
      16u);  // 16 blocks per pool
  if (ctx->blkalloc == NULL) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngf_create_context_cleanup;
  }

  // Create frame resource holders.
  const uint32_t max_inflight_frames = swapchain_info ? ctx->swapchain.nimgs : 3u;
  ctx->max_inflight_frames           = max_inflight_frames;
  ctx->frame_res                     = NGFI_ALLOCN(ngfvk_frame_resources, max_inflight_frames);
  if (ctx->frame_res == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_create_context_cleanup;
  }
  for (uint32_t f = 0u; f < max_inflight_frames; ++f) {
    memset(ctx->frame_res[f].retire_objs, 0, sizeof(ctx->frame_res[f].retire_objs));
    for (uint32_t i = 0u; i < NGFVK_RETIRE_OBJ_COUNT; ++i) {
      ctx->frame_res[f].retire_objs[i].blkalloc = ctx->blkalloc;
    }
    NGFI_DARRAY_RESET(ctx->frame_res[f].submitted_cmd_bufs, 8u);

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

  ctx->frame_id            = 0u;
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

void ngf_destroy_context(ngf_context ctx) {
  if (ctx != NULL) {
    ctx->being_destroyed = true;
    vkDeviceWaitIdle(_vk.device);

    if (ctx->default_render_target) {
      ngfvk_destroy_swapchain(&ctx->swapchain);
      if (ctx->surface != VK_NULL_HANDLE) { vkDestroySurfaceKHR(_vk.instance, ctx->surface, NULL); }
      ngf_destroy_render_target(ctx->default_render_target);
    }

    for (uint32_t f = 0u; ctx->frame_res != NULL && f < ctx->max_inflight_frames; ++f) {
      ngfvk_retire_resources(&ctx->frame_res[f]);
      NGFI_DARRAY_DESTROY(ctx->frame_res[f].submitted_cmd_bufs);
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

    if (ctx->frame_res != NULL) { NGFI_FREEN(ctx->frame_res, ctx->max_inflight_frames); }
    if (ctx->blkalloc) { ngfi_blkalloc_destroy(ctx->blkalloc); }

    if (CURRENT_CONTEXT == ctx) CURRENT_CONTEXT = NULL;
    NGFI_FREE(ctx);
  }
}

ngf_error ngf_set_context(ngf_context ctx) {
  CURRENT_CONTEXT = ctx;
  return NGF_ERROR_OK;
}

ngf_context ngf_get_context() {
  return CURRENT_CONTEXT;
}

ngf_error ngf_create_cmd_buffer(const ngf_cmd_buffer_info* info, ngf_cmd_buffer* result) {
  assert(info);
  assert(result);
  NGFI_IGNORE_VAR(info);

  ngf_cmd_buffer cmd_buf = NGFI_ALLOC(ngf_cmd_buffer_t);
  if (cmd_buf == NULL) { return NGF_ERROR_OUT_OF_MEM; }
  *result                                 = cmd_buf;
  cmd_buf->parent_frame                   = ~0u;
  cmd_buf->state                          = NGFI_CMD_BUFFER_NEW;
  cmd_buf->active_gfx_pipe                = NULL;
  cmd_buf->active_compute_pipe            = NULL;
  cmd_buf->active_attr_buf                = NULL;
  cmd_buf->active_idx_buf                 = NULL;
  cmd_buf->renderpass_active              = false;
  cmd_buf->compute_pass_active            = false;
  cmd_buf->destroy_on_submit              = false;
  cmd_buf->active_rt                      = NULL;
  cmd_buf->desc_pools_list                = NULL;
  cmd_buf->vk_cmd_buffer                  = VK_NULL_HANDLE;
  cmd_buf->vk_cmd_pool                    = VK_NULL_HANDLE;
  cmd_buf->pending_bind_ops.firstchnk     = NULL;
  cmd_buf->pending_bind_ops.blkalloc      = CURRENT_CONTEXT->blkalloc;
  cmd_buf->in_pass_cmd_chnks.blkalloc     = CURRENT_CONTEXT->blkalloc;
  cmd_buf->in_pass_cmd_chnks.firstchnk    = NULL;
  cmd_buf->virt_bind_ops_ranges.blkalloc  = CURRENT_CONTEXT->blkalloc;
  cmd_buf->virt_bind_ops_ranges.firstchnk = NULL;
  memset(&cmd_buf->pending_barriers, 0, sizeof(cmd_buf->pending_barriers));
  cmd_buf->pending_barriers.chnklist.blkalloc = CURRENT_CONTEXT->blkalloc;

  cmd_buf->local_res_states = ngfi_dict_create(100u, sizeof(ngfvk_sync_res_data));
  return NGF_ERROR_OK;
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
    } else if (rt->attachment_descs[i].type == NGF_ATTACHMENT_DEPTH ||
               rt->attachment_descs[i].type == NGF_ATTACHMENT_DEPTH_STENCIL) {
      clears[i].clear_depth_stencil.clear_depth   = clear_depth;
      clears[i].clear_depth_stencil.clear_stencil = clear_stencil;
    } else {
      assert(false);
    }

    const bool needs_resolve = rt->attachment_descs[i].type == NGF_ATTACHMENT_COLOR &&
                               rt->have_resolve_attachments &&
                               rt->attachment_descs[i].sample_count > NGF_SAMPLE_COUNT_1;
    store_ops[i] = needs_resolve ? NGF_STORE_OP_RESOLVE : NGF_STORE_OP_STORE;
  }
  const ngf_render_pass_info pass_info = {
      .render_target = rt,
      .load_ops      = load_ops,
      .store_ops     = store_ops,
      .clears        = clears,
  };
  return ngf_cmd_begin_render_pass(cmd_buf, &pass_info, enc);
}
ngf_error ngf_cmd_begin_render_pass(
    ngf_cmd_buffer              cmd_buf,
    const ngf_render_pass_info* pass_info,
    ngf_render_encoder*         enc) {
  ngf_error err = NGF_ERROR_OK;

  ngfvk_encoder_start(cmd_buf);
  if (err != NGF_ERROR_OK) return err;

  err = ngfvk_initialize_generic_encoder(cmd_buf, &enc->pvt_data_donotuse);
  if (err != NGF_ERROR_OK) { return err; }

  ngfi_sa_reset(ngfi_tmp_store());

  cmd_buf->active_rt         = pass_info->render_target;
  cmd_buf->renderpass_active = true;

  cmd_buf->pending_render_pass_info.render_target = pass_info->render_target;

  ngf_attachment_load_op* cloned_load_ops = ngfi_sa_alloc(
      ngfi_frame_store(),
      sizeof(ngf_attachment_load_op) * pass_info->render_target->nattachments);
  cmd_buf->pending_render_pass_info.load_ops = cloned_load_ops;
  if (cmd_buf->pending_render_pass_info.load_ops == NULL) { return NGF_ERROR_OUT_OF_MEM; }
  memcpy(
      cloned_load_ops,
      pass_info->load_ops,
      sizeof(ngf_attachment_load_op) * pass_info->render_target->nattachments);

  ngf_attachment_store_op* cloned_store_ops = ngfi_sa_alloc(
      ngfi_frame_store(),
      sizeof(ngf_attachment_store_op) * pass_info->render_target->nattachments);
  cmd_buf->pending_render_pass_info.store_ops = cloned_store_ops;
  if (cmd_buf->pending_render_pass_info.store_ops == NULL) { return NGF_ERROR_OUT_OF_MEM; }
  memcpy(
      cloned_store_ops,
      pass_info->store_ops,
      sizeof(ngf_attachment_store_op) * pass_info->render_target->nattachments);

  uint32_t   nclears   = 0u;
  uint32_t   clear_idx = 0u;
  ngf_clear* cloned_clears =
      ngfi_sa_alloc(ngfi_frame_store(), sizeof(ngf_clear) * pass_info->render_target->nattachments);
  if (cloned_clears == NULL) { return NGF_ERROR_OUT_OF_MEM; }
  for (uint32_t i = 0u; i < pass_info->render_target->nattachments; ++i) {
    if (cmd_buf->pending_render_pass_info.load_ops[i] == NGF_LOAD_OP_CLEAR) {
      nclears          = NGFI_MAX(nclears, i + 1);
      cloned_clears[i] = pass_info->clears[clear_idx++];
    }
  }
  if (nclears > 0u) {
    cmd_buf->pending_render_pass_info.clears = cloned_clears;
  } else {
    cmd_buf->pending_render_pass_info.clears = NULL;
  }
  cmd_buf->pending_clear_value_count = (uint16_t)nclears;

  ngfvk_sync_req_batch sync_req_batch;

  ngfvk_sync_req_batch_init(pass_info->render_target->nattachments, &sync_req_batch);

  for (size_t i = 0u; i < pass_info->render_target->nattachments; ++i) {
    const ngf_attachment_type attachment_type = pass_info->render_target->attachment_descs[i].type;
    const ngf_sample_count    attachment_sample_count =
        pass_info->render_target->attachment_descs[i].sample_count;
    switch (attachment_type) {
    case NGF_ATTACHMENT_COLOR: {
      ngfvk_sync_req sync_req;
      sync_req.barrier_masks.access_mask =
          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
      sync_req.barrier_masks.stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
      sync_req.layout                   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      ngf_image color_image =
          cmd_buf->active_rt->is_default
              ? (attachment_sample_count == NGF_SAMPLE_COUNT_1
                     ? CURRENT_CONTEXT->swapchain.wrapper_imgs[CURRENT_CONTEXT->swapchain.image_idx]
                     : CURRENT_CONTEXT->swapchain
                           .multisample_imgs[CURRENT_CONTEXT->swapchain.image_idx])
              : pass_info->render_target->attachment_images[i];
      ngfvk_sync_res res = ngfvk_sync_res_from_img(color_image);
      ngfvk_sync_req_batch_add_with_lookup(&sync_req_batch, cmd_buf, &res, &sync_req);
      break;
    }
    case NGF_ATTACHMENT_DEPTH:
    case NGF_ATTACHMENT_DEPTH_STENCIL: {
      ngfvk_sync_req sync_req;
      sync_req.barrier_masks.access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
      sync_req.barrier_masks.stage_mask =
          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
      sync_req.layout                    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      ngf_image      depth_stencil_image = cmd_buf->active_rt->is_default
                                               ? CURRENT_CONTEXT->swapchain.depth_img
                                               : pass_info->render_target->attachment_images[i];
      ngfvk_sync_res res                 = ngfvk_sync_res_from_img(depth_stencil_image);
      ngfvk_sync_req_batch_add_with_lookup(&sync_req_batch, cmd_buf, &res, &sync_req);
      break;
    }

    default:
      assert(0);
    }
  }
  ngfvk_sync_req_batch_process(&sync_req_batch, cmd_buf);

  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_begin_xfer_pass(
    ngf_cmd_buffer            cmd_buf,
    const ngf_xfer_pass_info* pass_info,
    ngf_xfer_encoder*         enc) {
  (void)pass_info;
  ngf_error err = ngfvk_encoder_start(cmd_buf);
  if (err != NGF_ERROR_OK) return err;

  err = ngfvk_initialize_generic_encoder(cmd_buf, &enc->pvt_data_donotuse);
  if (err != NGF_ERROR_OK) { return err; }

  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_begin_compute_pass(
    ngf_cmd_buffer               cmd_buf,
    const ngf_compute_pass_info* pass_info,
    ngf_compute_encoder*         enc) {
  (void)pass_info;
  ngf_error err = ngfvk_encoder_start(cmd_buf);
  if (err != NGF_ERROR_OK) return err;

  err = ngfvk_initialize_generic_encoder(cmd_buf, &enc->pvt_data_donotuse);
  if (err != NGF_ERROR_OK) { return err; }

  cmd_buf->compute_pass_active = true;
  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_end_render_pass(ngf_render_encoder enc) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);

  // Commit all the pending barriers.
  ngfvk_sync_commit_pending_barriers(&buf->pending_barriers, buf->vk_cmd_buffer);

  // Begin the real render pass.
  const ngf_render_pass_info* pass_info   = &buf->pending_render_pass_info;
  const VkRenderPass          render_pass = ngfvk_lookup_renderpass(
      pass_info->render_target,
      ngfvk_renderpass_ops_key(
          pass_info->render_target,
          pass_info->load_ops,
          pass_info->store_ops));

  const ngfvk_swapchain*  swapchain = &CURRENT_CONTEXT->swapchain;
  const ngf_render_target target    = pass_info->render_target;

  const VkFramebuffer fb =
      target->is_default ? swapchain->framebufs[swapchain->image_idx] : target->frame_buffer;
  const VkExtent2D render_extent = {
      target->is_default ? CURRENT_CONTEXT->swapchain_info.width : target->width,
      target->is_default ? CURRENT_CONTEXT->swapchain_info.height : target->height};

  const uint32_t clear_value_count = buf->pending_clear_value_count;
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
  vkCmdBeginRenderPass(buf->vk_cmd_buffer, &begin_info, VK_SUBPASS_CONTENTS_INLINE);

  // Clean up after the begin operation.
  ngfi_sa_reset(ngfi_tmp_store());

  // Encode each pending render command.
  ngfvk_cmd_buf_record_render_cmds(buf, &buf->in_pass_cmd_chnks);

  // Reset pending render command storage.
  ngfvk_cmd_buf_reset_render_cmds(buf);

  // Finish renderpass.
  vkCmdEndRenderPass(buf->vk_cmd_buffer);
  buf->renderpass_active = false;
  buf->active_rt         = NULL;

  return ngfvk_encoder_end(buf, &enc.pvt_data_donotuse);
}

ngf_error ngf_cmd_end_xfer_pass(ngf_xfer_encoder enc) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  return ngfvk_encoder_end(buf, &enc.pvt_data_donotuse);
}

ngf_error ngf_cmd_end_compute_pass(ngf_compute_encoder enc) {
  ngf_cmd_buffer cmd_buf       = NGFVK_ENC2CMDBUF(enc);
  cmd_buf->compute_pass_active = false;
  return ngfvk_encoder_end(cmd_buf, &enc.pvt_data_donotuse);
}

ngf_error ngf_start_cmd_buffer(ngf_cmd_buffer cmd_buf, ngf_frame_token token) {
  assert(cmd_buf);

  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_READY);

  cmd_buf->parent_frame        = token;
  cmd_buf->desc_pools_list     = NULL;
  cmd_buf->active_rt           = NULL;
  cmd_buf->active_gfx_pipe     = NULL;
  cmd_buf->active_compute_pipe = NULL;
  cmd_buf->compute_pass_active = false;
  cmd_buf->renderpass_active   = false;
  cmd_buf->npending_bind_ops   = 0u;

  ngfi_chnklist_clear(&cmd_buf->virt_bind_ops_ranges);
  ngfi_chnklist_clear(&cmd_buf->in_pass_cmd_chnks);
  ngfi_chnklist_clear(&cmd_buf->pending_barriers.chnklist);
  ngfi_dict_clear(cmd_buf->local_res_states);
  ngfvk_cleanup_pending_binds(cmd_buf);

  return ngfvk_cmd_buffer_allocate_for_frame(token, &cmd_buf->vk_cmd_pool, &cmd_buf->vk_cmd_buffer);
}

void ngf_destroy_cmd_buffer(ngf_cmd_buffer buffer) {
  if (buffer && buffer->state != NGFI_CMD_BUFFER_PENDING) {
    if (buffer->vk_cmd_buffer != VK_NULL_HANDLE) {
      vkFreeCommandBuffers(_vk.device, buffer->vk_cmd_pool, 1u, &buffer->vk_cmd_buffer);
    }
    ngfvk_cleanup_pending_binds(buffer);
    ngfi_dict_destroy(buffer->local_res_states);
    ngfi_chnklist_clear(&buffer->in_pass_cmd_chnks);
    ngfi_chnklist_clear(&buffer->virt_bind_ops_ranges);
    NGFI_FREE(buffer);
  } else if (buffer) {
    buffer->destroy_on_submit = true;
  }
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
    NGFI_TRANSITION_CMD_BUF(cmd_bufs[i], NGFI_CMD_BUFFER_PENDING);
    if (cmd_buf->desc_pools_list) {
      NGFVK_RETIRE_OBJECT(
          frame_res_data,
          NGFVK_RETIRE_OBJ_DESC_POOL_LIST,
          cmd_buf->desc_pools_list);
    }
    vkEndCommandBuffer(cmd_buf->vk_cmd_buffer);

    NGFI_DARRAY_APPEND(frame_res_data->submitted_cmd_bufs, cmd_buf);
    CURRENT_CONTEXT->cmd_buffer_counter++;
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_begin_frame(ngf_frame_token* token) {
  ngf_error err = NGF_ERROR_OK;

  // increment frame id.
  const uint32_t fi = (CURRENT_CONTEXT->frame_id + 1u) % CURRENT_CONTEXT->max_inflight_frames;
  CURRENT_CONTEXT->frame_id = fi;

  // setup frame capture
  if (_renderdoc.api && _renderdoc.capture_next) {
    _renderdoc.capture_next = false;
    _renderdoc.is_capturing = true;
    _renderdoc.api->StartFrameCapture(
        RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(_vk.instance),
        (RENDERDOC_WindowHandle)CURRENT_CONTEXT->swapchain_info.native_handle);
  }

  // reset stack allocators.
  ngfi_sa_reset(ngfi_tmp_store());
  ngfi_sa_reset(ngfi_frame_store());

  // Retire resources.
  ngfvk_frame_resources* next_frame_res = &CURRENT_CONTEXT->frame_res[fi];
  ngfvk_retire_resources(next_frame_res);

  const bool needs_present = CURRENT_CONTEXT->swapchain.vk_swapchain != VK_NULL_HANDLE;

  if (needs_present) {
    const VkResult acquire_result = vkAcquireNextImageKHR(
        _vk.device,
        CURRENT_CONTEXT->swapchain.vk_swapchain,
        UINT64_MAX,
        CURRENT_CONTEXT->swapchain.img_sems[fi],
        VK_NULL_HANDLE,
        &CURRENT_CONTEXT->swapchain.image_idx);
    if (acquire_result != VK_SUCCESS && acquire_result != VK_SUBOPTIMAL_KHR) {
      return NGF_ERROR_INVALID_OPERATION;
    }
  }

  CURRENT_CONTEXT->current_frame_token = ngfi_encode_frame_token(
      (uint16_t)((uintptr_t)CURRENT_CONTEXT & 0xffff),
      (uint8_t)CURRENT_CONTEXT->max_inflight_frames,
      (uint8_t)CURRENT_CONTEXT->frame_id);

  *token = CURRENT_CONTEXT->current_frame_token;
  return err;
}

void ngfi_dump_sys_alloc_dbgstats(FILE* out);

ngf_error ngf_get_current_swapchain_image(ngf_frame_token token, ngf_image* result) {
  assert(CURRENT_CONTEXT);
  assert(result);

  if (token != CURRENT_CONTEXT->current_frame_token) {
    NGFI_DIAG_ERROR("ending a frame with an unexpected frame token");
    return NGF_ERROR_INVALID_OPERATION;
  }
  if (CURRENT_CONTEXT->swapchain.vk_swapchain == VK_NULL_HANDLE) {
    NGFI_DIAG_ERROR("requesting a swapchain image handle from a context that does not have a swapchain");
    return NGF_ERROR_INVALID_OPERATION;
  }
  *result = CURRENT_CONTEXT->swapchain.wrapper_imgs[CURRENT_CONTEXT->swapchain.image_idx];
  return NGF_ERROR_OK;
}
ngf_error ngf_end_frame(ngf_frame_token token) {
  ngfi_blkalloc_cleanup(CURRENT_CONTEXT->blkalloc);

  if (token != CURRENT_CONTEXT->current_frame_token) {
    NGFI_DIAG_ERROR("ending a frame with an unexpected frame token");
    return NGF_ERROR_INVALID_OPERATION;
  }

  ngf_error err = NGF_ERROR_OK;

  // Obtain the current frame resource structure.
  const uint32_t         fi        = CURRENT_CONTEXT->frame_id;
  ngfvk_frame_resources* frame_res = &CURRENT_CONTEXT->frame_res[fi];

  frame_res->nwait_fences = 0u;

  // Submit pending commands & present.
  VkSemaphore image_semaphore = VK_NULL_HANDLE;
  const bool  needs_present   = CURRENT_CONTEXT->swapchain.vk_swapchain != VK_NULL_HANDLE;
  if (needs_present) { image_semaphore = CURRENT_CONTEXT->swapchain.img_sems[fi]; }

  ngf_error submit_result = ngfvk_submit_pending_cmd_buffers(
      frame_res,
      image_semaphore,
      frame_res->fences[frame_res->nwait_fences++]);

  // Present if necessary.
  if (submit_result == NGF_ERROR_OK && needs_present) {
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
  if (_renderdoc.api && _renderdoc.is_capturing) {
    _renderdoc.api->EndFrameCapture(
        RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(_vk.instance),
        (RENDERDOC_WindowHandle)CURRENT_CONTEXT->swapchain_info.native_handle);
    _renderdoc.is_capturing = false;
    _renderdoc.capture_next = false;
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
    vk_binding_desc->stride                            = binding_desc->stride;
    vk_binding_desc->inputRate = get_vk_input_rate(binding_desc->input_rate);
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
      .depthBiasEnable         = info->rasterization->enable_depth_bias ? VK_TRUE : VK_FALSE,
      .depthBiasConstantFactor = 0.0f,
      .depthBiasClamp          = 0.0f,
      .depthBiasSlopeFactor    = 0.0f,
      .lineWidth               = 1.0f};

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
    if (info->compatible_rt_attachment_descs->descs[i].type == NGF_ATTACHMENT_COLOR &&
        !info->compatible_rt_attachment_descs->descs[i].is_resolve)
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
      VK_DYNAMIC_STATE_DEPTH_BOUNDS,
      VK_DYNAMIC_STATE_DEPTH_BIAS};
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
    attachment_compat_pass_descs[i].load_op  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment_compat_pass_descs[i].store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment_compat_pass_descs[i].is_resolve =
        info->compatible_rt_attachment_descs->descs[i].is_resolve;
    attachment_compat_pass_descs[i].layout = VK_IMAGE_LAYOUT_GENERAL;
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
    NGFVK_RETIRE_OBJECT(res, NGFVK_RETIRE_OBJ_RENDER_PASS, p->compatible_render_pass);
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
  if (err != NGF_ERROR_OK) { goto ngf_create_compute_pipeline_cleanup; }

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

  uint32_t ncolor_attachments   = 0u;
  uint32_t nresolve_attachments = 0u;

  for (uint32_t a = 0u; a < info->attachment_descriptions->ndescs; ++a) {
    if (info->attachment_descriptions->descs[a].type == NGF_ATTACHMENT_COLOR) {
      if (info->attachment_descriptions->descs[a].is_resolve) {
        ++nresolve_attachments;
      } else {
        ++ncolor_attachments;
      }
    }
  }

  if (nresolve_attachments > 0 && ncolor_attachments != nresolve_attachments) {
    NGFI_DIAG_ERROR("the same number of resolve and color attachments must be provided");
    err = NGF_ERROR_INVALID_OPERATION;
    goto ngf_create_render_target_cleanup;
  }

  ngfvk_attachment_pass_desc* vk_attachment_pass_descs =
      NGFI_ALLOCN(ngfvk_attachment_pass_desc, info->attachment_descriptions->ndescs);

  VkImageView* attachment_views  = NGFI_ALLOCN(VkImageView, info->attachment_descriptions->ndescs);
  ngf_image*   attachment_images = NGFI_ALLOCN(ngf_image, info->attachment_descriptions->ndescs);

  if (vk_attachment_pass_descs == NULL || attachment_views == NULL || attachment_images == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_create_render_target_cleanup;
  }

  for (uint32_t a = 0u; a < info->attachment_descriptions->ndescs; ++a) {
    const ngf_attachment_description* ngf_attachment_desc =
        &info->attachment_descriptions->descs[a];
    ngfvk_attachment_pass_desc* attachment_pass_desc = &vk_attachment_pass_descs[a];
    const ngf_attachment_type   attachment_type      = ngf_attachment_desc->type;

    rt->have_resolve_attachments |= ngf_attachment_desc->is_resolve;

    switch (attachment_type) {
    case NGF_ATTACHMENT_COLOR:
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
    attachment_pass_desc->is_resolve        = ngf_attachment_desc->is_resolve;

    // These are needed just to create a compatible render pass, load/store ops don't affect render
    // pass compatibility.
    const ngf_attachment_load_op  load_op  = NGF_LOAD_OP_DONTCARE;
    const ngf_attachment_store_op store_op = NGF_STORE_OP_DONTCARE;
    attachment_pass_desc->load_op          = get_vk_load_op(load_op);
    attachment_pass_desc->store_op         = get_vk_store_op(store_op);
    const bool attachment_is_cubemap       = attachment_img_ref->image->type == NGF_IMAGE_TYPE_CUBE;

    const VkImageAspectFlags subresource_aspect_flags =
        (attachment_type == NGF_ATTACHMENT_COLOR ? VK_IMAGE_ASPECT_COLOR_BIT : 0u) |
        (attachment_type == NGF_ATTACHMENT_DEPTH ? VK_IMAGE_ASPECT_DEPTH_BIT : 0u) |
        (attachment_type == NGF_ATTACHMENT_DEPTH_STENCIL
             ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
             : 0u);
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
        .format           = attachment_img->vk_fmt,
        .subresourceRange = {
            .baseArrayLayer = attachment_is_cubemap ? 6u * attachment_img_ref->layer +
                                                          attachment_img_ref->cubemap_face
                                                    : attachment_img_ref->layer,
            .baseMipLevel   = attachment_img_ref->mip_level,
            .layerCount     = 1u,
            .levelCount     = 1u,
            .aspectMask     = subresource_aspect_flags}};
    vk_err = vkCreateImageView(_vk.device, &image_view_create_info, NULL, &attachment_views[a]);

    attachment_images[a] = attachment_img;

    if (vk_err != VK_SUCCESS) {
      err = NGF_ERROR_OBJECT_CREATION_FAILED;
      goto ngf_create_render_target_cleanup;
    }
  }

  rt->attachment_image_views = attachment_views;
  rt->attachment_images      = attachment_images;

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
  if (target && (!target->is_default || CURRENT_CONTEXT->being_destroyed)) {
    if (target->is_default && CURRENT_CONTEXT->default_render_target != target) {
      NGFI_DIAG_ERROR("default RT can only be destroyed by owning context\n");
      return;
    }
    ngfvk_frame_resources* res = &CURRENT_CONTEXT->frame_res[CURRENT_CONTEXT->frame_id];
    if (!target->is_default) {
      if (target->frame_buffer != VK_NULL_HANDLE) {
        NGFVK_RETIRE_OBJECT(res, NGFVK_RETIRE_OBJ_FRAMEBUFFER, target->frame_buffer);
      }
    }
    if (target->compat_render_pass != VK_NULL_HANDLE) {
      NGFVK_RETIRE_OBJECT(res, NGFVK_RETIRE_OBJ_RENDER_PASS, target->compat_render_pass);
    }
    if (target->attachment_image_views) {
      for (size_t i = 0; i < target->nattachments; ++i) {
        NGFVK_RETIRE_OBJECT(res, NGFVK_RETIRE_OBJ_IMG_VIEW, target->attachment_image_views[i]);
      }
      NGFI_FREEN(target->attachment_image_views, target->nattachments);
    }
    if (target->attachment_images) { NGFI_FREEN(target->attachment_images, target->nattachments); }
    if (target->attachment_descs) { NGFI_FREEN(target->attachment_descs, target->nattachments); }
    if (target->attachment_compat_pass_descs) {
      NGFI_FREEN(target->attachment_compat_pass_descs, target->nattachments);
    }
    if (target->is_default) CURRENT_CONTEXT->default_render_target = NULL;
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

  ngfi_sa_reset(ngfi_tmp_store());

  // Prepare a batch of sync requests by scanning all pending bind operations.
  ngfvk_sync_req_batch sync_req_batch;
  ngfvk_sync_req_batch_init(cmd_buf->npending_bind_ops, &sync_req_batch);

  NGFI_CHNKLIST_FOR_EACH(cmd_buf->pending_bind_ops, const ngf_resource_bind_op, bind_op) {
    ngfvk_sync_req sync_req =
        ngfvk_sync_req_for_bind_op(bind_op, &cmd_buf->active_compute_pipe->generic_pipeline);
    if (sync_req.barrier_masks.stage_mask == 0u) { continue; }
    const ngfvk_sync_res res = ngfvk_sync_res_from_bind_op(bind_op);
    if (res.type == NGFVK_SYNC_RES_COUNT) { continue; }
    ngfvk_sync_req_batch_add_with_lookup(&sync_req_batch, cmd_buf, &res, &sync_req);
  }

  // Emit the necessary barriers prior to dispatch.
  ngfvk_sync_req_batch_commit(&sync_req_batch, cmd_buf);

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

  uint32_t nmax_pending_sync_reqs = 2u;
  NGFI_CHNKLIST_FOR_EACH(cmd_buf->virt_bind_ops_ranges, ngfi_chnk_range, r) {
    nmax_pending_sync_reqs += r->size;
  }

  ngfvk_sync_req_batch sync_req_batch;
  ngfvk_sync_req_batch_init(nmax_pending_sync_reqs, &sync_req_batch);

  const ngfvk_sync_req attr_buf_sync_req = {
      .barrier_masks =
          {.access_mask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
           .stage_mask  = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT},
      .layout = VK_IMAGE_LAYOUT_UNDEFINED};
  if (cmd_buf->active_attr_buf) {
    const ngfvk_sync_res attr_buf_res = ngfvk_sync_res_from_buf(cmd_buf->active_attr_buf);
    ngfvk_sync_req_batch_add_with_lookup(
        &sync_req_batch,
        cmd_buf,
        &attr_buf_res,
        &attr_buf_sync_req);
  }
  if (indexed && cmd_buf->active_idx_buf) {
    const ngfvk_sync_req idx_buf_sync_req = {
        .barrier_masks =
            {.access_mask = VK_ACCESS_INDEX_READ_BIT,
             .stage_mask  = VK_PIPELINE_STAGE_VERTEX_INPUT_BIT},
        .layout = VK_IMAGE_LAYOUT_UNDEFINED};
    const ngfvk_sync_res idx_buf_res = ngfvk_sync_res_from_buf(cmd_buf->active_idx_buf);
    ngfvk_sync_req_batch_add_with_lookup(&sync_req_batch, cmd_buf, &idx_buf_res, &idx_buf_sync_req);
  }
  cmd_buf->active_attr_buf = NULL;
  cmd_buf->active_idx_buf  = NULL;

  NGFI_CHNKLIST_FOR_EACH(cmd_buf->virt_bind_ops_ranges, ngfi_chnk_range, r) {
    const ngfvk_render_cmd* render_cmds =
        ngfi_chnk_data(r->chnk, r->start * sizeof(ngfvk_render_cmd));
    for (size_t j = 0u; j < r->size; ++j) {
      const ngfvk_render_cmd* render_cmd = &render_cmds[j];
      assert(render_cmd->type == NGFVK_RENDER_CMD_BIND_RESOURCE);
      const ngfvk_sync_req sync_req = ngfvk_sync_req_for_bind_op(
          &render_cmd->data.bind_resource,
          &cmd_buf->active_gfx_pipe->generic_pipeline);
      if (sync_req.barrier_masks.stage_mask == 0u) { continue; }
      const ngfvk_sync_res sync_res = ngfvk_sync_res_from_bind_op(&render_cmd->data.bind_resource);
      ngfvk_sync_req_batch_add_with_lookup(&sync_req_batch, cmd_buf, &sync_res, &sync_req);
    }
  }
  ngfi_chnklist_clear(&cmd_buf->virt_bind_ops_ranges);
  ngfvk_sync_req_batch_process(&sync_req_batch, cmd_buf);

  const ngfvk_render_cmd cmd = {
      .type = NGFVK_RENDER_CMD_DRAW,
      .data = {
          .draw = {
              .first_element = first_element,
              .indexed       = indexed,
              .nelements     = nelements,
              .ninstances    = ninstances}}};
  ngfvk_cmd_buf_add_render_cmd(cmd_buf, &cmd, true);
}

void ngf_cmd_bind_gfx_pipeline(ngf_render_encoder enc, ngf_graphics_pipeline pipeline) {
  ngf_cmd_buffer         buf = NGFVK_ENC2CMDBUF(enc);
  const ngfvk_render_cmd cmd = {
      .type = NGFVK_RENDER_CMD_BIND_PIPELINE,
      .data = {.pipeline = pipeline}};
  ngfvk_cmd_buf_add_render_cmd(buf, &cmd, true);
  buf->active_gfx_pipe = pipeline;
}

void ngf_cmd_bind_resources(
    ngf_render_encoder          enc,
    const ngf_resource_bind_op* bind_operations,
    uint32_t                    nbind_operations) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  if (nbind_operations <= 0u) { return; }
  ngfi_chnk_range curr_range = {.chnk = NULL};
  for (size_t i = 0u; i < nbind_operations; ++i) {
    const ngfvk_render_cmd cmd = {
        .type = NGFVK_RENDER_CMD_BIND_RESOURCE,
        .data = {.bind_resource = bind_operations[i]}};
    const ngfvk_render_cmd* cmd_data =
        ngfi_chnklist_append(&buf->in_pass_cmd_chnks, &cmd, sizeof(cmd));
    ngfi_chnk_hdr* last_chnk = NGFI_CHNK_FROM_NODE(buf->in_pass_cmd_chnks.firstchnk->clnode.prev);
    if (last_chnk != curr_range.chnk) {
      if (curr_range.chnk) {
        ngfi_chnklist_append(&buf->virt_bind_ops_ranges, &curr_range, sizeof(curr_range));
      }
      curr_range.chnk = last_chnk;
      curr_range.start =
          (uint32_t)(cmd_data - (const ngfvk_render_cmd*)ngfi_chnk_data(last_chnk, 0u));
      curr_range.size = 0u;
    }
    ++curr_range.size;
  }
  if (curr_range.chnk) {
    ngfi_chnklist_append(&buf->virt_bind_ops_ranges, &curr_range, sizeof(curr_range));
  }
}

void ngf_cmd_bind_compute_resources(
    ngf_compute_encoder         enc,
    const ngf_resource_bind_op* bind_operations,
    uint32_t                    nbind_operations) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  ngfvk_cmd_bind_resources(buf, bind_operations, nbind_operations);
}

void ngf_cmd_bind_compute_pipeline(ngf_compute_encoder enc, ngf_compute_pipeline pipeline) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  if (buf->active_compute_pipe && buf->npending_bind_ops > 0u) { ngfvk_execute_pending_binds(buf); }

  buf->active_compute_pipe = pipeline;
  vkCmdBindPipeline(
      buf->vk_cmd_buffer,
      VK_PIPELINE_BIND_POINT_COMPUTE,
      pipeline->generic_pipeline.vk_pipeline);
}

void ngf_cmd_viewport(ngf_render_encoder enc, const ngf_irect2d* r) {
  ngf_cmd_buffer         buf = NGFVK_ENC2CMDBUF(enc);
  const ngfvk_render_cmd cmd = {.type = NGFVK_RENDER_CMD_SET_VIEWPORT, .data = {.rect = *r}};
  ngfvk_cmd_buf_add_render_cmd(buf, &cmd, true);
}

void ngf_cmd_scissor(ngf_render_encoder enc, const ngf_irect2d* r) {
  ngf_cmd_buffer         buf = NGFVK_ENC2CMDBUF(enc);
  const ngfvk_render_cmd cmd = {.type = NGFVK_RENDER_CMD_SET_SCISSOR, .data = {.rect = *r}};
  ngfvk_cmd_buf_add_render_cmd(buf, &cmd, true);
}

void ngf_cmd_stencil_reference(ngf_render_encoder enc, uint32_t front, uint32_t back) {
  ngf_cmd_buffer         buf = NGFVK_ENC2CMDBUF(enc);
  const ngfvk_render_cmd cmd = {
      .type = NGFVK_RENDER_CMD_SET_STENCIL_REFERENCE,
      .data = {.stencil_values = {.front = front, .back = back}}};
  ngfvk_cmd_buf_add_render_cmd(buf, &cmd, true);
}

void ngf_cmd_stencil_compare_mask(ngf_render_encoder enc, uint32_t front, uint32_t back) {
  ngf_cmd_buffer         buf = NGFVK_ENC2CMDBUF(enc);
  const ngfvk_render_cmd cmd = {
      .type = NGFVK_RENDER_CMD_SET_STENCIL_COMPARE_MASK,
      .data = {.stencil_values = {.front = front, .back = back}}};
  ngfvk_cmd_buf_add_render_cmd(buf, &cmd, true);
}

void ngf_cmd_stencil_write_mask(ngf_render_encoder enc, uint32_t front, uint32_t back) {
  ngf_cmd_buffer         buf = NGFVK_ENC2CMDBUF(enc);
  const ngfvk_render_cmd cmd = {
      .type = NGFVK_RENDER_CMD_SET_STENCIL_WRITE_MASK,
      .data = {.stencil_values = {.front = front, .back = back}}};
  ngfvk_cmd_buf_add_render_cmd(buf, &cmd, true);
}

void ngf_cmd_set_depth_bias(
    ngf_render_encoder enc,
    float              const_scale,
    float              slope_scale,
    float              clamp) {
  ngf_cmd_buffer         buf = NGFVK_ENC2CMDBUF(enc);
  const ngfvk_render_cmd cmd = {
      .type = NGFVK_RENDER_CMD_SET_DEPTH_BIAS,
      .data = {
          .depth_bias =
              {.const_factor = const_scale, .slope_factor = slope_scale, .clamp = clamp}}};
  ngfvk_cmd_buf_add_render_cmd(buf, &cmd, true);
}

void ngf_cmd_bind_attrib_buffer(
    ngf_render_encoder enc,
    ngf_buffer         abuf,
    uint32_t           binding,
    size_t             offset) {
  ngf_cmd_buffer         buf = NGFVK_ENC2CMDBUF(enc);
  const ngfvk_render_cmd cmd = {
      .type = NGFVK_RENDER_CMD_BIND_ATTRIB_BUFFER,
      .data = {.bind_attrib_buffer = {.binding = binding, .buffer = abuf, .offset = offset}}};
  buf->active_attr_buf = abuf;
  ngfvk_cmd_buf_add_render_cmd(buf, &cmd, true);
}

void ngf_cmd_bind_index_buffer(
    ngf_render_encoder enc,
    ngf_buffer         ibuf,
    size_t             offset,
    ngf_type           index_type) {
  ngf_cmd_buffer         buf = NGFVK_ENC2CMDBUF(enc);
  const ngfvk_render_cmd cmd = {
      .type = NGFVK_RENDER_CMD_BIND_INDEX_BUFFER,
      .data = {.bind_index_buffer = {.buffer = ibuf, .offset = offset, .type = index_type}}};
  buf->active_idx_buf = ibuf;
  ngfvk_cmd_buf_add_render_cmd(buf, &cmd, true);
}

void ngf_cmd_copy_buffer(
    ngf_xfer_encoder enc,
    ngf_buffer       src,
    ngf_buffer       dst,
    size_t           size,
    size_t           src_offset,
    size_t           dst_offset) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  assert(buf);
  ngfvk_sync_req_batch sync_req_batch;
  ngfi_sa_reset(ngfi_tmp_store());
  ngfvk_sync_req_batch_init(2, &sync_req_batch);
  const ngfvk_sync_req src_sync_req = {
      .barrier_masks =
          {.access_mask = VK_ACCESS_TRANSFER_READ_BIT,
           .stage_mask  = VK_PIPELINE_STAGE_TRANSFER_BIT},
      .layout = VK_IMAGE_LAYOUT_UNDEFINED};
  const ngfvk_sync_res src_sync_res = ngfvk_sync_res_from_buf(src);
  ngfvk_sync_req_batch_add_with_lookup(&sync_req_batch, buf, &src_sync_res, &src_sync_req);
  const ngfvk_sync_req dst_sync_req = {
      .barrier_masks =
          {.access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
           .stage_mask  = VK_PIPELINE_STAGE_TRANSFER_BIT},
      .layout = VK_IMAGE_LAYOUT_UNDEFINED};
  const ngfvk_sync_res dst_sync_res = ngfvk_sync_res_from_buf(dst);
  ngfvk_sync_req_batch_add_with_lookup(&sync_req_batch, buf, &dst_sync_res, &dst_sync_req);
  ngfvk_sync_req_batch_commit(&sync_req_batch, buf);

  const VkBufferCopy copy_region = {.srcOffset = src_offset, .dstOffset = dst_offset, .size = size};
  vkCmdCopyBuffer(
      buf->vk_cmd_buffer,
      (VkBuffer)src->alloc.obj_handle,
      (VkBuffer)dst->alloc.obj_handle,
      1u,
      &copy_region);
}
void ngf_cmd_write_image(
    ngf_xfer_encoder       enc,
    ngf_buffer             src,
    ngf_image              dst,
    const ngf_image_write* writes,
    uint32_t               nwrites) {
  ngf_cmd_buffer cmd_buf = NGFVK_ENC2CMDBUF(enc);
  assert(cmd_buf);
  assert(nwrites == 0u || writes);
  if (nwrites == 0u) return;
  ngfvk_sync_req_batch sync_req_batch;
  ngfi_sa_reset(ngfi_tmp_store());
  ngfvk_sync_req_batch_init(2, &sync_req_batch);
  const ngfvk_sync_req src_sync_req = {
      .barrier_masks =
          {.access_mask = VK_ACCESS_TRANSFER_READ_BIT,
           .stage_mask  = VK_PIPELINE_STAGE_TRANSFER_BIT},
      .layout = VK_IMAGE_LAYOUT_UNDEFINED};
  const ngfvk_sync_res src_sync_res = ngfvk_sync_res_from_buf(src);
  ngfvk_sync_req_batch_add_with_lookup(&sync_req_batch, cmd_buf, &src_sync_res, &src_sync_req);
  const ngfvk_sync_req dst_sync_req = {
      .barrier_masks =
          {.access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
           .stage_mask  = VK_PIPELINE_STAGE_TRANSFER_BIT},
      .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
  const ngfvk_sync_res dst_sync_res = ngfvk_sync_res_from_img(dst);
  ngfvk_sync_req_batch_add_with_lookup(&sync_req_batch, cmd_buf, &dst_sync_res, &dst_sync_req);
  ngfvk_sync_req_batch_commit(&sync_req_batch, cmd_buf);

  ngfi_sa_reset(ngfi_tmp_store());
  VkBufferImageCopy* vk_writes =
      ngfi_sa_alloc(ngfi_tmp_store(), sizeof(VkBufferImageCopy) * nwrites);
  if (vk_writes) {
    for (size_t i = 0u; i < nwrites; ++i) {
      const ngf_image_write* ngf_write = &writes[i];
      VkBufferImageCopy*     vk_write  = &vk_writes[i];
      memset(vk_write, 0, sizeof(VkBufferImageCopy));
      vk_write->bufferOffset                    = ngf_write->src_offset;
      vk_write->imageOffset.x                   = ngf_write->dst_offset.x;
      vk_write->imageOffset.y                   = ngf_write->dst_offset.y;
      vk_write->imageOffset.z                   = ngf_write->dst_offset.z;
      vk_write->imageExtent.width               = ngf_write->extent.width;
      vk_write->imageExtent.height              = ngf_write->extent.height;
      vk_write->imageExtent.depth               = ngf_write->extent.depth;
      vk_write->imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
      vk_write->imageSubresource.mipLevel       = ngf_write->dst_level;
      vk_write->imageSubresource.baseArrayLayer = ngf_write->dst_base_layer;
      vk_write->imageSubresource.layerCount     = ngf_write->nlayers;
    }
    vkCmdCopyBufferToImage(
        cmd_buf->vk_cmd_buffer,
        (VkBuffer)src->alloc.obj_handle,
        (VkImage)dst->alloc.obj_handle,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        nwrites,
        vk_writes);
  } else {
    NGFI_DIAG_ERROR("Image write failed");
  }
}

void ngf_cmd_copy_image_to_buffer(
    ngf_xfer_encoder    enc,
    const ngf_image_ref src,
    ngf_offset3d        src_offset,
    ngf_extent3d        src_extent,
    uint32_t            nlayers,
    ngf_buffer          dst,
    size_t              dst_offset) {
  ngf_cmd_buffer buf = NGFVK_ENC2CMDBUF(enc);
  assert(buf);
  ngfvk_sync_req_batch sync_req_batch;
  ngfi_sa_reset(ngfi_tmp_store());
  ngfvk_sync_req_batch_init(2, &sync_req_batch);
  const ngfvk_sync_req src_sync_req = {
      .barrier_masks =
          {.access_mask = VK_ACCESS_TRANSFER_READ_BIT,
           .stage_mask  = VK_PIPELINE_STAGE_TRANSFER_BIT},
      .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
  const ngfvk_sync_res src_sync_res = ngfvk_sync_res_from_img(src.image);
  ngfvk_sync_req_batch_add_with_lookup(&sync_req_batch, buf, &src_sync_res, &src_sync_req);
  const ngfvk_sync_req dst_sync_req = {
      .barrier_masks =
          {.access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
           .stage_mask  = VK_PIPELINE_STAGE_TRANSFER_BIT},
      .layout = VK_IMAGE_LAYOUT_UNDEFINED};
  const ngfvk_sync_res dst_sync_res = ngfvk_sync_res_from_buf(dst);
  ngfvk_sync_req_batch_add_with_lookup(&sync_req_batch, buf, &dst_sync_res, &dst_sync_req);
  ngfvk_sync_req_batch_commit(&sync_req_batch, buf);

  const uint32_t src_layer =
      src.image->type == NGF_IMAGE_TYPE_CUBE ? 6u * src.layer + src.cubemap_face : src.layer;
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
      .imageExtent =
          {.width = src_extent.width, .height = src_extent.height, .depth = src_extent.depth}};

  vkCmdCopyImageToBuffer(
      buf->vk_cmd_buffer,
      (VkImage)src.image->alloc.obj_handle,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      (VkBuffer)dst->alloc.obj_handle,
      1u,
      &copy_op);
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
  // TODO: hazard-track images on mip + level granularity.

  ngfvk_sync_req sync_req = {
      .barrier_masks =
          {.access_mask = VK_ACCESS_TRANSFER_WRITE_BIT,
           .stage_mask  = VK_PIPELINE_STAGE_TRANSFER_BIT},
      .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
  ngfvk_sync_res img_res = ngfvk_sync_res_from_img(img);
  ngfvk_handle_single_sync_req(buf, &img_res, &sync_req);

  uint32_t src_w = img->extent.width, src_h = img->extent.height, src_d = img->extent.depth,
           dst_w = 0, dst_h = 0, dst_d = 0;
  const uint32_t nlayers = img->nlayers;

  for (uint32_t src_level = 0u; src_level < img->nlevels; ++src_level) {
    const uint32_t dst_level                    = src_level + 1u;
    dst_w                                       = src_w > 1u ? (src_w >> 1u) : 1u;
    dst_h                                       = src_h > 1u ? (src_h >> 1u) : 1u;
    dst_d                                       = src_d > 1u ? (src_d >> 1u) : 1u;
    const VkImageMemoryBarrier pre_blit_barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext               = NULL,
        .srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = (VkImage)img->alloc.obj_handle,
        .subresourceRange    = {
               .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
               .baseMipLevel   = src_level,
               .levelCount     = 1u,
               .baseArrayLayer = 0u,
               .layerCount     = nlayers}};
    vkCmdPipelineBarrier(
        buf->vk_cmd_buffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0u,
        0u,
        NULL,
        0u,
        NULL,
        1u,
        &pre_blit_barrier);
    if (src_level < img->nlevels - 1) {
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
      src_w = dst_w;
      src_h = dst_h;
      src_d = dst_d;
    }
  }
  ngfvk_sync_res       r             = ngfvk_sync_res_from_img(img);
  ngfvk_sync_res_data* sync_res_data = NULL;
  ngfvk_cmd_buf_lookup_sync_res(buf, &r, &sync_res_data);
  sync_res_data->sync_state.active_readers_masks.stage_mask |= VK_PIPELINE_STAGE_TRANSFER_BIT;
  sync_res_data->sync_state.active_readers_masks.access_mask |= VK_ACCESS_TRANSFER_READ_BIT;
  sync_res_data->sync_state.layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

  return NGF_ERROR_OK;
}

void ngf_cmd_begin_debug_group(ngf_cmd_buffer cmd_buffer, const char* name) {
  ngfvk_debug_label_begin(cmd_buffer->vk_cmd_buffer, name);
}

void ngf_cmd_end_current_debug_group(ngf_cmd_buffer cmd_buffer) {
  ngfvk_debug_label_end(cmd_buffer->vk_cmd_buffer);
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
  buf_view->buffer = info->buffer;
  return NGF_ERROR_OK;
}

void ngf_destroy_texel_buffer_view(ngf_texel_buffer_view buf_view) {
  if (buf_view) {
    const uint32_t fi = CURRENT_CONTEXT->frame_id;
    NGFVK_RETIRE_OBJECT(
        &(CURRENT_CONTEXT->frame_res[fi]),
        NGFVK_RETIRE_OBJ_BUF_VIEW,
        buf_view->vk_buf_view);
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
  if (info->storage_type > NGF_BUFFER_STORAGE_DEVICE_LOCAL &&
      !DEVICE_CAPS.device_local_memory_is_host_visible) {
    NGFI_DIAG_ERROR("Host-visible device-local storage requested, but not supported.");
    return NGF_ERROR_INVALID_OPERATION;
  }

  ngf_buffer buf = NGFI_ALLOC(ngf_buffer_t);
  *result        = buf;
  if (buf == NULL) return NGF_ERROR_OUT_OF_MEM;

  const VkBufferUsageFlags    vk_usage_flags = get_vk_buffer_usage(info->buffer_usage);
  const VkMemoryPropertyFlags vk_mem_flags   = get_vk_memory_flags(info->storage_type);
  const bool     vk_mem_is_host_visible      = vk_mem_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  const uint32_t vma_usage_flags             = info->storage_type >= NGF_BUFFER_STORAGE_DEVICE_LOCAL
                                                   ? VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
                                                   : VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
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
      .flags          = ngfvk_get_vma_alloc_flags(info->storage_type),
      .usage          = vma_usage_flags,
      .requiredFlags  = vk_mem_flags,
      .preferredFlags = 0u,
      .memoryTypeBits = 0u,
      .pool           = VK_NULL_HANDLE,
      .pUserData      = NULL};

  VmaAllocationInfo alloc_info;
  alloc_info.pMappedData = NULL;
  VkResult vkresult      = vmaCreateBuffer(
      _vk.allocator,
      &buf_vk_info,
      &buf_alloc_info,
      (VkBuffer*)&alloc->obj_handle,
      &alloc->vma_alloc,
      &alloc_info);
  alloc->mapped_data = vk_mem_is_host_visible ? alloc_info.pMappedData : NULL;
  err                = (vkresult == VK_SUCCESS) ? NGF_ERROR_OK : NGF_ERROR_INVALID_OPERATION;

  if (err != NGF_ERROR_OK) {
    NGFI_FREE(buf);
  } else {
    buf->size = info->size;
  }

  buf->storage_type = info->storage_type;
  buf->usage_flags  = info->buffer_usage;
  buf->hash         = ngfvk_ptr_hash(buf);
  memset(&buf->sync_state, 0, sizeof(buf->sync_state));
  buf->sync_state.layout = VK_IMAGE_LAYOUT_UNDEFINED;

  return err;
}

void ngf_destroy_buffer(ngf_buffer buffer) {
  if (buffer) {
    const uint32_t fi = CURRENT_CONTEXT->frame_id;
    NGFVK_RETIRE_OBJECT(&CURRENT_CONTEXT->frame_res[fi], NGFVK_RETIRE_OBJ_BUF, buffer);
  }
}

void* ngf_buffer_map_range(ngf_buffer buf, size_t offset, size_t size) {
  NGFI_IGNORE_VAR(size);
  buf->mapped_offset = offset;
  return (uint8_t*)buf->alloc.mapped_data + buf->mapped_offset;
}

void ngf_buffer_flush_range(ngf_buffer buf, size_t offset, size_t size) {
  vmaFlushAllocation(_vk.allocator, buf->alloc.vma_alloc, buf->mapped_offset + offset, size);
}

void ngf_buffer_unmap(ngf_buffer buf) {
  // vk buffers are persistently mapped.
  NGFI_IGNORE_VAR(buf);
}

ngf_error ngf_create_image_view(const ngf_image_view_info* info, ngf_image_view* result) {
  assert(info);
  assert(result);

  *result = NGFI_ALLOC(ngf_image_view_t);
  if (*result == NULL) {
    return NGF_ERROR_OUT_OF_MEM;
  }

  const VkImageViewCreateInfo vk_view_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .pNext = NULL,
    .flags = 0u,
    .image = (VkImage)info->src_image->alloc.obj_handle,
    .viewType = get_vk_image_view_type(info->view_type, info->nlayers),
    .format = get_vk_image_format(info->view_format),
    .components = {
          .r = VK_COMPONENT_SWIZZLE_R,
          .g = VK_COMPONENT_SWIZZLE_G,
          .b = VK_COMPONENT_SWIZZLE_B,
          .a = VK_COMPONENT_SWIZZLE_A},
     .subresourceRange = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel = info->base_mip_level,
          .levelCount = info->nmips,
          .baseArrayLayer = info->base_layer,
          .layerCount = info->nlayers
     }
  };

  const VkResult vk_err = vkCreateImageView(_vk.device, &vk_view_info, NULL, &(*result)->vk_view);
  if (vk_err != VK_SUCCESS) { 
      NGFI_FREE(result);
      return NGF_ERROR_OBJECT_CREATION_FAILED;
  }
  (*result)->src = info->src_image;

  return NGF_ERROR_OK;
}

void ngf_destroy_image_view(ngf_image_view view) {
  if (view) {
    if (view->vk_view) {
      ngfvk_frame_resources* res = &CURRENT_CONTEXT->frame_res[CURRENT_CONTEXT->frame_id];
      NGFVK_RETIRE_OBJECT(res, NGFVK_RETIRE_OBJ_IMG_VIEW, view->vk_view);
    }
    NGFI_FREE(view);
  }
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

  ngf_error                err             = NGF_ERROR_OK;
  const bool               is_cubemap      = info->type == NGF_IMAGE_TYPE_CUBE;
  const VkFormat           vk_image_format = get_vk_image_format(info->format);
  const VkImageType        vk_image_type   = get_vk_image_type(info->type);
  const VkImageCreateFlags create_flags    = is_cubemap ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u;
  VkImageFormatProperties  dummy_props;
  const bool               optimal_tiling_supported = vkGetPhysicalDeviceImageFormatProperties(
                                            _vk.phys_dev,
                                            vk_image_format,
                                            vk_image_type,
                                            VK_IMAGE_TILING_OPTIMAL,
                                            usage_flags,
                                            create_flags,
                                            &dummy_props) == VK_SUCCESS;
  const VkImageCreateInfo vk_image_info = {
      .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .pNext     = NULL,
      .flags     = create_flags,
      .imageType = vk_image_type,
      .extent =
          {.width = info->extent.width, .height = info->extent.height, .depth = info->extent.depth},
      .format                = vk_image_format,
      .mipLevels             = info->nmips,
      .arrayLayers           = info->nlayers * (!is_cubemap ? 1u : 6u),
      .samples               = get_vk_sample_count(info->sample_count),
      .usage                 = usage_flags,
      .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .tiling = optimal_tiling_supported ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_LINEAR,
      .pQueueFamilyIndices = NULL,
      .initialLayout       = VK_IMAGE_LAYOUT_UNDEFINED};

  VmaAllocationCreateInfo vma_alloc_info = {
      .flags          = 0u,
      .usage          = VMA_MEMORY_USAGE_GPU_ONLY,
      .requiredFlags  = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
      .preferredFlags = 0u,
      .memoryTypeBits = 0u,
      .pool           = VK_NULL_HANDLE,
      .pUserData      = NULL};

  ngfvk_alloc alloc;

  const VkResult create_alloc_vkerr = vmaCreateImage(
      _vk.allocator,
      &vk_image_info,
      &vma_alloc_info,
      (VkImage*)&alloc.obj_handle,
      &alloc.vma_alloc,
      NULL);
  if (create_alloc_vkerr != VK_SUCCESS) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngf_create_image_cleanup;
  }

  err = ngfvk_create_image(info, &alloc, true, result);
  if (err != NGF_ERROR_OK) { goto ngf_create_image_cleanup; }

ngf_create_image_cleanup:
  if (err != NGF_ERROR_OK) { ngf_destroy_image(*result); }
  return err;
}

void ngf_destroy_image(ngf_image img) {
  if (img != NULL) {
    const uint32_t fi = CURRENT_CONTEXT->frame_id;
    NGFVK_RETIRE_OBJECT(&CURRENT_CONTEXT->frame_res[fi], NGFVK_RETIRE_OBJ_IMG, img);
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
      .compareEnable           = info->compare_op != NGF_COMPARE_OP_NEVER,
      .compareOp               = get_vk_compare_op(info->compare_op),
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
    NGFVK_RETIRE_OBJECT(
        &CURRENT_CONTEXT->frame_res[fi],
        NGFVK_RETIRE_OBJ_SAMPLER,
        sampler->vksampler);
    NGFI_FREE(sampler);
  }
}

void ngf_finish(void) {
  if (CURRENT_CONTEXT->current_frame_token != ~0u) {
    ngfvk_frame_resources* frame_res = &CURRENT_CONTEXT->frame_res[CURRENT_CONTEXT->frame_id];
    ngfvk_submit_pending_cmd_buffers(frame_res, VK_NULL_HANDLE, VK_NULL_HANDLE);
  }
  vkDeviceWaitIdle(_vk.device);
}

void ngf_renderdoc_capture_next_frame() {
  if (_renderdoc.api) _renderdoc.capture_next = true;
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

uintptr_t ngf_get_vk_device_handle() {
  return (uintptr_t)_vk.device;
}

uintptr_t ngf_get_vk_instance_handle() {
  return (uintptr_t)_vk.instance;
}

uintptr_t ngf_get_vk_image_handle(ngf_image image) {
  return image->alloc.obj_handle;
}

uintptr_t ngf_get_vk_buffer_handle(ngf_buffer buffer) {
  return buffer->alloc.obj_handle;
}

uintptr_t ngf_get_vk_cmd_buffer_handle(ngf_cmd_buffer cmd_buffer) {
  return (uintptr_t)(cmd_buffer->vk_cmd_buffer);
}

uintptr_t ngf_get_vk_sampler_handle(ngf_sampler sampler) {
  return (uintptr_t)(sampler->vksampler);
}

uint32_t ngf_get_vk_image_format_index(ngf_image_format format) {
  return (uint32_t)get_vk_image_format(format);
}

#pragma endregion

#if defined(NGFVK_TEST_MODE)
#include "../tests/vk-backend-tests.c"
#endif
