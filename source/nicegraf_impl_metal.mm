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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "nicegraf.h"
#include "nicegraf_internal.h"
#include "nicegraf_wrappers.h"

#include <memory>
#include <new>
#include <optional>
#include <string>
#pragma comment(lib, "Metal")
#import <Metal/Metal.h>
#pragma comment(lib, "QuartzCore")
#import <QuartzCore/QuartzCore.h>
#if TARGET_OS_OSX
#pragma comment(lib, "AppKit")
#import <AppKit/AppKit.h>
using NGFMTL_VIEW_TYPE = NSView;
#else
#pragma comment(lib, "UIKit")
#import <UIKit/UIKit.h>
using NGFMTL_VIEW_TYPE = UIView;
#endif

// Indicates the maximum amount of buffers (attrib, index and uniform) that
// could be bound at the same time.
// This is required to work around a discrepancy between nicegraf's and Metal's
// buffer binding models.
// In Metal, bindings for vertex attribute buffers share the same space of IDs
// with regular buffers. Therefore assigning binding 0 to a vertex
// attrib buffer would cause a conflict if a vertex shader also requires a
// uniform buffer bound at 0.
// In order to solve this, attribute buffer bindings are remapped in the
// following way:
// nicegraf's attrib binding 0 becomes Metal vertex buffer binding 30
// attrib binding 1 becomes Metal vertex buffer binding 29
// ...and so on.
// TODO: consider using information from pipeline metadata to use an alternative
//       remapping scheme: attrib binding 0 -> N; attrib binding 1 -> N+1;...
//       etc. where N is the total number of uniform buffers consumed by the
//       vertex stage.
static constexpr uint32_t MAX_BUFFER_BINDINGS = 30u;

// Metal device handle. We choose one upon initialization and always use that
// one.
id<MTLDevice> MTL_DEVICE = nil;

#pragma mark ngf_enum_maps

struct mtl_format {
  const MTLPixelFormat format;
  const uint8_t rbits;
  const uint8_t gbits;
  const uint8_t bbits;
  const uint8_t abits;
  const uint8_t dbits;
  const uint8_t sbits;
  const bool    srgb;
};

static MTLBlendFactor get_mtl_blend_factor(ngf_blend_factor f) {
  static constexpr MTLBlendFactor factors[NGF_BLEND_FACTOR_COUNT] = {
    MTLBlendFactorZero,
    MTLBlendFactorOne,
    MTLBlendFactorSourceColor,
    MTLBlendFactorOneMinusSourceColor,
    MTLBlendFactorDestinationColor,
    MTLBlendFactorOneMinusDestinationColor,
    MTLBlendFactorSourceAlpha,
    MTLBlendFactorOneMinusSourceAlpha,
    MTLBlendFactorDestinationAlpha,
    MTLBlendFactorOneMinusDestinationAlpha,
    MTLBlendFactorBlendColor,
    MTLBlendFactorOneMinusBlendColor,
    MTLBlendFactorBlendAlpha,
    MTLBlendFactorOneMinusBlendAlpha
  };
  return factors[f];
}

static MTLBlendOperation get_mtl_blend_operation(ngf_blend_op op) {
  static constexpr MTLBlendOperation ops[NGF_BLEND_OP_COUNT] = {
    MTLBlendOperationAdd,
    MTLBlendOperationSubtract,
    MTLBlendOperationReverseSubtract,
    MTLBlendOperationMin,
    MTLBlendOperationMax
  };
  return ops[op];
}

static mtl_format get_mtl_pixel_format(ngf_image_format f) {
  static const mtl_format formats[NGF_IMAGE_FORMAT_COUNT] = {
    {MTLPixelFormatR8Unorm, 8, 0, 0, 0, 0, 0, false},
    {MTLPixelFormatRG8Unorm, 8, 8, 0, 0, 0, 0, false},
    {MTLPixelFormatInvalid, 8, 8, 8, 0, 0, 0, false}, // RGB8, unsupported on Metal
    {MTLPixelFormatRGBA8Unorm, 8, 8, 8, 8, 0, 0, false},
    {MTLPixelFormatInvalid, 8, 8, 8, 0, 0, 0, true}, // SRGB8, unsupported on Metal
    {MTLPixelFormatRGBA8Unorm_sRGB, 8, 8, 8, 8, 0, 0, true},
    {MTLPixelFormatInvalid, 8, 8, 8, 0, 0, 0, false}, // BGR8, unsupported on Metal
    {MTLPixelFormatBGRA8Unorm, 8, 8, 8, 8, 0, 0, false},
    {MTLPixelFormatInvalid, 8, 8, 8, 0, 0, 0, true}, // BGR8_SRGB, unsupported on Metal
    {MTLPixelFormatBGRA8Unorm_sRGB, 8, 8, 8, 8, 0, 0, true},
    {MTLPixelFormatR32Float, 32, 0, 0, 0, 0, 0, false},
    {MTLPixelFormatRG32Float, 32, 32, 0, 0, 0, 0, false},
    {MTLPixelFormatInvalid, 32, 32, 32, 0, 0, 0,  false}, // RGB32F, unsupported on Metal
    {MTLPixelFormatRGBA32Float, 32, 32, 32, 32, 0, 0,  false},
    {MTLPixelFormatR16Float, 16, 0, 0, 0, 0, 0, false},
    {MTLPixelFormatRG16Float, 16, 16, 0, 0, 0, 0, false},
    {MTLPixelFormatInvalid, 16, 16, 16, 0, 0, 0, false}, // RGB16F, unsupported on Metal
    {MTLPixelFormatRGBA16Float, 16, 16, 16, 16, 0, 0, false},
    {MTLPixelFormatRG11B10Float, 11, 11, 10, 0, 0, 0, false},
    {MTLPixelFormatR16Unorm, 16, 0, 0, 0, 0, 0, false},
    {MTLPixelFormatR16Snorm, 16, 0, 0, 0, 0, 0, false},
    {MTLPixelFormatR16Uint, 16, 0, 0, 0, 0, 0, false},
    {MTLPixelFormatR16Sint, 16, 0, 0, 0, 0, 0, false},
    {MTLPixelFormatRG16Uint, 16, 16, 0, 0, 0, 0, false},
    {MTLPixelFormatInvalid, 16, 16, 16, 0, 0, 0, false}, // RGB16U, unsupported on Metal
    {MTLPixelFormatRGBA16Uint, 16, 16, 16, 16, 0, 0, false},
    {MTLPixelFormatR32Uint, 32, 0, 0, 0, 0, 0, false},
    {MTLPixelFormatRG32Uint, 32, 32, 0, 0, 0, 0, false},
    {MTLPixelFormatInvalid, 32, 32, 32, 0, 0, 0, false}, // RGB32U, unsupported on Metal
    {MTLPixelFormatRGBA32Uint, 32, 32, 32, 32, 0, 0, false},
    {MTLPixelFormatDepth32Float, 0, 0, 0, 0, 32, 0, false},
#if TARGET_OS_OSX
    {MTLPixelFormatDepth16Unorm, 0, 0, 0, 0, 16, 0, false},
    {MTLPixelFormatDepth32Float_Stencil8, 0, 0, 0, 0, 24, 8, false}, // instead of 24Unorm_Stencil8,
    // because metal validator doesn't
    // like it for some reason...
#else
    {MTLPixelFormatInvalid, 0, 0, 0, 0, 16, 0, false}, // DEPTH16, iOS does not support.
    {MTLPixelFormatDepth32Float_Stencil8, 0, 0, 0, 0, 24, 8, false}, // Emulate DEPTH24_STENCIL8 on iOS
#endif
    {MTLPixelFormatInvalid, 0, 0, 0, 0, 0, 0, false}
  };
  return formats[f];
}

static MTLLoadAction get_mtl_load_action(ngf_attachment_load_op op) {
  static const MTLLoadAction action[NGF_LOAD_OP_COUNT] = {
    MTLLoadActionDontCare,
    MTLLoadActionLoad,
    MTLLoadActionClear
  };
  return action[op];
}

static MTLStoreAction get_mtl_store_action(ngf_attachment_store_op op) {
  static const MTLStoreAction action[NGF_STORE_OP_COUNT] = {
    MTLStoreActionDontCare,
    MTLStoreActionStore
  };
  return action[op];
}

static MTLDataType get_mtl_type(ngf_type type) {
  static const MTLDataType types[NGF_TYPE_COUNT] = {
    MTLDataTypeNone, /* Int8, Metal does not support.*/
    MTLDataTypeNone, /*UInt8, Metal does not support*/
    MTLDataTypeShort,
    MTLDataTypeUShort,
    MTLDataTypeInt,
    MTLDataTypeUInt,
    MTLDataTypeFloat,
    MTLDataTypeHalf,
    MTLDataTypeNone /* Double,Metal does not support.*/
  };
  return types[type];
}

static MTLVertexFormat get_mtl_attrib_format(ngf_type type,
                                             uint32_t size,
                                             bool normalized) {
  static const MTLVertexFormat formats[NGF_TYPE_COUNT][2][4] = {
    { {MTLVertexFormatChar, MTLVertexFormatChar2,
      MTLVertexFormatChar3, MTLVertexFormatChar4},
      {MTLVertexFormatCharNormalized, MTLVertexFormatChar2Normalized,
        MTLVertexFormatChar3Normalized, MTLVertexFormatChar4Normalized} },
    { {MTLVertexFormatUChar, MTLVertexFormatUChar2,
      MTLVertexFormatUChar3, MTLVertexFormatUChar4},
      {MTLVertexFormatUCharNormalized, MTLVertexFormatUChar2Normalized,
        MTLVertexFormatUChar3Normalized, MTLVertexFormatUChar4Normalized} },
    { {MTLVertexFormatShort, MTLVertexFormatShort2,
      MTLVertexFormatShort3, MTLVertexFormatShort4},
      {MTLVertexFormatShortNormalized, MTLVertexFormatShort2Normalized,
        MTLVertexFormatShort3Normalized, MTLVertexFormatShort4Normalized} },
    { {MTLVertexFormatUShort, MTLVertexFormatUShort2,
      MTLVertexFormatUShort3, MTLVertexFormatUShort4},
      {MTLVertexFormatUShortNormalized, MTLVertexFormatUShort2Normalized,
        MTLVertexFormatUShort3Normalized, MTLVertexFormatUShort4Normalized} },
    { {MTLVertexFormatInt, MTLVertexFormatInt2,
      MTLVertexFormatInt3, MTLVertexFormatInt4},
      {MTLVertexFormatInvalid, MTLVertexFormatInvalid,
        MTLVertexFormatInvalid, MTLVertexFormatInvalid} },
    { {MTLVertexFormatUInt, MTLVertexFormatUInt2,
      MTLVertexFormatUInt3, MTLVertexFormatUInt4},
      {MTLVertexFormatInvalid, MTLVertexFormatInvalid,
        MTLVertexFormatInvalid, MTLVertexFormatInvalid} },
    { {MTLVertexFormatFloat, MTLVertexFormatFloat2,
      MTLVertexFormatFloat3, MTLVertexFormatFloat4},
      {MTLVertexFormatInvalid, MTLVertexFormatInvalid,
        MTLVertexFormatInvalid, MTLVertexFormatInvalid } },
    { {MTLVertexFormatHalf, MTLVertexFormatHalf2,
      MTLVertexFormatHalf3, MTLVertexFormatHalf4},
      {MTLVertexFormatInvalid, MTLVertexFormatInvalid,
        MTLVertexFormatInvalid, MTLVertexFormatInvalid} },
    { {MTLVertexFormatInvalid, MTLVertexFormatInvalid, // Double, Metal does not support.
      MTLVertexFormatInvalid, MTLVertexFormatInvalid},
      {MTLVertexFormatInvalid, MTLVertexFormatInvalid,
        MTLVertexFormatInvalid, MTLVertexFormatInvalid} }
  };
  assert(size <= 4u && size > 0u);
  return formats[type][normalized? 1 : 0][size - 1u];
}

