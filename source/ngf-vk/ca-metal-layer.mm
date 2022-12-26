#if defined(__APPLE__)

#include "nicegraf.h"
#import <QuartzCore/QuartzCore.h>
#import <Metal/Metal.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
using NGFMTL_VIEW_TYPE = NSView;
#else
#import <UIKit/UIKit.h>
using NGFMTL_VIEW_TYPE = UIView;
#endif

extern "C" {
void* ngfvk_create_ca_metal_layer(const ngf_swapchain_info* swapchain_info) {
  //const MTLPixelFormat pixel_format = get_mtl_pixel_format(swapchain_info->color_format).format;
  auto layer = [CAMetalLayer layer];
  layer.drawableSize    = CGSizeMake(swapchain_info->width, swapchain_info->height);
  //layer.pixelFormat     = pixel_format;
  layer.framebufferOnly = YES;
  #if TARGET_OS_OSX
      if (@available(macOS 10.13.2, *)) {
        layer.maximumDrawableCount = swapchain_info->capacity_hint;
      }
      if (@available(macOS 10.13, *)) {
        layer.displaySyncEnabled = (swapchain_info->present_mode == NGF_PRESENTATION_MODE_FIFO);
      }
  #endif

      // Associate the newly created Metal layer with the user-provided View.
      NGFMTL_VIEW_TYPE* view = CFBridgingRelease((void*)swapchain_info->native_handle);
  #if TARGET_OS_OSX
      [view setLayer:layer];
  #else
      [view.layer addSublayer:layer];
      [layer setContentsScale:view.layer.contentsScale];
      [layer setContentsGravity:kCAGravityResizeAspect];
      [layer setFrame:view.frame];
  #endif
      CFBridgingRetain(view);
  return layer;
}
}
#endif
