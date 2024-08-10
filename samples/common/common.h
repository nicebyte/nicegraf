#if defined(__APPLE__)
#include "TargetConditionals.h"
#endif

#if TARGET_OS_IPHONE
#include <MetalKit/MetalKit.h>
using NGF_WINDOW_TYPE = MTKView;
#else
#include <GLFW/glfw3.h>
using NGF_WINDOW_TYPE = GLFWwindow;
#endif

#include <stdint.h>

namespace ngf_samples {
// lifecycle functions
int init();

void run_loop();

void draw_frame();

void process_input();

void shutdown();

// platform-specific functions
void init_graphics_library();

void init_imgui(NGF_WINDOW_TYPE* window);

void create_window(
    const uint32_t    width,
    const uint32_t    height,
    const char*       title,
    NGF_WINDOW_TYPE*& window);

uintptr_t get_native_handle(NGF_WINDOW_TYPE* window);

bool window_should_close(NGF_WINDOW_TYPE* window);

void poll_events();

int get_key(NGF_WINDOW_TYPE* window, int key);

void get_framebuffer_size(NGF_WINDOW_TYPE* window, int* width, int* height);

void begin_imgui_frame(NGF_WINDOW_TYPE* window);
}  // namespace ngf_samples
