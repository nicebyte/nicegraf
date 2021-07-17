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
#include "nicegraf-exception.h"
#include "nicegraf.h"
#include "nicegraf-wrappers.h"
#include "imgui-backend.h"
#include "platform/window.h"
#include "sample-interface.h"

#include <stdio.h>
#include <chrono>

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
   * Create a window.
   * The `width` and `height` here refer to the dimensions of the window's "client area", i.e. the
   * area that can actually be rendered to (excludes borders and any other decorative elements). The
   * dimensions we request are a hint, we need to get the actual dimensions after the window is
   * created.
   */
  constexpr uint32_t  window_width_hint = 800, window_height_hint = 600;
  ngf_samples::window window =
      ngf_samples::window_create("nicegraf sample", window_width_hint, window_height_hint);
  uint32_t fb_width, fb_height;
  ngf_samples::window_get_size(window, &fb_width, &fb_height);

  /**
   * Configure the swapchain and create a nicegraf context.
   * Use an sRGB color attachment and a 32-bit float depth attachment. Enable MSAA with 8 samples
   * per pixel.
   */
  const ngf_swapchain_info swapchain_info = {
      .color_format  = NGF_IMAGE_FORMAT_BGRA8_SRGB,
      .depth_format  = NGF_IMAGE_FORMAT_DEPTH32,
      .sample_count  = NGF_SAMPLE_COUNT_8,
      .capacity_hint = 3u,
      .width         = fb_width,
      .height        = fb_height,
      .native_handle = ngf_samples::window_native_handle(window),
      .present_mode  = NGF_PRESENTATION_MODE_FIFO};
  const ngf_context_info ctx_info = {.swapchain_info = &swapchain_info, .shared_context = nullptr};
  ngf_context            context;
  NGF_SAMPLES_CHECK(ngf_create_context(&ctx_info, &context));

  /**
   * Make the newly created context current on this thread.
   * Once a context has been made current on a thread, it cannot be switched * to another thread,
   * and another context cannot be made current on that * thread.
   */
  NGF_SAMPLES_CHECK(ngf_set_context(context));

  /**
   * At this point, the sample can do any initialization specific to itself.
   * The initialization returns an opaque pointer, which we pass on when calling into
   * sample-specific code.
   */
  void* sample_opaque_data = ngf_samples::sample_initialize(fb_width, fb_height);

  /**
   * Main loop. Exit when either the window closes or `poll_events` returns false, indicating that
   * the application has received a request to exit.
   */
  while (!ngf_samples::window_is_closed(window) && ngf_samples::window_poll_events()) {
    /**
     * Query the updated size of the window and handle resize events.
     */
    const uint32_t old_fb_width = fb_width, old_fb_height = fb_height;
    ngf_samples::window_get_size(window, &fb_width, &fb_height);
    bool resize_successful = true;
    if (fb_width != old_fb_width || fb_height != old_fb_height) {
      resize_successful &= (NGF_ERROR_OK == ngf_resize_context(context, fb_width, fb_height));
    }

    /**
     * Begin frame, call into sample-specific code to perform rendering, and end frame.
     */
    if (resize_successful) {
      ngf_frame_token frame_token;
      ngf_begin_frame(&frame_token);
      ngf_samples::sample_draw_frame(frame_token, fb_width, fb_height, .0, sample_opaque_data);
      ngf_samples::sample_draw_ui(sample_opaque_data);
      ngf_end_frame(frame_token);
    }
  }

  /**
   * De-initialize any sample-specific data, destroy the nicegraf context and destroy the window, in
   * that order. Destroying the window before destroying the context associated with it may result
   * in misbehavior.
   */
  ngf_samples::sample_shutdown(sample_opaque_data);
  ngf_destroy_context(context);
  ngf_samples::window_destroy(window);

  return 0;
}