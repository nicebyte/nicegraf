#include "sample-interface.h"

#include <stdio.h>

namespace ngf_samples {

struct sample_data {
  uint32_t magic_number = 0xdeadbeef;
};

void* sample_initialize(uint32_t , uint32_t ) {
  printf("sample initializing.\n");
  auto d = new sample_data{};
  d->magic_number = 0xbadf00d;
  printf("sample initialization complete.\n");
  return static_cast<void*>(d);
}

void sample_draw_frame(
    ngf_frame_token frame_token,
    uint32_t        width,
    uint32_t        height,
    float           time,
    void*           userdata) {
  auto data = static_cast<sample_data*>(userdata);
  printf("drawing frame %d (w %d h %d) at time %f magic number 0x%x\n", frame_token, width, height, time, data->magic_number);
}

void sample_draw_ui(void*) {
}

void sample_shutdown(void* userdata) {
  auto data = static_cast<sample_data*>(userdata);
  delete data;
  printf("shutting down\n");
}

}
