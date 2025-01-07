#include "common.h"
#include "imgui_impl_metal.h"

// #import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <UIKit/UIKit.h>

namespace ngf_samples {

void init_graphics_library() {
}

void init_imgui(MTKView* window) {
  ImGui_ImplMetal_Init(MTLCreateSystemDefaultDevice());
}

void create_window(
    const uint32_t width,
    const uint32_t height,
    const char*    title,
    MTKView*&      window) {
  window =
      (MTKView*)[[[[[UIApplication sharedApplication] delegate] window] rootViewController] view];
}

uintptr_t get_native_handle(MTKView* window) {
  return (uintptr_t)window;
}

bool window_should_close(MTKView* window) {
  return false;
}

void poll_events() {
}

int get_key(MTKView* window, int key) {
  return -1;
};

void get_framebuffer_size(MTKView* window, int* width, int* height) {
  *width  = (int)window.drawableSize.width;
  *height = (int)window.drawableSize.height;
}

void begin_imgui_frame(MTKView* window) {
  // TODO: Is this even needed???
  //  CAMetalLayer* layer = (CAMetalLayer*)[[[[[[UIApplication sharedApplication] delegate] window]
  //      rootViewController] view] layer];

  // id<CAMetalDrawable> drawable = [layer nextDrawable];

  // MTLRenderPassDescriptor* descriptor    = [MTLRenderPassDescriptor renderPassDescriptor];
  // descriptor.colorAttachments[0].texture = drawable.texture;

  // ImGui_ImplMetal_NewFrame(nil);
}

}  // namespace ngf_samples
