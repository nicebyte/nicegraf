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

#include "ngf-common/cmdbuf-state.h"
#include "ngf-common/macros.h"
#include "ngf-common/native-binding-map.h"
#include "ngf-common/stack-alloc.h"
#include "nicegraf-wrappers.h"
#include "nicegraf.h"

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
using NGFMTL_VIEW_TYPE = NSView;
#else
#import <UIKit/UIKit.h>
using NGFMTL_VIEW_TYPE = UIView;
#endif

// Indicates the maximum amount of buffers (attrib, index and uniform) that
// can be bound at the same time.
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

ngf_device_capabilities DEVICE_CAPS;

#pragma mark ngf_enum_maps

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
      MTLBlendFactorOneMinusBlendAlpha};
  return factors[f];
}

static MTLBlendOperation get_mtl_blend_operation(ngf_blend_op op) {
  static constexpr MTLBlendOperation ops[NGF_BLEND_OP_COUNT] = {
      MTLBlendOperationAdd,
      MTLBlendOperationSubtract,
      MTLBlendOperationReverseSubtract,
      MTLBlendOperationMin,
      MTLBlendOperationMax};
  return ops[op];
}

struct mtl_format {
  const MTLPixelFormat format         = MTLPixelFormatInvalid;
  const uint8_t        bits_per_block = 0;
  const bool           srgb           = false;
  const uint8_t        block_width    = 1;
  const uint8_t        block_height   = 1;
};

static mtl_format get_mtl_pixel_format(ngf_image_format f) {
  static const mtl_format formats[NGF_IMAGE_FORMAT_COUNT] = {
    {MTLPixelFormatR8Unorm, 8},
    {MTLPixelFormatRG8Unorm, 16},
    {},  // RGB8, unsupported
    {MTLPixelFormatRGBA8Unorm, 32},
    {},  // SRGB8, unsupported
    {MTLPixelFormatRGBA8Unorm_sRGB, 32, true},
    {},  // BGR8, unsupported
    {MTLPixelFormatBGRA8Unorm, 32},
    {},  // BGR8_SRGB, unsupported
    {MTLPixelFormatBGRA8Unorm_sRGB, 32, true},
    {MTLPixelFormatRGB10A2Unorm, 32},
    {MTLPixelFormatR32Float, 32},
    {MTLPixelFormatRG32Float, 64},
    {},  // RGB32F, unsupported
    {MTLPixelFormatRGBA32Float, 128},
    {MTLPixelFormatR16Float, 16},
    {MTLPixelFormatRG16Float, 32},
    {},  // RGB16F, unsupported
    {MTLPixelFormatRGBA16Float, 64},
    {MTLPixelFormatRG11B10Float, 32},
    {MTLPixelFormatRGB9E5Float, 32},
    {MTLPixelFormatR16Unorm, 16},
    {MTLPixelFormatR16Snorm, 16},
    {MTLPixelFormatRG16Unorm, 32},
    {MTLPixelFormatRG16Snorm, 32},
    {MTLPixelFormatRGBA16Unorm, 64},
    {MTLPixelFormatRGBA16Snorm, 64},
    {MTLPixelFormatR8Uint, 8},
    {MTLPixelFormatR8Sint, 8},
    {MTLPixelFormatR16Uint, 16},
    {MTLPixelFormatR16Sint, 16},
    {MTLPixelFormatRG16Uint, 32},
    {},  // RGB16U, unsupported
    {MTLPixelFormatRGBA16Uint, 64},
    {MTLPixelFormatR32Uint, 32},
    {MTLPixelFormatRG32Uint, 64},
    {},  // RGB32U, unsupported
    {MTLPixelFormatRGBA32Uint, 128},
#if TARGET_OS_OSX
    {MTLPixelFormatBC7_RGBAUnorm, 128, false, 4, 4},
    {MTLPixelFormatBC7_RGBAUnorm_sRGB, 128, true, 4, 4},
#else
    {},
    {},  // BC7, unsupported on iOS
#endif
#if TARGET_OS_OSX && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ < 110000
    // ASTC is not supported till macOS 11.0
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
    {},
#else
    {MTLPixelFormatASTC_4x4_LDR, 128, false, 4, 4},
    {MTLPixelFormatASTC_4x4_sRGB, 128, true, 4, 4},
    {MTLPixelFormatASTC_5x4_LDR, 128, false, 5, 4},
    {MTLPixelFormatASTC_5x4_sRGB, 128, true, 5, 4},
    {MTLPixelFormatASTC_5x5_LDR, 128, false, 5, 5},
    {MTLPixelFormatASTC_5x5_sRGB, 128, true, 5, 5},
    {MTLPixelFormatASTC_6x5_LDR, 128, false, 6, 5},
    {MTLPixelFormatASTC_6x5_sRGB, 128, true, 6, 5},
    {MTLPixelFormatASTC_6x6_LDR, 128, false, 6, 6},
    {MTLPixelFormatASTC_6x6_sRGB, 128, true, 6, 6},
    {MTLPixelFormatASTC_8x5_LDR, 128, false, 8, 5},
    {MTLPixelFormatASTC_8x5_sRGB, 128, true, 8, 5},
    {MTLPixelFormatASTC_8x6_LDR, 128, false, 8, 6},
    {MTLPixelFormatASTC_8x6_sRGB, 128, true, 8, 6},
    {MTLPixelFormatASTC_8x8_LDR, 128, false, 8, 8},
    {MTLPixelFormatASTC_8x8_sRGB, 128, true, 8, 8},
    {MTLPixelFormatASTC_10x5_LDR, 128, false, 10, 5},
    {MTLPixelFormatASTC_10x5_sRGB, 128, true, 10, 5},
    {MTLPixelFormatASTC_10x6_LDR, 128, false, 10, 6},
    {MTLPixelFormatASTC_10x6_sRGB, 128, true, 10, 6},
    {MTLPixelFormatASTC_10x8_LDR, 128, false, 10, 8},
    {MTLPixelFormatASTC_10x8_sRGB, 128, true, 10, 8},
    {MTLPixelFormatASTC_10x10_LDR, 128, false, 10, 10},
    {MTLPixelFormatASTC_10x10_sRGB, 128, true, 10, 10},
    {MTLPixelFormatASTC_12x10_LDR, 128, false, 12, 10},
    {MTLPixelFormatASTC_12x10_sRGB, 128, true, 12, 10},
    {MTLPixelFormatASTC_12x12_LDR, 128, false, 12, 12},
    {MTLPixelFormatASTC_12x12_sRGB, 128, true, 12, 12},
#endif
    {MTLPixelFormatDepth32Float, 32},
#if TARGET_OS_OSX
    {MTLPixelFormatDepth16Unorm, 16},
    {MTLPixelFormatDepth32Float_Stencil8, 32},  // instead of 24Unorm_Stencil8,
                                                // because metal validator doesn't
                                                // like it for some reason...
#else
    {},                                         // DEPTH16, iOS does not support.
    {MTLPixelFormatDepth32Float_Stencil8, 32},  // Emulate DEPTH24_STENCIL8 on iOS
#endif
    {}
  };
  return formats[f];
}

static MTLLoadAction get_mtl_load_action(ngf_attachment_load_op op) {
  static const MTLLoadAction action[NGF_LOAD_OP_COUNT] = {
      MTLLoadActionDontCare,
      MTLLoadActionLoad,
      MTLLoadActionClear};
  return action[op];
}

static MTLStoreAction get_mtl_store_action(ngf_attachment_store_op op) {
  static const MTLStoreAction action[NGF_STORE_OP_COUNT] = {
      MTLStoreActionDontCare,
      MTLStoreActionStore,
      MTLStoreActionMultisampleResolve};
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

static MTLVertexFormat get_mtl_attrib_format(ngf_type type, uint32_t size, bool normalized) {
  static const MTLVertexFormat formats[NGF_TYPE_COUNT][2][4] = {
      {{MTLVertexFormatChar, MTLVertexFormatChar2, MTLVertexFormatChar3, MTLVertexFormatChar4},
       {MTLVertexFormatCharNormalized,
        MTLVertexFormatChar2Normalized,
        MTLVertexFormatChar3Normalized,
        MTLVertexFormatChar4Normalized}},
      {{MTLVertexFormatUChar, MTLVertexFormatUChar2, MTLVertexFormatUChar3, MTLVertexFormatUChar4},
       {MTLVertexFormatUCharNormalized,
        MTLVertexFormatUChar2Normalized,
        MTLVertexFormatUChar3Normalized,
        MTLVertexFormatUChar4Normalized}},
      {{MTLVertexFormatShort, MTLVertexFormatShort2, MTLVertexFormatShort3, MTLVertexFormatShort4},
       {MTLVertexFormatShortNormalized,
        MTLVertexFormatShort2Normalized,
        MTLVertexFormatShort3Normalized,
        MTLVertexFormatShort4Normalized}},
      {{MTLVertexFormatUShort,
        MTLVertexFormatUShort2,
        MTLVertexFormatUShort3,
        MTLVertexFormatUShort4},
       {MTLVertexFormatUShortNormalized,
        MTLVertexFormatUShort2Normalized,
        MTLVertexFormatUShort3Normalized,
        MTLVertexFormatUShort4Normalized}},
      {{MTLVertexFormatInt, MTLVertexFormatInt2, MTLVertexFormatInt3, MTLVertexFormatInt4},
       {MTLVertexFormatInvalid,
        MTLVertexFormatInvalid,
        MTLVertexFormatInvalid,
        MTLVertexFormatInvalid}},
      {{MTLVertexFormatUInt, MTLVertexFormatUInt2, MTLVertexFormatUInt3, MTLVertexFormatUInt4},
       {MTLVertexFormatInvalid,
        MTLVertexFormatInvalid,
        MTLVertexFormatInvalid,
        MTLVertexFormatInvalid}},
      {{MTLVertexFormatFloat, MTLVertexFormatFloat2, MTLVertexFormatFloat3, MTLVertexFormatFloat4},
       {MTLVertexFormatInvalid,
        MTLVertexFormatInvalid,
        MTLVertexFormatInvalid,
        MTLVertexFormatInvalid}},
      {{MTLVertexFormatHalf, MTLVertexFormatHalf2, MTLVertexFormatHalf3, MTLVertexFormatHalf4},
       {MTLVertexFormatInvalid,
        MTLVertexFormatInvalid,
        MTLVertexFormatInvalid,
        MTLVertexFormatInvalid}},
      {{MTLVertexFormatInvalid,
        MTLVertexFormatInvalid,  // Double, Metal does not support.
        MTLVertexFormatInvalid,
        MTLVertexFormatInvalid},
       {MTLVertexFormatInvalid,
        MTLVertexFormatInvalid,
        MTLVertexFormatInvalid,
        MTLVertexFormatInvalid}}};
  assert(size <= 4u && size > 0u);
  return formats[type][normalized ? 1 : 0][size - 1u];
}

static MTLVertexStepFunction get_mtl_step_function(ngf_vertex_input_rate rate) {
  static const MTLVertexStepFunction funcs[NGF_VERTEX_INPUT_RATE_COUNT] = {
      MTLVertexStepFunctionPerVertex,
      MTLVertexStepFunctionPerInstance};
  return funcs[rate];
}

static MTLPrimitiveTopologyClass get_mtl_primitive_topology_class(ngf_primitive_topology t) {
  static const MTLPrimitiveTopologyClass topo_class[NGF_PRIMITIVE_TOPOLOGY_COUNT] = {
      MTLPrimitiveTopologyClassTriangle,
      MTLPrimitiveTopologyClassTriangle,
      MTLPrimitiveTopologyClassLine,
      MTLPrimitiveTopologyClassLine,
  };
  return topo_class[t];
}

static MTLPrimitiveType get_mtl_primitive_type(ngf_primitive_topology type) {
  static const MTLPrimitiveType types[NGF_PRIMITIVE_TOPOLOGY_COUNT] = {
      MTLPrimitiveTypeTriangle,
      MTLPrimitiveTypeTriangleStrip,
      MTLPrimitiveTypeLine,
      MTLPrimitiveTypeLineStrip};
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
      MTLCompareFunctionAlways};
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
      MTLStencilOperationInvert};
  return stencil_ops[op];
}

static MTLCullMode get_mtl_culling(ngf_cull_mode c) {
  static const MTLCullMode cull_modes[NGF_CULL_MODE_COUNT] = {
      MTLCullModeBack,
      MTLCullModeFront,
      MTLCullModeNone, /* Metal has no front + back culling */
      MTLCullModeNone};
  return cull_modes[c];
}

static MTLWinding get_mtl_winding(ngf_front_face_mode w) {
  static const MTLWinding windings[NGF_FRONT_FACE_COUNT] = {
      MTLWindingCounterClockwise,
      MTLWindingClockwise};
  return windings[w];
}

static const CFStringRef get_mtl_colorspace(ngf_colorspace colorspace) {
  const CFStringRef color_spaces[NGF_COLORSPACE_COUNT] = {
    kCGColorSpaceSRGB,
    kCGColorSpaceExtendedSRGB,
    kCGColorSpaceExtendedLinearSRGB,
    kCGColorSpaceDisplayP3,
    kCGColorSpaceExtendedLinearDisplayP3,
    kCGColorSpaceDCIP3,
    kCGColorSpaceExtendedLinearITUR_2020
  };
  return color_spaces[colorspace];
}

