/**
 * Copyright (c) 2021 nicegraf contributors
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

#include "diagnostic-callback.h"
#include "imgui-backend.h"
#include "nicegraf-exception.h"
#include "nicegraf-wrappers.h"
#include "nicegraf.h"
#include "platform/window.h"
#include "sample-interface.h"

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

  /*
   * Begin by initializing nicegraf.
   * Set our rendering device preference to "discrete" to pick a high-power GPU if one is available,
   * and install a diagnostic callback.
   */
  const ngf_init_info init_info {
      .device_pref = NGF_DEVICE_PREFERENCE_DISCRETE,
      .diag_info   = {
          .verbosity = diagnostics_verbosity,
          .userdata  = nullptr,
          .callback  = ngf_samples::sample_diagnostic_callback}};
  NGF_SAMPLES_CHECK(ngf_initialize(&init_info));

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
   * Create a window.
   * The `width` and `height` here refer to the dimensions of the window's "client area", i.e. the
   * area that can actually be rendered to (excludes borders and any other decorative elements). The
   * dimensions we request are a hint, we need to get the actual dimensions after the window is
   * created.
   * Note that we deliberately create the window before setting up the nicegraf context. This is
   * done so that when the destructors are invoked, the context is destroyed before the window -
   * changing this sequence of events might lead to misbehavior.
   */
  constexpr uint32_t  window_width_hint = 800, window_height_hint = 600;
  ngf_samples::window window {"nicegraf sample", window_width_hint, window_height_hint};
  uint32_t            fb_width, fb_height;
  window.get_size(&fb_width, &fb_height);

  /**
   * Configure the swapchain and create a nicegraf context.
   * Use an sRGB color attachment and a 32-bit float depth attachment. Enable MSAA with 8 samples
   * per pixel.
   */
  const ngf_sample_count   main_render_target_sample_count = NGF_SAMPLE_COUNT_8;
  const ngf_swapchain_info swapchain_info                  = {
      .color_format  = NGF_IMAGE_FORMAT_BGRA8_SRGB,
      .depth_format  = NGF_IMAGE_FORMAT_DEPTH32,
      .sample_count  = main_render_target_sample_count,
      .capacity_hint = 3u,
      .width         = fb_width,
      .height        = fb_height,
      .native_handle = window.native_handle(),
      .present_mode  = NGF_PRESENTATION_MODE_FIFO};
  const ngf_context_info ctx_info = {.swapchain_info = &swapchain_info, .shared_context = nullptr};
  ngf::context           context;
  NGF_SAMPLES_CHECK(context.initialize(ctx_info));

  /**
   * Make the newly created context current on this thread.
   * Once a context has been made current on a thread, it cannot be switched * to another thread,
   * and another context cannot be made current on that * thread.
   */
  NGF_SAMPLES_CHECK(ngf_set_context(context));

  /**
   * This is the nicegraf-based rendering backend for ImGui - we will initialize it
   * on first frame.
   */
  std::optional<ngf_samples::ngf_imgui> imgui_backend;

  /**
   * Main command buffer that samples will record rendering commands into.
   */
  ngf::cmd_buffer main_cmd_buffer;
  NGF_SAMPLES_CHECK(main_cmd_buffer.initialize(ngf_cmd_buffer_info {}));

  /**
   * Pointer to sample-specific data, returned by sample_initialize.
   * It shall be passed to the sample on every frame.
   */
  void* sample_opaque_data = nullptr;

  /**
   * Main loop. Exit when either the window closes or `poll_events` returns false, indicating that
   * the application has received a request to exit.
   */
  bool first_frame = true;
  while (!window.is_closed() && ngf_samples::poll_events()) {
    ImGui::NewFrame();
    /**
     * Query the updated size of the window and handle resize events.
     */
    const uint32_t old_fb_width = fb_width, old_fb_height = fb_height;
    window.get_size(&fb_width, &fb_height);
    bool resize_successful = true;
    if (fb_width != old_fb_width || fb_height != old_fb_height) {
      resize_successful &= (NGF_ERROR_OK == ngf_resize_context(context, fb_width, fb_height));
    }

    if (resize_successful) {
      /**
       * Begin the frame and start the main command buffer.
       */
      ngf_frame_token frame_token;
      NGF_SAMPLES_CHECK(ngf_begin_frame(&frame_token));
      NGF_SAMPLES_CHECK(ngf_start_cmd_buffer(main_cmd_buffer, frame_token));

      /**
       * On first frame, initialize the sample and the ImGui rendering backend.
       */
      if (first_frame) {
        /**
         * Start a new transfer command encoder for uploading resources to the GPU.
         */
        ngf_xfer_encoder xfer_encoder {};
        NGF_SAMPLES_CHECK(ngf_cmd_begin_xfer_pass(main_cmd_buffer, &xfer_encoder));

        /**
         * Initialize the sample, and save the opaque data pointer.
         */
        sample_opaque_data = ngf_samples::sample_initialize(
            fb_width,
            fb_height,
            main_render_target_sample_count,
            xfer_encoder);

        /**
         * Initialize the ImGui rendering backend.
         */
        imgui_backend.emplace(
            xfer_encoder,
            imgui_font_atlas_bytes,
            imgui_font_atlas_width,
            imgui_font_atlas_height);

        /**
         * Finish the transfer encoder.
         */
        NGF_SAMPLES_CHECK(ngf_cmd_end_xfer_pass(xfer_encoder));

        first_frame = false;
      }

      /**
       * Begin the main render pass.
       */
      ngf_render_encoder main_render_pass;
      ngf_cmd_begin_render_pass_simple(
          main_cmd_buffer,
          ngf_default_render_target(),
          0.0f,
          0.0f,
          0.0f,
          0.0f,
          0.0f,
          0,
          &main_render_pass);

      /**
       * Call into the sample code to draw a single frame.
       */
      ngf_samples::sample_draw_frame(
          main_render_pass,
          frame_token,
          fb_width,
          fb_height,
          .0,
          sample_opaque_data);

      /**
       * Call into the sample-specific code to execute ImGui UI commands.
       */
      ngf_samples::sample_draw_ui(sample_opaque_data);
      ImGui::ShowDemoWindow();

      /**
       * Draw the UI on top of everything else.
       */
      imgui_backend->record_rendering_commands(main_render_pass);

      /**
       * Finish the main render pass, submit the command buffer and end the frame.
       */
      NGF_SAMPLES_CHECK(ngf_cmd_end_render_pass(main_render_pass));
      ngf_cmd_buffer submitted_cmd_bufs[] = { main_cmd_buffer.get() };
      NGF_SAMPLES_CHECK(ngf_submit_cmd_buffers(1, submitted_cmd_bufs));
      NGF_SAMPLES_CHECK(ngf_end_frame(frame_token));
    }
  }

  /**
   * De-initialize any sample-specific data, shut down ImGui.
   */
  ngf_samples::sample_shutdown(sample_opaque_data);
  ImGui::DestroyContext(imgui_ctx);

  return 0;
}