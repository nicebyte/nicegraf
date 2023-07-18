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

// Return type of CA::MetalLayer*
CA::MetalLayer* ngf_layer_add_to_view(MTL::Device* device,
                                 uint32_t width,
                                 uint32_t height,
                                 MTL::PixelFormat pixel_format,
                                 uint32_t capacity_hint,
                                 bool display_sync_enabled,
                                 uintptr_t native_handle) {
    CAMetalLayer* layer_   = [CAMetalLayer layer];
    layer_.device          = (__bridge id<MTLDevice>)device;
    layer_.drawableSize    = CGSizeMake(width, height);
    layer_.pixelFormat     = (MTLPixelFormat)pixel_format; // TODO: Is this cast correct?
    layer_.framebufferOnly = YES;
    #if TARGET_OS_OSX
    if (@available(macOS 10.13.2, *)) {
      layer_.maximumDrawableCount = capacity_hint;
    }
    if (@available(macOS 10.13, *)) {
      layer_.displaySyncEnabled = display_sync_enabled;
    }
    #endif

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