static std::optional<MTLTextureType>
get_mtl_texture_type(ngf_image_type type, uint32_t nlayers, ngf_sample_count sample_count) {
  if (type == NGF_IMAGE_TYPE_IMAGE_2D && nlayers == 1 && sample_count == NGF_SAMPLE_COUNT_1) {
    return MTLTextureType2D;
  } else if (type == NGF_IMAGE_TYPE_IMAGE_2D && nlayers > 1 && sample_count == NGF_SAMPLE_COUNT_1) {
    return MTLTextureType2DArray;
  }
  if (type == NGF_IMAGE_TYPE_IMAGE_2D && nlayers == 1 && sample_count != NGF_SAMPLE_COUNT_1) {
    return MTLTextureType2DMultisample;
  } else if (type == NGF_IMAGE_TYPE_IMAGE_2D && nlayers > 1 && sample_count != NGF_SAMPLE_COUNT_1) {
    if (@available(iOS 14.0, *)) return MTLTextureType2DMultisampleArray;
  } else if (type == NGF_IMAGE_TYPE_IMAGE_3D) {
    return MTLTextureType3D;
  } else if (type == NGF_IMAGE_TYPE_CUBE && nlayers == 1) {
    return MTLTextureTypeCube;
  } else if (type == NGF_IMAGE_TYPE_CUBE && nlayers > 1) {
    return MTLTextureTypeCubeArray;
  }
  return std::nullopt;
}

static std::optional<MTLSamplerAddressMode> get_mtl_address_mode(ngf_sampler_wrap_mode mode) {
  static const std::optional<MTLSamplerAddressMode> modes[NGF_WRAP_MODE_COUNT] = {
      MTLSamplerAddressModeClampToEdge,
      MTLSamplerAddressModeRepeat,
      MTLSamplerAddressModeMirrorRepeat};
  return modes[mode];
}

static MTLSamplerMinMagFilter get_mtl_minmag_filter(ngf_sampler_filter f) {
  static MTLSamplerMinMagFilter filters[NGF_FILTER_COUNT] = {
      MTLSamplerMinMagFilterNearest,
      MTLSamplerMinMagFilterLinear};
  return filters[f];
}

static MTLSamplerMipFilter get_mtl_mip_filter(ngf_sampler_filter f) {
  static MTLSamplerMipFilter filters[NGF_FILTER_COUNT] = {
      MTLSamplerMipFilterNearest,
      MTLSamplerMipFilterLinear};
  return filters[f];
}

static uint32_t ngfmtl_get_bytesperpel(const ngf_image_format format) {
  const mtl_format f = get_mtl_pixel_format(format);
  assert((f.block_width | f.block_height) == 1);  // invalid op for compressed formats
  return f.bits_per_block / 8;
}

static uint32_t ngfmtl_get_pitch(const uint32_t width, const ngf_image_format format) {
  const mtl_format f                    = get_mtl_pixel_format(format);
  const bool       is_compressed_format = (f.block_width | f.block_height) > 1;
  return is_compressed_format ? (width + f.block_width - 1) / f.block_width * f.bits_per_block / 8
                              : width * f.bits_per_block / 8;
}

static uint32_t ngfmtl_get_num_rows(const uint32_t height, const ngf_image_format format) {
  const mtl_format f                    = get_mtl_pixel_format(format);
  const bool       is_compressed_format = (f.block_width | f.block_height) > 1;
  return is_compressed_format ? (height + f.block_height - 1) / f.block_height : height;
}

#pragma mark ngf_struct_definitions

struct ngf_render_target_t {
  ngf_error initialize(
      const ngf_attachment_descriptions& attachment_descs,
      const ngf_image_ref*               img_refs,
      uint32_t                           rt_width,
      uint32_t                           rt_height) {
    width                             = rt_width;
    height                            = rt_height;
    ngf_attachment_description* attachment_descs_copy = NGFI_ALLOCN(ngf_attachment_description, attachment_descs.ndescs);
    this->attachment_descs.descs = attachment_descs_copy;
    if (this->attachment_descs.descs == NULL) {
      return NGF_ERROR_OUT_OF_MEM;
    }
    this->attachment_descs.ndescs = attachment_descs.ndescs;
    
    for (uint32_t i = 0; i < this->attachment_descs.ndescs; ++i) {
      if (attachment_descs.descs[i].is_resolve) {
        ++nresolve_attachments;
      } else {
        ++nrender_attachments;
      }
      attachment_descs_copy[i] = attachment_descs.descs[i];
    }

    if (img_refs) {
      render_image_refs = NGFI_ALLOCN(ngf_image_ref, nrender_attachments);

      if (nresolve_attachments > 0u) {
        resolve_image_refs = NGFI_ALLOCN(ngf_image_ref, nresolve_attachments);
      }

      uint32_t image_ref_idx         = 0u;
      uint32_t resolve_image_ref_idx = 0u;
      for (uint32_t i = 0; i < this->attachment_descs.ndescs; ++i) {
        if (!this->attachment_descs.descs[i].is_resolve) {
          render_image_refs[image_ref_idx++] = img_refs[i];
        } else if (nresolve_attachments > 0u) {
          resolve_image_refs[resolve_image_ref_idx++] = img_refs[i];
        } else {
          assert(0);
        }
      }
    }

    return NGF_ERROR_OK;
  }

  ~ngf_render_target_t() {
    if (nresolve_attachments > 0u) { NGFI_FREEN(resolve_image_refs, nresolve_attachments); }

    if (attachment_descs.descs) {
      NGFI_FREEN(render_image_refs, nrender_attachments);
      NGFI_FREEN(attachment_descs.descs, attachment_descs.ndescs);
    }
  }

  ngf_attachment_descriptions attachment_descs;
  ngf_image_ref*              render_image_refs    = nullptr;
  ngf_image_ref*              resolve_image_refs   = nullptr;
  uint32_t                    nrender_attachments  = 0u;
  uint32_t                    nresolve_attachments = 0u;
  bool                        is_default           = false;
  NSUInteger                  width;
  NSUInteger                  height;
};

struct ngf_cmd_buffer_t {
  ngfi_cmd_buffer_state        state               = NGFI_CMD_BUFFER_NEW;
  bool                         renderpass_active   = false;
  id<MTLCommandBuffer>         mtl_cmd_buffer      = nil;
  id<MTLRenderCommandEncoder>  active_rce          = nil;
  id<MTLBlitCommandEncoder>    active_bce          = nil;
  id<MTLComputeCommandEncoder> active_cce          = nil;
  ngf_graphics_pipeline        active_gfx_pipe     = nullptr;
  ngf_compute_pipeline         active_compute_pipe = nullptr;
  ngf_render_target            active_rt           = nullptr;
  id<MTLBuffer>                bound_index_buffer  = nil;
  MTLIndexType                 bound_index_buffer_type;
  size_t                       bound_index_buffer_offset = 0u;
};
#define NGFMTL_ENC2CMDBUF(enc) ((ngf_cmd_buffer)((void*)enc.pvt_data_donotuse.d0))

struct ngfmtl_niceshade_metadata {
  std::vector<std::vector<uint32_t>> native_binding_map;
  uint32_t                           threadgroup_size[3];
};

struct ngf_shader_stage_t {
  id<MTLLibrary> func_lib = nil;
  ngf_stage_type type;
  std::string    entry_point_name;
  std::string    source_code;
};

struct ngf_graphics_pipeline_t {
  id<MTLRenderPipelineState> pipeline           = nil;
  id<MTLDepthStencilState>   depth_stencil      = nil;
  MTLDepthStencilDescriptor* depth_stencil_desc = nil;

  uint32_t front_stencil_reference = 0u;
  uint32_t back_stencil_reference  = 0u;

  MTLPrimitiveType primitive_type = MTLPrimitiveTypeTriangle;
  MTLWinding       winding        = MTLWindingCounterClockwise;
  MTLCullMode      culling        = MTLCullModeBack;
  float            blend_color[4] {0};

  ngfmtl_niceshade_metadata niceshade_metadata;
};

struct ngf_compute_pipeline_t {
  id<MTLComputePipelineState> pipeline = nil;
  ngfmtl_niceshade_metadata   niceshade_metadata;
};

struct ngf_buffer_t {
  id<MTLBuffer> mtl_buffer    = nil;
  size_t        mapped_offset = 0;
};

struct ngf_texel_buffer_view_t {
  id<MTLTexture> mtl_buffer_view = nil;
};

struct ngf_sampler_t {
  id<MTLSamplerState> sampler = nil;
};

struct ngf_image_t {
  id<MTLTexture>   texture = nil;
  ngf_image_format format;
  uint32_t         usage_flags = 0u;
};

template<class NgfObjType, void (*Dtor)(NgfObjType*)> class ngfmtl_object_nursery {
  public:
  template<class... Args>
  explicit ngfmtl_object_nursery(NgfObjType* memory, Args&&... a) : ptr_(memory) {
    new (memory) NgfObjType(std::forward<Args>(a)...);
  }
  ~ngfmtl_object_nursery() {
    if (ptr_ != nullptr) { Dtor(ptr_); }
  }
  NgfObjType* operator->() {
    return ptr_;
  }
  NgfObjType* release() {
    NgfObjType* tmp = ptr_;
    ptr_            = nullptr;
    return tmp;
  }
  operator bool() {
    return ptr_ != nullptr;
  }

  private:
  NgfObjType* ptr_;
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

    id<MTLTexture> depth_attachment_texture() {
      return depth_texture;
    }

    id<CAMetalDrawable> color_drawable      = nil;
    id<MTLTexture>      depth_texture       = nil;
    id<MTLTexture>      multisample_texture = nil;
  };

  ngfmtl_swapchain() = default;
  ngfmtl_swapchain(ngfmtl_swapchain&& other) {
    *this = std::move(other);
  }
  ngfmtl_swapchain& operator=(ngfmtl_swapchain&& other) {
    layer_        = other.layer_;
    other.layer_  = nil;
    depth_images_ = std::move(other.depth_images_);
    capacity_     = other.capacity_;
    img_idx_      = other.img_idx_;

    return *this;
  }

  // Delete copy ctor and copy assignment to make this type move-only.
  ngfmtl_swapchain& operator=(const ngfmtl_swapchain&) = delete;
  ngfmtl_swapchain(const ngfmtl_swapchain&)            = delete;

  ngf_error initialize(const ngf_swapchain_info& swapchain_info, id<MTLDevice> device) {
    // Initialize the Metal layer.
    const MTLPixelFormat pixel_format = get_mtl_pixel_format(swapchain_info.color_format).format;
    if (pixel_format == MTLPixelFormatInvalid) {
      NGFI_DIAG_ERROR("Image format not supported by Metal backend");
      return NGF_ERROR_INVALID_FORMAT;
    }
    layer_                 = [CAMetalLayer layer];
    layer_.device          = device;
    layer_.drawableSize    = CGSizeMake(swapchain_info.width, swapchain_info.height);
    layer_.pixelFormat     = pixel_format;
    layer_.colorspace      = CGColorSpaceCreateWithName(get_mtl_colorspace(swapchain_info.colorspace));
    layer_.framebufferOnly = YES;
#if TARGET_OS_OSX
    if (@available(macOS 10.13.2, *)) {
      layer_.maximumDrawableCount = swapchain_info.capacity_hint;
    }
    if (@available(macOS 10.13, *)) {
      layer_.displaySyncEnabled = (swapchain_info.present_mode == NGF_PRESENTATION_MODE_FIFO);
    }
#endif

    // Associate the newly created Metal layer with the user-provided View.
    NGFMTL_VIEW_TYPE* view = CFBridgingRelease((void*)swapchain_info.native_handle);
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

  ngf_error resize(const ngf_swapchain_info& swapchain_info) {
    layer_.drawableSize = CGSizeMake(swapchain_info.width, swapchain_info.height);

    NGFMTL_VIEW_TYPE* view = CFBridgingRelease((void*)swapchain_info.native_handle);

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
    return {
        [layer_ nextDrawable],
        depth_images_.get() ? depth_images_[img_idx_] : nil,
        is_multisampled() ? multisample_images_[img_idx_]->texture : nil};
  }

  operator bool() {
    return layer_;
  }

  bool is_multisampled() const {
    return multisample_images_.get();
  }

  private:
  void initialize_depth_attachments(const ngf_swapchain_info& swapchain_info) {
    if (swapchain_info.depth_format != NGF_IMAGE_FORMAT_UNDEFINED) {
      depth_images_.reset(new id<MTLTexture>[swapchain_info.capacity_hint]);
      MTLPixelFormat depth_format = get_mtl_pixel_format(swapchain_info.depth_format).format;
      assert(depth_format != MTLPixelFormatInvalid);
      for (uint32_t i = 0u; i < swapchain_info.capacity_hint; ++i) {
        auto* depth_texture_desc = [MTLTextureDescriptor new];
        depth_texture_desc.textureType =
            swapchain_info.sample_count > 1u ? MTLTextureType2DMultisample : MTLTextureType2D;
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
        if (@available(macOS 10.14, *)) { depth_texture_desc.allowGPUOptimizedContents = true; }
        depth_images_[i] = [MTL_DEVICE newTextureWithDescriptor:depth_texture_desc];
      }
    } else {
      depth_images_.reset(nullptr);
    }
  }

  void initialize_multisample_images(const ngf_swapchain_info& swapchain_info) {
    destroy_multisample_images();
    if (swapchain_info.sample_count > NGF_SAMPLE_COUNT_1) {
      multisample_images_.reset(new ngf_image[capacity_]);
      for (size_t i = 0; i < capacity_; ++i) {
        const ngf_image_info info = {
            .type   = NGF_IMAGE_TYPE_IMAGE_2D,
            .extent = {.width = swapchain_info.width, .height = swapchain_info.height, .depth = 1u},
            .nmips  = 1u,
            .nlayers      = 1u,
            .format       = swapchain_info.color_format,
            .sample_count = (ngf_sample_count)swapchain_info.sample_count,
            .usage_hint   = NGF_IMAGE_USAGE_ATTACHMENT};
        ngf_create_image(&info, &multisample_images_[i]);
      }
    }
  }

  void destroy_multisample_images() {
    for (size_t i = 0; i < capacity_ && multisample_images_; ++i) {
      ngf_destroy_image(multisample_images_[i]);
    }
    multisample_images_.reset(nullptr);
  }

  CAMetalLayer*                     layer_    = nil;
  uint32_t                          img_idx_  = 0u;
  uint32_t                          capacity_ = 0u;
  std::unique_ptr<id<MTLTexture>[]> depth_images_;
  std::unique_ptr<ngf_image[]>      multisample_images_;
};

