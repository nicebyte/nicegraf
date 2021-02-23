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

#include "diagnostic-callback.h"

#include "logging.h"

#include <stdarg.h>

namespace ngf_samples {

void sample_diagnostic_callback(ngf_diagnostic_message_type msg_type, void*, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  switch (msg_type) {
  case NGF_DIAGNOSTIC_ERROR:
  case NGF_DIAGNOSTIC_WARNING:
    vloge(fmt, args);
    break;
  case NGF_DIAGNOSTIC_INFO:
    vlogi(fmt, args);
    break;
  default:;
  }
  va_end(args);
}

}  // namespace ngf_samples