static MTLVertexStepFunction get_mtl_step_function(ngf_input_rate rate) {
  static const MTLVertexStepFunction funcs[NGF_INPUT_RATE_COUNT] = {
    MTLVertexStepFunctionPerVertex,
    MTLVertexStepFunctionPerInstance
  };
  return funcs[rate];
}

static MTLPrimitiveTopologyClass
get_mtl_primitive_topology_class(ngf_primitive_type type) {
  static const MTLPrimitiveTopologyClass
  topo_class[NGF_PRIMITIVE_TYPE_COUNT] = {
    MTLPrimitiveTopologyClassTriangle,
    MTLPrimitiveTopologyClassTriangle,
    MTLPrimitiveTopologyClassUnspecified, // Triangle Fan, Metal does not
    // support.
    MTLPrimitiveTopologyClassLine,
    MTLPrimitiveTopologyClassLine,
  };
  return topo_class[type];
}

static std::optional<MTLPrimitiveType> get_mtl_primitive_type(ngf_primitive_type type) {
  static const std::optional<MTLPrimitiveType>
  types[NGF_PRIMITIVE_TYPE_COUNT] = {
    MTLPrimitiveTypeTriangle,
    MTLPrimitiveTypeTriangleStrip,
    std::nullopt, // Triangle Fan not supported.
    MTLPrimitiveTypeLine,
    MTLPrimitiveTypeLineStrip
  };
  return types[type];
}

static MTLIndexType get_mtl_index_type(ngf_type type) {
  assert(type == NGF_TYPE_UINT16 || type == NGF_TYPE_UINT32);
  return type == NGF_TYPE_UINT16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
}

static MTLCompareFunction get_mtl_compare_function(ngf_compare_op op) {
  static const MTLCompareFunction compare_fns[NGF_COMPARE_OP_COUNT] = {
    MTLCompareFunctionNever,
    MTLCompareFunctionLess,
    MTLCompareFunctionLessEqual,
    MTLCompareFunctionEqual,
    MTLCompareFunctionGreaterEqual,
    MTLCompareFunctionGreater,
    MTLCompareFunctionNotEqual,
    MTLCompareFunctionAlways
  };
  return compare_fns[op];
}

static MTLStencilOperation get_mtl_stencil_op(ngf_stencil_op op) {
  static const MTLStencilOperation stencil_ops[NGF_STENCIL_OP_COUNT] = {
    MTLStencilOperationKeep,
    MTLStencilOperationZero,
    MTLStencilOperationReplace,
    MTLStencilOperationIncrementClamp,
    MTLStencilOperationIncrementWrap,
    MTLStencilOperationDecrementClamp,
    MTLStencilOperationDecrementWrap,
    MTLStencilOperationInvert
  };
  return stencil_ops[op];
}

static MTLCullMode get_mtl_culling(ngf_cull_mode c) {
  static const MTLCullMode cull_modes[NGF_CULL_MODE_COUNT] = {
    MTLCullModeBack,
    MTLCullModeFront,
    MTLCullModeNone, /* Metal has no front + back culling */
    MTLCullModeNone
  };
  return cull_modes[c];
}

static MTLWinding get_mtl_winding(ngf_front_face_mode w) {
  static const MTLWinding windings[NGF_FRONT_FACE_COUNT] = {
    MTLWindingCounterClockwise,
    MTLWindingClockwise
  };
  return windings[w];
}

static std::optional<MTLTextureType> get_mtl_texture_type(ngf_image_type type,
                                                          uint32_t nlayers,
                                                          ngf_sample_count sample_count) {
  if (type == NGF_IMAGE_TYPE_IMAGE_2D && nlayers == 1 && sample_count == NGF_SAMPLE_COUNT_1) {
    return MTLTextureType2D;
  } else if (type == NGF_IMAGE_TYPE_IMAGE_2D && nlayers > 1 && sample_count == NGF_SAMPLE_COUNT_1) {
    return MTLTextureType2DArray;
  } if (type == NGF_IMAGE_TYPE_IMAGE_2D && nlayers == 1 && sample_count != NGF_SAMPLE_COUNT_1) {
    return MTLTextureType2DMultisample;
  } else if (type == NGF_IMAGE_TYPE_IMAGE_2D && nlayers > 1 && sample_count != NGF_SAMPLE_COUNT_1) {
    if (@available(iOS 14.0, *)) return MTLTextureType2DMultisampleArray;
  } else if (type == NGF_IMAGE_TYPE_IMAGE_3D) {
    return MTLTextureType3D;
  } else if (type == NGF_IMAGE_TYPE_CUBE && nlayers == 1) {
    return MTLTextureTypeCube;
  } else if(type == NGF_IMAGE_TYPE_CUBE && nlayers > 1) {
    return MTLTextureTypeCubeArray;
  }
  return std::nullopt;
}

static std::optional<MTLSamplerAddressMode>
get_mtl_address_mode(ngf_sampler_wrap_mode mode) {
  static const std::optional<MTLSamplerAddressMode> modes[NGF_WRAP_MODE_COUNT] =
  {
    MTLSamplerAddressModeClampToEdge,
#if TARGET_OS_OSX
    MTLSamplerAddressModeClampToBorderColor,
#else
    //ClampToBorderColor is unsupported on iOS, temp solution:
    std::nullopt,
#endif
    MTLSamplerAddressModeRepeat,
    MTLSamplerAddressModeMirrorRepeat
  };
  return modes[mode];
}

static MTLSamplerMinMagFilter get_mtl_minmag_filter(ngf_sampler_filter f) {
  static MTLSamplerMinMagFilter filters[NGF_FILTER_COUNT] = {
    MTLSamplerMinMagFilterNearest,
    MTLSamplerMinMagFilterLinear
  };
  return filters[f];
}

static MTLSamplerMipFilter get_mtl_mip_filter(ngf_sampler_filter f) {
  static MTLSamplerMipFilter filters[NGF_FILTER_COUNT] = {
    MTLSamplerMipFilterNearest,
    MTLSamplerMipFilterLinear
  };
  return filters[f];
}


#pragma mark ngf_struct_definitions

struct ngf_render_target_t {
  mutable MTLRenderPassDescriptor *pass_descriptor = nil;
  uint32_t ncolor_attachments = 0u;
  bool is_default = false;
  NSUInteger width;
  NSUInteger height;
};

struct ngf_cmd_buffer_t {
  ngfi_cmd_buffer_state state = NGFI_CMD_BUFFER_NEW;
  bool renderpass_active = false;
  id<MTLCommandBuffer> mtl_cmd_buffer = nil;
  id<MTLRenderCommandEncoder> active_rce = nil;
  id<MTLBlitCommandEncoder> active_bce = nil;
  ngf_graphics_pipeline active_pipe = nullptr;
  ngf_render_target active_rt = nullptr;
  id<MTLBuffer> bound_index_buffer = nil;
  MTLIndexType bound_index_buffer_type;
};

struct ngf_shader_stage_t {
  id<MTLLibrary> func_lib = nil;
  ngf_stage_type type;
  std::string entry_point_name;
};

struct ngf_graphics_pipeline_t {
  id<MTLRenderPipelineState> pipeline      = nil;
  id<MTLDepthStencilState>   depth_stencil = nil;
  
  uint32_t front_stencil_reference = 0u;
  uint32_t back_stencil_reference  = 0u;
  
  MTLPrimitiveType primitive_type = MTLPrimitiveTypeTriangle;
  MTLWinding       winding        = MTLWindingCounterClockwise;
  MTLCullMode      culling        = MTLCullModeBack;
  float            blend_color[4] {0};
  
  ngfi_native_binding_map   binding_map = nullptr;
  ngf_pipeline_layout_info layout;

  ~ngf_graphics_pipeline_t() {
    for (uint32_t s = 0u;
         layout.descriptor_set_layouts != nullptr &&
         s < layout.ndescriptor_set_layouts;
         ++s) {
      if (layout.descriptor_set_layouts[s].descriptors != nullptr) {
        NGFI_FREEN(layout.descriptor_set_layouts[s].descriptors,
                  layout.descriptor_set_layouts[s].ndescriptors);
      }
    }
    if (binding_map) {
      ngfi_destroy_binding_map(binding_map);
    }
  }
};

struct ngf_buffer_t {
  id<MTLBuffer> mtl_buffer = nil;
  size_t mapped_offset = 0;
};

struct ngf_attrib_buffer_t {
  id<MTLBuffer> mtl_buffer = nil;
  size_t mapped_offset = 0;
};

struct ngf_index_buffer_t {
  id<MTLBuffer> mtl_buffer = nil;
  size_t mapped_offset = 0;
};

struct ngf_uniform_buffer_t {
  id<MTLBuffer> mtl_buffer = nil;
  size_t mapped_offset = 0;
  uint32_t current_idx = 0u;
  size_t size = 0u;
};

struct ngf_pixel_buffer_t {
  id<MTLBuffer> mtl_buffer = nil;
  size_t mapped_offset = 0;
};

struct ngf_sampler_t {
  id<MTLSamplerState> sampler = nil;
};

struct ngf_image_t {
  id<MTLTexture> texture = nil;
  ngf_image_format format;
};

