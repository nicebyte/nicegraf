#pragma warning(disable : 4244) // conversion from 'int' to 'float', possible loss of data

#include "common.h"

#include "check.h"
#include "diagnostic-callback.h"
#include "imgui-backend.h"
#include "logging.h"
#include "nicegraf-mtl-handles.h"
#include "nicegraf-wrappers.h"
#include "sample-interface.h"

#include <chrono>
#include <iostream>
#include <optional>
#include <stdio.h>

#if defined(__APPLE__)
#include "TargetConditionals.h"
#endif

namespace ngf_samples {

NGF_WINDOW_TYPE* window;

/**
 * Imgui
 */
unsigned char* imgui_font_atlas_bytes;
int            imgui_font_atlas_width, imgui_font_atlas_height;
ImGuiContext*  imgui_ctx;
/**
 * This is the nicegraf-based rendering backend for ImGui - we will initialize it
 * on first frame.
 */
std::optional<ngf_samples::ngf_imgui> imgui_backend;

ngf_sample_count main_render_target_sample_count;
ngf::context     context;

ngf::cmd_buffer main_cmd_buffer;

/**
 * Pointer to sample-specific data, returned by sample_initialize.
 * It shall be passed to the sample on every frame.
 */
void* sample_opaque_data = nullptr;

std::chrono::time_point<std::chrono::system_clock> prev_frame_start;
bool                                               first_frame;
int fb_width, fb_height;

int init() {
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
  NGF_MISC_CHECK_NGF_ERROR(ngf_get_device_list(&devices, &ndevices));
  const char* device_perf_tier_names[NGF_DEVICE_PERFORMANCE_TIER_COUNT] = {
      "high",
      "low",
      "unknown"};
  /**
   * For the sample code, we try to select a high-perf tier device. If one isn't available, we
   * just fall back on the first device in the list. You may want to choose a different strategy
   * for your specific application, or allow the user to pick.
   */
  size_t high_power_device_idx = (~0u);
  ngf_misc::logi("available rendering devices: ");
  for (uint32_t i = 0; i < ndevices; ++i) {
    /**
     * If no preferred index has been selected yet, and the current device is high-power, pick
     * it as preferred. otherwise, just log the device details.
     */
    ngf_misc::logi(
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
  ngf_misc::logi("selected device %d", preferred_device_idx);

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
   * Set our rendering device preference to "discrete" to pick a high-power GPU if one is
   * available, and install a diagnostic callback.
   */
  const ngf_diagnostic_info diagnostic_info {
      .verbosity = diagnostics_verbosity,
      .userdata  = nullptr,
      .callback  = ngf_samples::sample_diagnostic_callback,
      .enable_debug_groups = true };

  const ngf_init_info init_info {
      .diag_info            = &diagnostic_info,
      .allocation_callbacks = NULL,
      .device               = device_handle,
      .renderdoc_info       = (renderdoc_info.renderdoc_lib_path != NULL) ? &renderdoc_info : NULL};
  NGF_MISC_CHECK_NGF_ERROR(ngf_initialize(&init_info));

  /**
   * Initialize imgui and generate its font atlas.
   */
  imgui_ctx = ImGui::CreateContext();
  ImGui::SetCurrentContext(imgui_ctx);
  ImGui::GetIO().Fonts->GetTexDataAsRGBA32(
      &imgui_font_atlas_bytes,
      &imgui_font_atlas_width,
      &imgui_font_atlas_height);

  ngf_samples::init_graphics_library();

  /**
   * Create a window.
   * The `width` and `height` here refer to the dimensions of the window's "client area", i.e.
   * the area that can actually be rendered to (excludes borders and any other decorative
   * elements). The dimensions we request are a hint, we need to get the actual dimensions after
   * the window is created. Note that we deliberately create the window before setting up the
   * nicegraf context. This is done so that when the destructors are invoked, the context is
   * destroyed before the window - changing this sequence of events might lead to misbehavior.
   */
  constexpr uint32_t window_width_hint = 800, window_height_hint = 600;

  ngf_samples::create_window(window_width_hint, window_height_hint, "nicegraf sample", window);

  if (window == nullptr) {
    ngf_misc::loge("Failed to create a window, exiting.");
    return 0;
  }
  ngf_samples::get_framebuffer_size(window, &fb_width, &fb_height);
  ngf_misc::logi("created a window with client area of size size %d x %d.", fb_width, fb_height);

  ImGuiIO& io    = ImGui::GetIO();
  io.DisplaySize = ImVec2(fb_width, fb_height);

  /**
   * Make sure keyboard/mouse work with imgui.
   */
  ngf_samples::init_imgui(window);

  /**
   * Retrieve the native window handle to pass on to nicegraf.
   */
  uintptr_t native_window_handle = ngf_samples::get_native_handle(window);

  /**
   * Configure the swapchain and create a nicegraf context.
   * Use an sRGB color attachment and a 32-bit float depth attachment. Enable MSAA with
   * the highest supported framebuffer sample count.
   */
  main_render_target_sample_count =
      ngf_get_device_capabilities()->max_supported_framebuffer_color_sample_count;
  const ngf_swapchain_info swapchain_info = {
      .color_format  = NGF_IMAGE_FORMAT_BGRA8_SRGB,
      .colorspace    = NGF_COLORSPACE_SRGB_NONLINEAR,
      .depth_format  = NGF_IMAGE_FORMAT_DEPTH32,
      .sample_count  = main_render_target_sample_count,
      .capacity_hint = 3u,
      .width         = (uint32_t)fb_width,
      .height        = (uint32_t)fb_height,
      .native_handle = native_window_handle,
      .present_mode  = NGF_PRESENTATION_MODE_FIFO};
  const ngf_context_info ctx_info = {.swapchain_info = &swapchain_info, .shared_context = nullptr};

  NGF_MISC_CHECK_NGF_ERROR(context.initialize(ctx_info));

  /**
   * Make the newly created context current on this thread.
   * Once a context has been made current on a thread, it cannot be switched to another
   * thread, and another context cannot be made current on that thread.
   */
  NGF_MISC_CHECK_NGF_ERROR(ngf_set_context(context));

  /**
   * Main command buffer that samples will record rendering commands into.
   */
  NGF_MISC_CHECK_NGF_ERROR(main_cmd_buffer.initialize(ngf_cmd_buffer_info {}));

  first_frame      = true;
  prev_frame_start = std::chrono::system_clock::now();

  return 1;
}

void run_loop() {
  /**
   * Main loop. Exit when either the window closes or `poll_events` returns false, indicating
   * that the application has received a request to exit.
   */
  while (!ngf_samples::window_should_close(window)) { draw_frame(); }
}

void draw_frame() {
  ngf_samples::poll_events();

  auto                               frame_start  = std::chrono::system_clock::now();
  const std::chrono::duration<float> time_delta   = frame_start - prev_frame_start;
  float                              time_delta_f = time_delta.count();
  prev_frame_start                                = frame_start;

  if (ngf_samples::get_key(window, 67) == 1) { ngf_renderdoc_capture_next_frame(); }

  /**
   * Query the updated size of the window and handle resize events.
   */
  const int old_fb_width = fb_width, old_fb_height = fb_height;
  ngf_samples::get_framebuffer_size(window, &fb_width, &fb_height);
  bool       resize_successful = true;
  const bool need_resize       = (fb_width != old_fb_width || fb_height != old_fb_height);
  if (need_resize) {
    ngf_misc::logd(
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
    if (ngf_begin_frame(&frame_token) != NGF_ERROR_OK) return;
    NGF_MISC_CHECK_NGF_ERROR(ngf_start_cmd_buffer(main_cmd_buffer, frame_token));

    /**
     * On first frame, initialize the sample and the ImGui rendering backend.
     */
    if (first_frame) {
      ngf_cmd_begin_debug_group(main_cmd_buffer, "Initial GPU uploads");
      /**
       * Start a new transfer command encoder for uploading resources to the GPU.
       */
      ngf_xfer_encoder   xfer_encoder {};
      ngf_xfer_pass_info xfer_pass_info {};
      NGF_MISC_CHECK_NGF_ERROR(
          ngf_cmd_begin_xfer_pass(main_cmd_buffer, &xfer_pass_info, &xfer_encoder));

      /**
       * Initialize the sample, and save the opaque data pointer.
       */
      ngf_misc::logi("Initializing sample");
      sample_opaque_data = ngf_samples::sample_initialize(
          (uint32_t)fb_width,
          (uint32_t)fb_height,
          main_render_target_sample_count,
          xfer_encoder);

      /**
       * Exit if sample failed to initialize.
       */
      if (sample_opaque_data == nullptr) {
        ngf_misc::loge("Sample failed to initialize");
        throw;
      }
      ngf_misc::logi("Sample initialized");

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
      NGF_MISC_CHECK_NGF_ERROR(ngf_cmd_end_xfer_pass(xfer_encoder));
      ngf_cmd_end_current_debug_group(main_cmd_buffer);
    }

    /**
     * Let the sample code record any commands prior to the main render pass.
     */
    ngf_cmd_begin_debug_group(main_cmd_buffer, "Sample pre-draw frame");
    ngf_samples::sample_pre_draw_frame(main_cmd_buffer, sample_opaque_data);
    ngf_cmd_end_current_debug_group(main_cmd_buffer);

    /**
     * Record the commands for the main render pass.
     */
    ngf_cmd_begin_debug_group(main_cmd_buffer, "Main render pass");
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
          0);

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
      ngf_samples::begin_imgui_frame(window);
      ImGui::NewFrame();

      /**
       * Call into the sample-specific code to execute ImGui UI commands, and end
       ImGui frame.
       */
      ngf_samples::sample_draw_ui(sample_opaque_data);
      ImGui::EndFrame();

      /**
       * Draw the UI on top of everything else.
       */
      imgui_backend->record_rendering_commands(main_render_pass_encoder);
    }
    ngf_cmd_end_current_debug_group(main_cmd_buffer);

    /**
     * Let the sample record commands after the main render pass.
     */
    ngf_cmd_begin_debug_group(main_cmd_buffer, "Sample post-draw frame");
    ngf_samples::sample_post_draw_frame(main_cmd_buffer, sample_opaque_data);
    ngf_cmd_end_current_debug_group(main_cmd_buffer);

    /**
     * Submit the main command buffer and end the frame.
     */
    ngf_cmd_buffer submitted_cmd_bufs[] = {main_cmd_buffer.get()};
    NGF_MISC_CHECK_NGF_ERROR(ngf_submit_cmd_buffers(1, submitted_cmd_bufs));
    ngf_samples::sample_post_submit(sample_opaque_data);
    if (ngf_end_frame(frame_token) != NGF_ERROR_OK) {
      ngf_misc::loge("failed to present image to swapchain!");
    }
  } else {
    ngf_misc::loge("failed to handle window resize!");
  }
  first_frame = false;
}

void process_input() {
}

void shutdown() {
  /**
   * De-initialize any sample-specific data, shut down ImGui.
   */
  ngf_misc::logi("Finishing execution");
  ngf_samples::sample_shutdown(sample_opaque_data);
  ImGui::DestroyContext(imgui_ctx);

  ngf_shutdown();
}
}  // namespace ngf_samples
