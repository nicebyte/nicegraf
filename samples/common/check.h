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

#include "logging.h"
#include "nicegraf-util.h"

#include <stdio.h>
#include <stdlib.h>

#pragma warning(disable:26812)

namespace ngf_samples {

#define NGF_SAMPLES_CHECK_NGF_ERROR(expr)                         \
  {                                                               \
    const ngf_error err = (expr);                                 \
    if (err != NGF_ERROR_OK) {                                    \
      ::ngf_samples::loge("nicegraf error %d (file %s line %d), aborting.\n", err, __FILE__, __LINE__); \
      fflush(stderr);                                             \
      abort();                                                    \
    }                                                             \
  }

#define NGF_SAMPLES_ASSERT(expr)                                                                 \
  {                                                                                              \
    if (!(expr)) {                                                                               \
      ::ngf_samples::loge("assertion %s failed (file %s line %d)\n", #expr, __FILE__, __LINE__); \
      fflush(stderr);                                                                            \
      abort();                                                                                   \
    }                                                                                            \
  }

}  // namespace ngf_samples
