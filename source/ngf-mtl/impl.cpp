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

#include "ngf-common/cmdbuf-state.h"
#include "ngf-common/macros.h"
#include "ngf-common/native-binding-map.h"
#include "ngf-common/stack-alloc.h"
#include "nicegraf-wrappers.h"
#include "nicegraf.h"
#include "nicegraf-mtl-handles.h"

#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include <MetalSingleHeader.hpp>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>

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
MTL::Device* MTL_DEVICE = nullptr;

ngf_device_capabilities DEVICE_CAPS;

#pragma mark ngf_enum_maps

static MTL::BlendFactor get_mtl_blend_factor(ngf_blend_factor f) {
  static constexpr MTL::BlendFactor factors[NGF_BLEND_FACTOR_COUNT] = {
      MTL::BlendFactorZero,
      MTL::BlendFactorOne,
      MTL::BlendFactorSourceColor,
      MTL::BlendFactorOneMinusSourceColor,
      MTL::BlendFactorDestinationColor,
      MTL::BlendFactorOneMinusDestinationColor,
      MTL::BlendFactorSourceAlpha,
      MTL::BlendFactorOneMinusSourceAlpha,
      MTL::BlendFactorDestinationAlpha,
      MTL::BlendFactorOneMinusDestinationAlpha,
      MTL::BlendFactorBlendColor,
      MTL::BlendFactorOneMinusBlendColor,
      MTL::BlendFactorBlendAlpha,
      MTL::BlendFactorOneMinusBlendAlpha};
  return factors[f];
}

static MTL::BlendOperation get_mtl_blend_operation(ngf_blend_op op) {
  static constexpr MTL::BlendOperation ops[NGF_BLEND_OP_COUNT] = {
      MTL::BlendOperationAdd,
      MTL::BlendOperationSubtract,
      MTL::BlendOperationReverseSubtract,
      MTL::BlendOperationMin,
      MTL::BlendOperationMax};
  return ops[op];
}

struct mtl_format {
  const MTL::PixelFormat format         = MTL::PixelFormatInvalid;
  const uint8_t          bits_per_block = 0;
  const bool             srgb           = false;
  const uint8_t          block_width    = 1;
  const uint8_t          block_height   = 1;
};

static mtl_format get_mtl_pixel_format(ngf_image_format f) {
  static const mtl_format formats[NGF_IMAGE_FORMAT_COUNT] = {
    {MTL::PixelFormatR8Unorm, 8},
    {MTL::PixelFormatRG8Unorm, 16},
    {MTL::PixelFormatRG8Snorm, 16},
    {},  // RGB8, unsupported
    {MTL::PixelFormatRGBA8Unorm, 32},
    {},  // SRGB8, unsupported
    {MTL::PixelFormatRGBA8Unorm_sRGB, 32, true},
    {},  // BGR8, unsupported
    {MTL::PixelFormatBGRA8Unorm, 32},
    {},  // BGR8_SRGB, unsupported
    {MTL::PixelFormatBGRA8Unorm_sRGB, 32, true},
    {MTL::PixelFormatRGB10A2Unorm, 32},
    {MTL::PixelFormatR32Float, 32},
    {MTL::PixelFormatRG32Float, 64},
    {},  // RGB32F, unsupported
    {MTL::PixelFormatRGBA32Float, 128},
    {MTL::PixelFormatR16Float, 16},
    {MTL::PixelFormatRG16Float, 32},
    {},  // RGB16F, unsupported
    {MTL::PixelFormatRGBA16Float, 64},
    {MTL::PixelFormatRG11B10Float, 32},
    {MTL::PixelFormatRGB9E5Float, 32},
    {MTL::PixelFormatR16Unorm, 16},
    {MTL::PixelFormatR16Snorm, 16},
    {MTL::PixelFormatRG16Unorm, 32},
    {MTL::PixelFormatRG16Snorm, 32},
    {MTL::PixelFormatRGBA16Unorm, 64},
    {MTL::PixelFormatRGBA16Snorm, 64},
    {MTL::PixelFormatR8Uint, 8},
    {MTL::PixelFormatR8Sint, 8},
    {MTL::PixelFormatR16Uint, 16},
    {MTL::PixelFormatR16Sint, 16},
    {MTL::PixelFormatRG16Uint, 32},
    {},  // RGB16U, unsupported
    {MTL::PixelFormatRGBA16Uint, 64},
    {MTL::PixelFormatR32Uint, 32},
    {MTL::PixelFormatRG32Uint, 64},
    {},  // RGB32U, unsupported
    {MTL::PixelFormatRGBA32Uint, 128},
#if TARGET_OS_OSX
    {MTL::PixelFormatBC7_RGBAUnorm, 128, false, 4, 4},
    {MTL::PixelFormatBC7_RGBAUnorm_sRGB, 128, true, 4, 4},
    {MTL::PixelFormatBC6H_RGBFloat, 128, false, 4, 4},
    {MTL::PixelFormatBC6H_RGBUfloat, 128, false, 4, 4},
    {MTL::PixelFormatBC5_RGUnorm, 128, false, 4, 4},
    {MTL::PixelFormatBC5_RGSnorm, 128, false, 4, 4},
#else
    // BCn formats unsupported un iOS until 16.4
    {},
    {},
    {},
    {},
    {},
    {},
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
    {MTL::PixelFormatASTC_4x4_LDR, 128, false, 4, 4},
    {MTL::PixelFormatASTC_4x4_sRGB, 128, true, 4, 4},
    {MTL::PixelFormatASTC_5x4_LDR, 128, false, 5, 4},
    {MTL::PixelFormatASTC_5x4_sRGB, 128, true, 5, 4},
    {MTL::PixelFormatASTC_5x5_LDR, 128, false, 5, 5},
    {MTL::PixelFormatASTC_5x5_sRGB, 128, true, 5, 5},
    {MTL::PixelFormatASTC_6x5_LDR, 128, false, 6, 5},
    {MTL::PixelFormatASTC_6x5_sRGB, 128, true, 6, 5},
    {MTL::PixelFormatASTC_6x6_LDR, 128, false, 6, 6},
    {MTL::PixelFormatASTC_6x6_sRGB, 128, true, 6, 6},
    {MTL::PixelFormatASTC_8x5_LDR, 128, false, 8, 5},
    {MTL::PixelFormatASTC_8x5_sRGB, 128, true, 8, 5},
    {MTL::PixelFormatASTC_8x6_LDR, 128, false, 8, 6},
    {MTL::PixelFormatASTC_8x6_sRGB, 128, true, 8, 6},
    {MTL::PixelFormatASTC_8x8_LDR, 128, false, 8, 8},
    {MTL::PixelFormatASTC_8x8_sRGB, 128, true, 8, 8},
    {MTL::PixelFormatASTC_10x5_LDR, 128, false, 10, 5},
    {MTL::PixelFormatASTC_10x5_sRGB, 128, true, 10, 5},
    {MTL::PixelFormatASTC_10x6_LDR, 128, false, 10, 6},
    {MTL::PixelFormatASTC_10x6_sRGB, 128, true, 10, 6},
    {MTL::PixelFormatASTC_10x8_LDR, 128, false, 10, 8},
    {MTL::PixelFormatASTC_10x8_sRGB, 128, true, 10, 8},
    {MTL::PixelFormatASTC_10x10_LDR, 128, false, 10, 10},
    {MTL::PixelFormatASTC_10x10_sRGB, 128, true, 10, 10},
    {MTL::PixelFormatASTC_12x10_LDR, 128, false, 12, 10},
    {MTL::PixelFormatASTC_12x10_sRGB, 128, true, 12, 10},
    {MTL::PixelFormatASTC_12x12_LDR, 128, false, 12, 12},
    {MTL::PixelFormatASTC_12x12_sRGB, 128, true, 12, 12},
#endif
    {MTL::PixelFormatDepth32Float, 32},
#if TARGET_OS_OSX
    {MTL::PixelFormatDepth16Unorm, 16},
    {MTL::PixelFormatDepth32Float_Stencil8, 32},  // instead of 24Unorm_Stencil8,
                                                  // because metal validator doesn't
                                                  // like it for some reason...
#else
    {},                                         // DEPTH16, iOS does not support.
    {MTL::PixelFormatDepth32Float_Stencil8, 32},  // Emulate DEPTH24_STENCIL8 on iOS
#endif
    {}
  };
  return formats[f];
}

static MTL::LoadAction get_mtl_load_action(ngf_attachment_load_op op) {
  static const MTL::LoadAction action[NGF_LOAD_OP_COUNT] = {
      MTL::LoadActionDontCare,
      MTL::LoadActionLoad,
      MTL::LoadActionClear};
  return action[op];
}

static MTL::StoreAction get_mtl_store_action(ngf_attachment_store_op op) {
  static const MTL::StoreAction action[NGF_STORE_OP_COUNT] = {
      MTL::StoreActionDontCare,
      MTL::StoreActionStore,
      MTL::StoreActionMultisampleResolve};
  return action[op];
}

static MTL::DataType get_mtl_type(ngf_type type) {
  static const MTL::DataType types[NGF_TYPE_COUNT] = {
      MTL::DataTypeNone, /* Int8, Metal does not support.*/
      MTL::DataTypeNone, /*UInt8, Metal does not support*/
      MTL::DataTypeShort,
      MTL::DataTypeUShort,
      MTL::DataTypeInt,
      MTL::DataTypeUInt,
      MTL::DataTypeFloat,
      MTL::DataTypeHalf,
      MTL::DataTypeNone /* Double,Metal does not support.*/
  };
  return types[type];
}

static MTL::VertexFormat get_mtl_attrib_format(ngf_type type, uint32_t size, bool normalized) {
  static const MTL::VertexFormat formats[NGF_TYPE_COUNT][2][4] = {
      {{MTL::VertexFormatChar,
        MTL::VertexFormatChar2,
        MTL::VertexFormatChar3,
        MTL::VertexFormatChar4},
       {MTL::VertexFormatCharNormalized,
        MTL::VertexFormatChar2Normalized,
        MTL::VertexFormatChar3Normalized,
        MTL::VertexFormatChar4Normalized}},
      {{MTL::VertexFormatUChar,
        MTL::VertexFormatUChar2,
        MTL::VertexFormatUChar3,
        MTL::VertexFormatUChar4},
       {MTL::VertexFormatUCharNormalized,
        MTL::VertexFormatUChar2Normalized,
        MTL::VertexFormatUChar3Normalized,
        MTL::VertexFormatUChar4Normalized}},
      {{MTL::VertexFormatShort,
        MTL::VertexFormatShort2,
        MTL::VertexFormatShort3,
        MTL::VertexFormatShort4},
       {MTL::VertexFormatShortNormalized,
        MTL::VertexFormatShort2Normalized,
        MTL::VertexFormatShort3Normalized,
        MTL::VertexFormatShort4Normalized}},
      {{MTL::VertexFormatUShort,
        MTL::VertexFormatUShort2,
        MTL::VertexFormatUShort3,
        MTL::VertexFormatUShort4},
       {MTL::VertexFormatUShortNormalized,
        MTL::VertexFormatUShort2Normalized,
        MTL::VertexFormatUShort3Normalized,
        MTL::VertexFormatUShort4Normalized}},
      {{MTL::VertexFormatInt, MTL::VertexFormatInt2, MTL::VertexFormatInt3, MTL::VertexFormatInt4},
       {MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid}},
      {{MTL::VertexFormatUInt,
        MTL::VertexFormatUInt2,
        MTL::VertexFormatUInt3,
        MTL::VertexFormatUInt4},
       {MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid}},
      {{MTL::VertexFormatFloat,
        MTL::VertexFormatFloat2,
        MTL::VertexFormatFloat3,
        MTL::VertexFormatFloat4},
       {MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid}},
      {{MTL::VertexFormatHalf,
        MTL::VertexFormatHalf2,
        MTL::VertexFormatHalf3,
        MTL::VertexFormatHalf4},
       {MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid}},
      {{MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid,  // Double, Metal does not support.
        MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid},
       {MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid,
        MTL::VertexFormatInvalid}}};
  assert(size <= 4u && size > 0u);
  return formats[type][normalized ? 1 : 0][size - 1u];
}

static MTL::VertexStepFunction get_mtl_step_function(ngf_vertex_input_rate rate) {
  static const MTL::VertexStepFunction funcs[NGF_VERTEX_INPUT_RATE_COUNT] = {
      MTL::VertexStepFunctionPerVertex,
      MTL::VertexStepFunctionPerInstance};
  return funcs[rate];
}

