#include "nicegraf.h"
#include "test-util.h"
#include "nicegraf-wrappers.h"
#include "nicegraf-util.h"
#include "shader-loader.h"
#include "check.h"

#include <stdio.h>

#if defined(__APPLE__)
#define NGF_TESTS_COMMON_MAIN apple_main
#else
#define NGF_TESTS_COMMON_MAIN main
#endif

void ngf_test_draw(ngf_image output_image){
  
}

int NGF_SAMPLES_COMMON_MAIN(int, char**){
  // ngf_test_init(...): initializes nicegraf; common for all tests

  // ngf_test_draw(ngf_image output_image): initializes test and draws the test render to output_image

  // ngf_validate_result(ngf_image, const char*): if false, save the output_image to log the issue. if true, test is passed
}