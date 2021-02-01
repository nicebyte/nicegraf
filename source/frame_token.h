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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t
ngfi_encode_frame_token(uint16_t ctx_id, uint8_t max_inflight_frames, uint8_t frame_id) {
  const uint32_t ctx_id_ext = ctx_id, max_inflight_frames_ext = max_inflight_frames,
                 frame_id_ext = frame_id;
  return (ctx_id_ext << 0x10) | (max_inflight_frames_ext << 0x08) | frame_id;
}

static inline uint32_t ngfi_frame_ctx_id(uint32_t frame_token) {
  return (frame_token >> 0x10) & 0xffff;
}

static inline uint32_t ngfi_frame_max_inflight_frames(uint32_t frame_token) {
  return (frame_token >> 0x08) & 0xff;
}

static inline uint32_t ngfi_frame_id(uint32_t frame_token) {
  return frame_token & 0xff;
}

#ifdef __cplusplus
}
#endif