static MTL::PrimitiveTopologyClass get_mtl_primitive_topology_class(ngf_primitive_topology t) {
  static const MTL::PrimitiveTopologyClass topo_class[NGF_PRIMITIVE_TOPOLOGY_COUNT] = {
      MTL::PrimitiveTopologyClassTriangle,
      MTL::PrimitiveTopologyClassTriangle,
      MTL::PrimitiveTopologyClassLine,
      MTL::PrimitiveTopologyClassLine,
  };
  return topo_class[t];
}

static MTL::PrimitiveType get_mtl_primitive_type(ngf_primitive_topology type) {
  static const MTL::PrimitiveType types[NGF_PRIMITIVE_TOPOLOGY_COUNT] = {
      MTL::PrimitiveTypeTriangle,
      MTL::PrimitiveTypeTriangleStrip,
      MTL::PrimitiveTypeLine,
      MTL::PrimitiveTypeLineStrip};
  return types[type];
}

static MTL::IndexType get_mtl_index_type(ngf_type type) {
  assert(type == NGF_TYPE_UINT16 || type == NGF_TYPE_UINT32);
  return type == NGF_TYPE_UINT16 ? MTL::IndexTypeUInt16 : MTL::IndexTypeUInt32;
}

static MTL::CompareFunction get_mtl_compare_function(ngf_compare_op op) {
  static const MTL::CompareFunction compare_fns[NGF_COMPARE_OP_COUNT] = {
      MTL::CompareFunctionNever,
      MTL::CompareFunctionLess,
      MTL::CompareFunctionLessEqual,
      MTL::CompareFunctionEqual,
      MTL::CompareFunctionGreaterEqual,
      MTL::CompareFunctionGreater,
      MTL::CompareFunctionNotEqual,
      MTL::CompareFunctionAlways};
  return compare_fns[op];
}

static MTL::StencilOperation get_mtl_stencil_op(ngf_stencil_op op) {
  static const MTL::StencilOperation stencil_ops[NGF_STENCIL_OP_COUNT] = {
      MTL::StencilOperationKeep,
      MTL::StencilOperationZero,
      MTL::StencilOperationReplace,
      MTL::StencilOperationIncrementClamp,
      MTL::StencilOperationIncrementWrap,
      MTL::StencilOperationDecrementClamp,
      MTL::StencilOperationDecrementWrap,
      MTL::StencilOperationInvert};
  return stencil_ops[op];
}

static MTL::CullMode get_mtl_culling(ngf_cull_mode c) {
  static const MTL::CullMode cull_modes[NGF_CULL_MODE_COUNT] = {
      MTL::CullModeBack,
      MTL::CullModeFront,
      MTL::CullModeNone, /* Metal has no front + back culling */
      MTL::CullModeNone};
  return cull_modes[c];
}

static MTL::Winding get_mtl_winding(ngf_front_face_mode w) {
  static const MTL::Winding windings[NGF_FRONT_FACE_COUNT] = {
      MTL::WindingCounterClockwise,
      MTL::WindingClockwise};
  return windings[w];
}

static std::optional<MTL::TextureType>
get_mtl_texture_type(ngf_image_type type, uint32_t nlayers, ngf_sample_count sample_count) {
  if (type == NGF_IMAGE_TYPE_IMAGE_2D && nlayers == 1 && sample_count == NGF_SAMPLE_COUNT_1) {
    return MTL::TextureType2D;
  } else if (type == NGF_IMAGE_TYPE_IMAGE_2D && nlayers > 1 && sample_count == NGF_SAMPLE_COUNT_1) {
    return MTL::TextureType2DArray;
  }
  if (type == NGF_IMAGE_TYPE_IMAGE_2D && nlayers == 1 && sample_count != NGF_SAMPLE_COUNT_1) {
    return MTL::TextureType2DMultisample;
  } else if (type == NGF_IMAGE_TYPE_IMAGE_2D && nlayers > 1 && sample_count != NGF_SAMPLE_COUNT_1) {
    if (__builtin_available(iOS 14.0, *)) return MTL::TextureType2DMultisampleArray;
  } else if (type == NGF_IMAGE_TYPE_IMAGE_3D) {
    return MTL::TextureType3D;
  } else if (type == NGF_IMAGE_TYPE_CUBE && nlayers == 1) {
    return MTL::TextureTypeCube;
  } else if (type == NGF_IMAGE_TYPE_CUBE && nlayers > 1) {
    return MTL::TextureTypeCubeArray;
  }
  return std::nullopt;
}

static std::optional<MTL::SamplerAddressMode> get_mtl_address_mode(ngf_sampler_wrap_mode mode) {
  static const std::optional<MTL::SamplerAddressMode> modes[NGF_WRAP_MODE_COUNT] = {
      MTL::SamplerAddressModeClampToEdge,
      MTL::SamplerAddressModeRepeat,
      MTL::SamplerAddressModeMirrorRepeat};
  return modes[mode];
}

static MTL::SamplerMinMagFilter get_mtl_minmag_filter(ngf_sampler_filter f) {
  static MTL::SamplerMinMagFilter filters[NGF_FILTER_COUNT] = {
      MTL::SamplerMinMagFilterNearest,
      MTL::SamplerMinMagFilterLinear};
  return filters[f];
}