struct ngf_context_t {
  ~ngf_context_t() {
    if (last_cmd_buffer) [last_cmd_buffer waitUntilCompleted];
  }
  id<MTLDevice>           device = nil;
  ngfmtl_swapchain        swapchain;
  ngfmtl_swapchain::frame frame;
  id<MTLCommandQueue>     queue      = nil;
  bool                    is_current = false;
  ngf_swapchain_info      swapchain_info;
  id<MTLCommandBuffer>    pending_cmd_buffer = nil;
  id<MTLCommandBuffer>    last_cmd_buffer    = nil;
  dispatch_semaphore_t    frame_sync_sem     = nil;
  ngf_render_target       default_rt;
};

constexpr MTLGPUFamily NGFMTL_GPU_FAMILIES[] = {
    MTLGPUFamilyCommon1,
    MTLGPUFamilyCommon2,
    MTLGPUFamilyApple1,
    MTLGPUFamilyApple2,
    MTLGPUFamilyApple3,
    MTLGPUFamilyApple4,
    MTLGPUFamilyApple5,
    MTLGPUFamilyApple6,
    MTLGPUFamilyApple7,
    MTLGPUFamilyMac2,
    MTLGPUFamilyMac2,
    MTLGPUFamilyMac2,
    MTLGPUFamilyMac2,
};

constexpr size_t NGFMTL_NUM_GPU_FAMILIES = sizeof(NGFMTL_GPU_FAMILIES) / sizeof(MTLGPUFamily);

static constexpr size_t ngfmtl_gpufam_idx(MTLGPUFamily fam) {
  for (size_t i = 0; i < NGFMTL_NUM_GPU_FAMILIES; ++i)
    if (NGFMTL_GPU_FAMILIES[i] == fam) return i;
  return 0;
}

static size_t ngfmtl_max_supported_gpu_family(id<MTLDevice> mtldev) {
  for (size_t fam_idx = NGFMTL_NUM_GPU_FAMILIES - 1; fam_idx >= 0; --fam_idx)
    if ([mtldev supportsFamily:NGFMTL_GPU_FAMILIES[fam_idx]]) return fam_idx;
  return 0;
}

extern "C" {
void             ngfi_set_allocation_callbacks(const ngf_allocation_callbacks* callbacks);
ngf_sample_count ngfi_get_highest_sample_count(size_t counts_bitmap);
}

static void ngfmtl_populate_ngf_device(uint32_t handle, ngf_device& ngfdev, id<MTLDevice> mtldev) {
  ngfdev.handle = handle;
#if TARGET_OS_OSX
  ngfdev.performance_tier =
      mtldev.lowPower ? NGF_DEVICE_PERFORMANCE_TIER_LOW : NGF_DEVICE_PERFORMANCE_TIER_HIGH;
#else
  ngfdev.performance_tier = NGF_DEVICE_PERFORMANCE_TIER_UNKNOWN;
#endif
  const size_t device_name_length = [mtldev.name dataUsingEncoding:NSUTF8StringEncoding].length;
  strncpy(
      ngfdev.name,
      [mtldev.name UTF8String],
      NGFI_MIN(NGF_DEVICE_NAME_MAX_LENGTH, device_name_length));
  ngf_device_capabilities& caps                 = ngfdev.capabilities;
  const size_t             gpu_family_idx       = ngfmtl_max_supported_gpu_family(mtldev);
  caps.clipspace_z_zero_to_one                  = true;
  caps.max_vertex_input_attributes_per_pipeline = 31;
  caps.max_uniform_buffers_per_stage            = 31;
  caps.max_sampler_anisotropy                   = 16.0f;
  caps.max_samplers_per_stage                   = 16;
  caps.max_3d_image_dimension                   = 2048;
  caps.max_image_layers                         = 2048;
  caps.max_uniform_buffer_range                 = NGF_DEVICE_LIMIT_UNKNOWN;

  if (gpu_family_idx >= ngfmtl_gpufam_idx(MTLGPUFamilyApple6)) {
    caps.max_sampled_images_per_stage = 128;
  } else if (gpu_family_idx >= ngfmtl_gpufam_idx(MTLGPUFamilyApple4)) {
    caps.max_sampled_images_per_stage = 96;
  } else {
    caps.max_sampled_images_per_stage = 31;
  }

  if (gpu_family_idx >= ngfmtl_gpufam_idx(MTLGPUFamilyApple4)) {
    caps.max_fragment_input_components = 124;
  } else {
    caps.max_fragment_input_components = 60;
  }
  if (gpu_family_idx >= ngfmtl_gpufam_idx(MTLGPUFamilyApple4)) {
    caps.max_fragment_inputs = 124;
  } else {
    caps.max_fragment_inputs = 60;
  }

  if (gpu_family_idx >= ngfmtl_gpufam_idx(MTLGPUFamilyMac2)) {
    caps.uniform_buffer_offset_alignment = 256;
    caps.texel_buffer_offset_alignment   = 256;
  } else {
    caps.uniform_buffer_offset_alignment = 4;
    caps.texel_buffer_offset_alignment   = 4;
  }

  if (gpu_family_idx >= ngfmtl_gpufam_idx(MTLGPUFamilyApple3)) {
    caps.max_1d_image_dimension   = 16384;
    caps.max_2d_image_dimension   = 16384;
    caps.max_cube_image_dimension = 16384;
  } else {
    caps.max_1d_image_dimension   = 8192;
    caps.max_2d_image_dimension   = 8192;
    caps.max_cube_image_dimension = 8192;
  }

  if (gpu_family_idx >= ngfmtl_gpufam_idx(MTLGPUFamilyApple2)) {
    caps.max_color_attachments_per_pass = 8;
  } else {
    caps.max_color_attachments_per_pass = 4;
  }

  caps.cubemap_arrays_supported = gpu_family_idx == ngfmtl_gpufam_idx(MTLGPUFamilyCommon2) ||
                                  gpu_family_idx == ngfmtl_gpufam_idx(MTLGPUFamilyCommon3) ||
                                  gpu_family_idx >= ngfmtl_gpufam_idx(MTLGPUFamilyApple3);

  size_t supports_samples_bitmap = ([mtldev supportsTextureSampleCount:1] ? 1 : 0) |
                                   ([mtldev supportsTextureSampleCount:2] ? 2 : 0) |
                                   ([mtldev supportsTextureSampleCount:4] ? 4 : 0) |
                                   ([mtldev supportsTextureSampleCount:8] ? 8 : 0);

  ngf_sample_count max_supported_sample_count =
      ngfi_get_highest_sample_count(supports_samples_bitmap);

  caps.texture_color_sample_counts                  = supports_samples_bitmap;
  caps.max_supported_texture_color_sample_count     = max_supported_sample_count;
  caps.texture_depth_sample_counts                  = supports_samples_bitmap;
  caps.max_supported_texture_depth_sample_count     = max_supported_sample_count;
  caps.framebuffer_color_sample_counts              = supports_samples_bitmap;
  caps.max_supported_framebuffer_color_sample_count = max_supported_sample_count;
  caps.framebuffer_depth_sample_counts              = supports_samples_bitmap;
  caps.max_supported_framebuffer_depth_sample_count = max_supported_sample_count;
}

NGFI_THREADLOCAL ngf_context CURRENT_CONTEXT = nullptr;

std::vector<ngf_device>       NGFMTL_DEVICES_LIST;
const NSArray<id<MTLDevice>>* NGFMTL_MTL_DEVICES;

