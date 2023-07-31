/**
 * Copyright (c) 2023 nicegraf contributors
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

#include "check.h"
#include "logging.h"

#include "nicegraf-wrappers.h"
#include "nicegraf.h"

#include "test-interface.h"
#include <chrono>
#include <optional>
#include <stdio.h>

#if defined(__APPLE__)
#define NGF_TESTS_COMMON_MAIN apple_main
#else
#define NGF_TESTS_COMMON_MAIN main
#endif

int NGF_SAMPLES_COMMON_MAIN(int, char**){
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
  NGF_SAMPLES_CHECK_NGF_ERROR(ngf_get_device_list(&devices, &ndevices));
  const char* device_perf_tier_names[NGF_DEVICE_PERFORMANCE_TIER_COUNT] = {
      "high",
      "low",
      "unknown"};
  /**
   * For the sample code, we try to select a high-perf tier device. If one isn't available, we just
   * fall back on the first device in the list. You may want to choose a different strategy for your
   * specific application, or allow the user to pick.
   */
  size_t high_power_device_idx = (~0u);
  ngf_samples::logi("available rendering devices: ");
  for (uint32_t i = 0; i < ndevices; ++i) {
    /**
     * If no preferred index has been selected yet, and the current device is high-power, pick it as
     * preferred. otherwise, just log the device details.
     */
    ngf_samples::logi(
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
  ngf_samples::logi("selected device %d", preferred_device_idx);

  /*
   * Initialize nicegraf.
   * Set our rendering device preference to "discrete" to pick a high-power GPU if one is available,
   * and install a diagnostic callback.
   */
  const ngf_diagnostic_info diagnostic_info {
      .verbosity = diagnostics_verbosity,
      .userdata  = nullptr,
      .callback  = ngf_samples::sample_diagnostic_callback};

  const ngf_init_info init_info {
      .diag_info            = &diagnostic_info,
      .allocation_callbacks = NULL,
      .device               = device_handle,
      .renderdoc_info       = (renderdoc_info.renderdoc_lib_path != NULL) ? &renderdoc_info : NULL};
  NGF_SAMPLES_CHECK_NGF_ERROR(ngf_initialize(&init_info));
}