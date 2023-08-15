#include "check.h"
#include "nicegraf-util.h"
#include "nicegraf-wrappers.h"
#include "nicegraf.h"

struct test_info {
  ngf::cmd_buffer* main_cmd_buffer;
  ngf_frame_token  frame_token;
};

struct test_info ngf_test_init(
    ngf_diagnostic_info* diagnostic_info,
    uint32_t             fb_width,
    uint32_t             fb_height,
    uintptr_t            native_window_handle);

void ngf_test_shutdown(struct test_info* info);

bool ngf_validate_result(
    ngf::image*     output_image,
    const char*     ref_image,
    ngf_frame_token frame_token);
