#include "cmdbuf_state.h"
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
  case NGFI_CMD_BUFFER_AWAITING_SUBMIT:
    if (*cur_state != NGFI_CMD_BUFFER_RECORDING) {
      NGFI_DIAG_ERROR("command buffer is not actively recording.");
      return NGF_ERROR_INVALID_OPERATION;
    }
    if (has_active_renderpass) {
      NGFI_DIAG_ERROR("cannot finish render encoder with an unterminated render pass.");
      return NGF_ERROR_INVALID_OPERATION;
    }
    break;
  case NGFI_CMD_BUFFER_SUBMITTED:
    if (*cur_state != NGFI_CMD_BUFFER_AWAITING_SUBMIT) {
      NGFI_DIAG_ERROR("command buffer not in a submittable state");
      return NGF_ERROR_INVALID_OPERATION;
    }
  }
  *cur_state = new_state;
  return NGF_ERROR_OK;
}
