/**
Copyright © 2018 nicegraf contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the Software), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED AS IS, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "nicegraf.h"
#include "nicegraf_internal.h"
#include <memory>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#if defined(TARGET_OS_MAC)
#import <AppKit/AppKit.h>
#define _NGF_VIEW_TYPE NSView
#elif defined(TARGET_OS_IPHONE)
#import <UIKit/UIKit.h>
#define _NGF_VIEW_TYPE UIView
#endif

id<MTLDevice> MTL_DEVICE = nil;

struct _ngf_context_shared_state {
  id<MTLCommandQueue> queue;
};

struct ngf_context {
  id<MTLDevice> device = nil;
  CAMetalLayer *layer = nil;
  std::shared_ptr<_ngf_context_shared_state> shared_state;
};

static MTLPixelFormat get_mtl_pixel_format(ngf_image_format) {
  return MTLPixelFormatBGRA8Unorm;
}

ngf_error ngf_initialize(ngf_device_preference dev_pref) {
  id<NSObject> dev_observer = nil;
  const NSArray<id<MTLDevice>> *devices =
      MTLCopyAllDevicesWithObserver(&dev_observer,
                                    ^(id<MTLDevice> d,
                                      MTLDeviceNotificationName n){});
  bool found_device = false;
  for (uint32_t d = 0u; !found_device && d < devices.count; ++d) {
    MTL_DEVICE = devices[d++];
    found_device = (dev_pref != NGF_DEVICE_PREFERENCE_DISCRETE) ||
                   !MTL_DEVICE.lowPower;
  }

  return found_device ? NGF_ERROR_OK : NGF_ERROR_INITIALIZATION_FAILED;
}

ngf_error ngf_create_context(const ngf_context_info *info,
                             ngf_context **result) {
  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_context);
  ngf_context *ctx = *result;

  if (ctx == nullptr) {
    err = NGF_ERROR_OUTOFMEM;
  }

  ctx->device = MTL_DEVICE;
  if (info->shared_context != nullptr) {
    ctx->shared_state = info->shared_context->shared_state;
  } else {
    ctx->shared_state = std::make_shared<_ngf_context_shared_state>();
    ctx->shared_state->queue = [ctx->device newCommandQueue];
  }

  if (info->swapchain_info) {
    const ngf_swapchain_info *swapchain_info = info->swapchain_info;
    ctx->layer = [CAMetalLayer layer];
    ctx->layer.device = ctx->device;
    CGSize size;
    size.width = swapchain_info->width;
    size.height = swapchain_info->height;
    ctx->layer.drawableSize = size; 
    ctx->layer.pixelFormat = get_mtl_pixel_format(swapchain_info->cfmt);
    if (@available(macOS 10.13.2, *)) {
      ctx->layer.maximumDrawableCount = swapchain_info->capacity_hint;
    }
    ctx->layer.displaySyncEnabled =
        (swapchain_info->present_mode == NGF_PRESENTATION_MODE_IMMEDIATE);
    // presents with transaction?
    // extended dynamic range
    // next drawable timeout
    // color space
    _NGF_VIEW_TYPE *window =
        CFBridgingRelease((void*)swapchain_info->native_handle);
    [window setLayer:ctx->layer];
  }

ngf_create_context_cleanup:
  return err;
}