template <class NgfObjType, void(*Dtor)(NgfObjType*)>
class ngfmtl_object_nursery {
public:
  explicit ngfmtl_object_nursery(NgfObjType *memory) : ptr_(memory) {
    new(memory) NgfObjType();
  }
  ~ngfmtl_object_nursery() { if(ptr_ != nullptr) { Dtor(ptr_); } }
  NgfObjType* operator->() { return ptr_; }
  NgfObjType* release() {
    NgfObjType *tmp = ptr_;
    ptr_ = nullptr;
    return tmp;
  }
  operator bool() { return ptr_ != nullptr; }
private:
  NgfObjType *ptr_;
};

// Manages the final presentation surfaces.
class ngfmtl_swapchain {
public:
  struct frame {
    id<MTLTexture> color_attachment_texture() {
      return multisample_texture ? multisample_texture : color_drawable.texture;
    }
    
    id<MTLTexture> resolve_attachment_texture() {
      return multisample_texture ? color_drawable.texture : nil;
    }
    
    id<MTLTexture> depth_attachment_texture() { return depth_texture; }
    
    id<CAMetalDrawable> color_drawable = nil;
    id<MTLTexture>      depth_texture  = nil;
    id<MTLTexture>      multisample_texture = nil;
  };
  
  ngfmtl_swapchain() = default;
  ngfmtl_swapchain(ngfmtl_swapchain &&other) { *this = std::move(other); }
  ngfmtl_swapchain& operator=(ngfmtl_swapchain &&other) {
    layer_        = other.layer_;
    other.layer_  = nil;
    depth_images_ = std::move(other.depth_images_);
    capacity_     = other.capacity_;
    img_idx_      = other.img_idx_;
    
    return *this;
  }
  
  // Delete copy ctor and copy assignment to make this type move-only.
  ngfmtl_swapchain& operator=(const ngfmtl_swapchain&) = delete;
  ngfmtl_swapchain(const ngfmtl_swapchain&) = delete;

  ngf_error initialize(const ngf_swapchain_info &swapchain_info,
                       id<MTLDevice>             device) {
    // Initialize the Metal layer.
    const MTLPixelFormat pixel_format =
        get_mtl_pixel_format(swapchain_info.color_format).format;
    if (pixel_format == MTLPixelFormatInvalid) {
      NGFI_DIAG_ERROR("Image format not supported by Metal backend");
      return NGF_ERROR_INVALID_FORMAT;
    }
    layer_ = [CAMetalLayer layer];
    layer_.device = device;
    layer_.drawableSize = CGSizeMake(swapchain_info.width, swapchain_info.height);
    layer_.pixelFormat = pixel_format;
    layer_.framebufferOnly = YES;
#if TARGET_OS_OSX
    if (@available(macOS 10.13.2, *)) {
      layer_.maximumDrawableCount = swapchain_info.capacity_hint;
    }
    if (@available(macOS 10.13, *)) {
      layer_.displaySyncEnabled =
      (swapchain_info.present_mode == NGF_PRESENTATION_MODE_FIFO);
    }
#endif
    
    // Associate the newly created Metal layer with the user-provided View.
    NGFMTL_VIEW_TYPE *view=
        CFBridgingRelease((void*)swapchain_info.native_handle);
#if TARGET_OS_OSX
    [view setLayer:layer_];
#else
    [view.layer addSublayer:layer_];
    [layer_ setContentsScale:view.layer.contentsScale];
    [layer_ setContentsGravity:kCAGravityResizeAspect];
    [layer_ setFrame:view.frame];
#endif
    CFBridgingRetain(view);
    
    // Remember the number of images in the swapchain.
    capacity_ = swapchain_info.capacity_hint;
    
    // Initialize depth attachments if necessary.
    initialize_depth_attachments(swapchain_info);
    initialize_multisample_images(swapchain_info);

    return NGF_ERROR_OK;
  }

  ngf_error resize(const ngf_swapchain_info &swapchain_info) {
    layer_.drawableSize = CGSizeMake(swapchain_info.width, swapchain_info.height);

    NGFMTL_VIEW_TYPE *view=
        CFBridgingRelease((void*)swapchain_info.native_handle);

    [layer_ setContentsScale:view.layer.contentsScale];
    [layer_ setFrame:view.frame];

    CFBridgingRetain(view);

    // ReiInitialize depth attachments & multisample images if necessary.
    initialize_depth_attachments(swapchain_info);
    initialize_multisample_images(swapchain_info);
    
    return NGF_ERROR_OK;
  }
  
  frame next_frame() {
    img_idx_ = (img_idx_ + 1u) % capacity_;
    return { [layer_ nextDrawable],
      depth_images_.get() ? depth_images_[img_idx_] : nil,
      is_multusampled() ? multisample_images_[img_idx_]->texture : nil
    };
  }
  
  operator bool() { return layer_; }
  
  bool is_multusampled() const { return multisample_images_.get(); }
  
private:
  void initialize_depth_attachments(const ngf_swapchain_info &swapchain_info) {
    if (swapchain_info.depth_format != NGF_IMAGE_FORMAT_UNDEFINED) {
      depth_images_.reset(
                          new id<MTLTexture>[swapchain_info.capacity_hint]);
      MTLPixelFormat depth_format = get_mtl_pixel_format(swapchain_info.depth_format).format;
      assert(depth_format != MTLPixelFormatInvalid);
      for (uint32_t i = 0u; i < swapchain_info.capacity_hint; ++i) {
        auto *depth_texture_desc            = [MTLTextureDescriptor new];
        depth_texture_desc.textureType      = swapchain_info.sample_count > 1u? MTLTextureType2DMultisample : MTLTextureType2D;
        depth_texture_desc.width            = swapchain_info.width;
        depth_texture_desc.height           = swapchain_info.height;
        depth_texture_desc.pixelFormat      = depth_format;
        depth_texture_desc.depth            = 1u;
        depth_texture_desc.sampleCount      = (NSUInteger)swapchain_info.sample_count;
        depth_texture_desc.mipmapLevelCount = 1u;
        depth_texture_desc.arrayLength      = 1u;
        depth_texture_desc.usage            = MTLTextureUsageRenderTarget;
        depth_texture_desc.storageMode      = MTLStorageModePrivate;
        depth_texture_desc.resourceOptions  = MTLResourceStorageModePrivate;
        if (@available(macOS 10.14, *)) {
          depth_texture_desc.allowGPUOptimizedContents = true;
        }
        depth_images_[i] =
            [MTL_DEVICE newTextureWithDescriptor:depth_texture_desc];
      }
    } else {
      depth_images_.reset(nullptr);
    }
  }
  
  void initialize_multisample_images(const ngf_swapchain_info &swapchain_info) {
    destroy_multisample_images();
    if (swapchain_info.sample_count > NGF_SAMPLE_COUNT_1) {
      multisample_images_.reset(new ngf_image[capacity_]);
      for (size_t i = 0; i < capacity_; ++i) {
        const ngf_image_info info = {
          .type = NGF_IMAGE_TYPE_IMAGE_2D,
          .extent = {
            .width = swapchain_info.width,
            .height = swapchain_info.height,
            .depth = 1u
          },
          .nmips = 1u,
          .format = swapchain_info.color_format,
          .sample_count = (ngf_sample_count)swapchain_info.sample_count,
          .usage_hint = NGF_IMAGE_USAGE_ATTACHMENT
        };
        ngf_create_image(&info, &multisample_images_[i]);
      }
    }
  }
  
  void destroy_multisample_images() {
    for(size_t i = 0; i < capacity_ && multisample_images_; ++i) {
      ngf_destroy_image(multisample_images_[i]);
    }
    multisample_images_.reset(nullptr);
  }

  CAMetalLayer                      *layer_    = nil;
  uint32_t                           img_idx_  = 0u;
  uint32_t                           capacity_ = 0u;
  std::unique_ptr<id<MTLTexture>[]>  depth_images_;
  std::unique_ptr<ngf_image[]>       multisample_images_;
};

struct ngf_context_t {
  ~ngf_context_t() {
    if (last_cmd_buffer)
      [last_cmd_buffer waitUntilCompleted];
  }
  id<MTLDevice> device = nil;
  ngfmtl_swapchain swapchain;
  ngfmtl_swapchain::frame frame;
  id<MTLCommandQueue> queue = nil;
  bool is_current = false;
  ngf_swapchain_info swapchain_info;
  id<MTLCommandBuffer> pending_cmd_buffer = nil;
  id<MTLCommandBuffer> last_cmd_buffer = nil;
  dispatch_semaphore_t frame_sync_sem = nil;
};

NGFI_THREADLOCAL ngf_context CURRENT_CONTEXT = nullptr;


