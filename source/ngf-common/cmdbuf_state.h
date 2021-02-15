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

#pragma once

#include "nicegraf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  NGFI_CMD_BUFFER_NEW,
  NGFI_CMD_BUFFER_READY,
  NGFI_CMD_BUFFER_RECORDING,
  NGFI_CMD_BUFFER_AWAITING_SUBMIT,
  NGFI_CMD_BUFFER_SUBMITTED
} ngfi_cmd_buffer_state;

#define NGFI_CMD_BUF_RECORDABLE(s) \
  (s == NGFI_CMD_BUFFER_READY || s == NGFI_CMD_BUFFER_AWAITING_SUBMIT)

ngf_error ngfi_transition_cmd_buf(
    ngfi_cmd_buffer_state* cur_state,
    bool                   has_active_renderpass,
    ngfi_cmd_buffer_state  new_state);

#define NGFI_TRANSITION_CMD_BUF(b, new_state)                                                    \
  if (ngfi_transition_cmd_buf(&(b)->state, (b)->renderpass_active, new_state) != NGF_ERROR_OK) { \
    return NGF_ERROR_INVALID_OPERATION;                                                          \
  }

#ifdef __cplusplus
}
#endif