static MTL::SamplerMipFilter get_mtl_mip_filter(ngf_sampler_filter f) {
  static MTL::SamplerMipFilter filters[NGF_FILTER_COUNT] = {
      MTL::SamplerMipFilterNearest,
      MTL::SamplerMipFilterLinear};
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

enum ngf_id_init_types { id_default };

// Shared pointer for managed objects
template<typename T> class ngf_id {
  public:
  // Create an ngf_id with an additional retain count
  // Useful for keeping AutoReleasePool managed objects alive beyond
  // Their pool lifetime
  static ngf_id add_retain(T* ptr) {
    ngf_id res = ptr;
    if (res) { res->retain(); }
    return res;
  }

  ngf_id() : ptr_(nullptr) {
  }
  ngf_id(const ngf_id_init_types& type) : ptr_(T::alloc()->init()) {
  }
  // Note: Does NOT increment ref count. You can use this directly after calling
  // alloc()->init()
  ngf_id(T* starting_ptr) : ptr_(starting_ptr) {
  }
  ~ngf_id() {
    destroy_if_necessary();
  }

  ngf_id(const ngf_id&)            = delete;
  ngf_id& operator=(const ngf_id&) = delete;
  ngf_id(ngf_id&& other) : ptr_(nullptr) {
    *this = std::move(other);
  }
  ngf_id& operator=(ngf_id&& other) {
    destroy_if_necessary();
    ptr_       = other.ptr_;
    other.ptr_ = nullptr;
    return *this;
  }

  T* get() const {
    return ptr_;
  }
  T* operator->() const {
    return ptr_;
  }
  operator bool() const {
    return ptr_ != nullptr;
  }

  private:
  void destroy_if_necessary() {
    if (ptr_) { ptr_->release(); }
  }

  T* ptr_;
};

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
    if (attachment_descs.descs) {
      if (nresolve_attachments > 0u) { NGFI_FREEN(resolve_image_refs, nresolve_attachments); }
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
  NS::UInteger                width;
  NS::UInteger                height;
};

struct ngf_cmd_buffer_t {
  ngfi_cmd_buffer_state       state                     = NGFI_CMD_BUFFER_NEW;
  bool                        renderpass_active         = false;
  MTL::CommandBuffer*         mtl_cmd_buffer            = nullptr;
  MTL::RenderCommandEncoder*  active_rce                = nullptr;
  MTL::BlitCommandEncoder*    active_bce                = nullptr;
  MTL::ComputeCommandEncoder* active_cce                = nullptr;
  ngf_graphics_pipeline       active_gfx_pipe           = nullptr;
  ngf_compute_pipeline        active_compute_pipe       = nullptr;
  ngf_render_target           active_rt                 = nullptr;
  ngf_id<MTL::Buffer>         bound_index_buffer        = nullptr;
  MTL::IndexType              bound_index_buffer_type   = MTL::IndexTypeUInt16;
  size_t                      bound_index_buffer_offset = 0u;

  ngf_id<MTL::RenderPassSampleBufferAttachmentDescriptor>  sample_buf_attachment_for_next_render_pass  = nullptr;
  ngf_id<MTL::ComputePassSampleBufferAttachmentDescriptor> sample_buf_attachment_for_next_compute_pass = nullptr;
};
#define NGFMTL_ENC2CMDBUF(enc) ((ngf_cmd_buffer)((void*)enc.pvt_data_donotuse.d0))

struct ngfmtl_niceshade_metadata {
  std::vector<std::vector<uint32_t>> native_binding_map;
  uint32_t                           threadgroup_size[3];
};

struct ngf_shader_stage_t {
  ngf_id<MTL::Library> func_lib = nullptr;
  ngf_stage_type       type;
  std::string          entry_point_name;
  std::string          source_code;
};

struct ngf_graphics_pipeline_t {
  ngf_id<MTL::RenderPipelineState>    pipeline           = nullptr;
  ngf_id<MTL::DepthStencilState>      depth_stencil      = nullptr;
  ngf_id<MTL::DepthStencilDescriptor> depth_stencil_desc = nullptr;

  uint32_t front_stencil_reference = 0u;
  uint32_t back_stencil_reference  = 0u;

  MTL::PrimitiveType primitive_type = MTL::PrimitiveTypeTriangle;
  MTL::Winding       winding        = MTL::WindingCounterClockwise;
  MTL::CullMode      culling        = MTL::CullModeBack;
  float              blend_color[4] {0};

  ngfmtl_niceshade_metadata niceshade_metadata;
};

struct ngf_compute_pipeline_t {
  ngf_id<MTL::ComputePipelineState> pipeline = nullptr;
  ngfmtl_niceshade_metadata         niceshade_metadata;
};

struct ngf_buffer_t {
  ngf_id<MTL::Buffer> mtl_buffer    = nullptr;
  size_t              mapped_offset = 0;
};

struct ngf_texel_buffer_view_t {
  ngf_id<MTL::Texture> mtl_buffer_view = nullptr;
};

struct ngf_sampler_t {
  ngf_id<MTL::SamplerState> sampler = nullptr;
};

struct ngf_image_t {
  ngf_id<MTL::Texture> texture = nullptr;

  // Workaround for binding srgb images as writeable storage images.
  ngf_id<MTL::Texture> non_srgb_view = nullptr;

  ngf_image_format     format;
  uint32_t             usage_flags = 0u;
};

struct ngf_image_view_t {
  ngf_id<MTL::Texture> view = nullptr;
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

CA::MetalLayer* ngf_layer_add_to_view(
    MTL::Device*     device,
    uint32_t         width,
    uint32_t         height,
    MTL::PixelFormat pixel_format,
    ngf_colorspace   colorspace,
    uint32_t         capacity_hint,
    bool             display_sync_enabled,
    bool             compute_access_enabled,
    uintptr_t        native_handle);

CA::MetalDrawable* ngf_layer_next_drawable(CA::MetalLayer* layer);

void ngf_resize_swapchain(
    CA::MetalLayer* layer,
    uint32_t        width,
    uint32_t        height,
    uintptr_t       native_handle);

// Manages the final presentation surfaces.
class ngfmtl_swapchain {
  public:
  struct frame {
    MTL::Texture* color_attachment_texture() {
      return multisample_texture ? multisample_texture : color_drawable->texture();
    }

    MTL::Texture* resolve_attachment_texture() {
      return multisample_texture ? color_drawable->texture() : nullptr;
    }

    MTL::Texture* depth_attachment_texture() {
      return depth_texture;
    }
    

    CA::MetalDrawable* color_drawable      = nullptr;
    MTL::Texture*      depth_texture       = nullptr;
    MTL::Texture*      multisample_texture = nullptr;
    ngf_image_t        img_wrapper;
  };

  ngfmtl_swapchain() = default;
  ngfmtl_swapchain(ngfmtl_swapchain&& other) {
    *this = std::move(other);
  }
  ngfmtl_swapchain& operator=(ngfmtl_swapchain&& other) {
    layer_        = other.layer_;
    other.layer_  = nullptr;
    depth_images_ = std::move(other.depth_images_);
    capacity_     = other.capacity_;
    img_idx_      = other.img_idx_;

    return *this;
  }

  // Delete copy ctor and copy assignment to make this type move-only.
  ngfmtl_swapchain& operator=(const ngfmtl_swapchain&) = delete;
  ngfmtl_swapchain(const ngfmtl_swapchain&)            = delete;

  ngf_error initialize(const ngf_swapchain_info& swapchain_info, MTL::Device* device) noexcept {
    // Initialize the Metal layer.
    pixel_format_ = get_mtl_pixel_format(swapchain_info.color_format).format;
    if (pixel_format_ == MTL::PixelFormatInvalid) {
      NGFI_DIAG_ERROR("Image format not supported by Metal backend");
      return NGF_ERROR_INVALID_FORMAT;
    }

    layer_ = ngf_layer_add_to_view(
        device,
        swapchain_info.width,
        swapchain_info.height,
        pixel_format_,
        swapchain_info.colorspace,
        swapchain_info.capacity_hint,
        (swapchain_info.present_mode == NGF_PRESENTATION_MODE_FIFO),
        swapchain_info.enable_compute_access,
        swapchain_info.native_handle);

    // Remember the number of images in the swapchain.
    capacity_ = swapchain_info.capacity_hint;

    // Initialize depth attachments if necessary.
    initialize_depth_attachments(swapchain_info);
    initialize_multisample_images(swapchain_info);
    
    compute_access_enabled_ = swapchain_info.enable_compute_access;

    return NGF_ERROR_OK;
  }
  
  bool compute_access_enabled() const noexcept {
    return compute_access_enabled_;
  }

  ngf_error resize(const ngf_swapchain_info& swapchain_info) {
    ngf_resize_swapchain(
        layer_,
        swapchain_info.width,
        swapchain_info.height,
        swapchain_info.native_handle);

    // ReiInitialize depth attachments & multisample images if necessary.
    initialize_depth_attachments(swapchain_info);
    initialize_multisample_images(swapchain_info);

    return NGF_ERROR_OK;
  }

  frame next_frame() {
    img_idx_ = (img_idx_ + 1u) % capacity_;
    return {
        ngf_layer_next_drawable(layer_),
        depth_images_.size() > 0 ? depth_images_[img_idx_].get() : nullptr,
        is_multisampled() ? multisample_images_[img_idx_]->texture.get() : nullptr};
  }
  
  MTL::PixelFormat get_pixel_format() const { return pixel_format_; }

  operator bool() {
    return layer_;
  }

  bool is_multisampled() const {
    return !multisample_images_.empty();
  }

  private:
  void initialize_depth_attachments(const ngf_swapchain_info& swapchain_info) {
    destroy_depth_attachments();
    if (swapchain_info.depth_format != NGF_IMAGE_FORMAT_UNDEFINED) {
      depth_images_.resize(swapchain_info.capacity_hint);
      MTL::PixelFormat depth_format = get_mtl_pixel_format(swapchain_info.depth_format).format;
      // assert(depth_format != MTL::PixelFormatInvalid);
      for (uint32_t i = 0u; i < swapchain_info.capacity_hint; ++i) {
        ngf_id<MTL::TextureDescriptor> depth_texture_desc = id_default;
        depth_texture_desc->setTextureType(
            swapchain_info.sample_count > 1u ? MTL::TextureType2DMultisample : MTL::TextureType2D);
        depth_texture_desc->setWidth(swapchain_info.width);
        depth_texture_desc->setHeight(swapchain_info.height);
        depth_texture_desc->setPixelFormat(depth_format);
        depth_texture_desc->setDepth(1u);
        depth_texture_desc->setSampleCount((NS::UInteger)swapchain_info.sample_count);
        depth_texture_desc->setMipmapLevelCount(1u);
        depth_texture_desc->setArrayLength(1u);
        depth_texture_desc->setUsage(MTL::TextureUsageRenderTarget);
        depth_texture_desc->setStorageMode(MTL::StorageModePrivate);
        depth_texture_desc->setResourceOptions(MTL::ResourceStorageModePrivate);
        if (__builtin_available(macOS 10.14, *)) {
          depth_texture_desc->setAllowGPUOptimizedContents(true);
        }
        depth_images_[i] = MTL_DEVICE->newTexture(depth_texture_desc.get());
      }
    }
  }

  void destroy_depth_attachments() {
    depth_images_.resize(0);
  }

  void initialize_multisample_images(const ngf_swapchain_info& swapchain_info) {
    destroy_multisample_images();
    if (swapchain_info.sample_count > NGF_SAMPLE_COUNT_1) {
      multisample_images_.resize(capacity_, nullptr);
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
    assert(multisample_images_.empty() || capacity_ == multisample_images_.size());
    for (size_t i = 0; i < multisample_images_.size(); ++i) {
      ngf_destroy_image(multisample_images_[i]);
    }
    multisample_images_.resize(0);
  }

  CA::MetalLayer*                   layer_    = nullptr;
  uint32_t                          img_idx_  = 0u;
  uint32_t                          capacity_ = 0u;
  std::vector<ngf_id<MTL::Texture>> depth_images_;
  std::vector<ngf_image>            multisample_images_;
  MTL::PixelFormat                  pixel_format_;
  bool                              compute_access_enabled_;
};

struct ngf_context_t {
  ~ngf_context_t() {
    if (last_cmd_buffer) { last_cmd_buffer->waitUntilCompleted(); }
  }
  ngf_id<MTL::Device>        device = nullptr;
  ngfmtl_swapchain           swapchain;
  ngfmtl_swapchain::frame    frame;
  ngf_id<MTL::CommandQueue>  queue      = nullptr;
  bool                       is_current = false;
  ngf_swapchain_info         swapchain_info;
  MTL::CommandBuffer*        pending_cmd_buffer = nullptr;
  ngf_id<MTL::CommandBuffer> last_cmd_buffer    = nullptr;
  dispatch_semaphore_t       frame_sync_sem     = nullptr;
  ngf_render_target          default_rt;
};

constexpr MTL::GPUFamily NGFMTL_GPU_FAMILIES[] = {
    MTL::GPUFamilyCommon1,
    MTL::GPUFamilyCommon2,
    MTL::GPUFamilyApple1,
    MTL::GPUFamilyApple2,
    MTL::GPUFamilyApple3,
    MTL::GPUFamilyApple4,
    MTL::GPUFamilyApple5,
    MTL::GPUFamilyApple6,
    MTL::GPUFamilyApple7,
    MTL::GPUFamilyMac2,
    MTL::GPUFamilyMac2,
    MTL::GPUFamilyMac2,
    MTL::GPUFamilyMac2,
};

constexpr size_t NGFMTL_NUM_GPU_FAMILIES = sizeof(NGFMTL_GPU_FAMILIES) / sizeof(MTL::GPUFamily);

static constexpr size_t ngfmtl_gpufam_idx(MTL::GPUFamily fam) {
  for (size_t i = 0; i < NGFMTL_NUM_GPU_FAMILIES; ++i)
    if (NGFMTL_GPU_FAMILIES[i] == fam) return i;
  return 0;
}

static size_t ngfmtl_max_supported_gpu_family(MTL::Device* mtldev) {
  for (size_t fam_idx = NGFMTL_NUM_GPU_FAMILIES - 1; fam_idx >= 0; --fam_idx)
    if (mtldev->supportsFamily(NGFMTL_GPU_FAMILIES[fam_idx])) return fam_idx;
  return 0;
}

extern "C" {
void             ngfi_set_allocation_callbacks(const ngf_allocation_callbacks* callbacks);
ngf_sample_count ngfi_get_highest_sample_count(size_t counts_bitmap);
}

static void ngfmtl_populate_ngf_device(uint32_t handle, ngf_device& ngfdev, MTL::Device* mtldev) {
  ngfdev.handle = handle;
#if TARGET_OS_OSX
  ngfdev.performance_tier =
      mtldev->lowPower() ? NGF_DEVICE_PERFORMANCE_TIER_LOW : NGF_DEVICE_PERFORMANCE_TIER_HIGH;
#else
  ngfdev.performance_tier = NGF_DEVICE_PERFORMANCE_TIER_UNKNOWN;
#endif
  const size_t device_name_length = mtldev->name()->length();
  strncpy(
      ngfdev.name,
      mtldev->name()->utf8String(),
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
  caps.device_local_memory_is_host_visible      = mtldev->hasUnifiedMemory();

  if (gpu_family_idx >= ngfmtl_gpufam_idx(MTL::GPUFamilyApple6)) {
    caps.max_sampled_images_per_stage = 128;
  } else if (gpu_family_idx >= ngfmtl_gpufam_idx(MTL::GPUFamilyApple4)) {
    caps.max_sampled_images_per_stage = 96;
  } else {
    caps.max_sampled_images_per_stage = 31;
  }

  if (gpu_family_idx >= ngfmtl_gpufam_idx(MTL::GPUFamilyApple4)) {
    caps.max_fragment_input_components = 124;
  } else {
    caps.max_fragment_input_components = 60;
  }
  if (gpu_family_idx >= ngfmtl_gpufam_idx(MTL::GPUFamilyApple4)) {
    caps.max_fragment_inputs = 124;
  } else {
    caps.max_fragment_inputs = 60;
  }

  if (gpu_family_idx >= ngfmtl_gpufam_idx(MTL::GPUFamilyMac2)) {
    caps.uniform_buffer_offset_alignment = 32;
  } else {
    caps.uniform_buffer_offset_alignment = 4;
  }
  caps.storage_buffer_offset_alignment = 64;
  caps.texel_buffer_offset_alignment   = 64;

  if (gpu_family_idx >= ngfmtl_gpufam_idx(MTL::GPUFamilyApple3)) {
    caps.max_1d_image_dimension   = 16384;
    caps.max_2d_image_dimension   = 16384;
    caps.max_cube_image_dimension = 16384;
  } else {
    caps.max_1d_image_dimension   = 8192;
    caps.max_2d_image_dimension   = 8192;
    caps.max_cube_image_dimension = 8192;
  }

  if (gpu_family_idx >= ngfmtl_gpufam_idx(MTL::GPUFamilyApple2)) {
    caps.max_color_attachments_per_pass = 8;
  } else {
    caps.max_color_attachments_per_pass = 4;
  }

  caps.cubemap_arrays_supported = gpu_family_idx == ngfmtl_gpufam_idx(MTL::GPUFamilyCommon2) ||
                                  gpu_family_idx == ngfmtl_gpufam_idx(MTL::GPUFamilyCommon3) ||
                                  gpu_family_idx >= ngfmtl_gpufam_idx(MTL::GPUFamilyApple3);

  size_t supports_samples_bitmap = (mtldev->supportsTextureSampleCount(1) ? 1 : 0) |
                                   (mtldev->supportsTextureSampleCount(2) ? 2 : 0) |
                                   (mtldev->supportsTextureSampleCount(4) ? 4 : 0) |
                                   (mtldev->supportsTextureSampleCount(8) ? 8 : 0);

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

std::vector<ngf_device> NGFMTL_DEVICES_LIST;
const NS::Array*        NGFMTL_MTL_DEVICES;

#define NGFMTL_NURSERY(type, name, ...)                           \
  ngfmtl_object_nursery<ngf_##type##_t, ngf_destroy_##type> name( \
      NGFI_ALLOC(ngf_##type##_t),                                 \
      ##__VA_ARGS__);

#pragma mark ngf_function_implementations

ngf_error ngf_get_device_list(const ngf_device** devices, uint32_t* ndevices) {
  if (NGFMTL_DEVICES_LIST.empty()) {
#if TARGET_OS_OSX
    NGFMTL_MTL_DEVICES = MTL::CopyAllDevices();
    NGFMTL_DEVICES_LIST.resize(NGFMTL_MTL_DEVICES->count());
    for (uint32_t d = 0u; d < NGFMTL_MTL_DEVICES->count(); ++d) {
      ngfmtl_populate_ngf_device(
          d,
          NGFMTL_DEVICES_LIST[d],
          static_cast<MTL::Device*>(NGFMTL_MTL_DEVICES->object(d)));
    }
#else
    NGFMTL_MTL_DEVICES = NS::Array::array(MTLCreateSystemDefaultDevice());
    NGFMTL_DEVICES_LIST.resize(1);
    ngfmtl_populate_ngf_device(0, NGFMTL_DEVICES_LIST[0], (MTL::Device*)NGFMTL_MTL_DEVICES->object(0));
#endif
  }
  if (devices) { *devices = NGFMTL_DEVICES_LIST.data(); }
  if (ndevices) { *ndevices = (uint32_t)NGFMTL_DEVICES_LIST.size(); }
  return NGF_ERROR_OK;
}

ngf_error ngf_initialize(const ngf_init_info* init_info) NGF_NOEXCEPT {
  if (MTL_DEVICE != nullptr || init_info->device >= NGFMTL_DEVICES_LIST.size()) {
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

  MTL_DEVICE = static_cast<MTL::Device*>(NGFMTL_MTL_DEVICES->object(init_info->device));

  // Initialize device capabilities.
  DEVICE_CAPS = NGFMTL_DEVICES_LIST[init_info->device].capabilities;

  return (MTL_DEVICE != nullptr) ? NGF_ERROR_OK : NGF_ERROR_INVALID_OPERATION;
}

void ngf_shutdown() NGF_NOEXCEPT {
  NGFI_DIAG_INFO("Shutting down nicegraf.");
}

const ngf_device_capabilities* ngf_get_device_capabilities() NGF_NOEXCEPT {
  return &DEVICE_CAPS;
}

extern "C" {
void* objc_autoreleasePoolPush(void);
void  objc_autoreleasePoolPop(void* pool);
}


ngf_error ngf_begin_frame(ngf_frame_token* token) NGF_NOEXCEPT {
  *token = (uintptr_t)objc_autoreleasePoolPush();
  dispatch_semaphore_wait(CURRENT_CONTEXT->frame_sync_sem, DISPATCH_TIME_FOREVER);
  CURRENT_CONTEXT->frame = CURRENT_CONTEXT->swapchain.next_frame();
  if (CURRENT_CONTEXT->frame.color_drawable && CURRENT_CONTEXT->swapchain.compute_access_enabled()) {
    CURRENT_CONTEXT->frame.img_wrapper.texture = CURRENT_CONTEXT->frame.color_drawable->texture()->newTextureView(CURRENT_CONTEXT->swapchain.get_pixel_format());
  }
  return (!CURRENT_CONTEXT->frame.color_drawable) ? NGF_ERROR_INVALID_OPERATION : NGF_ERROR_OK;
}

ngf_error ngf_end_frame(ngf_frame_token token) NGF_NOEXCEPT {
  ngf_context ctx = CURRENT_CONTEXT;
  if (CURRENT_CONTEXT->frame.color_drawable && CURRENT_CONTEXT->pending_cmd_buffer) {
    CURRENT_CONTEXT->pending_cmd_buffer->addCompletedHandler(
        [ctx](MTL::CommandBuffer*) { dispatch_semaphore_signal(ctx->frame_sync_sem); });
    CURRENT_CONTEXT->pending_cmd_buffer->presentDrawable(CURRENT_CONTEXT->frame.color_drawable);
    CURRENT_CONTEXT->last_cmd_buffer =
        ngf_id<MTL::CommandBuffer>::add_retain(CURRENT_CONTEXT->pending_cmd_buffer);
    CURRENT_CONTEXT->pending_cmd_buffer->commit();
    CURRENT_CONTEXT->pending_cmd_buffer = nullptr;
    CURRENT_CONTEXT->frame              = ngfmtl_swapchain::frame {};
  } else {
    dispatch_semaphore_signal(ctx->frame_sync_sem);
  }
  objc_autoreleasePoolPop((void*)token);
  return NGF_ERROR_OK;
}


ngf_error ngf_get_current_swapchain_image(ngf_frame_token token, ngf_image* result) NGF_NOEXCEPT {
  assert(CURRENT_CONTEXT);
  *result = &CURRENT_CONTEXT->frame.img_wrapper;
  return NGF_ERROR_OK;
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
    ctx->queue = ngf_id<MTL::CommandQueue>::add_retain(info->shared_context->queue.get());
  } else {
    ctx->queue = ctx->device->newCommandQueue();
  }

  if (info->swapchain_info) {
    ctx->swapchain_info = *(info->swapchain_info);
    ngf_error err       = ctx->swapchain.initialize(ctx->swapchain_info, ctx->device.get());
    if (err != NGF_ERROR_OK) return err;
    ngf_attachment_descriptions attachment_descs;
    ngf_attachment_description  desc_array[3];
    attachment_descs.descs     = desc_array;
    attachment_descs.ndescs    = 1;
    desc_array[0].format       = ctx->swapchain_info.color_format;
    desc_array[0].type         = NGF_ATTACHMENT_COLOR;
    desc_array[0].sample_count = ctx->swapchain_info.sample_count;
    desc_array[0].is_resolve   = false;
    if (ctx->swapchain_info.depth_format != NGF_IMAGE_FORMAT_UNDEFINED) {
      attachment_descs.ndescs++;
      desc_array[1].format     = ctx->swapchain_info.depth_format;
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
  CURRENT_CONTEXT = ctx;
  ctx->is_current = true;
  return NGF_ERROR_OK;
}

ngf_context ngf_get_context() NGF_NOEXCEPT {
  return CURRENT_CONTEXT;
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
  ngf_id<NS::String> source = NS::String::alloc()->init(
      (void*)info->content,
      info->content_length,
      NS::UTF8StringEncoding,
      false);
  ngf_id<MTL::CompileOptions> opts = id_default;
  NS::Error*                  err  = nullptr;
  stage->func_lib = CURRENT_CONTEXT->device->newLibrary(source.get(), opts.get(), &err);
  if (!stage->func_lib) {
    NGFI_DIAG_ERROR(err->localizedDescription()->utf8String());
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }

  // Set debug name.
  if (info->debug_name != nullptr) {
    stage->func_lib->setLabel(
        ngf_id<NS::String>(NS::String::alloc()->init(info->debug_name, NS::UTF8StringEncoding))
            .get());
  }

  if (info->entry_point_name) {
    stage->entry_point_name = info->entry_point_name;
  } else {
    stage->entry_point_name = stage->func_lib->functionNames()->object<NS::String>(0)->utf8String();
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
    MTL::RenderPassAttachmentDescriptor* attachment,
    uint32_t                             render_image_idx,
    ngf_attachment_type                  type,
    const ngf_render_target              rt,
    ngf_attachment_load_op               load_op,
    ngf_attachment_store_op              store_op) NGF_NOEXCEPT {
  if (!rt->is_default) {
    attachment->setTexture(rt->render_image_refs[render_image_idx].image->texture.get());
    attachment->setLevel(rt->render_image_refs[render_image_idx].mip_level);
    attachment->setSlice(rt->render_image_refs[render_image_idx].layer);
  } else {
    attachment->setTexture(
        type == NGF_ATTACHMENT_COLOR ? CURRENT_CONTEXT->frame.color_attachment_texture()
                                     : CURRENT_CONTEXT->frame.depth_attachment_texture());
    attachment->setLevel(0);
    attachment->setSlice(0);
  }
  attachment->setLoadAction(get_mtl_load_action(load_op));
  attachment->setStoreAction(get_mtl_store_action(store_op));
}

ngf_error ngf_create_render_target(const ngf_render_target_info* info, ngf_render_target* result)
    NGF_NOEXCEPT {
  assert(info);
  assert(result);

  NGFMTL_NURSERY(render_target, rt);
  if (!rt) { return NGF_ERROR_OUT_OF_MEM; }

  ngf_error err = rt->initialize(
      *info->attachment_descriptions,
      info->attachment_image_refs,
      (uint32_t)info->attachment_image_refs[0].image->texture->width(),
      (uint32_t)info->attachment_image_refs[0].image->texture->height());
  if (err != NGF_ERROR_OK) { return err; }

  *result = rt.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_render_target(ngf_render_target rt) NGF_NOEXCEPT {
  if (rt != nullptr) {
    rt->~ngf_render_target_t();
    NGFI_FREE(rt);
  }
}

ngf_id<MTL::Function> ngfmtl_get_shader_main(
    MTL::Library*                func_lib,
    const char*                  entry_point_name,
    MTL::FunctionConstantValues* spec_consts) {
  NS::Error*  err                 = nullptr;
  NS::String* ns_entry_point_name = NS::String::string(entry_point_name, NS::UTF8StringEncoding);
  ngf_id<MTL::Function> result    = func_lib->newFunction(ns_entry_point_name, spec_consts, &err);
  if (err) {
    NGFI_DIAG_ERROR(err->localizedDescription()->utf8String());
    return nullptr;
  } else {
    return result;
  }
}

ngf_id<MTL::StencilDescriptor> ngfmtl_create_stencil_descriptor(const ngf_stencil_info& info) {
  ngf_id<MTL::StencilDescriptor> result = id_default;
  result->setStencilCompareFunction(get_mtl_compare_function(info.compare_op));
  result->setStencilFailureOperation(get_mtl_stencil_op(info.fail_op));
  result->setDepthStencilPassOperation(get_mtl_stencil_op(info.pass_op));
  result->setDepthFailureOperation(get_mtl_stencil_op(info.depth_fail_op));
  result->setWriteMask(info.write_mask);
  result->setReadMask(info.compare_mask);
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
  
  std::vector<ngfmtl_binding_map_entry> tmp_binding_map_entries;
  uint32_t                  consumed_input_bytes;
  uint32_t                  max_set     = 0u;
  uint32_t                  max_binding = 0u;
  ngfmtl_binding_map_entry current_binding_map_entry;
  while (sscanf(
             serialized_binding_map,
             " ( %d %d ) : %d%n",
             &current_binding_map_entry.set,
             &current_binding_map_entry.binding,
             &current_binding_map_entry.native_binding,
             &consumed_input_bytes) == 3 &&
         current_binding_map_entry.set != -1 && current_binding_map_entry.binding != -1 &&
         current_binding_map_entry.native_binding != -1) {
    serialized_binding_map += consumed_input_bytes;
    max_set                   = std::max(max_set, current_binding_map_entry.set);
    max_binding               = std::max(max_binding, current_binding_map_entry.binding);
    tmp_binding_map_entries.emplace_back(current_binding_map_entry);
  }

  std::vector<std::vector<uint32_t>> native_binding_map(max_set + 1, std::vector<uint32_t>(max_binding + 1, ~0u));
  for (uint32_t e = 0u; e < tmp_binding_map_entries.size(); ++e) {
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

ngf_id<MTL::FunctionConstantValues>
ngfmtl_function_consts(const ngf_specialization_info* spec_info) {
  // Populate specialization constant values.
  ngf_id<MTL::FunctionConstantValues> spec_consts = id_default;
  if (spec_info != nullptr) {
    for (uint32_t s = 0u; s < spec_info->nspecializations; ++s) {
      const ngf_constant_specialization* spec = &spec_info->specializations[s];
      MTL::DataType                      type = get_mtl_type(spec->type);
      if (type == MTL::DataTypeNone) { return nullptr; }
      void* write_ptr = ((uint8_t*)spec_info->value_buffer + spec->offset);
      spec_consts->setConstantValue(write_ptr, type, spec->constant_id);
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

  ngf_id<MTL::FunctionConstantValues> func_const_values = ngfmtl_function_consts(info->spec_info);
  ngf_id<MTL::Function>               function          = ngfmtl_get_shader_main(
      info->shader_stage->func_lib.get(),
      info->shader_stage->entry_point_name.c_str(),
      func_const_values.get());
  if (!function) { return NGF_ERROR_OBJECT_CREATION_FAILED; }

  ngf_id<MTL::ComputePipelineDescriptor> mtl_compute_desc = id_default;
  mtl_compute_desc->setComputeFunction(function.get());

  if (info->debug_name != nullptr) {
    mtl_compute_desc->setLabel(NS::String::string(info->debug_name, NS::UTF8StringEncoding));
  }

  NS::Error*                        err = nullptr;
  ngf_id<MTL::ComputePipelineState> computePSO =
      CURRENT_CONTEXT->device->newComputePipelineState(mtl_compute_desc.get(), MTL::PipelineOptionNone, nullptr, &err);

  if (err) {
    NGFI_DIAG_ERROR(err->localizedDescription()->utf8String());
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }
  NGFMTL_NURSERY(compute_pipeline, compute_pipeline);
  compute_pipeline->pipeline           = std::move(computePSO);
  compute_pipeline->niceshade_metadata = std::move(metadata);
  *result                              = compute_pipeline.release();
  return NGF_ERROR_OK;
}

ngf_error ngf_create_graphics_pipeline(
    const ngf_graphics_pipeline_info* info,
    ngf_graphics_pipeline*            result) NGF_NOEXCEPT {
  assert(info);
  assert(result);

  ngf_id<MTL::RenderPipelineDescriptor> mtl_pipe_desc      = id_default;
  const ngf_attachment_descriptions&    attachment_descs   = *info->compatible_rt_attachment_descs;
  uint32_t                              ncolor_attachments = 0u;
  for (uint32_t i = 0u; i < attachment_descs.ndescs; ++i) {
    const ngf_attachment_description& attachment_desc = attachment_descs.descs[i];
    if (attachment_desc.is_resolve) continue;
    if (attachment_desc.type == NGF_ATTACHMENT_COLOR) {
      const ngf_blend_info                          blend = info->color_attachment_blend_states
                                                                ? info->color_attachment_blend_states[ncolor_attachments]
                                                                : ngf_blend_info {};
      MTL::RenderPipelineColorAttachmentDescriptor* mtl_attachment_desc =
          mtl_pipe_desc->colorAttachments()->object(ncolor_attachments++);
      mtl_attachment_desc->setPixelFormat(get_mtl_pixel_format(attachment_desc.format).format);
      mtl_attachment_desc->setBlendingEnabled(blend.enable);
      if (blend.enable) {
        mtl_attachment_desc->setSourceRGBBlendFactor(
            get_mtl_blend_factor(blend.src_color_blend_factor));
        mtl_attachment_desc->setDestinationRGBBlendFactor(
            get_mtl_blend_factor(blend.dst_color_blend_factor));
        mtl_attachment_desc->setSourceAlphaBlendFactor(
            get_mtl_blend_factor(blend.src_alpha_blend_factor));
        mtl_attachment_desc->setDestinationAlphaBlendFactor(
            get_mtl_blend_factor(blend.dst_alpha_blend_factor));
        mtl_attachment_desc->setRgbBlendOperation(get_mtl_blend_operation(blend.blend_op_color));
        mtl_attachment_desc->setAlphaBlendOperation(get_mtl_blend_operation(blend.blend_op_alpha));
      }
      if (info->color_attachment_blend_states) {
        mtl_attachment_desc->setWriteMask(
            (blend.color_write_mask & NGF_COLOR_MASK_WRITE_BIT_R ? MTL::ColorWriteMaskRed : 0) |
            (blend.color_write_mask & NGF_COLOR_MASK_WRITE_BIT_G ? MTL::ColorWriteMaskGreen : 0) |
            (blend.color_write_mask & NGF_COLOR_MASK_WRITE_BIT_B ? MTL::ColorWriteMaskBlue : 0) |
            (blend.color_write_mask & NGF_COLOR_MASK_WRITE_BIT_A ? MTL::ColorWriteMaskAlpha : 0));
      }
    } else if (
        attachment_desc.type == NGF_ATTACHMENT_DEPTH ||
        attachment_desc.type == NGF_ATTACHMENT_DEPTH_STENCIL) {
      mtl_pipe_desc->setDepthAttachmentPixelFormat(
          get_mtl_pixel_format(attachment_desc.format).format);
    }
  }

  mtl_pipe_desc->setRasterSampleCount(info->multisample->sample_count);
  mtl_pipe_desc->setAlphaToCoverageEnabled(info->multisample->alpha_to_coverage);

  mtl_pipe_desc->setStencilAttachmentPixelFormat(MTL::PixelFormatInvalid);

  if (mtl_pipe_desc->depthAttachmentPixelFormat() == MTL::PixelFormatDepth32Float_Stencil8) {
    mtl_pipe_desc->setStencilAttachmentPixelFormat(MTL::PixelFormatDepth32Float_Stencil8);
  }

  // Populate specialization constant values.
  ngf_id<MTL::FunctionConstantValues> spec_consts = ngfmtl_function_consts(info->spec_info);

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
      assert(!mtl_pipe_desc->vertexFunction());
      mtl_pipe_desc->setVertexFunction(ngfmtl_get_shader_main(
                                           stage->func_lib.get(),
                                           stage->entry_point_name.c_str(),
                                           spec_consts.get())
                                           .get());
    } else if (stage->type == NGF_STAGE_FRAGMENT) {
      assert(!mtl_pipe_desc->fragmentFunction());
      mtl_pipe_desc->setFragmentFunction(ngfmtl_get_shader_main(
                                             stage->func_lib.get(),
                                             stage->entry_point_name.c_str(),
                                             spec_consts.get())
                                             .get());
    }
  }
  if (!have_niceshade_metadata) {
    NGFI_DIAG_ERROR("Native binding map not found.");
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }

  // Configure vertex input.
  const ngf_vertex_input_info& vertex_input_info = *info->input_info;
  MTL::VertexDescriptor*       vert_desc         = mtl_pipe_desc->vertexDescriptor();
  for (uint32_t a = 0u; a < vertex_input_info.nattribs; ++a) {
    MTL::VertexAttributeDescriptor* attr_desc = vert_desc->attributes()->object(a);
    const ngf_vertex_attrib_desc&   attr_info = vertex_input_info.attribs[a];
    attr_desc->setOffset(vertex_input_info.attribs[a].offset);
    attr_desc->setBufferIndex(MAX_BUFFER_BINDINGS - vertex_input_info.attribs[a].binding);
    attr_desc->setFormat(
        get_mtl_attrib_format(attr_info.type, attr_info.size, attr_info.normalized));
    if (attr_desc->format() == MTL::VertexFormatInvalid) {
      NGFI_DIAG_ERROR("Vertex attrib format not supported by Metal backend.");
      return NGF_ERROR_INVALID_FORMAT;
    }
  }
  for (uint32_t b = 0u; b < vertex_input_info.nvert_buf_bindings; ++b) {
    MTL::VertexBufferLayoutDescriptor* binding_desc =
        vert_desc->layouts()->object(MAX_BUFFER_BINDINGS - b);
    const ngf_vertex_buf_binding_desc& binding_info = vertex_input_info.vert_buf_bindings[b];
    binding_desc->setStride(binding_info.stride);
    binding_desc->setStepFunction(get_mtl_step_function(binding_info.input_rate));
  }

  // Set primitive topology.
  mtl_pipe_desc->setInputPrimitiveTopology(
      get_mtl_primitive_topology_class(info->input_assembly_info->primitive_topology));
  if (mtl_pipe_desc->inputPrimitiveTopology() == MTL::PrimitiveTopologyClassUnspecified) {
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }

  NGFMTL_NURSERY(graphics_pipeline, pipeline);
  pipeline->niceshade_metadata = std::move(metadata);
  memcpy(pipeline->blend_color, info->blend_consts, sizeof(pipeline->blend_color));

  if (info->debug_name != nullptr) {
    mtl_pipe_desc->setLabel(NS::String::string(info->debug_name, NS::UTF8StringEncoding));
  }

  NS::Error* err     = nullptr;
  pipeline->pipeline = CURRENT_CONTEXT->device->newRenderPipelineState(mtl_pipe_desc.get(), &err);
  pipeline->primitive_type = get_mtl_primitive_type(info->input_assembly_info->primitive_topology);

  // Set winding order and culling mode.
  pipeline->winding = get_mtl_winding(info->rasterization->front_face);
  pipeline->culling = get_mtl_culling(info->rasterization->cull_mode);

  // Set up depth and stencil state.

  pipeline->depth_stencil_desc                     = id_default;
  const ngf_depth_stencil_info& depth_stencil_info = *info->depth_stencil;
  pipeline->depth_stencil_desc->setDepthCompareFunction(
      depth_stencil_info.depth_test ? get_mtl_compare_function(depth_stencil_info.depth_compare)
                                    : MTL::CompareFunctionAlways);
  pipeline->depth_stencil_desc->setDepthWriteEnabled(info->depth_stencil->depth_write);

  ngf_id<MTL::StencilDescriptor> backface_descriptor =
      ngfmtl_create_stencil_descriptor(depth_stencil_info.back_stencil);
  ngf_id<MTL::StencilDescriptor> frontface_descriptor =
      ngfmtl_create_stencil_descriptor(depth_stencil_info.front_stencil);
  pipeline->depth_stencil_desc->setBackFaceStencil(backface_descriptor.get());
  pipeline->depth_stencil_desc->setFrontFaceStencil(frontface_descriptor.get());
  pipeline->front_stencil_reference = depth_stencil_info.front_stencil.reference;
  pipeline->back_stencil_reference  = depth_stencil_info.back_stencil.reference;
  pipeline->depth_stencil =
      CURRENT_CONTEXT->device->newDepthStencilState(pipeline->depth_stencil_desc.get());

  if (err) {
    NGFI_DIAG_ERROR(err->localizedDescription()->utf8String());
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

ngf_id<MTL::Buffer> ngfmtl_create_buffer(const ngf_buffer_info& info) {
  MTL::ResourceOptions options         = 0u;
  switch (info.storage_type) {
  case NGF_BUFFER_STORAGE_HOST_READABLE:
  case NGF_BUFFER_STORAGE_HOST_READABLE_WRITEABLE:
  case NGF_BUFFER_STORAGE_DEVICE_LOCAL_HOST_READABLE_WRITEABLE:
    options = MTL::ResourceCPUCacheModeDefaultCache | MTL::ResourceStorageModeShared;
    break;
  case NGF_BUFFER_STORAGE_DEVICE_LOCAL_HOST_WRITEABLE:
  case NGF_BUFFER_STORAGE_HOST_WRITEABLE:
    options = MTL::ResourceCPUCacheModeWriteCombined | MTL::ResourceStorageModeShared;
    break;
  case NGF_BUFFER_STORAGE_DEVICE_LOCAL:
    options = MTL::ResourceStorageModePrivate;
    break;
  default:
    assert(false);
  }
  return CURRENT_CONTEXT->device->newBuffer(info.size, options);
}

uint8_t* _ngf_map_buffer(MTL::Buffer* buffer, size_t offset, [[maybe_unused]] size_t size) {
  return (uint8_t*)buffer->contents() + offset;
}

ngf_error ngf_create_texel_buffer_view(
    const ngf_texel_buffer_view_info* info,
    ngf_texel_buffer_view*            result) NGF_NOEXCEPT {
  NGFMTL_NURSERY(texel_buffer_view, view);
  ngf_id<MTL::TextureDescriptor> texel_buf_descriptor = id_default;

  texel_buf_descriptor->setDepth(1);
  texel_buf_descriptor->setMipmapLevelCount(1);
  texel_buf_descriptor->setPixelFormat(get_mtl_pixel_format(info->texel_format).format);
  texel_buf_descriptor->setTextureType(MTL::TextureTypeTextureBuffer);
  texel_buf_descriptor->setArrayLength(1);
  texel_buf_descriptor->setSampleCount(1);
  texel_buf_descriptor->setUsage(MTL::TextureUsageShaderRead);
  texel_buf_descriptor->setStorageMode(info->buffer->mtl_buffer->storageMode());
  texel_buf_descriptor->setWidth(info->size / ngfmtl_get_bytesperpel(info->texel_format));
  texel_buf_descriptor->setHeight(1);
  view->mtl_buffer_view =
      info->buffer->mtl_buffer->newTexture(texel_buf_descriptor.get(), info->offset, info->size);
  *result = view.release();
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
  if (info->storage_type > NGF_BUFFER_STORAGE_DEVICE_LOCAL && !DEVICE_CAPS.device_local_memory_is_host_visible) {
    NGFI_DIAG_ERROR("Host-visible device-local memory requested, but not supported.");
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }
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
  return (void*)_ngf_map_buffer(buf->mtl_buffer.get(), offset, size);
}

void ngf_buffer_flush_range(
    [[maybe_unused]] ngf_buffer buf,
    [[maybe_unused]] size_t     offset,
    [[maybe_unused]] size_t     size) NGF_NOEXCEPT {
}

void ngf_buffer_unmap(ngf_buffer) NGF_NOEXCEPT {
}

ngf_error ngf_create_sampler(const ngf_sampler_info* info, ngf_sampler* result) NGF_NOEXCEPT {
  ngf_id<MTL::SamplerDescriptor>         sampler_desc = id_default;
  std::optional<MTL::SamplerAddressMode> s            = get_mtl_address_mode(info->wrap_u),
                                         t            = get_mtl_address_mode(info->wrap_v),
                                         r            = get_mtl_address_mode(info->wrap_w);
  if (!(s && t && r)) { return NGF_ERROR_INVALID_ENUM; }
  sampler_desc->setSAddressMode(*s);
  sampler_desc->setTAddressMode(*t);
  sampler_desc->setRAddressMode(*r);
  sampler_desc->setMinFilter(get_mtl_minmag_filter(info->min_filter));
  sampler_desc->setMagFilter(get_mtl_minmag_filter(info->mag_filter));
  sampler_desc->setMipFilter(get_mtl_mip_filter(info->mip_filter));
  sampler_desc->setMaxAnisotropy(info->enable_anisotropy ? (NS::UInteger)info->max_anisotropy : 1);
  sampler_desc->setLodMinClamp(info->lod_min);
  sampler_desc->setLodMaxClamp(info->lod_max);
  if (info->compare_op != NGF_COMPARE_OP_NEVER) {
    sampler_desc->setCompareFunction(get_mtl_compare_function(info->compare_op));
  }
    
  NGFMTL_NURSERY(sampler, sampler);
  sampler->sampler = CURRENT_CONTEXT->device->newSamplerState(sampler_desc.get());
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

static ngf_sample_count get_ngf_sample_count(NS::UInteger sc) {
  switch(sc) {
    case 0:
    case 1:
      return NGF_SAMPLE_COUNT_1;
      
    case 2:
      return NGF_SAMPLE_COUNT_2;
    case 4:
      return NGF_SAMPLE_COUNT_4;
    case 8:
      return NGF_SAMPLE_COUNT_8;
    case 16:
      return NGF_SAMPLE_COUNT_16;
    case 32:
      return NGF_SAMPLE_COUNT_32;
    case 64:
      return NGF_SAMPLE_COUNT_64;
  }
  return NGF_SAMPLE_COUNT_1;
}

ngf_error ngf_create_image_view(const ngf_image_view_info* info, ngf_image_view* result) NGF_NOEXCEPT {
  const auto maybe_texture_type = get_mtl_texture_type(info->view_type,
                                                       info->nlayers,
                                                       get_ngf_sample_count(info->src_image->texture->sampleCount()));
  if (!maybe_texture_type) { return NGF_ERROR_OBJECT_CREATION_FAILED; }
  MTL::Texture* view = info->src_image->texture->newTextureView(
                                           get_mtl_pixel_format(info->view_format).format,
                                           maybe_texture_type.value(),
                                           NS::Range(info->base_mip_level, info->nmips),
                                           NS::Range(info->base_layer, info->nlayers));
  if (!view) { return NGF_ERROR_OBJECT_CREATION_FAILED; }
  
  NGFMTL_NURSERY(image_view, image_view);
  image_view->view = view;
  *result = image_view.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_image_view(ngf_image_view view) NGF_NOEXCEPT {
  if (view != nullptr) {
    view->~ngf_image_view_t();
    NGFI_FREE(view);
  }
}

ngf_error ngf_create_image(const ngf_image_info* info, ngf_image* result) NGF_NOEXCEPT {
  ngf_id<MTL::TextureDescriptor> mtl_img_desc = id_default;

  const MTL::PixelFormat fmt = get_mtl_pixel_format(info->format).format;
  if (fmt == MTL::PixelFormatInvalid) {
    NGFI_DIAG_ERROR("Image format %d not supported by Metal backend.", info->format);
    return NGF_ERROR_INVALID_FORMAT;
  }

  std::optional<MTL::TextureType> maybe_texture_type =
      get_mtl_texture_type(info->type, info->nlayers, info->sample_count);
  if (!maybe_texture_type.has_value()) {
    NGFI_DIAG_ERROR("Image type %d not supported by Metal backend.", info->type);
    return NGF_ERROR_INVALID_ENUM;
  }
  mtl_img_desc->setTextureType(maybe_texture_type.value());
  mtl_img_desc->setPixelFormat(fmt);
  mtl_img_desc->setWidth(info->extent.width);
  mtl_img_desc->setHeight(info->extent.height);
  mtl_img_desc->setDepth(info->extent.depth);
  mtl_img_desc->setArrayLength(info->nlayers);
  mtl_img_desc->setMipmapLevelCount(info->nmips);
  mtl_img_desc->setStorageMode(MTL::StorageModePrivate);
  mtl_img_desc->setSampleCount(info->sample_count);
  if (info->usage_hint & NGF_IMAGE_USAGE_ATTACHMENT) {
    mtl_img_desc->setUsage(mtl_img_desc->usage() | MTL::TextureUsageRenderTarget);
  }
  if (info->usage_hint & NGF_IMAGE_USAGE_SAMPLE_FROM) {
    mtl_img_desc->setUsage(mtl_img_desc->usage() | MTL::TextureUsageShaderRead);
  }
  if (info->usage_hint & NGF_IMAGE_USAGE_STORAGE) {
    mtl_img_desc->setUsage(mtl_img_desc->usage() | MTL::TextureUsageShaderWrite);
  }
  NGFMTL_NURSERY(image, image);
  image->texture     = MTL_DEVICE->newTexture(mtl_img_desc.get());
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
  cmd_buffer->mtl_cmd_buffer = CURRENT_CONTEXT->queue->commandBuffer();
  assert(!cmd_buffer->active_rce);
  assert(!cmd_buffer->active_bce);
  NGFI_TRANSITION_CMD_BUF(cmd_buffer, NGFI_CMD_BUFFER_READY);
  return NGF_ERROR_OK;
}

ngf_error ngf_submit_cmd_buffers(uint32_t n, ngf_cmd_buffer* cmd_buffers) NGF_NOEXCEPT {
  if (CURRENT_CONTEXT->pending_cmd_buffer) {
    CURRENT_CONTEXT->pending_cmd_buffer->commit();
    CURRENT_CONTEXT->pending_cmd_buffer = nullptr;
  }
  for (uint32_t b = 0u; b < n; ++b) {
    NGFI_TRANSITION_CMD_BUF(cmd_buffers[b], NGFI_CMD_BUFFER_PENDING);
    if (b < n - 1u) {
      cmd_buffers[b]->mtl_cmd_buffer->commit();
    } else {
      CURRENT_CONTEXT->pending_cmd_buffer = cmd_buffers[b]->mtl_cmd_buffer;
    }
    cmd_buffers[b]->mtl_cmd_buffer = nullptr;
    NGFI_TRANSITION_CMD_BUF(cmd_buffers[b], NGFI_CMD_BUFFER_SUBMITTED);
  }
  return NGF_ERROR_OK;
}

void ngfmtl_finish_pending_encoders(ngf_cmd_buffer cmd_buffer) {
  /* End any current Metal encoders.*/
  if (cmd_buffer->active_rce) {
    cmd_buffer->active_rce->endEncoding();
    cmd_buffer->active_rce = nullptr;
  } else if (cmd_buffer->active_bce) {
    cmd_buffer->active_bce->endEncoding();
    cmd_buffer->active_bce = nullptr;
  } else if (cmd_buffer->active_cce) {
    cmd_buffer->active_cce->endEncoding();
    cmd_buffer->active_cce = nullptr;
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
    } else if (rt->attachment_descs.descs[i].type == NGF_ATTACHMENT_DEPTH ||
               rt->attachment_descs.descs[i].type == NGF_ATTACHMENT_DEPTH_STENCIL) {
      clears[i].clear_depth_stencil.clear_depth   = clear_depth;
      clears[i].clear_depth_stencil.clear_stencil = clear_stencil;
    } else {
      assert(false);
    }
    const bool needs_resolve = rt->attachment_descs.descs[i].type == NGF_ATTACHMENT_COLOR &&
                               rt->attachment_descs.descs[i].sample_count > NGF_SAMPLE_COUNT_1 &&
                               (rt->resolve_image_refs || rt->is_default);
    store_ops[i] = (needs_resolve) ? NGF_STORE_OP_RESOLVE : NGF_STORE_OP_STORE;
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

  uint32_t                          color_attachment_idx   = 0u;
  uint32_t                          resolve_attachment_idx = 0u;
  uint32_t                          render_image_idx       = 0u;
  ngf_id<MTL::RenderPassDescriptor> pass_descriptor        = id_default;
  pass_descriptor->setRenderTargetWidth(rt->width);
  pass_descriptor->setRenderTargetHeight(rt->height);
  pass_descriptor->setDepthAttachment(nullptr);
  pass_descriptor->setStencilAttachment(nullptr);

  if (cmd_buffer->sample_buf_attachment_for_next_render_pass)
  {
    const auto& attachment_descriptor = cmd_buffer->sample_buf_attachment_for_next_render_pass;
    const auto attachment = pass_descriptor->sampleBufferAttachments()->object(0);

    attachment->setSampleBuffer( attachment_descriptor->sampleBuffer() );

    if (attachment_descriptor->startOfVertexSampleIndex() < attachment_descriptor->endOfVertexSampleIndex()) {
      attachment->setStartOfVertexSampleIndex(attachment_descriptor->startOfVertexSampleIndex());
      attachment->setEndOfVertexSampleIndex(attachment_descriptor->endOfVertexSampleIndex());
    }

    if (attachment_descriptor->startOfFragmentSampleIndex() < attachment_descriptor->endOfFragmentSampleIndex()) {
      attachment->setStartOfFragmentSampleIndex(attachment_descriptor->startOfFragmentSampleIndex());
      attachment->setEndOfFragmentSampleIndex(attachment_descriptor->endOfFragmentSampleIndex());
    }

    cmd_buffer->sample_buf_attachment_for_next_render_pass = nullptr;
  }

  for (uint32_t i = 0u; i < rt->attachment_descs.ndescs; ++i) {
    const ngf_attachment_description& attachment_desc = rt->attachment_descs.descs[i];
    if (attachment_desc.is_resolve) { continue; }
    const ngf_attachment_load_op      load_op         = pass_info->load_ops[i];
    const ngf_attachment_store_op     store_op        = pass_info->store_ops[i];
    const ngf_clear_info*             clear_info =
        load_op == NGF_LOAD_OP_CLEAR && pass_info->clears ? &pass_info->clears[i] : nullptr;
    switch (attachment_desc.type) {
    case NGF_ATTACHMENT_COLOR: {
      ngf_id<MTL::RenderPassColorAttachmentDescriptor> mtl_desc = id_default;
      ngfmtl_attachment_set_common(mtl_desc.get(), render_image_idx++, attachment_desc.type, rt, load_op, store_op);
      if (clear_info) {
        mtl_desc->setClearColor(MTL::ClearColor::Make(
            clear_info->clear_color[0],
            clear_info->clear_color[1],
            clear_info->clear_color[2],
            clear_info->clear_color[3]));
      }

      if (attachment_desc.sample_count > NGF_SAMPLE_COUNT_1) {
        if (rt->is_default) {
          mtl_desc->setResolveTexture(CURRENT_CONTEXT->frame.resolve_attachment_texture());
        } else if (rt->resolve_image_refs) {
          mtl_desc->setResolveTexture(
              rt->resolve_image_refs[resolve_attachment_idx++].image->texture.get());
        }
      }

      pass_descriptor->colorAttachments()->setObject(mtl_desc.get(), color_attachment_idx++);
      break;
    }
    case NGF_ATTACHMENT_DEPTH: {
      ngf_id<MTL::RenderPassDepthAttachmentDescriptor> mtl_desc = id_default;
      ngfmtl_attachment_set_common(mtl_desc.get(), render_image_idx++, attachment_desc.type, rt, load_op, store_op);
      if (clear_info) { mtl_desc->setClearDepth(clear_info->clear_depth_stencil.clear_depth); }
      pass_descriptor->setDepthAttachment(mtl_desc.get());
      break;
    }
    case NGF_ATTACHMENT_DEPTH_STENCIL: {
      ngf_id<MTL::RenderPassDepthAttachmentDescriptor> mtl_depth_desc = id_default;
      ngfmtl_attachment_set_common(
          mtl_depth_desc.get(),
          render_image_idx++,
          attachment_desc.type,
          rt,
          load_op,
          store_op);
      if (clear_info) {
        mtl_depth_desc->setClearDepth(clear_info->clear_depth_stencil.clear_depth);
      }
      pass_descriptor->setDepthAttachment(mtl_depth_desc.get());
      ngf_id<MTL::RenderPassStencilAttachmentDescriptor> mtl_stencil_desc = id_default;
      ngfmtl_attachment_set_common(
          mtl_stencil_desc.get(),
          render_image_idx++,
          attachment_desc.type,
          rt,
          load_op,
          store_op);
      if (clear_info) {
        mtl_stencil_desc->setClearStencil(clear_info->clear_depth_stencil.clear_stencil);
      }
      pass_descriptor->setStencilAttachment(mtl_stencil_desc.get());
      break;
    }
    }
  }

  assert(!cmd_buffer->active_rce);
  cmd_buffer->active_rce = cmd_buffer->mtl_cmd_buffer->renderCommandEncoder(pass_descriptor.get());
  cmd_buffer->active_rt  = rt;

  enc->pvt_data_donotuse.d0 = (uintptr_t)cmd_buffer;
  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_end_render_pass(ngf_render_encoder enc) NGF_NOEXCEPT {
  auto cmd_buffer = NGFMTL_ENC2CMDBUF(enc);
  if (cmd_buffer->active_rce) {
    cmd_buffer->active_rce->endEncoding();
    cmd_buffer->active_rce      = nullptr;
    cmd_buffer->active_gfx_pipe = nullptr;
  }
  cmd_buffer->renderpass_active = false;
  cmd_buffer->active_rt         = nullptr;
  NGFI_TRANSITION_CMD_BUF(cmd_buffer, NGFI_CMD_BUFFER_READY_TO_SUBMIT);

  return NGF_ERROR_OK;
}

ngf_error
ngf_cmd_begin_xfer_pass(ngf_cmd_buffer cmd_buf, const ngf_xfer_pass_info*, ngf_xfer_encoder* enc)
    NGF_NOEXCEPT {
  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_RECORDING);
  ngfmtl_finish_pending_encoders(cmd_buf);
  enc->pvt_data_donotuse.d0 = (uintptr_t)cmd_buf;
  cmd_buf->active_bce       = cmd_buf->mtl_cmd_buffer->blitCommandEncoder();
  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_end_xfer_pass(ngf_xfer_encoder enc) NGF_NOEXCEPT {
  auto cmd_buf = NGFMTL_ENC2CMDBUF(enc);
  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_READY_TO_SUBMIT);
  if (cmd_buf->active_bce) {
    cmd_buf->active_bce->endEncoding();
    cmd_buf->active_bce = nullptr;
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_begin_compute_pass(
    ngf_cmd_buffer cmd_buffer,
    const ngf_compute_pass_info* pass_info,
    ngf_compute_encoder* enc) NGF_NOEXCEPT {
  NGFI_TRANSITION_CMD_BUF(cmd_buffer, NGFI_CMD_BUFFER_RECORDING);

  ngf_id<MTL::ComputePassDescriptor> pass_descriptor        = id_default;

  if (cmd_buffer->sample_buf_attachment_for_next_compute_pass) {
    const auto& attachment_descriptor = cmd_buffer->sample_buf_attachment_for_next_compute_pass;
    const auto attachment = pass_descriptor->sampleBufferAttachments()->object(0);

    attachment->setSampleBuffer(attachment_descriptor->sampleBuffer());

    assert(attachment_descriptor->startOfEncoderSampleIndex() < attachment_descriptor->endOfEncoderSampleIndex());
    attachment->setStartOfEncoderSampleIndex(attachment_descriptor->startOfEncoderSampleIndex());
    attachment->setEndOfEncoderSampleIndex(attachment_descriptor->endOfEncoderSampleIndex());

    cmd_buffer->sample_buf_attachment_for_next_compute_pass = nullptr;
  }

  enc->pvt_data_donotuse.d0 = (uintptr_t)cmd_buffer;
  cmd_buffer->active_cce = cmd_buffer->mtl_cmd_buffer->computeCommandEncoder(pass_descriptor.get());

  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_end_compute_pass(ngf_compute_encoder enc) NGF_NOEXCEPT {
  auto cmd_buf = NGFMTL_ENC2CMDBUF(enc);
  assert(cmd_buf);
  NGFI_TRANSITION_CMD_BUF(cmd_buf, NGFI_CMD_BUFFER_READY_TO_SUBMIT);
  if (cmd_buf->active_cce) {
    cmd_buf->active_cce->endEncoding();
    cmd_buf->active_cce          = nullptr;
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
  cmd_buf->active_cce->setComputePipelineState(pipeline->pipeline.get());
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
    NGFI_DIAG_ERROR("Attempt to perform a compute dispatch without an active "
                    "compute encoder.");
    return;
  }
  assert(cmd_buf->active_compute_pipe);
  if (!cmd_buf->active_compute_pipe) {
    NGFI_DIAG_ERROR("Attempt to perform a compute dispatch without a bound "
                    "compute pipeline.");
    return;
  }
  const uint32_t* threadgroup_size =
      cmd_buf->active_compute_pipe->niceshade_metadata.threadgroup_size;
  cmd_buf->active_cce->dispatchThreadgroups(
      MTL::Size::Make(x_threadgroups, y_threadgroups, z_threadgroups),
      MTL::Size::Make(threadgroup_size[0], threadgroup_size[1], threadgroup_size[2]));
}

void ngf_cmd_bind_gfx_pipeline(ngf_render_encoder enc, const ngf_graphics_pipeline pipeline)
    NGF_NOEXCEPT {
  auto buf = NGFMTL_ENC2CMDBUF(enc);
  buf->active_rce->setRenderPipelineState(pipeline->pipeline.get());
  buf->active_rce->setCullMode(pipeline->culling);
  buf->active_rce->setFrontFacingWinding(pipeline->winding);

  buf->active_rce->setBlendColor(
      pipeline->blend_color[0],
      pipeline->blend_color[1],
      pipeline->blend_color[2],
      pipeline->blend_color[3]);
  if (pipeline->depth_stencil) {
    buf->active_rce->setDepthStencilState(pipeline->depth_stencil.get());
  }
  buf->active_rce->setStencilReferenceValues(
      pipeline->front_stencil_reference,
      pipeline->back_stencil_reference);
  buf->active_gfx_pipe = pipeline;
}

void ngf_cmd_viewport(ngf_render_encoder enc, const ngf_irect2d* r) NGF_NOEXCEPT {
  auto          buf = NGFMTL_ENC2CMDBUF(enc);
  MTL::Viewport viewport;
  viewport.originX = r->x;
  viewport.originY = r->y + (int32_t)r->height;
  viewport.width   = r->width;
  viewport.height  = -1.0 * r->height;

  // TODO: fix
  viewport.znear = 0.0f;
  viewport.zfar  = 1.0f;

  buf->active_rce->setViewport(viewport);
}

void ngf_cmd_scissor(ngf_render_encoder enc, const ngf_irect2d* r) NGF_NOEXCEPT {
  auto             buf = NGFMTL_ENC2CMDBUF(enc);
  MTL::ScissorRect scissor;
  scissor.x      = (uint32_t)r->x;
  scissor.y      = (uint32_t)r->y;
  scissor.width  = r->width;
  scissor.height = r->height;
  buf->active_rce->setScissorRect(scissor);
}

void ngf_cmd_draw(
    ngf_render_encoder enc,
    bool               indexed,
    uint32_t           first_element,
    uint32_t           nelements,
    uint32_t           ninstances) NGF_NOEXCEPT {
  auto               buf       = NGFMTL_ENC2CMDBUF(enc);
  MTL::PrimitiveType prim_type = buf->active_gfx_pipe->primitive_type;
  if (!indexed) {
    buf->active_rce->drawPrimitives(prim_type, first_element, nelements, ninstances, 0);
  } else {
    buf->active_rce->drawIndexedPrimitives(
        prim_type,
        nelements,
        buf->bound_index_buffer_type,
        buf->bound_index_buffer.get(),
        buf->bound_index_buffer_offset +
            first_element * (buf->bound_index_buffer_type == MTL::IndexTypeUInt16 ? 2 : 4),
        ninstances,
        0,
        0);
  }
}

void ngf_cmd_bind_attrib_buffer(
    ngf_render_encoder enc,
    const ngf_buffer   buf,
    uint32_t           binding,
    size_t             offset) NGF_NOEXCEPT {
  auto cmd_buf = NGFMTL_ENC2CMDBUF(enc);
  cmd_buf->active_rce->setVertexBuffer(
      buf->mtl_buffer.get(),
      offset,
      MAX_BUFFER_BINDINGS - binding);
}

void ngf_cmd_bind_index_buffer(
    ngf_render_encoder enc,
    const ngf_buffer   buf,
    size_t             offset,
    ngf_type           type) NGF_NOEXCEPT {
  auto cmd_buf                       = NGFMTL_ENC2CMDBUF(enc);
  cmd_buf->bound_index_buffer        = ngf_id<MTL::Buffer>::add_retain(buf->mtl_buffer.get());
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
      NGFI_DIAG_ERROR("Attempt to bind resources without an active render "
                      "command encoder.");
      return;
    }
    const uint32_t native_binding =
        cmd_buf->active_gfx_pipe->niceshade_metadata
      .native_binding_map[bind_op.target_set][bind_op.target_binding] + bind_op.array_index;
    if (native_binding == ~0) {
      NGFI_DIAG_ERROR(
          "Failed to  find  native binding for set %d binding %d",
          bind_op.target_set,
          bind_op.target_binding);
      continue;
    }
    switch (bind_op.type) {
    case NGF_DESCRIPTOR_TEXEL_BUFFER: {
      cmd_buf->active_rce->setVertexTexture(
          bind_op.info.texel_buffer_view->mtl_buffer_view.get(),
          native_binding);
      cmd_buf->active_rce->setFragmentTexture(
          bind_op.info.texel_buffer_view->mtl_buffer_view.get(),
          native_binding);
      break;
    }
    case NGF_DESCRIPTOR_STORAGE_BUFFER:
    case NGF_DESCRIPTOR_UNIFORM_BUFFER: {
      const ngf_buffer_bind_info& buf_bind_op = bind_op.info.buffer;
      const ngf_buffer            buf         = buf_bind_op.buffer;
      size_t                      offset      = buf_bind_op.offset;
      cmd_buf->active_rce->setVertexBuffer(buf->mtl_buffer.get(), offset, native_binding);
      cmd_buf->active_rce->setFragmentBuffer(buf->mtl_buffer.get(), offset, native_binding);
      break;
    }
    case NGF_DESCRIPTOR_IMAGE_AND_SAMPLER: {
      const ngf_image_sampler_bind_info& img_bind_op = bind_op.info.image_sampler;
      MTL::Texture* t = img_bind_op.is_image_view ? img_bind_op.resource.view->view.get() : img_bind_op.resource.image->texture.get();
      cmd_buf->active_rce->setVertexTexture(t, native_binding);
      cmd_buf->active_rce->setVertexSamplerState(
          img_bind_op.sampler->sampler.get(),
          native_binding);
      cmd_buf->active_rce->setFragmentTexture(t, native_binding);
      cmd_buf->active_rce->setFragmentSamplerState(
          img_bind_op.sampler->sampler.get(),
          native_binding);
      break;
    }
    case NGF_DESCRIPTOR_IMAGE: {
      const ngf_image_sampler_bind_info& img_bind_op = bind_op.info.image_sampler;
      MTL::Texture* t = img_bind_op.is_image_view ? img_bind_op.resource.view->view.get() : img_bind_op.resource.image->texture.get();
      cmd_buf->active_rce->setVertexTexture(t, native_binding);
      cmd_buf->active_rce->setFragmentTexture(t, native_binding);
      break;
    }
    case NGF_DESCRIPTOR_SAMPLER: {
      const ngf_image_sampler_bind_info& img_bind_op = bind_op.info.image_sampler;
      cmd_buf->active_rce->setVertexSamplerState(
          img_bind_op.sampler->sampler.get(),
          native_binding);
      cmd_buf->active_rce->setFragmentSamplerState(
          img_bind_op.sampler->sampler.get(),
          native_binding);
      break;
    }
    case NGF_DESCRIPTOR_STORAGE_IMAGE:
      NGFI_DIAG_ERROR("Binding storage images to non-compute shader is "
                      "currently unsupported.");
      break;
    case NGF_DESCRIPTOR_ACCELERATION_STRUCTURE:
      cmd_buf->active_rce->setVertexAccelerationStructure((MTL::AccelerationStructure*)bind_op.info.acceleration_structure, native_binding);
      cmd_buf->active_rce->setFragmentAccelerationStructure((MTL::AccelerationStructure*)bind_op.info.acceleration_structure, native_binding);
      break;
    case NGF_DESCRIPTOR_TYPE_COUNT:
      assert(false);
    }
  }
}

static std::optional<ngf_image_format> get_regular_format_from_srgb(const ngf_image_format f) {
  switch (f) {
    case NGF_IMAGE_FORMAT_SRGB8: return NGF_IMAGE_FORMAT_RGB8;
    case NGF_IMAGE_FORMAT_SRGBA8: return NGF_IMAGE_FORMAT_RGBA8;
    case NGF_IMAGE_FORMAT_BGR8_SRGB: return NGF_IMAGE_FORMAT_BGR8;
    case NGF_IMAGE_FORMAT_BGRA8_SRGB: return NGF_IMAGE_FORMAT_BGRA8;
    default: return std::nullopt;
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
      NGFI_DIAG_ERROR("Attempt to bind resources without an active compute "
                      "command encoder.");
      return;
    }
    const uint32_t native_binding =
        cmd_buf->active_compute_pipe->niceshade_metadata
      .native_binding_map[bind_op.target_set][bind_op.target_binding] + bind_op.array_index;
    if (native_binding == ~0) {
      NGFI_DIAG_ERROR(
          "Failed to  find  native binding for set %d binding %d",
          bind_op.target_set,
          bind_op.target_binding);
      continue;
    }
    switch (bind_op.type) {
    case NGF_DESCRIPTOR_TEXEL_BUFFER: {
      cmd_buf->active_cce->setTexture(
          bind_op.info.texel_buffer_view->mtl_buffer_view.get(),
          native_binding);
      break;
    }
    case NGF_DESCRIPTOR_STORAGE_BUFFER:
    case NGF_DESCRIPTOR_UNIFORM_BUFFER: {
      const ngf_buffer_bind_info& buf_bind_op = bind_op.info.buffer;
      const ngf_buffer            buf         = buf_bind_op.buffer;
      size_t                      offset      = buf_bind_op.offset;
      cmd_buf->active_cce->setBuffer(buf->mtl_buffer.get(), offset, native_binding);
      break;
    }
    case NGF_DESCRIPTOR_IMAGE_AND_SAMPLER: {
      const ngf_image_sampler_bind_info& img_bind_op = bind_op.info.image_sampler;
      MTL::Texture* t = img_bind_op.is_image_view ? img_bind_op.resource.view->view.get() : img_bind_op.resource.image->texture.get();
      cmd_buf->active_cce->setTexture(t, native_binding);
      cmd_buf->active_cce->setSamplerState(img_bind_op.sampler->sampler.get(), native_binding);
      break;
    }
    case NGF_DESCRIPTOR_STORAGE_IMAGE:
    case NGF_DESCRIPTOR_IMAGE: {
      const ngf_image_sampler_bind_info& img_bind_op = bind_op.info.image_sampler;
      if (img_bind_op.is_image_view) {
        cmd_buf->active_cce->setTexture(img_bind_op.resource.view->view.get(), native_binding);
      } else {
        if (const auto maybe_format = get_regular_format_from_srgb(img_bind_op.resource.image->format) ) {
          if (!img_bind_op.resource.image->non_srgb_view)
            img_bind_op.resource.image->non_srgb_view = img_bind_op.resource.image->texture.get()->newTextureView(
                                                                                                get_mtl_pixel_format(maybe_format.value()).format);
          cmd_buf->active_cce->setTexture(img_bind_op.resource.image->non_srgb_view.get(), native_binding);
        } else {
          cmd_buf->active_cce->setTexture(img_bind_op.resource.image->texture.get(), native_binding);
        }
      }
      break;
    }
    case NGF_DESCRIPTOR_SAMPLER: {
      const ngf_image_sampler_bind_info& img_bind_op = bind_op.info.image_sampler;
      cmd_buf->active_cce->setSamplerState(img_bind_op.sampler->sampler.get(), native_binding);
      break;
    }
    case NGF_DESCRIPTOR_ACCELERATION_STRUCTURE:
      cmd_buf->active_cce->setAccelerationStructure((MTL::AccelerationStructure*)bind_op.info.acceleration_structure, native_binding);
      break;
    case NGF_DESCRIPTOR_TYPE_COUNT:
      assert(false);
    }
  }
}

void ngfmtl_cmd_copy_buffer(
    ngf_xfer_encoder enc,
    MTL::Buffer*     src,
    MTL::Buffer*     dst,
    size_t           size,
    size_t           src_offset,
    size_t           dst_offset) {
  auto buf = NGFMTL_ENC2CMDBUF(enc);
  assert(buf->active_rce == nullptr);
  buf->active_bce->copyFromBuffer(src, src_offset, dst, dst_offset, size);
}

void ngf_cmd_copy_buffer(
    ngf_xfer_encoder enc,
    const ngf_buffer src,
    ngf_buffer       dst,
    size_t           size,
    size_t           src_offset,
    size_t           dst_offset) NGF_NOEXCEPT {
  ngfmtl_cmd_copy_buffer(
      enc,
      src->mtl_buffer.get(),
      dst->mtl_buffer.get(),
      size,
      src_offset,
      dst_offset);
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
      buf->active_bce->copyFromBuffer(src->mtl_buffer.get(),
                                      w->src_offset + (l * pitch * num_rows),
                                      pitch,
                                      pitch * num_rows,
                                      MTL::Size::Make(w->extent.width, w->extent.height, w->extent.depth),
                                      dst->texture.get(),
                                      w->dst_base_layer + l,
                                      w->dst_level,
                                      MTL::Origin::Make(
                                                        (NS::UInteger)w->dst_offset.x,
                                                        (NS::UInteger)w->dst_offset.y,
                                                        (NS::UInteger)w->dst_offset.z));
    }
  }
}
                
/*
void ngf_cmd_write_image(
    ngf_xfer_encoder enc,
    const ngf_buffer src,
    size_t           src_offset,
    ngf_image_ref    dst,
    ngf_offset3d     offset,
    ngf_extent3d     extent,
    uint32_t         nlayers) NGF_NOEXCEPT {
  auto buf = NGFMTL_ENC2CMDBUF(enc);
  assert(buf->active_rce == nullptr);
  const MTL::TextureType texture_type = dst.image->texture->textureType();
  const bool             is_cubemap =
      texture_type == MTL::TextureTypeCube || texture_type == MTL::TextureTypeCubeArray;
  const uint32_t target_slice =
      (is_cubemap ? 6u : 1u) * dst.layer + (is_cubemap ? dst.cubemap_face : 0);
  const uint32_t pitch    = ngfmtl_get_pitch(extent.width, dst.image->format);
  const uint32_t num_rows = ngfmtl_get_num_rows(extent.height, dst.image->format);
  for (uint32_t l = 0; l < nlayers; ++l) {
    buf->active_bce->copyFromBuffer(
        src->mtl_buffer.get(),
        src_offset + (l * pitch * num_rows),
        pitch,
        pitch * num_rows,
        MTL::Size::Make(extent.width, extent.height, extent.depth),
        dst.image->texture.get(),
        target_slice + l,
        dst.mip_level,
        MTL::Origin::Make((NS::UInteger)offset.x, (NS::UInteger)offset.y, (NS::UInteger)offset.z));
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
  const MTL::TextureType texture_type = src.image->texture->textureType();
  const bool             is_cubemap =
      texture_type == MTL::TextureTypeCube || texture_type == MTL::TextureTypeCubeArray;
  const uint32_t src_slice =
      (is_cubemap ? 6u : 1u) * src.layer + (is_cubemap ? src.cubemap_face : 0);
  const uint32_t pitch    = ngfmtl_get_pitch(src_extent.width, src.image->format);
  const uint32_t num_rows = ngfmtl_get_num_rows(src_extent.height, src.image->format);
  for (uint32_t l = 0; l < nlayers; ++l) {
    buf->active_bce->copyFromTexture(
        src.image->texture.get(),
        src_slice + l,
        src.mip_level,
        MTL::Origin::Make(
            (NS::UInteger)src_offset.x,
            (NS::UInteger)src_offset.y,
            (NS::UInteger)src_offset.z),
        MTL::Size::Make(src_extent.width, src_extent.height, src_extent.depth),
        dst->mtl_buffer.get(),
        dst_offset + (l * pitch * num_rows),
        pitch,
        pitch * num_rows);
  }
}

ngf_error ngf_cmd_generate_mipmaps(ngf_xfer_encoder xfenc, ngf_image img) NGF_NOEXCEPT {
  if (!(img->usage_flags & NGF_IMAGE_USAGE_MIPMAP_GENERATION)) {
    NGFI_DIAG_ERROR("mipmap generation was requested for an image that was created "
                    "without the NGF_IMAGE_USAGE_MIPMAP_GENERATION flag");
    return NGF_ERROR_INVALID_OPERATION;
  }
  auto buf = NGFMTL_ENC2CMDBUF(xfenc);
  assert(buf->active_rce == nullptr);
  buf->active_bce->generateMipmaps(img->texture.get());
  return NGF_ERROR_OK;
}
#define PLACEHOLDER_CMD(name, ...)                    \
  void ngf_cmd_##name(ngf_cmd_buffer*, __VA_ARGS__) { \
  }

void ngf_cmd_stencil_reference(ngf_render_encoder enc, uint32_t front, uint32_t back) NGF_NOEXCEPT {
  auto cmd_buf = NGFMTL_ENC2CMDBUF(enc);
  cmd_buf->active_rce->setStencilReferenceValues(front, back);
}

void ngf_cmd_stencil_compare_mask(ngf_render_encoder enc, uint32_t front, uint32_t back)
    NGF_NOEXCEPT {
  auto cmd_buf = NGFMTL_ENC2CMDBUF(enc);

  cmd_buf->active_gfx_pipe->depth_stencil_desc->frontFaceStencil()->setReadMask(front);
  cmd_buf->active_gfx_pipe->depth_stencil_desc->backFaceStencil()->setReadMask(back);
  ngf_id<MTL::DepthStencilState> depth_stencil_state =
      CURRENT_CONTEXT->device->newDepthStencilState(
          cmd_buf->active_gfx_pipe->depth_stencil_desc.get());
  cmd_buf->active_rce->setDepthStencilState(depth_stencil_state.get());
}

void ngf_cmd_stencil_write_mask(ngf_render_encoder enc, uint32_t front, uint32_t back)
    NGF_NOEXCEPT {
  auto cmd_buf = NGFMTL_ENC2CMDBUF(enc);
  cmd_buf->active_gfx_pipe->depth_stencil_desc->frontFaceStencil()->setWriteMask(front);
  cmd_buf->active_gfx_pipe->depth_stencil_desc->backFaceStencil()->setWriteMask(back);
  ngf_id<MTL::DepthStencilState> depth_stencil_state =
      CURRENT_CONTEXT->device->newDepthStencilState(
          cmd_buf->active_gfx_pipe->depth_stencil_desc.get());
  cmd_buf->active_rce->setDepthStencilState(depth_stencil_state.get());
}

void ngf_cmd_set_depth_bias(ngf_render_encoder enc, float const_scale, float slope_scale, float clamp) NGF_NOEXCEPT {
  auto cmd_buf = NGFMTL_ENC2CMDBUF(enc);
  cmd_buf->active_rce->setDepthBias(const_scale, slope_scale, clamp);
}

void ngf_cmd_begin_debug_group(ngf_cmd_buffer cmd_buf,  const char* name) NGF_NOEXCEPT {
  auto name_nsstr = NS::String::string(name, NS::ASCIIStringEncoding);
  cmd_buf->mtl_cmd_buffer->pushDebugGroup(name_nsstr);
}

void ngf_cmd_end_current_debug_group(ngf_cmd_buffer cmd_buf) NGF_NOEXCEPT {
  cmd_buf->mtl_cmd_buffer->popDebugGroup();
}

void ngf_finish() NGF_NOEXCEPT {
  if (CURRENT_CONTEXT->pending_cmd_buffer) {
    CURRENT_CONTEXT->last_cmd_buffer =
        ngf_id<MTL::CommandBuffer>::add_retain(CURRENT_CONTEXT->pending_cmd_buffer);
    CURRENT_CONTEXT->pending_cmd_buffer->commit();
    CURRENT_CONTEXT->pending_cmd_buffer = nullptr;
  }

  if (CURRENT_CONTEXT->last_cmd_buffer) { CURRENT_CONTEXT->last_cmd_buffer->waitUntilCompleted(); }
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

uintptr_t ngf_get_mtl_image_handle(ngf_image image) NGF_NOEXCEPT {
  return (uintptr_t)(image->texture.get());
}

uintptr_t ngf_get_mtl_buffer_handle(ngf_buffer buffer) NGF_NOEXCEPT {
  return (uintptr_t)(buffer->mtl_buffer.get());
}

uintptr_t ngf_get_mtl_cmd_buffer_handle(ngf_cmd_buffer cmd_buffer) NGF_NOEXCEPT {
  return (uintptr_t)(cmd_buffer->mtl_cmd_buffer);
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

uintptr_t ngf_get_mtl_sampler_handle(ngf_sampler sampler) NGF_NOEXCEPT {
  return (uintptr_t)(sampler->sampler.get());
}

uint32_t ngf_get_mtl_pixel_format_index(ngf_image_format format) NGF_NOEXCEPT {
  return (uint32_t)get_mtl_pixel_format(format).format;
}

uintptr_t ngf_get_mtl_device() NGF_NOEXCEPT {
  return (uintptr_t)(void*)MTL_DEVICE;
}

void ngf_mtl_set_sample_attachment_for_next_render_pass( ngf_cmd_buffer cmd_buffer, uintptr_t sample_buf_attachment_descriptor ) NGF_NOEXCEPT
{
  cmd_buffer->sample_buf_attachment_for_next_render_pass = ngf_id<MTL::RenderPassSampleBufferAttachmentDescriptor>::add_retain(
    static_cast< MTL::RenderPassSampleBufferAttachmentDescriptor* >( (void*)sample_buf_attachment_descriptor )
  );
}

void ngf_mtl_set_sample_attachment_for_next_compute_pass( ngf_cmd_buffer cmd_buffer, uintptr_t sample_buf_attachment_descriptor ) NGF_NOEXCEPT
{
  cmd_buffer->sample_buf_attachment_for_next_compute_pass = ngf_id<MTL::ComputePassSampleBufferAttachmentDescriptor>::add_retain(
    static_cast< MTL::ComputePassSampleBufferAttachmentDescriptor* >( (void*)sample_buf_attachment_descriptor )
  );
}
