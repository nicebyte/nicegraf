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

#include "sample-interface.h"
#include "diagnostic-callback.h"
#include "platform/window.h"

#include "nicegraf.h"

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
      .callback  = ngf_samples::sample_diagnostic_callback
    }
   };
  const ngf_error init_error = ngf_initialize(&init_info);
  

  ngf_samples::window w = ngf_samples::window_create("nicegraf sample", 800, 600);
  uint32_t cw, ch;
  ngf_samples::window_get_size(w, &cw, &ch);
  printf("created %dx%d window\n", cw, ch);
  void* userdata =  ngf_samples::sample_initialize(0, 0);
  while(!ngf_samples::window_is_closed(w) &&
         ngf_samples::window_poll_events()) {
    ngf_samples::sample_draw_frame(0, 0, 0, .0, userdata);
    ngf_samples::sample_draw_ui(userdata);
  }
  ngf_samples::sample_shutdown(userdata);
  ngf_samples::window_destroy(w);
  return 0;
}