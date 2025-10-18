#include "nicegraf.h"

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
using NGFMTL_VIEW_TYPE = NSView;
#else
#import <UIKit/UIKit.h>
using NGFMTL_VIEW_TYPE = UIView;
#endif

// Implementation is defined in impl.cpp, header only here
#include "MetalSingleHeader.hpp"

static const CFStringRef get_mtl_colorspace(ngf_colorspace colorspace) {
  const CFStringRef color_spaces[NGF_COLORSPACE_COUNT] = {
    kCGColorSpaceSRGB,
    kCGColorSpaceExtendedSRGB,
    kCGColorSpaceExtendedLinearSRGB,
    kCGColorSpaceDisplayP3,
    kCGColorSpaceExtendedLinearDisplayP3,
    kCGColorSpaceDCIP3,
    kCGColorSpaceExtendedLinearITUR_2020,
    kCGColorSpaceITUR_2100_PQ
  };
  return color_spaces[colorspace];
}

// Return type of CA::MetalLayer*
CA::MetalLayer* ngf_layer_add_to_view(MTL::Device* device,
                                 uint32_t width,
                                 uint32_t height,
                                 MTL::PixelFormat pixel_format,
                                 ngf_colorspace colorspace,
                                 uint32_t capacity_hint,
                                 bool display_sync_enabled,
                                 bool compute_access_enabled,
                                 uintptr_t native_handle) {
    CAMetalLayer* layer_   = [CAMetalLayer layer];
    layer_.device          = (__bridge id<MTLDevice>)device;
    layer_.drawableSize    = CGSizeMake(width, height);
    layer_.pixelFormat     = (MTLPixelFormat)pixel_format; // TODO: Is this cast correct?
    layer_.colorspace      = CGColorSpaceCreateWithName(get_mtl_colorspace(colorspace));
    layer_.framebufferOnly = compute_access_enabled ? NO : YES;
    #if TARGET_OS_OSX
    if (@available(macOS 10.13.2, *)) {
      layer_.maximumDrawableCount = capacity_hint;
    }
    if (@available(macOS 10.13, *)) {
      layer_.displaySyncEnabled = display_sync_enabled;
    }
    #endif

    const bool supports_edr = colorspace == NGF_COLORSPACE_EXTENDED_SRGB_LINEAR ||
                              colorspace == NGF_COLORSPACE_DISPLAY_P3_LINEAR ||
                              colorspace == NGF_COLORSPACE_ITUR_BT2020 ||
                              colorspace == NGF_COLORSPACE_ITUR_BT2100_PQ;

    if (supports_edr) {
      #if TARGET_OS_OSX
      if (@available(macOS 10.11, *)) {
        layer_.wantsExtendedDynamicRangeContent = YES;
      }
      #else
      if (@available(iOS 16.0, *)) {
        layer_.wantsExtendedDynamicRangeContent = YES;
      }
      #endif
    }

    // Associate the newly created Metal layer with the user-provided View.
    NGFMTL_VIEW_TYPE* view = CFBridgingRelease((void*)native_handle);
    #if TARGET_OS_OSX
    [view setLayer:layer_];
    #else
    [view.layer addSublayer:layer_];
    [layer_ setContentsScale:view.layer.contentsScale];
    [layer_ setContentsGravity:kCAGravityResizeAspect];
    [layer_ setFrame:view.frame];
    #endif
    CFBridgingRetain(view);
    
    return (__bridge_retained CA::MetalLayer*)layer_;
}

CA::MetalDrawable* ngf_layer_next_drawable(CA::MetalLayer* layer) {
    return (__bridge CA::MetalDrawable*)[(__bridge CAMetalLayer*)layer nextDrawable];
}

void ngf_resize_swapchain(CA::MetalLayer* layer,
                          uint32_t width,
                          uint32_t height,
                          uintptr_t native_handle) {
    CAMetalLayer* bridged_layer = (__bridge CAMetalLayer*)layer;
    
    bridged_layer.drawableSize = CGSizeMake(width, height);

    NGFMTL_VIEW_TYPE* view = CFBridgingRelease((void*)native_handle);

    [bridged_layer setContentsScale:view.layer.contentsScale];
    [bridged_layer setFrame:view.frame];

    CFBridgingRetain(view);
}
