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

#pragma once
#include "nicegraf.h"

#ifdef __cplusplus
extern "C" {
#endif 

/**
 * \ingroup ngf
 *
 * Returns a uintptr_t to the underlying MTLTexture. The caller is responsible for casting the return
 * value to a MTLTexture.
 *
 * @param image A handle to a nicegraf image.
 */
uintptr_t ngf_get_mtl_image_handle(ngf_image image) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 *
 * Returns a uintptr_t to the underlying MTLBuffer. The caller is responsible for casting the return
 * value to a MTLBuffer.
 *
 * @param buffer A handle to a nicegraf buffer.
 */
uintptr_t ngf_get_mtl_buffer_handle(ngf_buffer buffer) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 *
 * Returns a uintptr_t to the underlying MTLSamplerState. The caller is responsible for casting the
 * return value to a MTLSamplerState.
 *
 * @param sampler A handle to a nicegraf sampler.
 */
uintptr_t ngf_get_mtl_sampler_handle(ngf_sampler sampler) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 *
 * Returns a uintptr_t to the underlying MTLCommandBuffer. The caller is responsible for casting
 * the return value to a MTLCommandBuffer.
 *
 * @param cmd_buffer A handle to a nicegraf command buffer.
 */
uintptr_t ngf_get_mtl_cmd_buffer_handle(ngf_cmd_buffer cmd_buffer) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 *
 * Returns a uintptr_t to the underlying MTLRenderCommandEncoder. The caller is responsible for casting
 * the return value to a MTLRenderCommandEncoder.
 *
 * @param cmd_buffer A handle to a nicegraf command buffer.
 */
uintptr_t ngf_get_mtl_render_encoder_handle(ngf_render_encoder render_encoder) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 *
 * Returns a uintptr_t to the underlying MTLBlitCommandEncoder. The caller is responsible for casting
 * the return value to a MTLBlitCommandEncoder.
 *
 * @param cmd_buffer A handle to a nicegraf command buffer.
 */
uintptr_t ngf_get_mtl_xfer_encoder_handle(ngf_xfer_encoder xfer_encoder) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 *
 * Returns a uintptr_t to the underlying MTLComputeCommandEncoder. The caller is responsible for casting
 * the return value to a MTLComputeCommandEncoder.
 *
 * @param cmd_buffer A handle to a nicegraf command buffer.
 */
uintptr_t ngf_get_mtl_compute_encoder_handle(ngf_compute_encoder compute_encoder) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 *
 * Returns a uint32_t representing the underlying MTLPixelFormat. The caller is responsible for casting the return
 * value to a MTLPixelFormat.
 *
 * @param format A nicegraf image format.
 */
uint32_t ngf_get_mtl_pixel_format_index(ngf_image_format format) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 *
 * Returns a uintptr_t to the underlying MTLDevice. The caller is responsible for casting the return value
 * to a MTLDevice.
 */
uintptr_t ngf_get_mtl_device() NGF_NOEXCEPT;

/**
 * TODO: Add comment
 */
void ngf_mtl_set_sample_attachment_for_next_compute_pass( ngf_cmd_buffer cmd_buffer, uintptr_t sample_buf_attachment_descriptor ) NGF_NOEXCEPT;

/**
 * TODO: Add comment
 */
void ngf_mtl_set_sample_attachment_for_next_render_pass( ngf_cmd_buffer cmd_buffer, uintptr_t sample_buf_attachment_descriptor ) NGF_NOEXCEPT;

#ifdef __cplusplus
}
#endif 