#define NGFMTL_NURSERY(type, name, ...)                           \
  ngfmtl_object_nursery<ngf_##type##_t, ngf_destroy_##type> name( \
      NGFI_ALLOC(ngf_##type##_t),                                 \
      ##__VA_ARGS__);

#pragma mark ngf_function_implementations

ngf_error ngf_get_device_list(const ngf_device** devices, uint32_t* ndevices) {
  if (NGFMTL_DEVICES_LIST.empty()) {
#if TARGET_OS_OSX
    NGFMTL_MTL_DEVICES = MTLCopyAllDevices();
    NGFMTL_DEVICES_LIST.resize(NGFMTL_MTL_DEVICES.count);
    for (uint32_t d = 0u; d < NGFMTL_MTL_DEVICES.count; ++d) {
      ngfmtl_populate_ngf_device(d, NGFMTL_DEVICES_LIST[d], NGFMTL_MTL_DEVICES[d]);
    }
#else
    NGFMTL_MTL_DEVICES = [[NSArray alloc] initWithObjects:MTLCreateSystemDefaultDevice(), nil];
    NGFMTL_DEVICES_LIST.resize(1);
    ngfmtl_populate_ngf_device(0, NGFMTL_DEVICES_LIST[0], NGFMTL_MTL_DEVICES[0]);
#endif
  }
  if (devices) { *devices = NGFMTL_DEVICES_LIST.data(); }
  if (ndevices) { *ndevices = (uint32_t)NGFMTL_DEVICES_LIST.size(); }
  return NGF_ERROR_OK;
}

ngf_error ngf_initialize(const ngf_init_info* init_info) NGF_NOEXCEPT {
  if (MTL_DEVICE != nil || init_info->device >= NGFMTL_DEVICES_LIST.size()) {
    return NGF_ERROR_INVALID_OPERATION;
  }
  if (init_info->diag_info != NULL) {
    ngfi_diag_info = *init_info->diag_info;
  } else {
    ngfi_diag_info.callback  = NULL;
    ngfi_diag_info.userdata  = NULL;
    ngfi_diag_info.verbosity = NGF_DIAGNOSTICS_VERBOSITY_DEFAULT;
  }
  ngfi_set_allocation_callbacks(init_info->allocation_callbacks);

  MTL_DEVICE = NGFMTL_MTL_DEVICES[init_info->device];

  // Initialize device capabilities.
  DEVICE_CAPS = NGFMTL_DEVICES_LIST[init_info->device].capabilities;

  return (MTL_DEVICE != nil) ? NGF_ERROR_OK : NGF_ERROR_INVALID_OPERATION;
}

const ngf_device_capabilities* ngf_get_device_capabilities() NGF_NOEXCEPT {
  return &DEVICE_CAPS;
}

extern "C" {
void* NSPushAutoreleasePool(NSUInteger capacity);
void  NSPopAutoreleasePool(void* token);
}

ngf_error ngf_begin_frame(ngf_frame_token* token) NGF_NOEXCEPT {
  *token = (uintptr_t)NSPushAutoreleasePool(0);
  dispatch_semaphore_wait(CURRENT_CONTEXT->frame_sync_sem, DISPATCH_TIME_FOREVER);
  CURRENT_CONTEXT->frame = CURRENT_CONTEXT->swapchain.next_frame();
  return (!CURRENT_CONTEXT->frame.color_drawable) ? NGF_ERROR_INVALID_OPERATION : NGF_ERROR_OK;
}

ngf_error ngf_end_frame(ngf_frame_token token) NGF_NOEXCEPT {
  ngf_context ctx = CURRENT_CONTEXT;
  if (CURRENT_CONTEXT->frame.color_drawable && CURRENT_CONTEXT->pending_cmd_buffer) {
    [CURRENT_CONTEXT->pending_cmd_buffer addCompletedHandler:^(id<MTLCommandBuffer> _Nonnull) {
      dispatch_semaphore_signal(ctx->frame_sync_sem);
    }];
    [CURRENT_CONTEXT->pending_cmd_buffer presentDrawable:CURRENT_CONTEXT->frame.color_drawable];
    [CURRENT_CONTEXT->pending_cmd_buffer commit];
    CURRENT_CONTEXT->frame              = ngfmtl_swapchain::frame {};
    CURRENT_CONTEXT->last_cmd_buffer    = CURRENT_CONTEXT->pending_cmd_buffer;
    CURRENT_CONTEXT->pending_cmd_buffer = nil;
  } else {
    dispatch_semaphore_signal(ctx->frame_sync_sem);
  }
  NSPopAutoreleasePool((void*)token);
  return NGF_ERROR_OK;
}

void ngf_shutdown() NGF_NOEXCEPT {
  NGFI_DIAG_INFO("Shutting down nicegraf.");
}

ngf_render_target ngf_default_render_target() NGF_NOEXCEPT {
  return CURRENT_CONTEXT->default_rt;
}

const ngf_attachment_descriptions* ngf_default_render_target_attachment_descs() NGF_NOEXCEPT {
  return &CURRENT_CONTEXT->default_rt->attachment_descs;
}

ngf_error ngf_create_context(const ngf_context_info* info, ngf_context* result) NGF_NOEXCEPT {
  assert(info);
  assert(result);
  NGFMTL_NURSERY(context, ctx);
  if (!ctx) { return NGF_ERROR_OUT_OF_MEM; }

  ctx->device = MTL_DEVICE;
  if (info->shared_context != nullptr) {
    ctx->queue = info->shared_context->queue;
  } else {
    ctx->queue = [ctx->device newCommandQueue];
  }

  if (info->swapchain_info) {
    ctx->swapchain_info = *(info->swapchain_info);
    ngf_error err       = ctx->swapchain.initialize(ctx->swapchain_info, ctx->device);
    if (err != NGF_ERROR_OK) return err;
    ngf_attachment_descriptions attachment_descs;
    ngf_attachment_description  desc_array[3];
    attachment_descs.descs     = desc_array;
    attachment_descs.ndescs    = 1;
    desc_array[0].format       = ctx->swapchain_info.color_format;
    desc_array[0].is_sampled   = false;
    desc_array[0].type         = NGF_ATTACHMENT_COLOR;
    desc_array[0].sample_count = ctx->swapchain_info.sample_count;
    desc_array[0].is_resolve   = false;
    if (ctx->swapchain_info.depth_format != NGF_IMAGE_FORMAT_UNDEFINED) {
      attachment_descs.ndescs++;
      desc_array[1].format     = ctx->swapchain_info.depth_format;
      desc_array[1].is_sampled = false;
      desc_array[1].type = ctx->swapchain_info.depth_format == NGF_IMAGE_FORMAT_DEPTH24_STENCIL8
                               ? NGF_ATTACHMENT_DEPTH_STENCIL
                               : NGF_ATTACHMENT_DEPTH;
      desc_array[1].sample_count = ctx->swapchain_info.sample_count;
      desc_array[1].is_resolve   = false;
    }

    NGFMTL_NURSERY(render_target, default_rt);
    if (!default_rt) { return NGF_ERROR_OUT_OF_MEM; }

    err = default_rt->initialize(
        attachment_descs,
        nullptr,
        info->swapchain_info->width,
        info->swapchain_info->height);
    if (err != NGF_ERROR_OK) { return err; }

    ctx->default_rt             = default_rt.release();
    ctx->default_rt->is_default = true;
  }

  ctx->frame_sync_sem = dispatch_semaphore_create(ctx->swapchain_info.capacity_hint);
  *result             = ctx.release();

  return NGF_ERROR_OK;
}

void ngf_destroy_context(ngf_context ctx) NGF_NOEXCEPT {
  // TODO: unset current context
  assert(ctx);
  ctx->~ngf_context_t();
  NGFI_FREE(ctx);
}

ngf_error
ngf_resize_context(ngf_context ctx, uint32_t new_width, uint32_t new_height) NGF_NOEXCEPT {
  assert(ctx);
  ctx->swapchain_info.width  = new_width;
  ctx->swapchain_info.height = new_height;
  ctx->default_rt->width     = new_width;
  ctx->default_rt->height    = new_height;
  return ctx->swapchain.resize(ctx->swapchain_info);
}

ngf_error ngf_set_context(ngf_context ctx) NGF_NOEXCEPT {
  if (CURRENT_CONTEXT == ctx) {
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

ngf_error
ngf_create_shader_stage(const ngf_shader_stage_info* info, ngf_shader_stage* result) NGF_NOEXCEPT {
  assert(info);
  assert(result);

  NGFMTL_NURSERY(shader_stage, stage);
  if (!stage) { return NGF_ERROR_OUT_OF_MEM; }

  stage->type        = info->type;
  stage->source_code = std::string {(const char*)info->content, info->content_length};

  // Create a MTLLibrary for this stage.
  NSString*          source = [[NSString alloc] initWithBytes:info->content
                                              length:info->content_length
                                            encoding:NSUTF8StringEncoding];
  MTLCompileOptions* opts   = [MTLCompileOptions new];
  NSError*           err    = nil;
  stage->func_lib = [CURRENT_CONTEXT->device newLibraryWithSource:source options:opts error:&err];
  if (!stage->func_lib) {
    NGFI_DIAG_ERROR([err.localizedDescription UTF8String]);
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }

  // Set debug name.
  if (info->debug_name != nullptr) {
    stage->func_lib.label = [[NSString alloc] initWithUTF8String:info->debug_name];
  }

  if (info->entry_point_name) {
    stage->entry_point_name = info->entry_point_name;
  } else {
    std::string tmp         = [[stage->func_lib functionNames].firstObject UTF8String];
    stage->entry_point_name = tmp;
  }

  *result = stage.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_shader_stage(ngf_shader_stage stage) NGF_NOEXCEPT {
  if (stage != nullptr) {
    stage->~ngf_shader_stage_t();
    NGFI_FREE(stage);
  }
}

void ngfmtl_attachment_set_common(
    MTLRenderPassAttachmentDescriptor* attachment,
    uint32_t                           render_image_index,
    ngf_attachment_type                type,
    const ngf_render_target            rt,
    ngf_attachment_load_op             load_op,
    ngf_attachment_store_op            store_op) NGF_NOEXCEPT {
  if (!rt->is_default) {
    attachment.texture = rt->render_image_refs[render_image_index].image->texture;
    attachment.level   = rt->render_image_refs[render_image_index].mip_level;
    attachment.slice   = rt->render_image_refs[render_image_index].layer;
  } else {
    attachment.texture = type == NGF_ATTACHMENT_COLOR
                             ? CURRENT_CONTEXT->frame.color_attachment_texture()
                             : CURRENT_CONTEXT->frame.depth_attachment_texture();
    attachment.level   = 0;
    attachment.slice   = 0;
  }
  attachment.loadAction  = get_mtl_load_action(load_op);
  attachment.storeAction = get_mtl_store_action(store_op);
}

ngf_error ngf_create_render_target(const ngf_render_target_info* info, ngf_render_target* result)
    NGF_NOEXCEPT {
  assert(info);
  assert(result);

  NGFMTL_NURSERY(render_target, rt);
  if (!rt) { return NGF_ERROR_OUT_OF_MEM; }

  const ngf_error err = rt->initialize(
      *info->attachment_descriptions,
      info->attachment_image_refs,
      (uint32_t)info->attachment_image_refs[0].image->texture.width,
      (uint32_t)info->attachment_image_refs[0].image->texture.height);

  if (err != NGF_ERROR_OK) return err;

  *result = rt.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_render_target(ngf_render_target rt) NGF_NOEXCEPT {
  if (rt != nullptr) {
    rt->~ngf_render_target_t();
    NGFI_FREE(rt);
  }
}

id<MTLFunction> ngfmtl_get_shader_main(
    id<MTLLibrary>             func_lib,
    const char*                entry_point_name,
    MTLFunctionConstantValues* spec_consts) {
  NSError*        err                 = nil;
  NSString*       ns_entry_point_name = [NSString stringWithUTF8String:entry_point_name];
  id<MTLFunction> result              = [func_lib newFunctionWithName:ns_entry_point_name
                                          constantValues:spec_consts
                                                   error:&err];
  if (err) {
    NGFI_DIAG_ERROR([err.localizedDescription UTF8String]);
    return nil;
  } else {
    return result;
  }
}

MTLStencilDescriptor* ngfmtl_create_stencil_descriptor(const ngf_stencil_info& info) {
  auto* result                     = [MTLStencilDescriptor new];
  result.stencilCompareFunction    = get_mtl_compare_function(info.compare_op);
  result.stencilFailureOperation   = get_mtl_stencil_op(info.fail_op);
  result.depthStencilPassOperation = get_mtl_stencil_op(info.pass_op);
  result.depthFailureOperation     = get_mtl_stencil_op(info.depth_fail_op);
  result.writeMask                 = info.write_mask;
  result.readMask                  = info.compare_mask;
  return result;
}

ngf_error ngfmtl_parse_niceshade_metadata(
    const char*                input,
    bool                       need_threadgroup_size,
    ngfmtl_niceshade_metadata* output) {
  static const char binding_map_tag[]           = "NGF_NATIVE_BINDING_MAP";
  static const char threadgroup_size_tag[]      = "NGF_THREADGROUP_SIZE";
  const char*       serialized_binding_map      = NULL;
  const char*       serialized_threadgroup_size = NULL;
  bool              in_comment                  = false;

  for (; *input != '\0' && (serialized_binding_map == NULL ||
                            (!need_threadgroup_size || serialized_threadgroup_size == NULL));
       ++input) {
    if (!in_comment && *input == '/' && *(input + 1) == '*') {
      in_comment = true;
      input++;
      continue;
    }
    if (in_comment && *input == '*' && *(input + 1) == '/') {
      in_comment = false;
      input++;
      continue;
    }
    if (!in_comment) continue;

    if (serialized_binding_map == NULL &&
        strncmp(input, binding_map_tag, sizeof(binding_map_tag) - 1) == 0) {
      serialized_binding_map = input + sizeof(binding_map_tag) - 1;
    }
    if (need_threadgroup_size && serialized_threadgroup_size == NULL &&
        strncmp(input, threadgroup_size_tag, sizeof(threadgroup_size_tag) - 1) == 0) {
      serialized_threadgroup_size = input + sizeof(threadgroup_size_tag) - 1;
    }
  }
  if (!serialized_binding_map) {
    NGFI_DIAG_ERROR("Failed to find a serialized binding map");
    return NGF_ERROR_INVALID_OPERATION;
  }
  if (need_threadgroup_size && !serialized_threadgroup_size) {
    NGFI_DIAG_ERROR("Failed to find a serialized threadgroup size");
    return NGF_ERROR_INVALID_OPERATION;
  }

  // Parse the native binding map.
  struct ngfmtl_binding_map_entry {
    uint32_t set;
    uint32_t binding;
    uint32_t native_binding;
  };
  ngfi_sa_reset(ngfi_tmp_store());
  auto tmp_binding_map_entries =
      (ngfmtl_binding_map_entry*)ngfi_sa_alloc(ngfi_tmp_store(), sizeof(ngfmtl_binding_map_entry));
  ngfmtl_binding_map_entry* current_binding_map_entry = tmp_binding_map_entries;
  uint32_t                  num_entries               = 0u;
  uint32_t                  consumed_input_bytes;
  uint32_t                  max_set     = 0u;
  uint32_t                  max_binding = 0u;
  while (sscanf(
             serialized_binding_map,
             " ( %d %d ) : %d%n",
             &current_binding_map_entry->set,
             &current_binding_map_entry->binding,
             &current_binding_map_entry->native_binding,
             &consumed_input_bytes) == 3 &&
         current_binding_map_entry->set != -1 && current_binding_map_entry->binding != -1 &&
         current_binding_map_entry->native_binding != -1) {
    serialized_binding_map += consumed_input_bytes;
    max_set                   = std::max(max_set, current_binding_map_entry->set);
    max_binding               = std::max(max_binding, current_binding_map_entry->binding);
    current_binding_map_entry = (ngfmtl_binding_map_entry*)ngfi_sa_alloc(
        ngfi_tmp_store(),
        sizeof(ngfmtl_binding_map_entry));
    ++num_entries;
  }

  std::vector<std::vector<uint32_t>> native_binding_map(
      max_set + 1,
      std::vector<uint32_t>(max_binding + 1, ~0u));
  for (uint32_t e = 0u; e < num_entries; ++e) {
    native_binding_map[tmp_binding_map_entries[e].set][tmp_binding_map_entries[e].binding] =
        tmp_binding_map_entries[e].native_binding;
  }
  output->native_binding_map = std::move(native_binding_map);

  if (need_threadgroup_size && serialized_threadgroup_size) {
    if (sscanf(
            serialized_threadgroup_size,
            "%d %d %d",
            &output->threadgroup_size[0],
            &output->threadgroup_size[1],
            &output->threadgroup_size[2]) != 3) {
      NGFI_DIAG_ERROR("Failed to parse threadgroup size");
      return NGF_ERROR_INVALID_OPERATION;
    }
  }

  return NGF_ERROR_OK;
}

MTLFunctionConstantValues* ngfmtl_function_consts(const ngf_specialization_info* spec_info) {
  // Populate specialization constant values.
  MTLFunctionConstantValues* spec_consts = [MTLFunctionConstantValues new];
  if (spec_info != nullptr) {
    for (uint32_t s = 0u; s < spec_info->nspecializations; ++s) {
      const ngf_constant_specialization* spec = &spec_info->specializations[s];
      MTLDataType                        type = get_mtl_type(spec->type);
      if (type == MTLDataTypeNone) { return nil; }
      void* write_ptr = ((uint8_t*)spec_info->value_buffer + spec->offset);
      [spec_consts setConstantValue:write_ptr type:type atIndex:spec->constant_id];
    }
  }
  return spec_consts;
}

ngf_error ngf_create_compute_pipeline(
    const ngf_compute_pipeline_info* info,
    ngf_compute_pipeline*            result) NGF_NOEXCEPT {
  assert(info);
  assert(result);

  ngfmtl_niceshade_metadata metadata;
  const ngf_error           metadata_parse_error =
      ngfmtl_parse_niceshade_metadata(info->shader_stage->source_code.c_str(), true, &metadata);
  if (metadata_parse_error != NGF_ERROR_OK) return metadata_parse_error;

  MTLFunctionConstantValues* func_const_values = ngfmtl_function_consts(info->spec_info);
  id<MTLFunction>            function          = ngfmtl_get_shader_main(
      info->shader_stage->func_lib,
      info->shader_stage->entry_point_name.c_str(),
      func_const_values);
  if (!function) { return NGF_ERROR_OBJECT_CREATION_FAILED; }
  NSError*                    err = nil;
  id<MTLComputePipelineState> computePSO =
      [CURRENT_CONTEXT->device newComputePipelineStateWithFunction:function error:&err];
  if (err) {
    NGFI_DIAG_ERROR([err.localizedDescription UTF8String]);
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }
  NGFMTL_NURSERY(compute_pipeline, compute_pipeline);
  compute_pipeline->pipeline           = computePSO;
  compute_pipeline->niceshade_metadata = std::move(metadata);
  *result                              = compute_pipeline.release();
  return NGF_ERROR_OK;
}

ngf_error ngf_create_graphics_pipeline(
    const ngf_graphics_pipeline_info* info,
    ngf_graphics_pipeline*            result) NGF_NOEXCEPT {
  assert(info);
  assert(result);

  auto*                              mtl_pipe_desc      = [MTLRenderPipelineDescriptor new];
  const ngf_attachment_descriptions& attachment_descs   = *info->compatible_rt_attachment_descs;
  uint32_t                           ncolor_attachments = 0u;
  for (uint32_t i = 0u; i < attachment_descs.ndescs; ++i) {
    const ngf_attachment_description& attachment_desc = attachment_descs.descs[i];

    if (attachment_desc.is_resolve) continue;

    if (attachment_desc.type == NGF_ATTACHMENT_COLOR) {
      const ngf_blend_info                        blend = info->color_attachment_blend_states
                                                              ? info->color_attachment_blend_states[ncolor_attachments]
                                                              : ngf_blend_info {};
      MTLRenderPipelineColorAttachmentDescriptor* mtl_attachment_desc =
          mtl_pipe_desc.colorAttachments[ncolor_attachments++];
      mtl_attachment_desc.pixelFormat     = get_mtl_pixel_format(attachment_desc.format).format;
      mtl_attachment_desc.blendingEnabled = blend.enable;
      if (blend.enable) {
        mtl_attachment_desc.sourceRGBBlendFactor =
            get_mtl_blend_factor(blend.src_color_blend_factor);
        mtl_attachment_desc.destinationRGBBlendFactor =
            get_mtl_blend_factor(blend.dst_color_blend_factor);
        mtl_attachment_desc.sourceAlphaBlendFactor =
            get_mtl_blend_factor(blend.src_alpha_blend_factor);
        mtl_attachment_desc.destinationAlphaBlendFactor =
            get_mtl_blend_factor(blend.dst_alpha_blend_factor);
        mtl_attachment_desc.rgbBlendOperation   = get_mtl_blend_operation(blend.blend_op_color);
        mtl_attachment_desc.alphaBlendOperation = get_mtl_blend_operation(blend.blend_op_alpha);
      }
      if (info->color_attachment_blend_states) {
        mtl_attachment_desc.writeMask =
            (blend.color_write_mask & NGF_COLOR_MASK_WRITE_BIT_R ? MTLColorWriteMaskRed : 0) |
            (blend.color_write_mask & NGF_COLOR_MASK_WRITE_BIT_G ? MTLColorWriteMaskGreen : 0) |
            (blend.color_write_mask & NGF_COLOR_MASK_WRITE_BIT_B ? MTLColorWriteMaskBlue : 0) |
            (blend.color_write_mask & NGF_COLOR_MASK_WRITE_BIT_A ? MTLColorWriteMaskAlpha : 0);
      }
    } else if (
        attachment_desc.type == NGF_ATTACHMENT_DEPTH ||
        attachment_desc.type == NGF_ATTACHMENT_DEPTH_STENCIL) {
      mtl_pipe_desc.depthAttachmentPixelFormat =
          get_mtl_pixel_format(attachment_desc.format).format;
    }
  }

  mtl_pipe_desc.rasterSampleCount      = info->multisample->sample_count;
  mtl_pipe_desc.alphaToCoverageEnabled = info->multisample->alpha_to_coverage;

  mtl_pipe_desc.stencilAttachmentPixelFormat = MTLPixelFormatInvalid;

  if (mtl_pipe_desc.depthAttachmentPixelFormat == MTLPixelFormatDepth32Float_Stencil8) {
    mtl_pipe_desc.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
  }

  // Populate specialization constant values.
  MTLFunctionConstantValues* spec_consts = ngfmtl_function_consts(info->spec_info);

  // Set stage functions.
  bool                      have_niceshade_metadata = false;
  ngfmtl_niceshade_metadata metadata;
  for (uint32_t s = 0u; s < info->nshader_stages; ++s) {
    const ngf_shader_stage stage = info->shader_stages[s];
    if (!have_niceshade_metadata) {
      const ngf_error metadata_parse_result = ngfmtl_parse_niceshade_metadata(
          stage->source_code.c_str(),
          stage->type == NGF_STAGE_COMPUTE,
          &metadata);
      have_niceshade_metadata = (metadata_parse_result == NGF_ERROR_OK);
    }
    if (stage->type == NGF_STAGE_VERTEX) {
      assert(!mtl_pipe_desc.vertexFunction);
      mtl_pipe_desc.vertexFunction =
          ngfmtl_get_shader_main(stage->func_lib, stage->entry_point_name.c_str(), spec_consts);
    } else if (stage->type == NGF_STAGE_FRAGMENT) {
      assert(!mtl_pipe_desc.fragmentFunction);
      mtl_pipe_desc.fragmentFunction =
          ngfmtl_get_shader_main(stage->func_lib, stage->entry_point_name.c_str(), spec_consts);
    }
  }
  if (!have_niceshade_metadata) {
    NGFI_DIAG_ERROR("Native binding map not found.");
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }

  // Configure vertex input.
  const ngf_vertex_input_info& vertex_input_info = *info->input_info;
  MTLVertexDescriptor*         vert_desc         = mtl_pipe_desc.vertexDescriptor;
  for (uint32_t a = 0u; a < vertex_input_info.nattribs; ++a) {
    MTLVertexAttributeDescriptor* attr_desc = vert_desc.attributes[a];
    const ngf_vertex_attrib_desc& attr_info = vertex_input_info.attribs[a];
    attr_desc.offset                        = vertex_input_info.attribs[a].offset;
    attr_desc.bufferIndex = MAX_BUFFER_BINDINGS - vertex_input_info.attribs[a].binding;
    attr_desc.format = get_mtl_attrib_format(attr_info.type, attr_info.size, attr_info.normalized);
    if (attr_desc.format == MTLVertexFormatInvalid) {
      NGFI_DIAG_ERROR("Vertex attrib format not supported by Metal backend.");
      return NGF_ERROR_INVALID_FORMAT;
    }
  }
  for (uint32_t b = 0u; b < vertex_input_info.nvert_buf_bindings; ++b) {
    MTLVertexBufferLayoutDescriptor*   binding_desc = vert_desc.layouts[MAX_BUFFER_BINDINGS - b];
    const ngf_vertex_buf_binding_desc& binding_info = vertex_input_info.vert_buf_bindings[b];
    binding_desc.stride                             = binding_info.stride;
    binding_desc.stepFunction = get_mtl_step_function(binding_info.input_rate);
  }

  // Set primitive topology.
  mtl_pipe_desc.inputPrimitiveTopology =
      get_mtl_primitive_topology_class(info->input_assembly_info->primitive_topology);
  if (mtl_pipe_desc.inputPrimitiveTopology == MTLPrimitiveTopologyClassUnspecified) {
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }

  NGFMTL_NURSERY(graphics_pipeline, pipeline);
  pipeline->niceshade_metadata = std::move(metadata);
  memcpy(pipeline->blend_color, info->blend_consts, sizeof(pipeline->blend_color));

  if (info->debug_name != nullptr) {
    mtl_pipe_desc.label = [NSString stringWithUTF8String:info->debug_name];
  }

  NSError* err       = nil;
  pipeline->pipeline = [CURRENT_CONTEXT->device newRenderPipelineStateWithDescriptor:mtl_pipe_desc
                                                                               error:&err];
  pipeline->primitive_type = get_mtl_primitive_type(info->input_assembly_info->primitive_topology);

  // Set winding order and culling mode.
  pipeline->winding = get_mtl_winding(info->rasterization->front_face);
  pipeline->culling = get_mtl_culling(info->rasterization->cull_mode);

  // Set up depth and stencil state.
  pipeline->depth_stencil_desc                     = [MTLDepthStencilDescriptor new];
  const ngf_depth_stencil_info& depth_stencil_info = *info->depth_stencil;
  pipeline->depth_stencil_desc.depthCompareFunction =
      depth_stencil_info.depth_test ? get_mtl_compare_function(depth_stencil_info.depth_compare)
                                    : MTLCompareFunctionAlways;
  pipeline->depth_stencil_desc.depthWriteEnabled = info->depth_stencil->depth_write;
  pipeline->depth_stencil_desc.backFaceStencil =
      ngfmtl_create_stencil_descriptor(depth_stencil_info.back_stencil);
  pipeline->depth_stencil_desc.frontFaceStencil =
      ngfmtl_create_stencil_descriptor(depth_stencil_info.front_stencil);
  pipeline->front_stencil_reference = depth_stencil_info.front_stencil.reference;
  pipeline->back_stencil_reference  = depth_stencil_info.back_stencil.reference;
  pipeline->depth_stencil =
      [CURRENT_CONTEXT->device newDepthStencilStateWithDescriptor:pipeline->depth_stencil_desc];

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

void ngf_destroy_compute_pipeline(ngf_compute_pipeline pipe) NGF_NOEXCEPT {
  if (pipe != nullptr) {
    pipe->~ngf_compute_pipeline_t();
    NGFI_FREE(pipe);
  }
}

id<MTLBuffer> ngfmtl_create_buffer(const ngf_buffer_info& info) {
  MTLResourceOptions options         = 0u;
  switch (info.storage_type) {
  case NGF_BUFFER_STORAGE_HOST_READABLE:
  case NGF_BUFFER_STORAGE_HOST_READABLE_WRITEABLE:
    options = MTLResourceCPUCacheModeDefaultCache | MTLResourceStorageModeShared;
    break;
  case NGF_BUFFER_STORAGE_HOST_WRITEABLE:
    options = MTLResourceCPUCacheModeWriteCombined | MTLResourceStorageModeShared;
    break;
  case NGF_BUFFER_STORAGE_PRIVATE:
    options = MTLResourceStorageModePrivate;
    break;
  default:
    assert(false);
  }
  id<MTLBuffer> mtl_buffer = [CURRENT_CONTEXT->device newBufferWithLength:info.size
                                                                  options:options];
  return mtl_buffer;
}

uint8_t* _ngf_map_buffer(id<MTLBuffer> buffer, size_t offset, [[maybe_unused]] size_t size) {
  return (uint8_t*)buffer.contents + offset;
}

ngf_error ngf_create_texel_buffer_view(
    const ngf_texel_buffer_view_info* info,
    ngf_texel_buffer_view*            result) NGF_NOEXCEPT {
  NGFMTL_NURSERY(texel_buffer_view, view);
  auto texel_buf_descriptor = [MTLTextureDescriptor new];

  texel_buf_descriptor.depth            = 1;
  texel_buf_descriptor.mipmapLevelCount = 1;
  texel_buf_descriptor.pixelFormat      = get_mtl_pixel_format(info->texel_format).format;
  texel_buf_descriptor.textureType      = MTLTextureTypeTextureBuffer;
  texel_buf_descriptor.arrayLength      = 1;
  texel_buf_descriptor.sampleCount      = 1;
  texel_buf_descriptor.usage            = MTLTextureUsageShaderRead;
  texel_buf_descriptor.storageMode      = info->buffer->mtl_buffer.storageMode;
  texel_buf_descriptor.width            = info->size / ngfmtl_get_bytesperpel(info->texel_format);
  texel_buf_descriptor.height           = 1;
  view->mtl_buffer_view = [info->buffer->mtl_buffer newTextureWithDescriptor:texel_buf_descriptor
                                                                      offset:info->offset
                                                                 bytesPerRow:info->size];
  *result               = view.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_texel_buffer_view(ngf_texel_buffer_view buf_view) NGF_NOEXCEPT {
  if (buf_view) {
    buf_view->~ngf_texel_buffer_view_t();
    NGFI_FREE(buf_view);
  }
}

ngf_error ngf_create_buffer(const ngf_buffer_info* info, ngf_buffer* result) NGF_NOEXCEPT {
  NGFMTL_NURSERY(buffer, buf);
  buf->mtl_buffer = ngfmtl_create_buffer(*info);
  *result         = buf.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_buffer(ngf_buffer buf) NGF_NOEXCEPT {
  if (buf != nullptr) {
    buf->~ngf_buffer_t();
    NGFI_FREE(buf);
  }
}

void* ngf_buffer_map_range(ngf_buffer buf, size_t offset, size_t size) NGF_NOEXCEPT {
  buf->mapped_offset = offset;
  return (void*)_ngf_map_buffer(buf->mtl_buffer, offset, size);
}

void ngf_buffer_flush_range(
    [[maybe_unused]] ngf_buffer buf,
    [[maybe_unused]] size_t     offset,
    [[maybe_unused]] size_t     size) NGF_NOEXCEPT {
}

void ngf_buffer_unmap(ngf_buffer) NGF_NOEXCEPT {
}

ngf_error ngf_create_sampler(const ngf_sampler_info* info, ngf_sampler* result) NGF_NOEXCEPT {
  auto*                                sampler_desc = [MTLSamplerDescriptor new];
  std::optional<MTLSamplerAddressMode> s            = get_mtl_address_mode(info->wrap_u),
                                       t            = get_mtl_address_mode(info->wrap_v),
                                       r            = get_mtl_address_mode(info->wrap_w);
  if (!(s && t && r)) { return NGF_ERROR_INVALID_ENUM; }
  sampler_desc.sAddressMode  = *s;
  sampler_desc.tAddressMode  = *t;
  sampler_desc.rAddressMode  = *r;
  sampler_desc.minFilter     = get_mtl_minmag_filter(info->min_filter);
  sampler_desc.magFilter     = get_mtl_minmag_filter(info->mag_filter);
  sampler_desc.mipFilter     = get_mtl_mip_filter(info->mip_filter);
  sampler_desc.maxAnisotropy = info->enable_anisotropy ? (NSUInteger)info->max_anisotropy : 0;
  sampler_desc.lodMinClamp   = info->lod_min;
  sampler_desc.lodMaxClamp   = info->lod_max;
  NGFMTL_NURSERY(sampler, sampler);
  sampler->sampler = [CURRENT_CONTEXT->device newSamplerStateWithDescriptor:sampler_desc];
  *result          = sampler.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_sampler(ngf_sampler sampler) NGF_NOEXCEPT {
  if (sampler) {
    sampler->~ngf_sampler_t();
    NGFI_FREE(sampler);
  }
}

ngf_error ngf_create_cmd_buffer(const ngf_cmd_buffer_info*, ngf_cmd_buffer* result) NGF_NOEXCEPT {
  NGFMTL_NURSERY(cmd_buffer, cmd_buffer);
  *result = cmd_buffer.release();
  return NGF_ERROR_OK;
}

ngf_error ngf_create_image(const ngf_image_info* info, ngf_image* result) NGF_NOEXCEPT {
  auto* mtl_img_desc = [MTLTextureDescriptor new];

  const MTLPixelFormat fmt = get_mtl_pixel_format(info->format).format;
  if (fmt == MTLPixelFormatInvalid) {
    NGFI_DIAG_ERROR("Image format %d not supported by Metal backend.", info->format);
    return NGF_ERROR_INVALID_FORMAT;
  }

  std::optional<MTLTextureType> maybe_texture_type =
      get_mtl_texture_type(info->type, info->nlayers, info->sample_count);
  if (!maybe_texture_type.has_value()) {
    NGFI_DIAG_ERROR("Image type %d not supported by Metal backend.", info->type);
    return NGF_ERROR_INVALID_ENUM;
  }
  mtl_img_desc.textureType      = maybe_texture_type.value();
  mtl_img_desc.pixelFormat      = fmt;
  mtl_img_desc.width            = info->extent.width;
  mtl_img_desc.height           = info->extent.height;
  mtl_img_desc.depth            = info->extent.depth;
  mtl_img_desc.arrayLength      = info->nlayers;
  mtl_img_desc.mipmapLevelCount = info->nmips;
  mtl_img_desc.storageMode      = MTLStorageModePrivate;
  mtl_img_desc.sampleCount      = info->sample_count;
  if (info->usage_hint & NGF_IMAGE_USAGE_ATTACHMENT) {
    mtl_img_desc.usage |= MTLTextureUsageRenderTarget;
  }
  if (info->usage_hint & NGF_IMAGE_USAGE_SAMPLE_FROM) {
    mtl_img_desc.usage |= MTLTextureUsageShaderRead;
  }
  if (info->usage_hint & NGF_IMAGE_USAGE_STORAGE) {
    mtl_img_desc.usage |= MTLTextureUsageShaderWrite;
  }
  NGFMTL_NURSERY(image, image);
  image->texture     = [MTL_DEVICE newTextureWithDescriptor:mtl_img_desc];
  image->usage_flags = info->usage_hint;
  image->format      = info->format;
  *result            = image.release();
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
  cmd_buffer->active_rce     = nil;
  cmd_buffer->active_bce     = nil;
  NGFI_TRANSITION_CMD_BUF(cmd_buffer, NGFI_CMD_BUFFER_READY);
  return NGF_ERROR_OK;
}

ngf_error ngf_submit_cmd_buffers(uint32_t n, ngf_cmd_buffer* cmd_buffers) NGF_NOEXCEPT {
  if (CURRENT_CONTEXT->pending_cmd_buffer) {
    [CURRENT_CONTEXT->pending_cmd_buffer commit];
    CURRENT_CONTEXT->pending_cmd_buffer = nil;
  }
  for (uint32_t b = 0u; b < n; ++b) {
    NGFI_TRANSITION_CMD_BUF(cmd_buffers[b], NGFI_CMD_BUFFER_PENDING);
    if (b < n - 1u) {
      [cmd_buffers[b]->mtl_cmd_buffer commit];
    } else {
      CURRENT_CONTEXT->pending_cmd_buffer = cmd_buffers[b]->mtl_cmd_buffer;
    }
    cmd_buffers[b]->mtl_cmd_buffer = nil;
    NGFI_TRANSITION_CMD_BUF(cmd_buffers[b], NGFI_CMD_BUFFER_SUBMITTED);
  }
  return NGF_ERROR_OK;
}

void ngfmtl_finish_pending_encoders(ngf_cmd_buffer cmd_buffer) {
  /* End any current Metal encoders.*/
  if (cmd_buffer->active_rce) {
    [cmd_buffer->active_rce endEncoding];
    cmd_buffer->active_rce = nil;
  } else if (cmd_buffer->active_bce) {
    [cmd_buffer->active_bce endEncoding];
    cmd_buffer->active_bce = nil;
  } else if (cmd_buffer->active_cce) {
    [cmd_buffer->active_cce endEncoding];
    cmd_buffer->active_cce = nil;
  }
}

ngf_error ngf_cmd_begin_render_pass_simple(
    ngf_cmd_buffer    cmd_buf,
    ngf_render_target rt,
    float             clear_color_r,
    float             clear_color_g,
    float             clear_color_b,
    float             clear_color_a,
    float             clear_depth,
    uint32_t          clear_stencil,
    ngf_render_encoder* enc) NGF_NOEXCEPT {
  ngfi_sa_reset(ngfi_tmp_store());
  const uint32_t nattachments = rt->attachment_descs.ndescs;
  auto           load_ops     = (ngf_attachment_load_op*)ngfi_sa_alloc(
      ngfi_tmp_store(),
      sizeof(ngf_attachment_load_op) * nattachments);
  auto store_ops = (ngf_attachment_store_op*)ngfi_sa_alloc(
      ngfi_tmp_store(),
      sizeof(ngf_attachment_store_op) * nattachments);
  auto clears = (ngf_clear*)ngfi_sa_alloc(ngfi_tmp_store(), sizeof(ngf_clear) * nattachments);

  for (size_t i = 0u; i < nattachments; ++i) {
    load_ops[i] = NGF_LOAD_OP_CLEAR;
    if (rt->attachment_descs.descs[i].type == NGF_ATTACHMENT_COLOR) {
      clears[i].clear_color[0] = clear_color_r;
      clears[i].clear_color[1] = clear_color_g;
      clears[i].clear_color[2] = clear_color_b;
      clears[i].clear_color[3] = clear_color_a;
    } else if (rt->attachment_descs.descs[i].type == NGF_ATTACHMENT_DEPTH) {
      clears[i].clear_depth_stencil.clear_depth   = clear_depth;
      clears[i].clear_depth_stencil.clear_stencil = clear_stencil;
    } else {
      assert(false);
    }

    const bool needs_resolve = rt->attachment_descs.descs[i].type == NGF_ATTACHMENT_COLOR &&
                               rt->attachment_descs.descs[i].sample_count > NGF_SAMPLE_COUNT_1 &&
                               (rt->resolve_image_refs || rt->is_default);
    store_ops[i] = needs_resolve                              ? NGF_STORE_OP_RESOLVE
                   : rt->attachment_descs.descs[i].is_sampled ? NGF_STORE_OP_STORE
                                                              : NGF_STORE_OP_DONTCARE;
  }
  const ngf_render_pass_info pass_info =
      {.render_target = rt, .load_ops = load_ops, .store_ops = store_ops, .clears = clears};
  return ngf_cmd_begin_render_pass(cmd_buf, &pass_info, enc);
}

ngf_error ngf_cmd_begin_render_pass(
    ngf_cmd_buffer              cmd_buffer,
    const ngf_render_pass_info* pass_info,
    ngf_render_encoder*         enc) NGF_NOEXCEPT {
  NGFI_TRANSITION_CMD_BUF(cmd_buffer, NGFI_CMD_BUFFER_RECORDING);
  assert(pass_info);
  const ngf_render_target rt = pass_info->render_target;
  assert(rt);
  assert(cmd_buffer);

  ngfmtl_finish_pending_encoders(cmd_buffer);
  cmd_buffer->renderpass_active = true;

  uint32_t color_attachment_idx      = 0u;
  uint32_t resolve_attachment_idx    = 0u;
  uint32_t render_image_idx          = 0u;
  auto     pass_descriptor           = [MTLRenderPassDescriptor new];
  pass_descriptor.renderTargetWidth  = rt->width;
  pass_descriptor.renderTargetHeight = rt->height;
  pass_descriptor.depthAttachment    = nil;
  pass_descriptor.stencilAttachment  = nil;
  for (uint32_t i = 0u; i < rt->attachment_descs.ndescs; ++i) {
    const ngf_attachment_description& attachment_desc = rt->attachment_descs.descs[i];
    if (attachment_desc.is_resolve) { continue; }
    const ngf_attachment_load_op      load_op         = pass_info->load_ops[i];
    const ngf_attachment_store_op     store_op        = pass_info->store_ops[i];
    const ngf_clear_info*             clear_info =
        load_op == NGF_LOAD_OP_CLEAR && pass_info->clears ? &pass_info->clears[i] : nullptr;
    switch (attachment_desc.type) {
    case NGF_ATTACHMENT_COLOR: {
      auto mtl_desc = [MTLRenderPassColorAttachmentDescriptor new];
      ngfmtl_attachment_set_common(mtl_desc, render_image_idx++, attachment_desc.type, rt, load_op, store_op);
      if (clear_info) {
        mtl_desc.clearColor = MTLClearColorMake(
            clear_info->clear_color[0],
            clear_info->clear_color[1],
            clear_info->clear_color[2],
            clear_info->clear_color[3]);
      }

      if (attachment_desc.sample_count > NGF_SAMPLE_COUNT_1) {
        if (rt->is_default) {
          mtl_desc.resolveTexture = CURRENT_CONTEXT->frame.resolve_attachment_texture();
        } else if (rt->resolve_image_refs) {
          mtl_desc.resolveTexture = rt->resolve_image_refs[resolve_attachment_idx++].image->texture;
        }
      }

      pass_descriptor.colorAttachments[color_attachment_idx++] = mtl_desc;
      break;
    }
    case NGF_ATTACHMENT_DEPTH: {
      auto mtl_desc = [MTLRenderPassDepthAttachmentDescriptor new];
      ngfmtl_attachment_set_common(mtl_desc, render_image_idx++, attachment_desc.type, rt, load_op, store_op);
      if (clear_info) { mtl_desc.clearDepth = clear_info->clear_depth_stencil.clear_depth; }
      pass_descriptor.depthAttachment = mtl_desc;
      break;
    }
    case NGF_ATTACHMENT_DEPTH_STENCIL: {
      auto mtl_depth_desc = [MTLRenderPassDepthAttachmentDescriptor new];
      ngfmtl_attachment_set_common(mtl_depth_desc, render_image_idx++, attachment_desc.type, rt, load_op, store_op);
      if (clear_info) { mtl_depth_desc.clearDepth = clear_info->clear_depth_stencil.clear_depth; }
      pass_descriptor.depthAttachment = mtl_depth_desc;
      auto mtl_stencil_desc           = [MTLRenderPassStencilAttachmentDescriptor new];
      ngfmtl_attachment_set_common(
          mtl_stencil_desc,
          render_image_idx++,
          attachment_desc.type,
          rt,
          load_op,
          store_op);
      if (clear_info) {
        mtl_stencil_desc.clearStencil = clear_info->clear_depth_stencil.clear_stencil;
      }
      pass_descriptor.stencilAttachment = mtl_stencil_desc;
      break;
    }
    }
  }

  cmd_buffer->active_rce =
      [cmd_buffer->mtl_cmd_buffer renderCommandEncoderWithDescriptor:pass_descriptor];
  cmd_buffer->active_rt = rt;

  enc->pvt_data_donotuse.d0 = (uintptr_t)cmd_buffer;
  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_end_render_pass(ngf_render_encoder enc) NGF_NOEXCEPT {
  auto cmd_buffer = NGFMTL_ENC2CMDBUF(enc);
  if (cmd_buffer->active_rce) {
    [cmd_buffer->active_rce endEncoding];
    cmd_buffer->active_rce      = nil;
    cmd_buffer->active_gfx_pipe = nullptr;
  }
  cmd_buffer->renderpass_active = false;
  NGFI_TRANSITION_CMD_BUF(cmd_buffer, NGFI_CMD_BUFFER_READY_TO_SUBMIT);

  return NGF_ERROR_OK;
}

ngf_error
ngf_cmd_begin_xfer_pass(ngf_cmd_buffer cmd_buf, const ngf_xfer_pass_info*, ngf_xfer_encoder* enc)
    NGF_NOEXCEPT {
  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_RECORDING);
  ngfmtl_finish_pending_encoders(cmd_buf);
  enc->pvt_data_donotuse.d0 = (uintptr_t)cmd_buf;
  cmd_buf->active_bce       = [cmd_buf->mtl_cmd_buffer blitCommandEncoder];
  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_end_xfer_pass(ngf_xfer_encoder enc) NGF_NOEXCEPT {
  auto cmd_buf = NGFMTL_ENC2CMDBUF(enc);
  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_READY_TO_SUBMIT);
  if (cmd_buf->active_bce) {
    [cmd_buf->active_bce endEncoding];
    cmd_buf->active_bce = nil;
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_begin_compute_pass(
    ngf_cmd_buffer cmd_buf,
    const ngf_compute_pass_info*,
    ngf_compute_encoder* enc) NGF_NOEXCEPT {
  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_RECORDING);
  enc->pvt_data_donotuse.d0 = (uintptr_t)cmd_buf;
  cmd_buf->active_cce       = [cmd_buf->mtl_cmd_buffer computeCommandEncoder];
  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_end_compute_pass(ngf_compute_encoder enc) NGF_NOEXCEPT {
  auto cmd_buf = NGFMTL_ENC2CMDBUF(enc);
  assert(cmd_buf);
  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_READY_TO_SUBMIT);
  if (cmd_buf->active_cce) {
    [cmd_buf->active_cce endEncoding];
    cmd_buf->active_cce          = nil;
    cmd_buf->active_compute_pipe = nullptr;
  }
  return NGF_ERROR_OK;
}

void ngf_cmd_bind_compute_pipeline(ngf_compute_encoder enc, ngf_compute_pipeline pipeline)
    NGF_NOEXCEPT {
  auto cmd_buf = NGFMTL_ENC2CMDBUF(enc);
  assert(cmd_buf);
  assert(cmd_buf->active_cce);
  if (!cmd_buf->active_cce) {
    NGFI_DIAG_ERROR("Attempt to bind compute pipeline without an active compute encoder");
    return;
  }
  [cmd_buf->active_cce setComputePipelineState:pipeline->pipeline];
  cmd_buf->active_compute_pipe = pipeline;
}

void ngf_cmd_dispatch(
    ngf_compute_encoder enc,
    uint32_t            x_threadgroups,
    uint32_t            y_threadgroups,
    uint32_t            z_threadgroups) NGF_NOEXCEPT {
  auto cmd_buf = NGFMTL_ENC2CMDBUF(enc);
  assert(cmd_buf->active_cce);
  if (!cmd_buf->active_cce) {
    NGFI_DIAG_ERROR("Attempt to perform a compute dispatch without an active compute encoder.");
    return;
  }
  assert(cmd_buf->active_compute_pipe);
  if (!cmd_buf->active_compute_pipe) {
    NGFI_DIAG_ERROR("Attempt to perform a compute dispatch without a bound compute pipeline.");
    return;
  }
  const uint32_t* threadgroup_size =
      cmd_buf->active_compute_pipe->niceshade_metadata.threadgroup_size;
  [cmd_buf->active_cce
       dispatchThreadgroups:MTLSizeMake(x_threadgroups, y_threadgroups, z_threadgroups)
      threadsPerThreadgroup:MTLSizeMake(
                                threadgroup_size[0],
                                threadgroup_size[1],
                                threadgroup_size[2])];
}

void ngf_cmd_bind_gfx_pipeline(ngf_render_encoder enc, const ngf_graphics_pipeline pipeline)
    NGF_NOEXCEPT {
  auto buf = NGFMTL_ENC2CMDBUF(enc);
  [buf->active_rce setRenderPipelineState:pipeline->pipeline];
  [buf->active_rce setCullMode:pipeline->culling];
  [buf->active_rce setFrontFacingWinding:pipeline->winding];

  [buf->active_rce setBlendColorRed:pipeline->blend_color[0]
                              green:pipeline->blend_color[1]
                               blue:pipeline->blend_color[2]
                              alpha:pipeline->blend_color[3]];
  if (pipeline->depth_stencil) { [buf->active_rce setDepthStencilState:pipeline->depth_stencil]; }
  [buf->active_rce setStencilFrontReferenceValue:pipeline->front_stencil_reference
                              backReferenceValue:pipeline->back_stencil_reference];
  buf->active_gfx_pipe = pipeline;
}

void ngf_cmd_viewport(ngf_render_encoder enc, const ngf_irect2d* r) NGF_NOEXCEPT {
  auto        buf = NGFMTL_ENC2CMDBUF(enc);
  MTLViewport viewport;
  viewport.originX = r->x;
  viewport.originY = r->y + (int32_t)r->height;
  viewport.width   = r->width;
  viewport.height  = -1.0 * r->height;

  // TODO: fix
  viewport.znear = 0.0f;
  viewport.zfar  = 1.0f;

  [buf->active_rce setViewport:viewport];
}

void ngf_cmd_scissor(ngf_render_encoder enc, const ngf_irect2d* r) NGF_NOEXCEPT {
  auto           buf = NGFMTL_ENC2CMDBUF(enc);
  MTLScissorRect scissor;
  scissor.x      = (uint32_t)r->x;
  scissor.y      = (uint32_t)r->y;
  scissor.width  = r->width;
  scissor.height = r->height;
  [buf->active_rce setScissorRect:scissor];
}

void ngf_cmd_draw(
    ngf_render_encoder enc,
    bool               indexed,
    uint32_t           first_element,
    uint32_t           nelements,
    uint32_t           ninstances) NGF_NOEXCEPT {
  auto             buf       = NGFMTL_ENC2CMDBUF(enc);
  MTLPrimitiveType prim_type = buf->active_gfx_pipe->primitive_type;
  if (!indexed) {
    [buf->active_rce drawPrimitives:prim_type
                        vertexStart:first_element
                        vertexCount:nelements
                      instanceCount:ninstances
                       baseInstance:0];
  } else {
    [buf->active_rce
        drawIndexedPrimitives:prim_type
                   indexCount:nelements
                    indexType:buf->bound_index_buffer_type
                  indexBuffer:buf->bound_index_buffer
            indexBufferOffset:buf->bound_index_buffer_offset +
                              first_element *
                                  (buf->bound_index_buffer_type == MTLIndexTypeUInt16 ? 2 : 4)
                instanceCount:ninstances
                   baseVertex:0
                 baseInstance:0];
  }
}

void ngf_cmd_bind_attrib_buffer(
    ngf_render_encoder enc,
    const ngf_buffer   buf,
    uint32_t           binding,
    size_t             offset) NGF_NOEXCEPT {
  auto cmd_buf = NGFMTL_ENC2CMDBUF(enc);
  [cmd_buf->active_rce setVertexBuffer:buf->mtl_buffer
                                offset:offset
                               atIndex:MAX_BUFFER_BINDINGS - binding];
}

void ngf_cmd_bind_index_buffer(
    ngf_render_encoder enc,
    const ngf_buffer   buf,
    size_t             offset,
    ngf_type           type) NGF_NOEXCEPT {
  auto cmd_buf                       = NGFMTL_ENC2CMDBUF(enc);
  cmd_buf->bound_index_buffer        = buf->mtl_buffer;
  cmd_buf->bound_index_buffer_type   = get_mtl_index_type(type);
  cmd_buf->bound_index_buffer_offset = offset;
}

void ngf_cmd_bind_resources(
    ngf_render_encoder          enc,
    const ngf_resource_bind_op* bind_ops,
    uint32_t                    nbind_ops) NGF_NOEXCEPT {
  auto cmd_buf = NGFMTL_ENC2CMDBUF(enc);
  assert(cmd_buf);
  for (uint32_t o = 0u; o < nbind_ops; ++o) {
    const ngf_resource_bind_op& bind_op = bind_ops[o];
    assert(cmd_buf->active_gfx_pipe);
    if (!cmd_buf->active_gfx_pipe) {
      NGFI_DIAG_ERROR("Attempt to bind resources without a bound graphics pipeline.");
      return;
    }
    assert(cmd_buf->active_rce);
    if (!cmd_buf->active_rce) {
      NGFI_DIAG_ERROR("Attempt to bind resources without an active render command encoder.");
      return;
    }
    const uint32_t native_binding =
        cmd_buf->active_gfx_pipe->niceshade_metadata
            .native_binding_map[bind_op.target_set][bind_op.target_binding];
    if (native_binding == ~0) {
      NGFI_DIAG_ERROR(
          "Failed to  find  native binding for set %d binding %d",
          bind_op.target_set,
          bind_op.target_binding);
      continue;
    }
    switch (bind_op.type) {
    case NGF_DESCRIPTOR_TEXEL_BUFFER: {
      [cmd_buf->active_rce setVertexTexture:bind_op.info.texel_buffer_view->mtl_buffer_view
                                    atIndex:native_binding];
      [cmd_buf->active_rce setFragmentTexture:bind_op.info.texel_buffer_view->mtl_buffer_view
                                      atIndex:native_binding];
      break;
    }
    case NGF_DESCRIPTOR_STORAGE_BUFFER:
    case NGF_DESCRIPTOR_UNIFORM_BUFFER: {
      const ngf_buffer_bind_info& buf_bind_op = bind_op.info.buffer;
      const ngf_buffer            buf         = buf_bind_op.buffer;
      size_t                      offset      = buf_bind_op.offset;
      [cmd_buf->active_rce setVertexBuffer:buf->mtl_buffer offset:offset atIndex:native_binding];
      [cmd_buf->active_rce setFragmentBuffer:buf->mtl_buffer offset:offset atIndex:native_binding];
      break;
    }
    case NGF_DESCRIPTOR_IMAGE_AND_SAMPLER: {
      const ngf_image_sampler_bind_info& img_bind_op = bind_op.info.image_sampler;
      [cmd_buf->active_rce setVertexTexture:img_bind_op.image->texture atIndex:native_binding];
      [cmd_buf->active_rce setVertexSamplerState:img_bind_op.sampler->sampler
                                         atIndex:native_binding];
      [cmd_buf->active_rce setFragmentTexture:img_bind_op.image->texture atIndex:native_binding];
      [cmd_buf->active_rce setFragmentSamplerState:img_bind_op.sampler->sampler
                                           atIndex:native_binding];
      break;
    }
    case NGF_DESCRIPTOR_IMAGE: {
      const ngf_image_sampler_bind_info& img_bind_op = bind_op.info.image_sampler;
      [cmd_buf->active_rce setVertexTexture:img_bind_op.image->texture atIndex:native_binding];
      [cmd_buf->active_rce setFragmentTexture:img_bind_op.image->texture atIndex:native_binding];
      break;
    }
    case NGF_DESCRIPTOR_SAMPLER: {
      const ngf_image_sampler_bind_info& img_bind_op = bind_op.info.image_sampler;
      [cmd_buf->active_rce setVertexSamplerState:img_bind_op.sampler->sampler
                                         atIndex:native_binding];
      [cmd_buf->active_rce setFragmentSamplerState:img_bind_op.sampler->sampler
                                           atIndex:native_binding];
      break;
    }
    case NGF_DESCRIPTOR_STORAGE_IMAGE:
      NGFI_DIAG_ERROR("Binding storage images to non-compute shader is currently unsupported.");
      break;
    case NGF_DESCRIPTOR_TYPE_COUNT:
      assert(false);
    }
  }
}

void ngf_cmd_bind_compute_resources(
    ngf_compute_encoder         enc,
    const ngf_resource_bind_op* bind_ops,
    uint32_t                    nbind_ops) NGF_NOEXCEPT {
  auto cmd_buf = NGFMTL_ENC2CMDBUF(enc);
  assert(cmd_buf);
  for (uint32_t o = 0u; o < nbind_ops; ++o) {
    const ngf_resource_bind_op& bind_op = bind_ops[o];
    assert(cmd_buf->active_compute_pipe);
    if (!cmd_buf->active_compute_pipe) {
      NGFI_DIAG_ERROR("Attempt to bind resources without a bound compute pipeline.");
      return;
    }
    assert(cmd_buf->active_cce);
    if (!cmd_buf->active_cce) {
      NGFI_DIAG_ERROR("Attempt to bind resources without an active compute command encoder.");
      return;
    }
    const uint32_t native_binding =
        cmd_buf->active_compute_pipe->niceshade_metadata
            .native_binding_map[bind_op.target_set][bind_op.target_binding];
    if (native_binding == ~0) {
      NGFI_DIAG_ERROR(
          "Failed to  find  native binding for set %d binding %d",
          bind_op.target_set,
          bind_op.target_binding);
      continue;
    }
    switch (bind_op.type) {
    case NGF_DESCRIPTOR_TEXEL_BUFFER: {
      [cmd_buf->active_cce setTexture:bind_op.info.texel_buffer_view->mtl_buffer_view
                              atIndex:native_binding];
      break;
    }
    case NGF_DESCRIPTOR_STORAGE_BUFFER:
    case NGF_DESCRIPTOR_UNIFORM_BUFFER: {
      const ngf_buffer_bind_info& buf_bind_op = bind_op.info.buffer;
      const ngf_buffer            buf         = buf_bind_op.buffer;
      size_t                      offset      = buf_bind_op.offset;
      [cmd_buf->active_cce setBuffer:buf->mtl_buffer offset:offset atIndex:native_binding];
      break;
    }
    case NGF_DESCRIPTOR_IMAGE_AND_SAMPLER: {
      const ngf_image_sampler_bind_info& img_bind_op = bind_op.info.image_sampler;
      [cmd_buf->active_cce setTexture:img_bind_op.image->texture atIndex:native_binding];
      [cmd_buf->active_cce setSamplerState:img_bind_op.sampler->sampler atIndex:native_binding];
      break;
    }
    case NGF_DESCRIPTOR_STORAGE_IMAGE:
    case NGF_DESCRIPTOR_IMAGE: {
      const ngf_image_sampler_bind_info& img_bind_op = bind_op.info.image_sampler;
      [cmd_buf->active_cce setTexture:img_bind_op.image->texture atIndex:native_binding];
      break;
    }
    case NGF_DESCRIPTOR_SAMPLER: {
      const ngf_image_sampler_bind_info& img_bind_op = bind_op.info.image_sampler;
      [cmd_buf->active_cce setSamplerState:img_bind_op.sampler->sampler atIndex:native_binding];
      break;
    }
    case NGF_DESCRIPTOR_TYPE_COUNT:
      assert(false);
    }
  }
}

void ngfmtl_cmd_copy_buffer(
    ngf_xfer_encoder enc,
    id<MTLBuffer>    src,
    id<MTLBuffer>    dst,
    size_t           size,
    size_t           src_offset,
    size_t           dst_offset) {
  auto buf = NGFMTL_ENC2CMDBUF(enc);
  assert(buf->active_rce == nil);
  [buf->active_bce copyFromBuffer:src
                     sourceOffset:src_offset
                         toBuffer:dst
                destinationOffset:dst_offset
                             size:size];
}

void ngf_cmd_copy_buffer(
    ngf_xfer_encoder enc,
    const ngf_buffer src,
    ngf_buffer       dst,
    size_t           size,
    size_t           src_offset,
    size_t           dst_offset) NGF_NOEXCEPT {
  ngfmtl_cmd_copy_buffer(enc, src->mtl_buffer, dst->mtl_buffer, size, src_offset, dst_offset);
}

void ngf_cmd_write_image(
    ngf_xfer_encoder       enc,
    ngf_buffer             src,
    ngf_image              dst,
    const ngf_image_write* writes,
    uint32_t               nwrites) NGF_NOEXCEPT {
        auto buf = NGFMTL_ENC2CMDBUF(enc);
  assert(buf->active_rce == nil);
  for (size_t i = 0u; i < nwrites; ++i) {
    const ngf_image_write* w = &writes[i];
    for (uint32_t l = 0u; l < w->nlayers; ++l) {
      const uint32_t pitch    = ngfmtl_get_pitch(w->extent.width , dst->format);
      const uint32_t num_rows = ngfmtl_get_num_rows(w->extent.height, dst->format);
      [buf->active_bce copyFromBuffer:src->mtl_buffer
                       sourceOffset:w->src_offset + (l * pitch * num_rows)
                  sourceBytesPerRow:pitch
                sourceBytesPerImage:pitch * num_rows
                         sourceSize:MTLSizeMake(w->extent.width, w->extent.height, w->extent.depth)
                          toTexture:dst->texture
                   destinationSlice:w->dst_base_layer + l
                     destinationLevel: w->dst_level
                    destinationOrigin:MTLOriginMake(
                                        (NSUInteger)w->dst_offset.x,
                                        (NSUInteger)w->dst_offset.y,
                                        (NSUInteger)w->dst_offset.z)];
    }
  }
}

/*void ngf_cmd_write_image(
    ngf_xfer_encoder enc,
    const ngf_buffer src,
    size_t           src_offset,
    ngf_image_ref    dst,
    ngf_offset3d     offset,
    ngf_extent3d     extent,
    uint32_t         nlayers) NGF_NOEXCEPT {
  auto buf = NGFMTL_ENC2CMDBUF(enc);
  assert(buf->active_rce == nil);
  const MTLTextureType texture_type = dst.image->texture.textureType;
  const bool           is_cubemap =
      texture_type == MTLTextureTypeCube || texture_type == MTLTextureTypeCubeArray;
  const uint32_t target_slice =
      (is_cubemap ? 6u : 1u) * dst.layer + (is_cubemap ? dst.cubemap_face : 0);
  const uint32_t pitch    = ngfmtl_get_pitch(extent.width, dst.image->format);
  const uint32_t num_rows = ngfmtl_get_num_rows(extent.height, dst.image->format);
  for (uint32_t l = 0; l < nlayers; ++l) {
    [buf->active_bce copyFromBuffer:src->mtl_buffer
                       sourceOffset:src_offset + (l * pitch * num_rows)
                  sourceBytesPerRow:pitch
                sourceBytesPerImage:pitch * num_rows
                         sourceSize:MTLSizeMake(extent.width, extent.height, extent.depth)
                          toTexture:dst.image->texture
                   destinationSlice:target_slice + l
                   destinationLevel:dst.mip_level
                  destinationOrigin:MTLOriginMake(
                                        (NSUInteger)offset.x,
                                        (NSUInteger)offset.y,
                                        (NSUInteger)offset.z)];
  }
}*/

void ngf_cmd_copy_image_to_buffer(
    ngf_xfer_encoder    enc,
    const ngf_image_ref src,
    ngf_offset3d        src_offset,
    ngf_extent3d        src_extent,
    uint32_t            nlayers,
    ngf_buffer          dst,
    size_t              dst_offset) NGF_NOEXCEPT {
  auto buf = NGFMTL_ENC2CMDBUF(enc);
  assert(buf->active_rce == nullptr);
  const MTLTextureType texture_type = src.image->texture.textureType;
  const bool           is_cubemap =
      texture_type == MTLTextureTypeCube || texture_type == MTLTextureTypeCubeArray;
  const uint32_t src_slice =
      (is_cubemap ? 6u : 1u) * src.layer + (is_cubemap ? src.cubemap_face : 0);
  const uint32_t pitch    = ngfmtl_get_pitch(src_extent.width, src.image->format);
  const uint32_t num_rows = ngfmtl_get_num_rows(src_extent.height, src.image->format);
  for (uint32_t l = 0; l < nlayers; ++l) {
    [buf->active_bce
                 copyFromTexture:src.image->texture
                     sourceSlice:src_slice + l
                     sourceLevel:src.mip_level
                    sourceOrigin:MTLOriginMake(
                                     (NSUInteger)src_offset.x,
                                     (NSUInteger)src_offset.y,
                                     (NSUInteger)src_offset.z)
                      sourceSize:MTLSizeMake(src_extent.width, src_extent.height, src_extent.depth)
                        toBuffer:dst->mtl_buffer
               destinationOffset:dst_offset + (l * pitch * num_rows)
          destinationBytesPerRow:pitch
        destinationBytesPerImage:pitch * num_rows];
  }
}

ngf_error ngf_cmd_generate_mipmaps(ngf_xfer_encoder xfenc, ngf_image img) NGF_NOEXCEPT {
  if (!(img->usage_flags & NGF_IMAGE_USAGE_MIPMAP_GENERATION)) {
    NGFI_DIAG_ERROR("mipmap generation was requested for an image that was created "
                    "without the NGF_IMAGE_USAGE_MIPMAP_GENERATION flag");
    return NGF_ERROR_INVALID_OPERATION;
  }
  auto buf = NGFMTL_ENC2CMDBUF(xfenc);
  assert(buf->active_rce == nil);
  [buf->active_bce generateMipmapsForTexture:img->texture];
  return NGF_ERROR_OK;
}
#define PLACEHOLDER_CMD(name, ...)                    \
  void ngf_cmd_##name(ngf_cmd_buffer*, __VA_ARGS__) { \
  }

void ngf_cmd_stencil_reference(ngf_render_encoder enc, uint32_t front, uint32_t back) NGF_NOEXCEPT {
  auto cmd_buf = NGFMTL_ENC2CMDBUF(enc);
  [cmd_buf->active_rce setStencilFrontReferenceValue:front backReferenceValue:back];
}

void ngf_cmd_stencil_compare_mask(ngf_render_encoder enc, uint32_t front, uint32_t back)
    NGF_NOEXCEPT {
  auto cmd_buf                                                           = NGFMTL_ENC2CMDBUF(enc);
  cmd_buf->active_gfx_pipe->depth_stencil_desc.frontFaceStencil.readMask = front;
  cmd_buf->active_gfx_pipe->depth_stencil_desc.backFaceStencil.readMask  = back;
  [cmd_buf->active_rce
      setDepthStencilState:[CURRENT_CONTEXT->device
                               newDepthStencilStateWithDescriptor:cmd_buf->active_gfx_pipe->
                                                                  depth_stencil_desc]];
}

void ngf_cmd_stencil_write_mask(ngf_render_encoder enc, uint32_t front, uint32_t back)
    NGF_NOEXCEPT {
  auto cmd_buf                                                            = NGFMTL_ENC2CMDBUF(enc);
  cmd_buf->active_gfx_pipe->depth_stencil_desc.frontFaceStencil.writeMask = front;
  cmd_buf->active_gfx_pipe->depth_stencil_desc.backFaceStencil.writeMask  = back;
  [cmd_buf->active_rce
      setDepthStencilState:[CURRENT_CONTEXT->device
                               newDepthStencilStateWithDescriptor:cmd_buf->active_gfx_pipe->
                                                                  depth_stencil_desc]];
}

void ngf_finish() NGF_NOEXCEPT {
  if (CURRENT_CONTEXT->pending_cmd_buffer != nil) {
    [CURRENT_CONTEXT->pending_cmd_buffer commit];
    CURRENT_CONTEXT->last_cmd_buffer    = CURRENT_CONTEXT->pending_cmd_buffer;
    CURRENT_CONTEXT->pending_cmd_buffer = nil;
  }

  if (CURRENT_CONTEXT->last_cmd_buffer != nil) {
    [CURRENT_CONTEXT->last_cmd_buffer waitUntilCompleted];
  }
}

void ngf_renderdoc_capture_next_frame() NGF_NOEXCEPT {
  NGFI_DIAG_WARNING("RenderDoc functionality is not implemented for Metal backend");
}

void ngf_renderdoc_capture_begin() NGF_NOEXCEPT {
  NGFI_DIAG_WARNING("RenderDoc functionality is not implemented for Metal backend");
}

void ngf_renderdoc_capture_end() NGF_NOEXCEPT {
  NGFI_DIAG_WARNING("RenderDoc functionality is not implemented for Metal backend");
}

extern "C" {

uintptr_t ngf_get_mtl_image_handle(ngf_image image) NGF_NOEXCEPT {
  return (uintptr_t)(image->texture);
}

uintptr_t ngf_get_mtl_buffer_handle(ngf_buffer buffer) NGF_NOEXCEPT {
  return (uintptr_t)(buffer->mtl_buffer);
}

uintptr_t ngf_get_mtl_cmd_buffer_handle(ngf_cmd_buffer cmd_buffer) NGF_NOEXCEPT {
  return (uintptr_t)(cmd_buffer->mtl_cmd_buffer);
}

uintptr_t ngf_get_mtl_sampler_handle(ngf_sampler sampler) NGF_NOEXCEPT {
  return (uintptr_t)(sampler->sampler);
}

uintptr_t ngf_get_mtl_render_encoder_handle(ngf_render_encoder render_encoder) NGF_NOEXCEPT {
  auto buf = NGFMTL_ENC2CMDBUF(render_encoder);
  return (uintptr_t)(buf->active_rce);
}

uintptr_t ngf_get_mtl_xfer_encoder_handle(ngf_xfer_encoder xfer_encoder) NGF_NOEXCEPT {
  auto buf = NGFMTL_ENC2CMDBUF(xfer_encoder);
  return (uintptr_t)(buf->active_bce);
}

uintptr_t ngf_get_mtl_compute_encoder_handle(ngf_compute_encoder compute_encoder) NGF_NOEXCEPT {
  auto buf = NGFMTL_ENC2CMDBUF(compute_encoder);
  return (uintptr_t)(buf->active_cce);
}

}
