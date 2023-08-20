#include "nicegraf-util.h"
#include "nicegraf-wrappers.h"
#include "nicegraf.h"

void ngf_test_draw(ngf::image& output_image, ngf_frame_token frame_token);
bool ngf_validate_result(
    ngf::image*     output_image,
    const char*     ref_image,
    ngf_frame_token frame_token);

int main(int, char*[]) {
  uint32_t          ndevices = 0u;
  const ngf_device* devices  = nullptr;
  ngf_get_device_list(&devices, &ndevices);
  size_t device_idx = ndevices <= 0 ? (~0u) : 0u;
   const ngf_device_handle device_handle = devices[device_idx].handle;

  const ngf_init_info init_info {
      .diag_info            = nullptr,
      .allocation_callbacks = nullptr,
      .device               = device_handle,
      .renderdoc_info       = nullptr};
  ngf_initialize(&init_info);

  const ngf_context_info ctx_info = {.swapchain_info = nullptr, .shared_context = nullptr};
  ngf::context           context;

  context.initialize(ctx_info);
  ngf_set_context(context);

  ngf::cmd_buffer main_cmd_buffer;
  main_cmd_buffer.initialize(ngf_cmd_buffer_info {});
  ngf_frame_token frame_token;
  if (ngf_begin_frame(&frame_token) == NGF_ERROR_OK) return {};
  ngf_start_cmd_buffer(main_cmd_buffer, frame_token);

  // [PENDING] Create an ngf_image to render to
  ngf::image           output_image;
  const ngf_extent3d   img_size {512u, 512u, 1u};
  const ngf_image_info img_info {
      NGF_IMAGE_TYPE_IMAGE_2D,
      img_size,
      1u,
      1u,
      NGF_IMAGE_FORMAT_BGRA8_SRGB,
      NGF_SAMPLE_COUNT_1,
      NGF_IMAGE_USAGE_SAMPLE_FROM | NGF_IMAGE_USAGE_ATTACHMENT};
  output_image.initialize(img_info);

  // ngf_test_draw(...): initializes test and draws the test render to output_image
  ngf_test_draw(output_image, frame_token);
  ngf_cmd_buffer submitted_cmd_bufs[] = {main_cmd_buffer.get()};
  ngf_submit_cmd_buffers(1, submitted_cmd_bufs);
  ngf_end_frame(frame_token);

  // ngf_validate_result(ngf_image, const char*): if false, save the output_image to log the issue.
  // if true, test is passed
  if (!ngf_validate_result(&output_image, "references/triangle_reference.data", frame_token)) {
    // Print Test failure message
    printf("[NICEGRAF TEST] Test failed. Please view output image in the output directory.\n");
  }

  ngf_shutdown();


  return 0;
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
  ngf_cmd_begin_xfer_pass(xfer_cmd_buf, &xfer_pass_info, &xfer_encoder);
  ngf_cmd_copy_image_to_buffer(xfer_encoder, src_image, src_offset, src_extent, 1u, dst_buffer, 0u);
  ngf_cmd_end_xfer_pass(xfer_encoder);
  ngf_submit_cmd_buffers(1, &xfer_cmd_buf);
  ngf_destroy_cmd_buffer(xfer_cmd_buf);
  ngf_finish();

  void* host_dst = ngf_buffer_map_range(dst_buffer, 0u, 512u * 512u * 4u);

  // compare host_dst to references/ref_image
  size_t buff_size = 512u * 512u * 4u;
  FILE*  file      = fopen(ref_image, "rb");

  if (file == nullptr) return false;
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
