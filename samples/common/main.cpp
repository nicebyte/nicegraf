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

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(_WIN32) || defined(_WIN64)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#include "platform/macos/glfw-cocoa-contentview.h"
#else
#define GLFW_EXPOSE_NATIVE_X11
#endif
#include "check.h"
#include "diagnostic-callback.h"
#include "imgui-backend.h"
#include "imgui_impl_glfw.h"
#include "logging.h"
#include "nicegraf-wrappers.h"
#include "nicegraf.h"
#include "sample-interface.h"

#include <GLFW/glfw3native.h>
#include <chrono>
#include <optional>
#include <stdio.h>

/*
 * Below we define the "common main" function, where all nicegraf samples begin
 * meaningful execution. On some platforms, a special `main` might be required
 * (which is defined in the platform-specific code), but it will eventually call
 * into this one.
 */
#if defined(__APPLE__)
#define NGF_SAMPLES_COMMON_MAIN apple_main
#else
#define NGF_SAMPLES_COMMON_MAIN main
#endif

int NGF_SAMPLES_COMMON_MAIN(int, char**) {
  /**
   * We prefer a more verbose diagnostic output from nicegraf in debug builds.
   */
#if defined(NDEBUG)
  constexpr ngf_diagnostic_log_verbosity diagnostics_verbosity = NGF_DIAGNOSTICS_VERBOSITY_DEFAULT;
#else
  constexpr ngf_diagnostic_log_verbosity diagnostics_verbosity = NGF_DIAGNOSTICS_VERBOSITY_DETAILED;
#endif

  /**
   * Select a rendering device to be used by nicegraf.
   */
  uint32_t          ndevices = 0u;
  const ngf_device* devices  = NULL;
  NGF_SAMPLES_CHECK_NGF_ERROR(ngf_get_device_list(&devices, &ndevices));
  const char* device_perf_tier_names[NGF_DEVICE_PERFORMANCE_TIER_COUNT] = {
      "high",
      "low",
      "unknown"};
  /**
   * For the sample code, we try to select a high-perf tier device. If one isn't available, we just
   * fall back on the first device in the list. You may want to choose a different strategy for your
   * specific application, or allow the user to pick.
   */
  size_t high_power_device_idx = (~0u);
  ngf_samples::logi("available rendering devices: ");
  for (uint32_t i = 0; i < ndevices; ++i) {
    /**
     * If no preferred index has been selected yet, and the current device is high-power, pick it as
     * preferred. otherwise, just log the device details.
     */
    ngf_samples::logi(
        " device %d : %s (perf tier : `%s`)",
        i,
        devices[i].name,
        device_perf_tier_names[devices[i].performance_tier]);
    if (high_power_device_idx == (~0u) &&
        devices[i].performance_tier == NGF_DEVICE_PERFORMANCE_TIER_HIGH) {
      high_power_device_idx = i;
    }
  }
  /* Fall back to 1st device if no high-power device was found. */
  const size_t preferred_device_idx = (high_power_device_idx == ~0u) ? 0 : high_power_device_idx;
  const ngf_device_handle device_handle = devices[preferred_device_idx].handle;
  ngf_samples::logi("selected device %d", preferred_device_idx);

  /*
   * Initialize RenderDoc.
   * Allows capturing of frame data to be opened in the RenderDoc debugger.
   * To enable RenderDoc functionality, fill in the below struct with the path
   * to the RenderDoc library (renderdoc.dll on Windows, librenderdoc.so on Linux,
   * N/A on Mac OSX) and a file path template for where the captures should be stored.
   *
   * For example, if your library is saved in C:\example\dir\renderdoc.dll and you want to save
   * your captures as C:\capture\dir\test. You would fill out the struct as such:
   *
   * const ngf_renderdoc_info renderdoc_info = {
   *   .renderdoc_lib_path             = "C:\\example\\dir\\renderdoc.dll",
   *   .renderdoc_destination_template = "C:\\capture\\dir\\test"};
   *
   * Provided that the above steps are completed, captures can be taken by pressing the
   * "C" key while a sample is running. Captures will be saved to the specified directory. 
   * Custom instrumenting within the samples can also be done by making calls to 
   * ngf_capture_begin and ngf_capture end, respectively.
   */
  const ngf_renderdoc_info renderdoc_info = {
      .renderdoc_lib_path             = NULL,
      .renderdoc_destination_template = NULL};

  /*
   * Initialize nicegraf.
   * Set our rendering device preference to "discrete" to pick a high-power GPU if one is available,
   * and install a diagnostic callback.
   */
  const ngf_diagnostic_info diagnostic_info {
      .verbosity = diagnostics_verbosity,
      .userdata  = nullptr,
      .callback  = ngf_samples::sample_diagnostic_callback};

  const ngf_init_info init_info {
      .diag_info            = &diagnostic_info,
      .allocation_callbacks = NULL,
      .device               = device_handle,
      .renderdoc_info       = (renderdoc_info.renderdoc_lib_path != NULL) ? &renderdoc_info : NULL};
  NGF_SAMPLES_CHECK_NGF_ERROR(ngf_initialize(&init_info));

  /**
   * Initialize imgui and generate its font atlas.
   */
  ImGuiContext* imgui_ctx = ImGui::CreateContext();
  ImGui::SetCurrentContext(imgui_ctx);
  unsigned char* imgui_font_atlas_bytes;
  int            imgui_font_atlas_width, imgui_font_atlas_height;
  ImGui::GetIO().Fonts->GetTexDataAsRGBA32(
      &imgui_font_atlas_bytes,
      &imgui_font_atlas_width,
      &imgui_font_atlas_height);

  /**
   * Initialize glfw.
   */
  glfwInit();

  /**
   * Create a window.
   * The `width` and `height` here refer to the dimensions of the window's "client area", i.e. the
   * area that can actually be rendered to (excludes borders and any other decorative elements). The
   * dimensions we request are a hint, we need to get the actual dimensions after the window is
   * created.
   * Note that we deliberately create the window before setting up the nicegraf context. This is
   * done so that when the destructors are invoked, the context is destroyed before the window -
   * changing this sequence of events might lead to misbehavior.
   * Also note that we set a special window hint to make sure GLFW does _not_ attempt to create
   * an OpenGL (or other API) context for us - this is nicegraf's job.
   */
  constexpr uint32_t window_width_hint = 800, window_height_hint = 600;
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window =
      glfwCreateWindow(window_width_hint, window_height_hint, "nicegraf sample", nullptr, nullptr);
  if (window == nullptr) {
    ngf_samples::loge("Failed to create a window, exiting.");
    return 0;
  }
  int fb_width, fb_height;
  glfwGetFramebufferSize(window, &fb_width, &fb_height);
  ngf_samples::logi("created a window with client area of size size %d x %d.", fb_width, fb_height);

  /**
   * Make sure keyboard/mouse work with imgui.
   */
  ImGui_ImplGlfw_InitForOther(window, true);

  /**
   * Retrieve the native window handle to pass on to nicegraf.
   */
  uintptr_t native_window_handle = 0;
#if defined(_WIN32) || defined(_WIN64)
  native_window_handle = (uintptr_t)glfwGetWin32Window(window);
#elif defined(__APPLE__)
  native_window_handle = (uintptr_t)ngf_samples::get_glfw_contentview(window);
#else
  native_window_handle = (uintptr_t)glfwGetX11Window(window);
#endif

// Begin Context Scope 
  {
  /**
   * Configure the swapchain and create a nicegraf context.
   * Use an sRGB color attachment and a 32-bit float depth attachment. Enable MSAA with
   * the highest supported framebuffer sample count.
   */
  const ngf_sample_count main_render_target_sample_count = ngf_get_device_capabilities()->max_supported_framebuffer_color_sample_count;
  const ngf_swapchain_info swapchain_info                  = {
                       .color_format  = NGF_IMAGE_FORMAT_BGRA8_SRGB,
                       .depth_format  = NGF_IMAGE_FORMAT_DEPTH32,
                       .sample_count  = main_render_target_sample_count,
                       .capacity_hint = 3u,
                       .width         = (uint32_t)fb_width,
                       .height        = (uint32_t)fb_height,
                       .native_handle = native_window_handle,
                       .present_mode  = NGF_PRESENTATION_MODE_FIFO};
  const ngf_context_info ctx_info = {.swapchain_info = &swapchain_info, .shared_context = nullptr};
  ngf::context           context;
  NGF_SAMPLES_CHECK_NGF_ERROR(context.initialize(ctx_info));

  /**
   * Make the newly created context current on this thread.
   * Once a context has been made current on a thread, it cannot be switched to another thread,
   * and another context cannot be made current on that thread.
   */
  NGF_SAMPLES_CHECK_NGF_ERROR(ngf_set_context(context));

  /**
   * This is the nicegraf-based rendering backend for ImGui - we will initialize it
   * on first frame.
   */
  std::optional<ngf_samples::ngf_imgui> imgui_backend;

  /**
   * Main command buffer that samples will record rendering commands into.
   */
  ngf::cmd_buffer main_cmd_buffer;
  NGF_SAMPLES_CHECK_NGF_ERROR(main_cmd_buffer.initialize(ngf_cmd_buffer_info {}));

  /**
   * Pointer to sample-specific data, returned by sample_initialize.
   * It shall be passed to the sample on every frame.
   */
  void* sample_opaque_data = nullptr;

  /**
   * Main loop. Exit when either the window closes or `poll_events` returns false, indicating that
   * the application has received a request to exit.
   */
  bool first_frame      = true;
  auto prev_frame_start = std::chrono::system_clock::now();
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    auto                               frame_start  = std::chrono::system_clock::now();
    const std::chrono::duration<float> time_delta   = frame_start - prev_frame_start;
    float                              time_delta_f = time_delta.count();
    prev_frame_start                                = frame_start;

    if (glfwGetKey(window, GLFW_KEY_C) == GLFW_PRESS) { ngf_renderdoc_capture_next_frame(); }
      
    /**
     * Query the updated size of the window and handle resize events.
     */
    const int old_fb_width = fb_width, old_fb_height = fb_height;
    glfwGetFramebufferSize(window, &fb_width, &fb_height);
    bool       resize_successful = true;
    const bool need_resize       = (fb_width != old_fb_width || fb_height != old_fb_height);
    if (need_resize) {
      ngf_samples::logd(
          "window resizing detected, calling ngf_resize context. "
          "old size: %d x %d; new size: %d x %d",
          old_fb_width,
          old_fb_height,
          fb_width,
          fb_height);
      resize_successful &=
          (NGF_ERROR_OK == ngf_resize_context(context, (uint32_t)fb_width, (uint32_t)fb_height));
    }

    if (resize_successful) {
      /**
       * Begin the frame and start the main command buffer.
       */
      ngf_frame_token frame_token;
      if (ngf_begin_frame(&frame_token) != NGF_ERROR_OK) continue;
      NGF_SAMPLES_CHECK_NGF_ERROR(ngf_start_cmd_buffer(main_cmd_buffer, frame_token));

      /**
       * On first frame, initialize the sample and the ImGui rendering backend.
       */
      if (first_frame) {
        /**
         * Start a new transfer command encoder for uploading resources to the GPU.
         */
        ngf_xfer_encoder xfer_encoder {};
        ngf_xfer_pass_info xfer_pass_info {};
        NGF_SAMPLES_CHECK_NGF_ERROR(ngf_cmd_begin_xfer_pass(main_cmd_buffer, &xfer_pass_info, &xfer_encoder));

        /**
         * Initialize the sample, and save the opaque data pointer.
         */
        ngf_samples::logi("Initializing sample");
        sample_opaque_data = ngf_samples::sample_initialize(
            (uint32_t)fb_width,
            (uint32_t)fb_height,
            main_render_target_sample_count,
            xfer_encoder);

        /**
         * Exit if sample failed to initialize.
         */
        if (sample_opaque_data == nullptr) {
          ngf_samples::loge("Sample failed to initialize");
          break;
        }
        ngf_samples::logi("Sample initialized");

        /**
         * Initialize the ImGui rendering backend.
         */
        imgui_backend.emplace(
            xfer_encoder,
            main_render_target_sample_count,
            imgui_font_atlas_bytes,
            imgui_font_atlas_width,
            imgui_font_atlas_height);

        /**
         * Finish the transfer encoder.
         */
        NGF_SAMPLES_CHECK_NGF_ERROR(ngf_cmd_end_xfer_pass(xfer_encoder));
      }

      /**
       * Let the sample code record any commands prior to the main render pass, and specify which compute passes to
       * wait on (if any).
       */
      ngf_sync_compute_resource main_render_pass_sync_resources[64u] = {};
      ngf_samples::main_render_pass_sync_info main_pass_sync_info {
          .nsync_compute_resources = 0u,
          .sync_compute_resources  = main_render_pass_sync_resources};
      ngf_samples::sample_pre_draw_frame(main_cmd_buffer, &main_pass_sync_info, sample_opaque_data);
      assert(main_pass_sync_info.nsync_compute_resources < 64u);
      ngf_render_encoder main_render_encoder_handle;

      /**
       * Record the commands for the main render pass.
       */
      {
        /**
         * Begin the main render pass.
         */
        ngf::render_encoder main_render_pass_encoder(
            main_cmd_buffer,
            ngf_default_render_target(),
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0,
            main_pass_sync_info.nsync_compute_resources,
            main_pass_sync_info.sync_compute_resources);

        /**
         * Call into the sample code to draw a single frame.
         */
        static float t = 0.0;
        ngf_samples::sample_draw_frame(
            main_render_pass_encoder,
            time_delta_f,
            frame_token,
            (uint32_t)fb_width,
            (uint32_t)fb_height,
            t,
            sample_opaque_data);
        t += 0.008f;

        /**
         * Begin a new ImGui frame.
         */
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        /**
         * Call into the sample-specific code to execute ImGui UI commands, and end ImGui frame.
         */
        ngf_samples::sample_draw_ui(sample_opaque_data);
        ImGui::EndFrame();

        /**
         * Draw the UI on top of everything else.
         */
        imgui_backend->record_rendering_commands(main_render_pass_encoder);

        /**
         * The C++ wrappers for command encoders end the pass automatically when the wrapper object goes out of scope.
         * However, the raw handle remains valid to use for synchronization purposes.
         */
        main_render_encoder_handle = main_render_pass_encoder;
      }

      /**
       * Let the sample record commands after the main render pass.
       */
      ngf_samples::sample_post_draw_frame(
          main_cmd_buffer,
          main_render_encoder_handle,
          sample_opaque_data);

      /**
       * Submit the main command buffer and end the frame.
       */
      ngf_cmd_buffer submitted_cmd_bufs[] = {main_cmd_buffer.get()};
      NGF_SAMPLES_CHECK_NGF_ERROR(ngf_submit_cmd_buffers(1, submitted_cmd_bufs));
      ngf_samples::sample_post_submit(sample_opaque_data);
      if (ngf_end_frame(frame_token) != NGF_ERROR_OK) {
        ngf_samples::loge("failed to present image to swapchain!");
      }
    } else {
      ngf_samples::loge("failed to handle window resize!");
    }
    first_frame = false;
  }

  /**
   * De-initialize any sample-specific data, shut down ImGui.
   */
  ngf_samples::logi("Finishing execution");
  ngf_samples::sample_shutdown(sample_opaque_data);
  ImGui::DestroyContext(imgui_ctx);
  } // End Context Scope

  ngf_shutdown();

  return 0;
}
