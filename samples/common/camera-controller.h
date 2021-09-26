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

#include "nicemath.h"

#include <utility>

namespace ngf_samples {

struct camera_state {
  nm::float3 look_at {0.0f, 0.0f, 0.0f};
  float      radius      = 3.0f;
  float      azimuth     = 0.0f;
  float      inclination = 3.14f / 2.0f;
  float      vfov        = 60.0f;
};

struct camera_matrices {
  nm::float4x4 world_to_view_transform;
  nm::float4x4 view_to_clip_transform;
};

camera_matrices compute_camera_matrices(const camera_state& state, float aspect_ratio);
void            camera_ui(
               camera_state&           state,
               std::pair<float, float> look_at_range,
               float                   look_at_speed,
               std::pair<float, float> radius_range,
               float                   radius_speed);

}  // namespace ngf_samples