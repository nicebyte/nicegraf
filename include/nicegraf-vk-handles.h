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
 * Returns a uintptr_t to the underlying VkDevice. The caller is responsible for casting the return
 * value to a VkDevice.
 */
uintptr_t ngf_get_vk_device_handle() NGF_NOEXCEPT;

/**
 * \ingroup ngf
 *
 * Returns a uintptr_t to the underlying VkInstance. The caller is responsible for casting the return
 * value to a VkInstance.
 */
uintptr_t ngf_get_vk_instance_handle() NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Returns a uintptr_t to the underlying VkImage. The caller is responsible for casting the return
 * value to a VkImage.
 *
 * @param image A handle to a nicegraf image.
 */
uintptr_t ngf_get_vk_image_handle(ngf_image image) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Returns a uintptr_t to the underlying VkBuffer. The caller is responsible for casting the return
 * value to a VkBuffer.
 *
 * @param buffer A handle to a nicegraf buffer.
 */
uintptr_t ngf_get_vk_buffer_handle(ngf_buffer buffer) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Returns a uintptr_t to the underlying VkCommandBuffer. The caller is responsible for casting
 * the return value to a VkCommandBuffer.
 *
 * @param cmd_buffer A handle to a nicegraf command buffer.
 */
uintptr_t ngf_get_vk_cmd_buffer_handle(ngf_cmd_buffer cmd_buffer) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Returns a uintptr_t to the underlying VkSampler. The caller is responsible for casting the
 * return value to a VkSampler.
 *
 * @param sampler A handle to a nicegraf sampler.
 */
uintptr_t ngf_get_vk_sampler_handle(ngf_sampler sampler) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 *
 * Returns a uint32_t representing the underlying VkFormat. The caller is responsible for casting the return
 * value to a VkFormat.
 *
 * @param format A nicegraf image format.
 */
uint32_t ngf_get_vk_image_format_index(ngf_image_format format) NGF_NOEXCEPT;

#ifdef __cplusplus
}
#endif

