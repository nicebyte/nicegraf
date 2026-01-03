/**
 * Copyright (c) 2026 nicegraf contributors
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

#include "ngf-common/macros.h"
#include "nicegraf.h"

namespace ngfi {

enum cmd_buffer_state {
  CMD_BUFFER_STATE_NEW,
  CMD_BUFFER_STATE_READY,
  CMD_BUFFER_STATE_RECORDING,
  CMD_BUFFER_STATE_READY_TO_SUBMIT,
  CMD_BUFFER_STATE_PENDING,
  CMD_BUFFER_STATE_SUBMITTED
};

template<class CmdBufT> bool transition_cmd_buf(CmdBufT cmd_buf, cmd_buffer_state new_state) {
  cmd_buffer_state cur_state = cmd_buf->state;
  bool             has_active_pass =
      (cmd_buf)->renderpass_active || (cmd_buf)->compute_pass_active || (cmd_buf)->xfer_pass_active;
  bool is_recordable = cur_state == ::ngfi::CMD_BUFFER_STATE_READY ||
                       cur_state == ::ngfi::CMD_BUFFER_STATE_READY_TO_SUBMIT;
  switch (new_state) {
  case ngfi::CMD_BUFFER_STATE_NEW:
    NGFI_DIAG_ERROR("command buffer cannot go back to a `new` state");
    return false;
  case ngfi::CMD_BUFFER_STATE_READY:
    if (cur_state != ngfi::CMD_BUFFER_STATE_SUBMITTED &&
        cur_state != ngfi::CMD_BUFFER_STATE_READY && cur_state != ngfi::CMD_BUFFER_STATE_NEW) {
      NGFI_DIAG_ERROR("command buffer not in a startable state.");
      return false;
    }
    break;
  case ngfi::CMD_BUFFER_STATE_RECORDING:
    if (!is_recordable) {
      NGFI_DIAG_ERROR("command buffer not in a recordable state.");
      return false;
    }
    break;
  case ngfi::CMD_BUFFER_STATE_READY_TO_SUBMIT:
    if (cur_state != ngfi::CMD_BUFFER_STATE_RECORDING) {
      NGFI_DIAG_ERROR("command buffer is not actively recording.");
      return false;
    }
    if (has_active_pass) {
      NGFI_DIAG_ERROR("cannot finish render encoder with an unterminated pass.");
      return false;
    }
    break;
  case ngfi::CMD_BUFFER_STATE_PENDING:
    if (cur_state != ngfi::CMD_BUFFER_STATE_READY_TO_SUBMIT &&
        cur_state != ngfi::CMD_BUFFER_STATE_READY) {
      NGFI_DIAG_ERROR("command buffer not ready to be submitted");
      return false;
    }
    break;
  case ngfi::CMD_BUFFER_STATE_SUBMITTED:
    if (cur_state != ngfi::CMD_BUFFER_STATE_PENDING) {
      NGFI_DIAG_ERROR("command buffer not in a submittable state");
      return false;
    }
    break;
  }
  cmd_buf->state = new_state;
  return true;
}

}  // namespace ngfi

#define NGFI_TRANSITION_CMD_BUF(b, new_state) \
  if (!ngfi::transition_cmd_buf(b, new_state)) { return NGF_ERROR_INVALID_OPERATION; }
