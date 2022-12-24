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

#include "catch.hpp"
#include "ngf-common/frame-token.h"

TEST_CASE("encode-decode frame token works") {
  const uint16_t  test_ctx_id = 65534u, test_max_inflight_frames = 3u, test_frame_id = 255u;
  const uint32_t test_token =
      ngfi_encode_frame_token(test_ctx_id, test_max_inflight_frames, test_frame_id);
  REQUIRE(test_ctx_id == ngfi_frame_ctx_id(test_token));
  REQUIRE(test_max_inflight_frames == ngfi_frame_max_inflight_frames(test_token));
  REQUIRE(test_frame_id == ngfi_frame_id(test_token));
}


