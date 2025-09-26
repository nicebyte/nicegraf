#include "metalfx.h"
#include <MetalFX/MetalFX.h>
#include <nicegraf-mtl-handles.h>

struct mtlfx_scaler::mtlfx_scaler_impl {
    id<MTLFXSpatialScaler> mtl_scaler;
};

namespace {
static MTLPixelFormat get_mtl_format(ngf_image_format f) {
    switch(f) {
        case NGF_IMAGE_FORMAT_BGRA8: return MTLPixelFormatBGRA8Unorm;
        case NGF_IMAGE_FORMAT_BGRA8_SRGB: return MTLPixelFormatBGRA8Unorm_sRGB;
        case NGF_IMAGE_FORMAT_RGBA8: return MTLPixelFormatRGBA8Unorm;
        case NGF_IMAGE_FORMAT_SRGBA8: return MTLPixelFormatRGBA8Unorm_sRGB;
        default: return MTLPixelFormatInvalid;
    }
}
}

void mtlfx_scaler::destroy() {
    if (impl_) {
        impl_->mtl_scaler = nil;
        delete impl_;
        impl_ = nullptr;
    }
}


mtlfx_scaler mtlfx_scaler::create(const mtlfx_scaler_info& info) {
    auto scaler_desc = [[MTLFXSpatialScalerDescriptor alloc] init];
    scaler_desc.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual; // Set appropriately!
    scaler_desc.inputWidth = info.input_width;
    scaler_desc.inputHeight = info.input_height;
    scaler_desc.outputWidth = info.output_width;
    scaler_desc.outputHeight = info.output_height;
    scaler_desc.colorTextureFormat = get_mtl_format(info.input_format);
    scaler_desc.outputTextureFormat = get_mtl_format(info.output_format);
    auto scaler = [scaler_desc newSpatialScalerWithDevice:MTLCreateSystemDefaultDevice()];
    auto impl = new mtlfx_scaler_impl{};
    impl->mtl_scaler = scaler;
    mtlfx_scaler result;
    result.impl_ = impl;
    return result;
}

void mtlfx_scaler::encode(const mtlfx_encode_info& info) {
    auto cmd_buf = (__bridge id<MTLCommandBuffer>)ngf_get_mtl_cmd_buffer_handle(info.cmd_buf);
    auto in_img = (__bridge id<MTLTexture>)ngf_get_mtl_image_handle(info.in_img);
    auto out_img = (__bridge id<MTLTexture>)ngf_get_mtl_image_handle(info.out_img);
    impl_->mtl_scaler.outputTexture = out_img;
    impl_->mtl_scaler.colorTexture = in_img;
    [impl_->mtl_scaler encodeToCommandBuffer:cmd_buf];
}

