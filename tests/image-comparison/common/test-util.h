#include "nicegraf.h"
#include "nicegraf-wrappers.h"
#include "nicegraf-util.h"
#include "check.h"

struct render_to_texture_data {
  ngf::render_target default_rt;
  ngf::render_target offscreen_rt;
  ngf::graphics_pipeline blit_pipeline;
  ngf::graphics_pipeline offscreen_pipeline;
  ngf::image rt_texture;
  ngf::sampler sampler;
  ngf_frame_token frame_token;
};

bool ngf_validate_result(void* userdata, const char* ref_image)
{
  auto state = reinterpret_cast<render_to_texture_data*>(userdata);

  ngf_cmd_buffer      xfer_cmd_buf  = nullptr;
  ngf_cmd_buffer_info xfer_cmd_info = {};

  ngf_extent3d        src_extent = {512u, 512u, 1u};
  ngf_offset3d        src_offset = {0u, 0u, 0u};
  const ngf_image_ref src_image  = {
       .image        = state->rt_texture.get(),
       .mip_level    = 0u,
       .layer        = 0u,
       .cubemap_face = NGF_CUBEMAP_FACE_COUNT
  };
  ngf_buffer      dst_buffer;
  ngf_buffer_info dst_buffer_info = {
      .size         = 512u * 512u * 4u,
      .storage_type = NGF_BUFFER_STORAGE_HOST_READABLE,
      .buffer_usage = NGF_BUFFER_USAGE_XFER_DST
  };
  ngf_create_buffer(&dst_buffer_info, &dst_buffer);
  ngf_create_cmd_buffer(&xfer_cmd_info, &xfer_cmd_buf);
  ngf_start_cmd_buffer(xfer_cmd_buf, state->frame_token);
  ngf_xfer_encoder   xfer_encoder {};
  ngf_xfer_pass_info xfer_pass_info {};
  NGF_SAMPLES_CHECK_NGF_ERROR(ngf_cmd_begin_xfer_pass(xfer_cmd_buf, &xfer_pass_info, &xfer_encoder));
  ngf_cmd_copy_image_to_buffer(xfer_encoder, src_image, src_offset, src_extent, 1u, dst_buffer, 0u);
  NGF_SAMPLES_CHECK_NGF_ERROR(ngf_cmd_end_xfer_pass(xfer_encoder));
  ngf_submit_cmd_buffers(1, &xfer_cmd_buf);
  ngf_destroy_cmd_buffer(xfer_cmd_buf);
  ngf_finish();

  void* host_dst = ngf_buffer_map_range(dst_buffer, 0u, 512u * 512u * 4u);

  // compare host_dst to references/ref_image
  size_t buff_size = 512u*512u*4u;
  FILE* file = fopen(ref_image, "rb");

  if(file == NULL) return false;
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

  const char* mem_data = (const char*)host_dst;
    for (size_t i = 0; i < buff_size; i++) {
        if (mem_data[i] != buffer[i]) {  
            free(buffer);
            return false;
        }
    }
}