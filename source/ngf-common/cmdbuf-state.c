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

#include "cmdbuf-state.h"

#include "macros.h"

ngf_error ngfi_transition_cmd_buf(
    ngfi_cmd_buffer_state* cur_state,
    bool                   has_active_renderpass,
    ngfi_cmd_buffer_state  new_state) {
  switch (new_state) {
  case NGFI_CMD_BUFFER_NEW:
    NGFI_DIAG_ERROR("command buffer cannot go back to a `new` state");
    return NGF_ERROR_INVALID_OPERATION;
  case NGFI_CMD_BUFFER_READY:
    if (*cur_state != NGFI_CMD_BUFFER_SUBMITTED && *cur_state != NGFI_CMD_BUFFER_READY &&
        *cur_state != NGFI_CMD_BUFFER_NEW) {
      NGFI_DIAG_ERROR("command buffer not in a startable state.");
      return NGF_ERROR_INVALID_OPERATION;
    }
    break;
  case NGFI_CMD_BUFFER_RECORDING:
    if (!NGFI_CMD_BUF_RECORDABLE(*cur_state)) {
      NGFI_DIAG_ERROR("command buffer not in a recordable state.");
      return NGF_ERROR_INVALID_OPERATION;
    }
    break;
  case NGFI_CMD_BUFFER_READY_TO_SUBMIT:
    if (*cur_state != NGFI_CMD_BUFFER_RECORDING) {
      NGFI_DIAG_ERROR("command buffer is not actively recording.");
      return NGF_ERROR_INVALID_OPERATION;
    }
    if (has_active_renderpass) {
      NGFI_DIAG_ERROR("cannot finish render encoder with an unterminated render pass.");
      return NGF_ERROR_INVALID_OPERATION;
    }
    break;
  case NGFI_CMD_BUFFER_PENDING:
    if (*cur_state != NGFI_CMD_BUFFER_READY_TO_SUBMIT) {
      NGFI_DIAG_ERROR("command buffer not ready to be submitted");
      return NGF_ERROR_INVALID_OPERATION;
    }
    break;
  case NGFI_CMD_BUFFER_SUBMITTED:
    if (*cur_state != NGFI_CMD_BUFFER_PENDING) {
      NGFI_DIAG_ERROR("command buffer not in a submittable state");
      return NGF_ERROR_INVALID_OPERATION;
    }
    break;
  }
  *cur_state = new_state;
  return NGF_ERROR_OK;
}
