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

#include <stdint.h>
#include <stddef.h>

namespace ngf_samples {

/**
 * Decodes an RLE-encoded true color targa file with an optional
 * alpha channel into the target buffer.
 * Assumes the source file uses sRGB color space.
 * If `out_buf` is non-NULL, raw RGBA values, in sRGB, with
 * premultiplied alpha, will be written to it. The width and
 * height of the image are returned in the output parameters.
 * If `out_buf` is NULL, no decoding is performed, however
 * the width and height of the image are still returned.
 */
void load_targa(
    const void* in_buf,
    size_t      in_buf_size,
    void*       out_buf,
    size_t      out_buf_size,
    uint32_t*   width_px,
    uint32_t*   height_px);

}  // namespace ngf_samples
