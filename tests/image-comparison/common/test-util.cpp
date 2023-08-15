#include "test-util.h"

#include "check.h"
#include "nicegraf-util.h"
#include "nicegraf-wrappers.h"
#include "nicegraf.h"

struct test_info ngf_test_init(
    ngf_diagnostic_info* diagnostic_info,
    uint32_t             fb_width,
    uint32_t             fb_height,
    uintptr_t            native_window_handle) {
  uint32_t          ndevices = 0u;
  const ngf_device* devices  = NULL;
  ngf_get_device_list(&devices, &ndevices);
  // const char* device_perf_tier_names[NGF_DEVICE_PERFORMANCE_TIER_COUNT] = {
  //     "high",
  //     "low",
  //     "unknown"};

  size_t high_power_device_idx = (~0u);
  for (uint32_t i = 0; i < ndevices; ++i) {
    if (high_power_device_idx == (~0u) &&
        devices[i].performance_tier == NGF_DEVICE_PERFORMANCE_TIER_HIGH) {
      high_power_device_idx = i;
    }
  }
  /* Fall back to 1st device if no high-power device was found. */
  const size_t preferred_device_idx = (high_power_device_idx == ~0u) ? 0 : high_power_device_idx;
  const ngf_device_handle device_handle = devices[preferred_device_idx].handle;

  const ngf_init_info init_info {
      .diag_info            = diagnostic_info,
      .allocation_callbacks = NULL,
      .device               = device_handle,
      .renderdoc_info       = NULL};
  ngf_initialize(&init_info);

  const ngf_sample_count main_render_target_sample_count =
      ngf_get_device_capabilities()->max_supported_framebuffer_color_sample_count;
  const ngf_swapchain_info swapchain_info = {
      .color_format  = NGF_IMAGE_FORMAT_BGRA8_SRGB,
      .depth_format  = NGF_IMAGE_FORMAT_DEPTH32,
      .sample_count  = main_render_target_sample_count,
      .capacity_hint = 3u,
      .width         = fb_width,
      .height        = fb_height,
      .native_handle = native_window_handle,
      .present_mode  = NGF_PRESENTATION_MODE_FIFO};
  const ngf_context_info ctx_info = {.swapchain_info = &swapchain_info, .shared_context = nullptr};
  ngf::context           context;

  context.initialize(ctx_info);
  ngf_set_context(context);

  ngf::cmd_buffer main_cmd_buffer;
  main_cmd_buffer.initialize(ngf_cmd_buffer_info {});
  ngf_frame_token frame_token;
  if (ngf_begin_frame(&frame_token) == NGF_ERROR_OK) return {};
  ngf_start_cmd_buffer(main_cmd_buffer, frame_token);

  struct test_info info = {.main_cmd_buffer = &main_cmd_buffer, .frame_token = frame_token};
  return info;
}

void ngf_test_shutdown(struct test_info* info) {
  // submit main cmd buffer
  ngf_cmd_buffer submitted_cmd_bufs[] = {info->main_cmd_buffer->get()};
  ngf_submit_cmd_buffers(1, submitted_cmd_bufs);
  // end frame
  ngf_end_frame(info->frame_token);
  // ngf_shutdown()
  ngf_shutdown();
}

bool ngf_validate_result(
    ngf::image*     output_image,
    const char*     ref_image,
    ngf_frame_token frame_token) {
  ngf_cmd_buffer      xfer_cmd_buf  = nullptr;
  ngf_cmd_buffer_info xfer_cmd_info = {};

  ngf_extent3d        src_extent = {512u, 512u, 1u};
  ngf_offset3d        src_offset = {0u, 0u, 0u};
  const ngf_image_ref src_image  = {
       .image        = output_image->get(),
       .mip_level    = 0u,
       .layer        = 0u,
       .cubemap_face = NGF_CUBEMAP_FACE_COUNT};
  ngf_buffer      dst_buffer;
  ngf_buffer_info dst_buffer_info = {
      .size         = 512u * 512u * 4u,
      .storage_type = NGF_BUFFER_STORAGE_HOST_READABLE,
      .buffer_usage = NGF_BUFFER_USAGE_XFER_DST};
  ngf_create_buffer(&dst_buffer_info, &dst_buffer);
  ngf_create_cmd_buffer(&xfer_cmd_info, &xfer_cmd_buf);
  ngf_start_cmd_buffer(xfer_cmd_buf, frame_token);
  ngf_xfer_encoder   xfer_encoder {};
  ngf_xfer_pass_info xfer_pass_info {};
  NGF_IMAGE_COMPARISON_CHECK_NGF_ERROR(
      ngf_cmd_begin_xfer_pass(xfer_cmd_buf, &xfer_pass_info, &xfer_encoder));
  ngf_cmd_copy_image_to_buffer(xfer_encoder, src_image, src_offset, src_extent, 1u, dst_buffer, 0u);
  NGF_IMAGE_COMPARISON_CHECK_NGF_ERROR(ngf_cmd_end_xfer_pass(xfer_encoder));
  ngf_submit_cmd_buffers(1, &xfer_cmd_buf);
  ngf_destroy_cmd_buffer(xfer_cmd_buf);
  ngf_finish();

  void* host_dst = ngf_buffer_map_range(dst_buffer, 0u, 512u * 512u * 4u);

  // compare host_dst to references/ref_image
  size_t buff_size = 512u * 512u * 4u;
  FILE*  file      = fopen(ref_image, "rb");

  if (file == NULL) return false;
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  if (file_size != buff_size) {
    fclose(file);
    return false;
  }

  fseek(file, 0, SEEK_SET);
  char* buffer = (char*)malloc(buff_size);
  fread(buffer, 1, buff_size, file);
  fclose(file);

  bool        equal    = true;
  const char* mem_data = (const char*)host_dst;
  for (size_t i = 0; i < buff_size; i++) {
    if (mem_data[i] != buffer[i]) {
      free(buffer);
      equal = false;
    }
  }
  if (!equal) {
    FILE* f = fopen("./output/triangle_test.data", "wb");
    fwrite(host_dst, 1u, 512 * 512 * 4, f);
    fclose(f);
  }
  return equal;
}