#define NGFMTL_NURSERY(type, name) \
ngfmtl_object_nursery<ngf_##type##_t, ngf_destroy_##type> \
    name(NGFI_ALLOC(ngf_##type##_t));

#pragma mark ngf_function_implementations

ngf_error ngf_initialize(const ngf_init_info *init_info) NGF_NOEXCEPT {
  ngfi_diag_info = init_info->diag_info;
#if TARGET_OS_OSX
  // Try setting environment variable to enable Metal API validation if
  // necessary.
  if (ngfi_diag_info.verbosity == NGF_DIAGNOSTICS_VERBOSITY_DETAILED) {
    if (setenv("METAL_DEVICE_WRAPPER_TYPE", "1", 1)) {
      NGFI_DIAG_WARNING("Could not enable detailed diagnostic log");
    }
  }
  
  // On macOS, try picking a device in accordance with user's preferences.
  id<NSObject> dev_observer = nil;
  const NSArray<id<MTLDevice>> *devices =
      MTLCopyAllDevicesWithObserver(&dev_observer,
                                    ^(id<MTLDevice> d,
                                      MTLDeviceNotificationName n){});
  bool found_preferred_device = false;
  const ngf_device_preference dev_pref = init_info->device_pref;
  for (uint32_t d = 0u; !found_preferred_device && d < devices.count; ++d) {
    MTL_DEVICE = devices[d];
    // If the user prefers discrete GPU, do not pick a low-power device.
    // Otherwise, anything will do.
    found_preferred_device = (dev_pref != NGF_DEVICE_PREFERENCE_DISCRETE) ||
                             !MTL_DEVICE.lowPower;
  }
#else
  // On other targets, simply pick the default device.
  MTL_DEVICE = MTLCreateSystemDefaultDevice();
#endif

  ngfi_device_caps_create();
  ngf_device_capabilities* caps_ptr = ngfi_device_caps_lock();
  if (caps_ptr) {
    caps_ptr->clipspace_z_zero_to_one = true; // always true on metal.
    caps_ptr->uniform_buffer_offset_alignment = 256u;
    ngfi_device_caps_unlock(caps_ptr);
  }
  return (MTL_DEVICE != nil) ? NGF_ERROR_OK : NGF_ERROR_INVALID_OPERATION;
}

const ngf_device_capabilities* ngf_get_device_capabilities()  NGF_NOEXCEPT{
  return ngfi_device_caps_read();
}

ngf_error ngf_begin_frame(ngf_frame_token*) NGF_NOEXCEPT {
  dispatch_semaphore_wait(CURRENT_CONTEXT->frame_sync_sem,
                          DISPATCH_TIME_FOREVER);
  CURRENT_CONTEXT->frame = CURRENT_CONTEXT->swapchain.next_frame();
  return (!CURRENT_CONTEXT->frame.color_drawable)
           ? NGF_ERROR_INVALID_OPERATION
           : NGF_ERROR_OK;
}

ngf_error ngf_end_frame(ngf_frame_token) NGF_NOEXCEPT {
  ngf_context ctx = CURRENT_CONTEXT;
  if(CURRENT_CONTEXT->frame.color_drawable &&
     CURRENT_CONTEXT->pending_cmd_buffer) {
    [CURRENT_CONTEXT->pending_cmd_buffer addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull) {
      dispatch_semaphore_signal(ctx->frame_sync_sem);
    }];
    [CURRENT_CONTEXT->pending_cmd_buffer
       presentDrawable:CURRENT_CONTEXT->frame.color_drawable];
    [CURRENT_CONTEXT->pending_cmd_buffer commit];
    CURRENT_CONTEXT->frame = ngfmtl_swapchain::frame{};
    CURRENT_CONTEXT->last_cmd_buffer = CURRENT_CONTEXT->pending_cmd_buffer;
    CURRENT_CONTEXT->pending_cmd_buffer = nil;
  } else {
    dispatch_semaphore_signal(ctx->frame_sync_sem);
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_default_render_target(
    ngf_attachment_load_op  color_load_op,
    ngf_attachment_load_op  depth_load_op,
    ngf_attachment_store_op color_store_op,
    ngf_attachment_store_op depth_store_op,
    const ngf_clear        *clear_color,
    const ngf_clear        *clear_depth,
    ngf_render_target     *result) NGF_NOEXCEPT {
  assert(result);
  if (CURRENT_CONTEXT->swapchain) {
    NGFMTL_NURSERY(render_target, rt);
    rt->is_default = true;
    rt->ncolor_attachments = 1u;
    rt->pass_descriptor = [MTLRenderPassDescriptor new];
    rt->pass_descriptor.colorAttachments[0].texture = nil;
    rt->pass_descriptor.colorAttachments[0].loadAction =
        get_mtl_load_action(color_load_op);
    rt->pass_descriptor.colorAttachments[0].storeAction =
        CURRENT_CONTEXT->swapchain.is_multusampled()
          ? MTLStoreActionMultisampleResolve
          : get_mtl_store_action(color_store_op);
    if (color_load_op == NGF_LOAD_OP_CLEAR) {
      assert(clear_color);
      rt->pass_descriptor.colorAttachments[0].clearColor =
          MTLClearColorMake(clear_color->clear_color[0],
                            clear_color->clear_color[1],
                            clear_color->clear_color[2],
                            clear_color->clear_color[3]);
    }
    const ngf_image_format dfmt = CURRENT_CONTEXT->swapchain_info.depth_format;
    if (dfmt != NGF_IMAGE_FORMAT_UNDEFINED) {
      rt->pass_descriptor.depthAttachment =
          [MTLRenderPassDepthAttachmentDescriptor new];
      rt->pass_descriptor.depthAttachment.loadAction =
        get_mtl_load_action(depth_load_op);
      rt->pass_descriptor.depthAttachment.storeAction =
          CURRENT_CONTEXT->swapchain.is_multusampled()
            ? MTLStoreActionDontCare
            : get_mtl_store_action(depth_store_op);
      if (depth_load_op == NGF_LOAD_OP_CLEAR) {
        assert(clear_depth);
        rt->pass_descriptor.depthAttachment.clearDepth =
            clear_depth->clear_depth;
      }
      if (dfmt == NGF_IMAGE_FORMAT_DEPTH24_STENCIL8) {
        rt->pass_descriptor.stencilAttachment =
            [MTLRenderPassStencilAttachmentDescriptor new];
        rt->pass_descriptor.stencilAttachment.loadAction =
            get_mtl_load_action(depth_load_op);
        rt->pass_descriptor.stencilAttachment.storeAction =
            get_mtl_store_action(depth_store_op);
      }
    } else {
      rt->pass_descriptor.depthAttachment = nil;
      rt->pass_descriptor.stencilAttachment = nil;
    }
    *result = rt.release();
    return NGF_ERROR_OK;
  } else {
    NGFI_DIAG_ERROR("Current context provides no default render target");
    return NGF_ERROR_INVALID_OPERATION;
  }
}

ngf_error ngf_create_context(const ngf_context_info *info,
                             ngf_context *result) NGF_NOEXCEPT {
  assert(info);
  assert(result);
  NGFMTL_NURSERY(context, ctx);
  if (!ctx) {
    return NGF_ERROR_OUT_OF_MEM;
  }

  ctx->device = MTL_DEVICE;
  if (info->shared_context != nullptr) {
    ctx->queue = info->shared_context->queue;
  } else {
    ctx->queue = [ctx->device newCommandQueue];
  }

  if (info->swapchain_info) {
    ctx->swapchain_info = *(info->swapchain_info);
    ngf_error err = ctx->swapchain.initialize(ctx->swapchain_info, ctx->device);
    if (err != NGF_ERROR_OK) return err;
  }
 
  ctx->frame_sync_sem =
      dispatch_semaphore_create(ctx->swapchain_info.capacity_hint);
  *result = ctx.release(); 
  return NGF_ERROR_OK;
}

void ngf_destroy_context(ngf_context ctx) NGF_NOEXCEPT {
  // TODO: unset current context
  assert(ctx);
  ctx->~ngf_context_t();
  NGFI_FREE(ctx);
}

ngf_error ngf_resize_context(ngf_context ctx,
                             uint32_t new_width,
                             uint32_t new_height) NGF_NOEXCEPT {
  assert(ctx);
  ctx->swapchain_info.width = new_width;
  ctx->swapchain_info.height = new_height;
  return ctx->swapchain.resize(ctx->swapchain_info);
}

ngf_error ngf_set_context(ngf_context ctx) NGF_NOEXCEPT {
  if(CURRENT_CONTEXT == ctx) {
    NGFI_DIAG_WARNING("Attempt to set a context that is already "
                      "current on the calling thread");
    return NGF_ERROR_OK;
  } else if (CURRENT_CONTEXT) {
    NGFI_DIAG_ERROR("Attempt to set a context when the calling thread "
                   "already has a current context.")
    return NGF_ERROR_INVALID_OPERATION;
  }
  CURRENT_CONTEXT = ctx;
  ctx->is_current = true;
  return NGF_ERROR_OK;
}

ngf_error ngf_create_shader_stage(const ngf_shader_stage_info *info,
                                  ngf_shader_stage *result) NGF_NOEXCEPT {
  assert(info);
  assert(result);
 
  NGFMTL_NURSERY(shader_stage, stage);
  if (!stage) {
    return NGF_ERROR_OUT_OF_MEM;
  }
  
  stage->type = info->type;

  // Create a MTLLibrary for this stage.
  NSString *source = [[NSString alloc] initWithBytes:info->content
                               length:info->content_length
                               encoding:NSUTF8StringEncoding];
  MTLCompileOptions *opts = [MTLCompileOptions new];
  NSError *err = nil;
  stage->func_lib = [CURRENT_CONTEXT->device newLibraryWithSource:source
                                options:opts
                                error:&err];
  if (!stage->func_lib) {
    NGFI_DIAG_ERROR([err.localizedDescription UTF8String]);
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }
  
  // Set debug name.
  if (info->debug_name != nullptr) {
    stage->func_lib.label = [[NSString alloc]
                               initWithUTF8String:info->debug_name];
  }
  
  if (info->entry_point_name) {
    stage->entry_point_name = info->entry_point_name;
  } else {
    std::string tmp = [[stage->func_lib functionNames].firstObject UTF8String];
    stage->entry_point_name = tmp;
  }/*
  stage->entry_point_name = info->entry_point_name ?
    info->entry_point_name :
    [[stage->func_lib functionNames].firstObject UTF8String];*/

  *result = stage.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_shader_stage(ngf_shader_stage stage) NGF_NOEXCEPT {
  if (stage != nullptr) {
    stage->~ngf_shader_stage_t();
    NGFI_FREE(stage);
  }
}

void _ngf_attachment_set_common(MTLRenderPassAttachmentDescriptor *attachment,
                                const ngf_attachment &info) NGF_NOEXCEPT {
  attachment.texture     = info.image_ref.image->texture;
  attachment.level       = info.image_ref.mip_level;
  attachment.slice       = info.image_ref.layer;
  attachment.loadAction  = get_mtl_load_action(info.load_op);
  attachment.storeAction = get_mtl_store_action(info.store_op);
}

ngf_error ngf_create_render_target(const ngf_render_target_info *info,
                                   ngf_render_target *result) NGF_NOEXCEPT {
  assert(info);
  assert(result);
  NGFMTL_NURSERY(render_target, rt);
  rt->pass_descriptor = [MTLRenderPassDescriptor new];
  rt->is_default = false;
  uint32_t color_attachment_idx = 0u;
  rt->pass_descriptor.depthAttachment.texture = nil;
  rt->pass_descriptor.stencilAttachment.texture = nil;
  for (uint32_t a = 0u; a < info->nattachments; ++a) {
    const ngf_attachment &attachment = info->attachments[a];
    rt->width = attachment.image_ref.image->texture.width;
    rt->height = attachment.image_ref.image->texture.height;
    switch(attachment.type) {
    case NGF_ATTACHMENT_COLOR: {
      auto desc = [MTLRenderPassColorAttachmentDescriptor new];
      _ngf_attachment_set_common(desc, attachment);
      desc.clearColor = MTLClearColorMake(attachment.clear.clear_color[0],
                                          attachment.clear.clear_color[1],
                                          attachment.clear.clear_color[2],
                                          attachment.clear.clear_color[3]);
      rt->pass_descriptor.colorAttachments[color_attachment_idx++] =
          desc;
      break;
    }
    case NGF_ATTACHMENT_DEPTH: {
      auto desc = [MTLRenderPassDepthAttachmentDescriptor new];
      _ngf_attachment_set_common(desc, attachment);
      desc.clearDepth = attachment.clear.clear_depth;
      rt->pass_descriptor.depthAttachment = desc;
      if (attachment.image_ref.image->texture.pixelFormat ==
          MTLPixelFormatDepth32Float_Stencil8) {
        rt->pass_descriptor.stencilAttachment =
        [MTLRenderPassStencilAttachmentDescriptor new];
        _ngf_attachment_set_common(rt->pass_descriptor.stencilAttachment,
                                   attachment);
      }
      break;
    }
    case NGF_ATTACHMENT_STENCIL: {
      assert(false); // TODO: implement
      break;
    }
    case NGF_ATTACHMENT_DEPTH_STENCIL: {
      assert(false); // TODO: implement
      break;
    }
    }
  }
  rt->ncolor_attachments = color_attachment_idx;
  *result = rt.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_render_target(ngf_render_target rt) NGF_NOEXCEPT {
  if (rt != nullptr) {
    rt->~ngf_render_target_t();
    NGFI_FREE(rt);
  }
}

id<MTLFunction> ngfmtl_get_shader_main(id<MTLLibrary> func_lib,
                                       const char *entry_point_name,
                                       MTLFunctionConstantValues *spec_consts) {
  NSError *err = nil;
  NSString *ns_entry_point_name =
      [NSString stringWithUTF8String:entry_point_name]; 
  return spec_consts == nil
           ? [func_lib newFunctionWithName:ns_entry_point_name]
           : [func_lib newFunctionWithName:ns_entry_point_name
                            constantValues:spec_consts
                                     error:&err];
}

MTLStencilDescriptor* ngfmtl_create_stencil_descriptor(const ngf_stencil_info &info) {
  auto *result = [MTLStencilDescriptor new];
  result.stencilCompareFunction = get_mtl_compare_function(info.compare_op);
  result.stencilFailureOperation = get_mtl_stencil_op(info.fail_op);
  result.depthStencilPassOperation = get_mtl_stencil_op(info.pass_op);
  result.depthFailureOperation = get_mtl_stencil_op(info.depth_fail_op);
  result.writeMask = info.write_mask;
  result.readMask = info.compare_mask;
  return result;
}

ngf_error ngf_create_graphics_pipeline(const ngf_graphics_pipeline_info *info,
                                       ngf_graphics_pipeline *result) NGF_NOEXCEPT {
  assert(info);
  assert(result);
  
  auto *mtl_pipe_desc = [MTLRenderPipelineDescriptor new];
  const ngf_render_target_t &compatible_rt = *info->compatible_render_target;
  for (uint32_t ca = 0u; ca < compatible_rt.ncolor_attachments; ++ca) {
    if (compatible_rt.is_default) {
      mtl_pipe_desc.colorAttachments[ca].pixelFormat =
          get_mtl_pixel_format(CURRENT_CONTEXT->swapchain_info.color_format).format;
    }
    else {
      mtl_pipe_desc.colorAttachments[ca].pixelFormat =
        compatible_rt.pass_descriptor.colorAttachments[ca].texture.pixelFormat;
    }
    mtl_pipe_desc.colorAttachments[ca].blendingEnabled = info->blend->enable;
    if (info->blend->enable) {
      mtl_pipe_desc.colorAttachments[ca].sourceRGBBlendFactor =
        get_mtl_blend_factor(info->blend->src_color_blend_factor);
      mtl_pipe_desc.colorAttachments[ca].destinationRGBBlendFactor =
        get_mtl_blend_factor(info->blend->dst_color_blend_factor);
      mtl_pipe_desc.colorAttachments[ca].sourceAlphaBlendFactor =
        get_mtl_blend_factor(info->blend->src_alpha_blend_factor);
      mtl_pipe_desc.colorAttachments[ca].destinationAlphaBlendFactor =
        get_mtl_blend_factor(info->blend->dst_alpha_blend_factor);
      mtl_pipe_desc.colorAttachments[ca].rgbBlendOperation =
        get_mtl_blend_operation(info->blend->blend_op_color);
      mtl_pipe_desc.colorAttachments[ca].alphaBlendOperation =
        get_mtl_blend_operation(info->blend->blend_op_alpha);
    }
  }
  
  mtl_pipe_desc.rasterSampleCount = info->multisample->sample_count;

  if (compatible_rt.pass_descriptor.depthAttachment.texture) {
    mtl_pipe_desc.depthAttachmentPixelFormat =
        compatible_rt.pass_descriptor.depthAttachment.texture.pixelFormat;
  } else if (compatible_rt.is_default &&
             CURRENT_CONTEXT->swapchain_info.depth_format !=
                 NGF_IMAGE_FORMAT_UNDEFINED) {
    mtl_pipe_desc.depthAttachmentPixelFormat =
        get_mtl_pixel_format(CURRENT_CONTEXT->swapchain_info.depth_format).format;
  }

  mtl_pipe_desc.stencilAttachmentPixelFormat = MTLPixelFormatInvalid;

  if (mtl_pipe_desc.depthAttachmentPixelFormat ==
          MTLPixelFormatDepth32Float_Stencil8) {
    // TODO support other stencil formats
    mtl_pipe_desc.stencilAttachmentPixelFormat =
        MTLPixelFormatDepth32Float_Stencil8;
  }

  // Populate specialization constant values.
  MTLFunctionConstantValues *spec_consts = nil;
  if (info->spec_info != nullptr) {
    spec_consts = [MTLFunctionConstantValues new];
    for (uint32_t s = 0u; s < info->spec_info->nspecializations; ++s) {
      const ngf_constant_specialization *spec =
          &info->spec_info->specializations[s];
      MTLDataType type = get_mtl_type(spec->type);
      if (type == MTLDataTypeNone) {
        return NGF_ERROR_OBJECT_CREATION_FAILED;
      }
      void *write_ptr =
          ((uint8_t*)info->spec_info->value_buffer + spec->offset);
      [spec_consts setConstantValue:write_ptr
                               type:type
                            atIndex:spec->constant_id];
    }
  }
  
  // Set stage functions.
  for (uint32_t s = 0u; s < info->nshader_stages; ++s) {
    const ngf_shader_stage stage = info->shader_stages[s];
    if (stage->type == NGF_STAGE_VERTEX) {
      assert(!mtl_pipe_desc.vertexFunction);
      mtl_pipe_desc.vertexFunction =
          ngfmtl_get_shader_main(stage->func_lib, stage->entry_point_name.c_str(),
                               spec_consts);
    } else if (stage->type == NGF_STAGE_FRAGMENT) {
      assert(!mtl_pipe_desc.fragmentFunction);
      mtl_pipe_desc.fragmentFunction =
          ngfmtl_get_shader_main(stage->func_lib, stage->entry_point_name.c_str(),
                               spec_consts);
    }
  }
  
  // Configure vertex input.
  const ngf_vertex_input_info &vertex_input_info = *info->input_info;
  MTLVertexDescriptor *vert_desc = mtl_pipe_desc.vertexDescriptor;
  for (uint32_t a = 0u; a < vertex_input_info.nattribs; ++a) {
    MTLVertexAttributeDescriptor *attr_desc = vert_desc.attributes[a];
    const ngf_vertex_attrib_desc &attr_info = vertex_input_info.attribs[a];
    attr_desc.offset = vertex_input_info.attribs[a].offset;
    attr_desc.bufferIndex =
        MAX_BUFFER_BINDINGS - vertex_input_info.attribs[a].binding;
    attr_desc.format = get_mtl_attrib_format(attr_info.type,
                                             attr_info.size,
                                             attr_info.normalized);
    if (attr_desc.format == MTLVertexFormatInvalid) {
      NGFI_DIAG_ERROR("Vertex attrib format not supported by Metal backend.");
      return NGF_ERROR_INVALID_FORMAT;
    }
  }
  for (uint32_t b = 0u; b < vertex_input_info.nvert_buf_bindings; ++b) {
    MTLVertexBufferLayoutDescriptor *binding_desc =
        vert_desc.layouts[MAX_BUFFER_BINDINGS - b];
    const ngf_vertex_buf_binding_desc &binding_info =
        vertex_input_info.vert_buf_bindings[b];
    binding_desc.stride = binding_info.stride;
    binding_desc.stepFunction = get_mtl_step_function(binding_info.input_rate);
  }
  
  // Set primitive topology.
  mtl_pipe_desc.inputPrimitiveTopology =
      get_mtl_primitive_topology_class(info->primitive_type);
  if (mtl_pipe_desc.inputPrimitiveTopology ==
      MTLPrimitiveTopologyClassUnspecified) {
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }
  
  NGFMTL_NURSERY(graphics_pipeline, pipeline);
  memcpy(pipeline->blend_color,
         info->blend->blend_color,
         sizeof(pipeline->blend_color));
  pipeline->layout.ndescriptor_set_layouts =
      info->layout->ndescriptor_set_layouts;
  ngf_descriptor_set_layout_info *descriptor_set_layouts =
      NGFI_ALLOCN(ngf_descriptor_set_layout_info,
                 info->layout->ndescriptor_set_layouts);
  pipeline->layout.descriptor_set_layouts = descriptor_set_layouts;
  if (pipeline->layout.descriptor_set_layouts == nullptr) {
    return NGF_ERROR_OUT_OF_MEM;
  }
  memset(pipeline->layout.descriptor_set_layouts, 0,
         sizeof(ngf_descriptor_set_layout_info));
  for (uint32_t s = 0u; s < info->layout->ndescriptor_set_layouts; ++s) {
    descriptor_set_layouts[s].ndescriptors =
        info->layout->descriptor_set_layouts[s].ndescriptors;
    ngf_descriptor_info *descriptors  =
        NGFI_ALLOCN(ngf_descriptor_info, descriptor_set_layouts[s].ndescriptors);
    descriptor_set_layouts[s].descriptors = descriptors;
    if (descriptors == nullptr) return NGF_ERROR_OUT_OF_MEM;
    memcpy(descriptors,
           info->layout->descriptor_set_layouts[s].descriptors,
           sizeof(ngf_descriptor_info) *
           descriptor_set_layouts[s].ndescriptors);
  }
  ngf_error ngf_err =
       ngfi_create_native_binding_map(info->layout,
                                     nullptr,
                                     nullptr,
                                    &pipeline->binding_map);
  if (ngf_err != NGF_ERROR_OK) {
    return ngf_err;
  }
  NSError *err = nil;
  pipeline->pipeline = [CURRENT_CONTEXT->device
      newRenderPipelineStateWithDescriptor:mtl_pipe_desc
      error:&err];
  std::optional<MTLPrimitiveType> prim_type =
      get_mtl_primitive_type(info->primitive_type);
  if (!prim_type.has_value()) {
    NGFI_DIAG_ERROR("Primitive type %d not supported by Metal backend.",
                     info->primitive_type);
    return NGF_ERROR_INVALID_ENUM;
  }
  pipeline->primitive_type = *prim_type;
  
  // Set winding order and culling mode.
  pipeline->winding = get_mtl_winding(info->rasterization->front_face);
  pipeline->culling = get_mtl_culling(info->rasterization->cull_mode);
  
  // Set up depth and stencil state.
  if (info->depth_stencil->depth_test) {
    auto *mtl_depth_stencil_desc = [MTLDepthStencilDescriptor new];
    const ngf_depth_stencil_info &depth_stencil_info = *info->depth_stencil;
    mtl_depth_stencil_desc.depthCompareFunction =
        get_mtl_compare_function(depth_stencil_info.depth_compare);
    mtl_depth_stencil_desc.depthWriteEnabled = info->depth_stencil->depth_write;
    mtl_depth_stencil_desc.backFaceStencil =
        ngfmtl_create_stencil_descriptor(depth_stencil_info.back_stencil);
    mtl_depth_stencil_desc.frontFaceStencil =
        ngfmtl_create_stencil_descriptor(depth_stencil_info.front_stencil);
    pipeline->front_stencil_reference =
        depth_stencil_info.front_stencil.reference;
    pipeline->back_stencil_reference = depth_stencil_info.back_stencil.reference;
    pipeline->depth_stencil =
        [CURRENT_CONTEXT->device
         newDepthStencilStateWithDescriptor:mtl_depth_stencil_desc];
  }
  
  if (err) {
    NGFI_DIAG_ERROR([err.localizedDescription UTF8String]);
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  } else {
    *result = pipeline.release();
    return NGF_ERROR_OK;
  }
}

void ngf_destroy_graphics_pipeline(ngf_graphics_pipeline pipe) NGF_NOEXCEPT {
  if (pipe != nullptr) {
    pipe->~ngf_graphics_pipeline_t();
    NGFI_FREE(pipe);
  }
}

id<MTLBuffer> ngfmtl_create_buffer(const ngf_buffer_info &info) {
  MTLResourceOptions options = 0u;
  MTLResourceOptions managed_storage = 0u;
#if TARGET_OS_OSX
  managed_storage = MTLResourceStorageModeManaged;
#endif
  switch(info.storage_type) {
  case NGF_BUFFER_STORAGE_HOST_READABLE:
  case NGF_BUFFER_STORAGE_HOST_READABLE_WRITEABLE:
      options = MTLResourceCPUCacheModeDefaultCache | managed_storage;
      break;
  case NGF_BUFFER_STORAGE_HOST_WRITEABLE:
      options = MTLResourceCPUCacheModeWriteCombined | managed_storage;
      break;
  case NGF_BUFFER_STORAGE_PRIVATE:
      options = MTLResourceStorageModePrivate;
      break;
  default: assert(false);
  }
  id<MTLBuffer> mtl_buffer =
      [CURRENT_CONTEXT->device
          newBufferWithLength:info.size
                      options:options];
  return mtl_buffer;
}

uint8_t* _ngf_map_buffer(id<MTLBuffer> buffer,
                      size_t offset,
                      [[maybe_unused]] size_t size) {
  return (uint8_t*)buffer.contents + offset;
}

ngf_error ngf_create_attrib_buffer(const ngf_attrib_buffer_info *info,
                                   ngf_attrib_buffer *result) NGF_NOEXCEPT {
  NGFMTL_NURSERY(attrib_buffer, buf);
  buf->mtl_buffer = ngfmtl_create_buffer(*info);
  *result = buf.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_attrib_buffer(ngf_attrib_buffer buf) NGF_NOEXCEPT {
  if (buf != nullptr) {
    buf->~ngf_attrib_buffer_t();
    NGFI_FREE(buf);
  }
}

void* ngf_attrib_buffer_map_range(ngf_attrib_buffer buf,
                                  size_t offset,
                                  size_t size,
                                  [[maybe_unused]] uint32_t flags) NGF_NOEXCEPT {
  buf->mapped_offset = offset;
  return (void*)_ngf_map_buffer(buf->mtl_buffer, offset, size);
}

void ngf_attrib_buffer_flush_range(
    [[maybe_unused]] ngf_attrib_buffer buf,
    [[maybe_unused]] size_t offset,
    [[maybe_unused]] size_t size) NGF_NOEXCEPT {
#if TARGET_OS_OSX
  [buf->mtl_buffer didModifyRange:NSMakeRange(buf->mapped_offset + offset,
                                              size)];
#endif
}

void ngf_attrib_buffer_unmap([[maybe_unused]] ngf_attrib_buffer buf) NGF_NOEXCEPT {}

ngf_error ngf_create_index_buffer(const ngf_index_buffer_info *info,
                                  ngf_index_buffer *result) NGF_NOEXCEPT {
  NGFMTL_NURSERY(index_buffer, buf);
  buf->mtl_buffer = ngfmtl_create_buffer(*info);
  *result = buf.release();
  return NGF_ERROR_OK;
}

void* ngf_index_buffer_map_range(ngf_index_buffer buf,
                                 size_t offset,
                                 size_t size,
                                 [[maybe_unused]] uint32_t flags) NGF_NOEXCEPT {
  buf->mapped_offset = offset;
  return (void*)_ngf_map_buffer(buf->mtl_buffer, offset, size);
}

void ngf_index_buffer_flush_range(
    [[maybe_unused]] ngf_index_buffer buf,
    [[maybe_unused]] size_t offset,
    [[maybe_unused]] size_t size) NGF_NOEXCEPT {
#if TARGET_OS_OSX
  [buf->mtl_buffer didModifyRange:NSMakeRange(buf->mapped_offset + offset,
                                              size)];
#endif
}

void ngf_index_buffer_unmap([[maybe_unused]] ngf_index_buffer buf) NGF_NOEXCEPT {}

void ngf_destroy_index_buffer(ngf_index_buffer buf) NGF_NOEXCEPT {
  if (buf != nullptr) {
    buf->~ngf_index_buffer_t();
    NGFI_FREE(buf);
  }
}

ngf_error ngf_create_uniform_buffer(const ngf_uniform_buffer_info *info,
                                    ngf_uniform_buffer *result) NGF_NOEXCEPT {
  NGFMTL_NURSERY(uniform_buffer, buf);
  buf->mtl_buffer = ngfmtl_create_buffer(*info);
  *result = buf.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_uniform_buffer(ngf_uniform_buffer buf) NGF_NOEXCEPT {
  if (buf != nullptr) {
    buf->~ngf_uniform_buffer_t();
    NGFI_FREE(buf);
  }
}

void* ngf_uniform_buffer_map_range(ngf_uniform_buffer buf,
                                   size_t offset,
                                   size_t size,
                                   [[maybe_unused]] uint32_t flags) NGF_NOEXCEPT {
  buf->mapped_offset = offset;
  return (void*)_ngf_map_buffer(buf->mtl_buffer, offset, size);
}

void ngf_uniform_buffer_flush_range([[maybe_unused]] ngf_uniform_buffer buf,
                                    [[maybe_unused]] size_t offset,
                                    [[maybe_unused]] size_t size) NGF_NOEXCEPT {
#if TARGET_OS_OSX
  [buf->mtl_buffer didModifyRange:NSMakeRange(buf->mapped_offset + offset,
                                              size)];
#endif
}

void ngf_uniform_buffer_unmap([[maybe_unused]] ngf_uniform_buffer buf) NGF_NOEXCEPT {}

ngf_error ngf_create_pixel_buffer(const ngf_pixel_buffer_info *info,
                                  ngf_pixel_buffer *result) NGF_NOEXCEPT {
  assert(info);
  assert(result);
  NGFMTL_NURSERY(pixel_buffer, buf);
  ngf_buffer_info bufinfo = {
    info->size,
    NGF_BUFFER_STORAGE_HOST_WRITEABLE
  };
  buf->mtl_buffer = ngfmtl_create_buffer(bufinfo);
  *result = buf.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_pixel_buffer(ngf_pixel_buffer buf) NGF_NOEXCEPT {
  if (buf != nullptr) {
    buf->~ngf_pixel_buffer_t();
    NGFI_FREE(buf);
  }
}

void* ngf_pixel_buffer_map_range(ngf_pixel_buffer buf,
                                 size_t offset,
                                 size_t size,
                                 [[maybe_unused]] uint32_t flags) NGF_NOEXCEPT {
  buf->mapped_offset = offset;
  return (void*)_ngf_map_buffer(buf->mtl_buffer, offset, size);
}

void ngf_pixel_buffer_flush_range([[maybe_unused]] ngf_pixel_buffer buf,
                                  [[maybe_unused]] size_t offset,
                                  [[maybe_unused]] size_t size) NGF_NOEXCEPT {
#if TARGET_OS_OSX
  [buf->mtl_buffer didModifyRange:NSMakeRange(buf->mapped_offset + offset,
                                              size)];
#endif
}

void ngf_pixel_buffer_unmap([[maybe_unused]] ngf_pixel_buffer buf) NGF_NOEXCEPT {}

ngf_error ngf_create_sampler(const ngf_sampler_info *info,
                             ngf_sampler *result) NGF_NOEXCEPT {
  auto *sampler_desc = [MTLSamplerDescriptor new];
  std::optional<MTLSamplerAddressMode> s = get_mtl_address_mode(info->wrap_s),
                                       t = get_mtl_address_mode(info->wrap_t),
                                       r = get_mtl_address_mode(info->wrap_r);
  if (!(s && t && r)) {
    return NGF_ERROR_INVALID_ENUM;
  }
  sampler_desc.sAddressMode = *s;
  sampler_desc.tAddressMode = *t;
  sampler_desc.rAddressMode = *r;
  sampler_desc.minFilter = get_mtl_minmag_filter(info->min_filter);
  sampler_desc.magFilter = get_mtl_minmag_filter(info->mag_filter);
  sampler_desc.mipFilter = get_mtl_mip_filter(info->mip_filter);
  sampler_desc.maxAnisotropy = (NSUInteger)info->max_anisotropy;
  // TODO unmipped images
  sampler_desc.lodMinClamp = info->lod_min;
  sampler_desc.lodMaxClamp = info->lod_max;
  NGFMTL_NURSERY(sampler, sampler);
  sampler->sampler =
      [CURRENT_CONTEXT->device newSamplerStateWithDescriptor:sampler_desc];
  *result = sampler.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_sampler(ngf_sampler sampler) NGF_NOEXCEPT {
  if (sampler) {
    sampler->~ngf_sampler_t();
    NGFI_FREE(sampler);
  }
}

ngf_error ngf_create_cmd_buffer(const ngf_cmd_buffer_info*,
                                ngf_cmd_buffer *result) NGF_NOEXCEPT {
  NGFMTL_NURSERY(cmd_buffer, cmd_buffer);
  *result = cmd_buffer.release();
  return NGF_ERROR_OK;
}

ngf_error ngf_create_image(const ngf_image_info *info, ngf_image *result) NGF_NOEXCEPT {
  auto *mtl_img_desc = [MTLTextureDescriptor new];
  
  const MTLPixelFormat fmt = get_mtl_pixel_format(info->format).format;
  if (fmt == MTLPixelFormatInvalid) {
    NGFI_DIAG_ERROR("Image format %d not supported by Metal backend.",
                    info->format);
    return NGF_ERROR_INVALID_FORMAT;
  }

  std::optional<MTLTextureType> maybe_texture_type =
      get_mtl_texture_type(info->type, info->extent.depth, info->sample_count);
  if (!maybe_texture_type.has_value()) {
    NGFI_DIAG_ERROR("Image type %d not supported by Metal backend.",
                    info->type);
    return NGF_ERROR_INVALID_ENUM;
  }
  mtl_img_desc.textureType      = maybe_texture_type.value();
  mtl_img_desc.pixelFormat      = fmt;
  mtl_img_desc.width            = info->extent.width;
  mtl_img_desc.height           = info->extent.height;
  mtl_img_desc.mipmapLevelCount = info->nmips;
  mtl_img_desc.storageMode      = MTLStorageModePrivate;
  mtl_img_desc.sampleCount      = info->sample_count;
  if (info->usage_hint & NGF_IMAGE_USAGE_ATTACHMENT) {
    mtl_img_desc.usage |= MTLTextureUsageRenderTarget;
  }
  if (info->usage_hint & NGF_IMAGE_USAGE_SAMPLE_FROM) {
    mtl_img_desc.usage |= MTLTextureUsageShaderRead;
  }
  switch(mtl_img_desc.textureType) {
  case MTLTextureType2D:
  case MTLTextureType2DMultisample:
  case MTLTextureType3D:
  case MTLTextureTypeCube:
      mtl_img_desc.depth = info->extent.depth;
      break;
  case MTLTextureType2DArray:
  case MTLTextureType2DMultisampleArray:
  case MTLTextureTypeCubeArray:
      mtl_img_desc.depth       = 1u;
      mtl_img_desc.arrayLength = info->extent.depth;
      break;
  default:
      assert(false);
  }
  NGFMTL_NURSERY(image, image);
  image->texture =
      [MTL_DEVICE newTextureWithDescriptor:mtl_img_desc];
  image->format = info->format;
  *result = image.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_image(ngf_image image) NGF_NOEXCEPT {
  if (image != nullptr) {
    image->~ngf_image_t();
    NGFI_FREE(image);
  }
}

void ngf_destroy_cmd_buffer(ngf_cmd_buffer cmd_buffer) NGF_NOEXCEPT {
  if (cmd_buffer != nullptr) {
    cmd_buffer->~ngf_cmd_buffer_t();
    NGFI_FREE(cmd_buffer);
  }
}

ngf_error ngf_start_cmd_buffer(ngf_cmd_buffer cmd_buffer, ngf_frame_token) NGF_NOEXCEPT {
  assert(cmd_buffer);
  cmd_buffer->mtl_cmd_buffer = nil;
  cmd_buffer->mtl_cmd_buffer = [CURRENT_CONTEXT->queue commandBuffer];
  cmd_buffer->active_rce = nil;
  cmd_buffer->active_bce = nil;
  NGFI_TRANSITION_CMD_BUF(cmd_buffer, NGFI_CMD_BUFFER_READY);
  return NGF_ERROR_OK;
}

ngf_error ngf_submit_cmd_buffers(uint32_t n, ngf_cmd_buffer *cmd_buffers) NGF_NOEXCEPT {
  if (CURRENT_CONTEXT->pending_cmd_buffer) {
    [CURRENT_CONTEXT->pending_cmd_buffer commit];
    CURRENT_CONTEXT->pending_cmd_buffer = nil;
  }
  for (uint32_t b = 0u; b < n; ++b) {
    NGFI_TRANSITION_CMD_BUF(cmd_buffers[b], NGFI_CMD_BUFFER_SUBMITTED);
    if (b < n - 1u) {
      [cmd_buffers[b]->mtl_cmd_buffer commit];
    } else {
      CURRENT_CONTEXT->pending_cmd_buffer = cmd_buffers[b]->mtl_cmd_buffer;
    }
    cmd_buffers[b]->mtl_cmd_buffer = nil;
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_buffer_start_render(ngf_cmd_buffer cmd_buf,
                                      ngf_render_encoder *enc) NGF_NOEXCEPT {
  enc->__handle = 0u;
  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_RECORDING);
  enc->__handle = (uintptr_t)cmd_buf;
  return NGF_ERROR_OK;
}

ngf_error ngf_render_encoder_end(ngf_render_encoder enc) NGF_NOEXCEPT {
  auto cmd_buf = (ngf_cmd_buffer)enc.__handle;
  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_AWAITING_SUBMIT);
  if (cmd_buf->active_rce){
    [cmd_buf->active_rce endEncoding];
    cmd_buf->active_rce = nil;
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_buffer_start_xfer(ngf_cmd_buffer cmd_buf,
                                    ngf_xfer_encoder *enc) NGF_NOEXCEPT {
  enc->__handle = 0u;
  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_RECORDING);
  enc->__handle = (uintptr_t)cmd_buf;
  return NGF_ERROR_OK;
}

ngf_error ngf_xfer_encoder_end(ngf_xfer_encoder enc) NGF_NOEXCEPT {
  auto cmd_buf = (ngf_cmd_buffer)enc.__handle;
  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_AWAITING_SUBMIT);
  if (cmd_buf->active_bce) {
    [cmd_buf->active_bce endEncoding];
    cmd_buf->active_bce = nil;
  }
  return NGF_ERROR_OK;
}

void ngf_cmd_begin_pass(ngf_render_encoder enc,
                        const ngf_render_target rt) NGF_NOEXCEPT {
  auto cmd_buffer = (ngf_cmd_buffer)enc.__handle;
  cmd_buffer->renderpass_active = true;
  if (cmd_buffer->active_rce) {
    [cmd_buffer->active_rce endEncoding];
    cmd_buffer->active_rce = nil;
  } else if (cmd_buffer->active_bce) {
    [cmd_buffer->active_bce endEncoding];
    cmd_buffer->active_bce = nil;
  }
  if (rt->is_default) {
    assert(CURRENT_CONTEXT->frame.color_drawable);
    rt->pass_descriptor.colorAttachments[0].texture =
      CURRENT_CONTEXT->frame.color_attachment_texture();
    rt->pass_descriptor.colorAttachments[0].resolveTexture =
      CURRENT_CONTEXT->frame.resolve_attachment_texture();
    ngf_image_format dfmt = CURRENT_CONTEXT->swapchain_info.depth_format;
    if (dfmt != NGF_IMAGE_FORMAT_UNDEFINED) {
      id<MTLTexture> depth_tex = CURRENT_CONTEXT->frame.depth_attachment_texture();
      rt->pass_descriptor.depthAttachment.texture = depth_tex;
      if (dfmt == NGF_IMAGE_FORMAT_DEPTH24_STENCIL8) {
        rt->pass_descriptor.stencilAttachment.texture = depth_tex;
      }
    }
  }
  cmd_buffer->active_rce =
      [cmd_buffer->mtl_cmd_buffer
       renderCommandEncoderWithDescriptor:rt->pass_descriptor];
  cmd_buffer->active_rt = rt;
}

void ngf_cmd_end_pass(ngf_render_encoder enc) NGF_NOEXCEPT {
  auto cmd_buffer = (ngf_cmd_buffer)enc.__handle;
  cmd_buffer->renderpass_active = false;
  [cmd_buffer->active_rce endEncoding];
  cmd_buffer->active_rce = nil;
  cmd_buffer->active_pipe = nullptr;
}

void ngf_cmd_bind_gfx_pipeline(ngf_render_encoder enc,
                               const ngf_graphics_pipeline pipeline) NGF_NOEXCEPT {
  auto buf = (ngf_cmd_buffer)enc.__handle;
  [buf->active_rce setRenderPipelineState:pipeline->pipeline];
  [buf->active_rce setCullMode:pipeline->culling];
  if (buf->active_rt->is_default) {
    [buf->active_rce setFrontFacingWinding:pipeline->winding];
  } else {
    if (pipeline->winding == MTLWindingClockwise)
      [buf->active_rce setFrontFacingWinding:MTLWindingCounterClockwise];
    else if (pipeline->winding == MTLWindingCounterClockwise)
      [buf->active_rce setFrontFacingWinding:MTLWindingClockwise];
    else
      [buf->active_rce setFrontFacingWinding:pipeline->winding];
  }

  [buf->active_rce setBlendColorRed:pipeline->blend_color[0]
                              green:pipeline->blend_color[1]
                               blue:pipeline->blend_color[2]
                              alpha:pipeline->blend_color[3]];
  if (pipeline->depth_stencil) {
    [buf->active_rce setDepthStencilState:pipeline->depth_stencil];
  }
  [buf->active_rce
      setStencilFrontReferenceValue:pipeline->front_stencil_reference
      backReferenceValue:pipeline->back_stencil_reference];
  buf->active_pipe = pipeline;
}

void ngf_cmd_viewport(ngf_render_encoder enc, const ngf_irect2d *r) NGF_NOEXCEPT {
  auto buf = (ngf_cmd_buffer)enc.__handle;
  MTLViewport viewport;
  viewport.originX = r->x;
  const uint32_t top = (uint32_t)r->y + r->height;
  const ngf_render_target rt = buf->active_rt;
  if (rt->is_default) {
    viewport.originY = CURRENT_CONTEXT->swapchain_info.height - top;
  } else {
    viewport.originY = top;
  }
  viewport.width = r->width;
  viewport.height = (rt->is_default ? 1.0 : -1.0) * r->height;

  // TODO: fix
  viewport.znear = 0.0f;
  viewport.zfar = 1.0f;

  [buf->active_rce setViewport:viewport];
}

void ngf_cmd_scissor(ngf_render_encoder enc, const ngf_irect2d *r) NGF_NOEXCEPT {
  auto buf = (ngf_cmd_buffer)enc.__handle;
  MTLScissorRect scissor;
  scissor.x = (NSUInteger)r->x;
  const uint32_t top = (uint32_t)r->y + r->height;
  const ngf_render_target rt = buf->active_rt;
  if (rt->is_default) {
    scissor.y = CURRENT_CONTEXT->swapchain_info.height - top;
  } else {
    scissor.y = rt->height - top;
  }
  scissor.width = r->width;
  scissor.height = r->height;
  [buf->active_rce setScissorRect:scissor];
}

void ngf_cmd_draw(ngf_render_encoder enc, bool indexed,
                  uint32_t first_element, uint32_t nelements,
                  uint32_t ninstances) NGF_NOEXCEPT {
  auto buf = (ngf_cmd_buffer)enc.__handle;
  MTLPrimitiveType prim_type = buf->active_pipe->primitive_type;
  if (!indexed) {
    [buf->active_rce drawPrimitives:prim_type
                      vertexStart:first_element
                      vertexCount:nelements
                      instanceCount:ninstances
                      baseInstance:0];
  } else {
    MTLIndexType index_type = buf->bound_index_buffer_type;
    size_t index_size =
        index_type == MTLIndexTypeUInt16 ? sizeof(uint16_t) : sizeof(uint32_t);
    [buf->active_rce drawIndexedPrimitives:prim_type
     indexCount:nelements indexType:buf->bound_index_buffer_type
     indexBuffer:buf->bound_index_buffer
     indexBufferOffset:first_element * index_size
     instanceCount:ninstances
     baseVertex:0
     baseInstance:0];
  }
}

void ngf_cmd_bind_attrib_buffer(ngf_render_encoder enc,
                                const ngf_attrib_buffer buf,
                                uint32_t binding,
                                uint32_t offset) NGF_NOEXCEPT {
  auto cmd_buf = (ngf_cmd_buffer)enc.__handle;
  [cmd_buf->active_rce setVertexBuffer:buf->mtl_buffer
                                offset:offset
                               atIndex:MAX_BUFFER_BINDINGS - binding];
}

void ngf_cmd_bind_index_buffer(ngf_render_encoder enc,
                               const ngf_index_buffer buf,
                               ngf_type type) NGF_NOEXCEPT {
  auto cmd_buf = (ngf_cmd_buffer)enc.__handle;
  cmd_buf->bound_index_buffer = buf->mtl_buffer;
  cmd_buf->bound_index_buffer_type = get_mtl_index_type(type);
}

void ngf_cmd_bind_gfx_resources(ngf_render_encoder enc,
                                const ngf_resource_bind_op *bind_ops,
                                uint32_t nbind_ops) NGF_NOEXCEPT {
  auto cmd_buf = (ngf_cmd_buffer)enc.__handle;
  for (uint32_t o = 0u; o < nbind_ops; ++o) {
    const ngf_resource_bind_op &bind_op = bind_ops[o];
    assert(cmd_buf->active_pipe);
    const ngfi_native_binding *nb =
         ngfi_binding_map_lookup(cmd_buf->active_pipe->binding_map,
                                bind_op.target_set,
                                bind_op.target_binding);
    if (nb == nullptr) {
      // TODO: call debug callback.
      continue;
    }
    const uint32_t ngf_binding = bind_op.target_binding;
    const uint32_t native_binding = nb->native_binding_id;
    const ngf_pipeline_layout_info &layout = cmd_buf->active_pipe->layout;
    const ngf_descriptor_set_layout_info &set_layout =
        layout.descriptor_set_layouts[bind_op.target_set];
    const auto descriptor_it =
        std::find_if(set_layout.descriptors,
                     set_layout.descriptors + set_layout.ndescriptors,
                     [ngf_binding](const ngf_descriptor_info &d){
                       return d.id == ngf_binding;
                     });
    assert(descriptor_it != set_layout.descriptors + set_layout.ndescriptors);
    const uint32_t set_stage_flags = descriptor_it->stage_flags;
    
    const bool frag_stage_visible = set_stage_flags &
                                    NGF_DESCRIPTOR_FRAGMENT_STAGE_BIT,
               vert_stage_visible = set_stage_flags &
                                    NGF_DESCRIPTOR_VERTEX_STAGE_BIT;
    switch(bind_op.type) {
      case NGF_DESCRIPTOR_UNIFORM_BUFFER: {
        const  ngf_uniform_buffer_bind_info &buf_bind_op =
            bind_op.info.uniform_buffer;
        const ngf_uniform_buffer buf = buf_bind_op.buffer;
        size_t offset = buf->current_idx * buf->size + buf_bind_op.offset;
        if (vert_stage_visible) {
          [cmd_buf->active_rce setVertexBuffer:buf->mtl_buffer
                                        offset:offset
                                       atIndex:native_binding];
        }
        if (frag_stage_visible) {
          [cmd_buf->active_rce setFragmentBuffer:buf->mtl_buffer
                                          offset:offset
                                         atIndex:native_binding];
        }
        break;}
      case NGF_DESCRIPTOR_TEXTURE_AND_SAMPLER: {
        // TODO use texture view
        const ngf_image_sampler_bind_info &img_bind_op =
            bind_op.info.image_sampler;
        if (vert_stage_visible) {
          [cmd_buf->active_rce
           setVertexTexture:img_bind_op.image_subresource.image->texture
           atIndex:native_binding];
          [cmd_buf->active_rce
           setVertexSamplerState:img_bind_op.sampler->sampler
           atIndex:native_binding];
        }
        if (frag_stage_visible) {
          [cmd_buf->active_rce
           setFragmentTexture:img_bind_op.image_subresource.image->texture
           atIndex:native_binding];
          [cmd_buf->active_rce
           setFragmentSamplerState:img_bind_op.sampler->sampler
           atIndex:native_binding];
        }
        break; }
      case NGF_DESCRIPTOR_TEXTURE: {// TODO use texture view
        const ngf_image_sampler_bind_info &img_bind_op =
            bind_op.info.image_sampler;
        if (vert_stage_visible) {
          [cmd_buf->active_rce
           setVertexTexture:img_bind_op.image_subresource.image->texture
           atIndex:native_binding];
        }
        if (frag_stage_visible) {
          [cmd_buf->active_rce
           setFragmentTexture:img_bind_op.image_subresource.image->texture
           atIndex:native_binding];
        }
        break; }
      case NGF_DESCRIPTOR_SAMPLER: {
        const ngf_image_sampler_bind_info &img_bind_op =
            bind_op.info.image_sampler;
        if (vert_stage_visible) {
          [cmd_buf->active_rce
           setVertexSamplerState:img_bind_op.sampler->sampler
           atIndex:native_binding];
        }
        if (frag_stage_visible) {
          [cmd_buf->active_rce
           setFragmentSamplerState:img_bind_op.sampler->sampler
           atIndex:native_binding];
        }
        break; }
      case NGF_DESCRIPTOR_TYPE_COUNT: assert(false);
    }
  }
}

void ngfmtl_cmd_copy_buffer(ngf_xfer_encoder enc,
                          id<MTLBuffer> src, id<MTLBuffer> dst,
                          size_t size, size_t src_offset, size_t dst_offset) {
  auto buf = (ngf_cmd_buffer)enc.__handle;
  assert(buf->active_rce == nil);
  if (buf->active_bce == nil) {
    buf->active_bce = [buf->mtl_cmd_buffer blitCommandEncoder];
  }
  [buf->active_bce copyFromBuffer:src sourceOffset:src_offset toBuffer:dst
                destinationOffset:dst_offset size:size];
}

void ngf_cmd_copy_attrib_buffer(ngf_xfer_encoder enc,
                                const ngf_attrib_buffer src,
                                ngf_attrib_buffer dst,
                                size_t size,
                                size_t src_offset,
                                size_t dst_offset) NGF_NOEXCEPT {
  ngfmtl_cmd_copy_buffer(enc, src->mtl_buffer, dst->mtl_buffer, size, src_offset,
                       dst_offset);
}

void ngf_cmd_copy_index_buffer(ngf_xfer_encoder enc,
                               const ngf_index_buffer src,
                               ngf_index_buffer dst,
                               size_t size,
                               size_t src_offset,
                               size_t dst_offset) NGF_NOEXCEPT {
  ngfmtl_cmd_copy_buffer(enc, src->mtl_buffer, dst->mtl_buffer, size, src_offset,
                       dst_offset);
}

void ngf_cmd_copy_uniform_buffer(ngf_xfer_encoder enc,
                                 const ngf_uniform_buffer src,
                                 ngf_uniform_buffer dst,
                                 size_t size,
                                 size_t src_offset,
                                 size_t dst_offset) NGF_NOEXCEPT {
  ngfmtl_cmd_copy_buffer(enc, src->mtl_buffer, dst->mtl_buffer, size, src_offset,
                       dst_offset);
}

static uint32_t _get_pitch(const uint32_t width, const ngf_image_format format) {
  const mtl_format f = get_mtl_pixel_format(format);
  const uint32_t bits = f.rbits + f.gbits + f.bbits + f.abits + f.dbits + f.sbits;
  return width * bits / 8;
}

void ngf_cmd_write_image(ngf_xfer_encoder enc,
                         const ngf_pixel_buffer src,
                         size_t src_offset,
                         ngf_image_ref dst,
                         const ngf_offset3d *offset,
                         const ngf_extent3d *extent) NGF_NOEXCEPT {
  auto buf = (ngf_cmd_buffer)enc.__handle;
  assert(buf->active_rce == nil);
  if (buf->active_bce == nil) {
    buf->active_bce = [buf->mtl_cmd_buffer blitCommandEncoder];
  }
  const MTLTextureType texture_type = dst.image->texture.textureType;
  const bool           is_cubemap   = texture_type == MTLTextureTypeCube ||
                                      texture_type == MTLTextureTypeCubeArray;
  const uint32_t       target_slice = (is_cubemap ? 6u : 1u) * dst.layer +
                                      (is_cubemap ? dst.cubemap_face : 0);
  const uint32_t pitch = _get_pitch(extent->width, dst.image->format);
  [buf->active_bce copyFromBuffer:src->mtl_buffer
                     sourceOffset:src_offset
                sourceBytesPerRow:pitch
              sourceBytesPerImage:pitch * extent->height
                       sourceSize:MTLSizeMake(extent->width,
                                              extent->height,
                                              extent->depth)
                        toTexture:dst.image->texture
                 destinationSlice:target_slice
                 destinationLevel:dst.mip_level
                destinationOrigin:MTLOriginMake((NSUInteger)offset->x,
                                                (NSUInteger)offset->y,
                                                (NSUInteger)offset->z)];
}

#define PLACEHOLDER_CMD(name, ...) \
void ngf_cmd_##name(ngf_cmd_buffer*, __VA_ARGS__) {}

PLACEHOLDER_CMD(stencil_reference, uint32_t uint32_t)
PLACEHOLDER_CMD(stencil_compare_mask, uint32_t uint32_t)
PLACEHOLDER_CMD(stencil_write_mask, uint32_t uint32_t)
PLACEHOLDER_CMD(line_width, float)
