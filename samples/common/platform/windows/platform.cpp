#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(_WIN32) || defined(_WIN64)
#define GLFW_EXPOSE_NATIVE_WIN32
#endif

#include "common.h"
#include "imgui_impl_glfw.h"

#include <GLFW/glfw3native.h>

namespace ngf_samples {

void init_graphics_library() {
  /**
   * Initialize glfw.
   */
  glfwInit();
}

void init_imgui(const GLFWwindow* window) {
  ImGui_ImplGlfw_InitForOther(window, true);
}

/**
 * Note that we set a special window hint to make sure GLFW does _not_ attempt to create
 * an OpenGL (or other API) context for us - this is nicegraf's job.
 */
void create_window(
    const uint32_t width,
    const uint32_t height,
    const char*    title,
    GLFWwindow*&   window) {
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* glfwWindow =
      glfwCreateWindow((int)width, (int)height, "nicegraf sample", nullptr, nullptr);

  window = glfwWindow;
}

uintptr_t get_native_handle(const GLFWwindow* window) {
  return (uintptr_t)glfwGetWin32Window(window);
}

bool window_should_close(const GLFWwindow* window) {
  return glfwWindowShouldClose(window);
}

void poll_events() {
  glfwPollEvents();
}

int get_key(const GLFWwindow* window, int key) {
  return glfwGetKey(window, key);
}

void get_framebuffer_size(const GLFWwindow* window, int* width, int* height) {
  glfwGetFramebufferSize(window, width, height);
}

void begin_imgui_frame() {
  ImGui_ImplGlfw_NewFrame();
}

}  // namespace ngf_samples