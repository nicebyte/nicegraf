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

#include "camera-controller.h"
#include "imgui.h"
#define _USE_MATH_DEFINES
#include <math.h>

namespace ngf_samples {

camera_matrices compute_camera_matrices(const camera_state& state, float aspect_ratio) {
  const float      r = state.radius, azimuth = state.azimuth, incline = state.inclination;
  const nm::float3 point_on_sphere {r * sinf(azimuth)*sinf(incline), r * cosf(incline), r * sinf(incline) * cosf(azimuth) };
  return {
      nm::look_at(state.look_at + point_on_sphere, state.look_at, nm::float3 {0.0f, 1.0f, 0.0f}),
      nm::perspective(nm::deg2rad(state.vfov), aspect_ratio, 0.01f, 1000.0f)};
}

void camera_ui(camera_state& state) {
  ImGui::Text("camera");
  ImGui::SliderFloat3("look at", state.look_at.data, -10.0f, 10.0f, "%.1f", 0);
  ImGui::SliderFloat("azimuth", &state.azimuth, 0.0f, (float)M_PI * 2.0f, "%.1f", 0);
  ImGui::SliderFloat("inclination", &state.inclination, 0.0f, (float)M_PI, "%.1f", 0);
  ImGui::SliderFloat("radius", &state.radius, 1.0f, 10.0f, "%.f", 0);
  ImGui::DragFloat("fov", &state.vfov, 0.08f, 25.0f, 90.0f);
}

}  // namespace ngf_samples