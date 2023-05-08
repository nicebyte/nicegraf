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

#include <stdarg.h>
#include <stdio.h>

namespace ngf_samples {

inline void vlog_msg(const char* prefix, const char* fmt, va_list args) {
  fprintf(stderr, "\n[%s] ", prefix);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
}

inline void vloge(const char* fmt, va_list args) {
  vlog_msg("E", fmt, args);
}

inline void vlogi(const char* fmt, va_list args) {
  vlog_msg("I", fmt, args);
}

inline void vlogd(const char* fmt, va_list args) {
  vlog_msg("D", fmt, args);
}

inline void loge(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vloge(fmt, args);
  va_end(args);
}

inline void logi(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlogi(fmt, args);
  va_end(args);
}

inline void logd(const char* fmt, ...) {
#if !defined(NDEBUG)
  va_list args;
  va_start(args, fmt);
  vlogd(fmt, args);
  va_end(args);
#else
  (void)fmt;
#endif
}

}  // namespace ngf_samples
