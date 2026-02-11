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

// NOTE: this file is meant to be included from the backend implementation file.

namespace ngfi {
template<class T, class InfoT> ngf_error generic_create(const InfoT& info, T** result) {
  auto maybe_t = T::make(info);
  if (!maybe_t.has_error()) result[0] = maybe_t.value().release();
  return maybe_t.has_error() ? maybe_t.error() : NGF_ERROR_OK;
}
}  // namespace ngfi

ngf_error ngf_create_context(const ngf_context_info* info, ngf_context* result) NGF_NOEXCEPT {
  assert(info);
  assert(result);
  return ngfi::generic_create(*info, result);
}

void ngf_destroy_context(ngf_context ctx) NGF_NOEXCEPT {
  // TODO: unset current context
  assert(ctx);
  NGFI_FREE(ctx);
}

ngf_error
ngf_create_shader_stage(const ngf_shader_stage_info* info, ngf_shader_stage* result) NGF_NOEXCEPT {
  assert(info);
  assert(result);
  return ngfi::generic_create(*info, result);
}

void ngf_destroy_shader_stage(ngf_shader_stage stage) NGF_NOEXCEPT {
  if (stage != nullptr) { NGFI_FREE(stage); }
}

ngf_error ngf_create_render_target(const ngf_render_target_info* info, ngf_render_target* result)
    NGF_NOEXCEPT {
  assert(info);
  assert(result);

  return ngfi::generic_create(*info, result);
}

void ngf_destroy_render_target(ngf_render_target rt) NGF_NOEXCEPT {
  if (rt != nullptr) { 
    if (rt->is_default) {
      NGFI_DIAG_ERROR("default RT can only be destroyed by owning context\n");
      return;
    }
    NGFI_FREE(rt);
   }
}

ngf_error ngf_create_compute_pipeline(
    const ngf_compute_pipeline_info* info,
    ngf_compute_pipeline*            result) NGF_NOEXCEPT {
  assert(info);
  assert(result);
  return ngfi::generic_create(*info, result);
}

ngf_error ngf_create_graphics_pipeline(
    const ngf_graphics_pipeline_info* info,
    ngf_graphics_pipeline*            result) NGF_NOEXCEPT {
  assert(info);
  assert(result);
  return ngfi::generic_create(*info, result);
}

void ngf_destroy_graphics_pipeline(ngf_graphics_pipeline pipe) NGF_NOEXCEPT {
  if (pipe != nullptr) { NGFI_FREE(pipe); }
}

void ngf_destroy_compute_pipeline(ngf_compute_pipeline pipe) NGF_NOEXCEPT {
  if (pipe != nullptr) { NGFI_FREE(pipe); }
}


ngf_error ngf_create_texel_buffer_view(
    const ngf_texel_buffer_view_info* info,
    ngf_texel_buffer_view*            result) NGF_NOEXCEPT {
  assert(info);
  assert(result);
  return ngfi::generic_create(*info, result);
}

void ngf_destroy_texel_buffer_view(ngf_texel_buffer_view buf_view) NGF_NOEXCEPT {
  if (buf_view) { NGFI_FREE(buf_view); }
}

ngf_error ngf_create_buffer(const ngf_buffer_info* info, ngf_buffer* result) NGF_NOEXCEPT {
  assert(info);
  assert(result);
  return ngfi::generic_create(*info, result);
}

void ngf_destroy_buffer(ngf_buffer buf) NGF_NOEXCEPT {
  if (buf != nullptr) { NGFI_FREE(buf); }
}


ngf_error ngf_create_sampler(const ngf_sampler_info* info, ngf_sampler* result) NGF_NOEXCEPT {
  assert(info);
  assert(result);
  return ngfi::generic_create(*info, result);
}

void ngf_destroy_sampler(ngf_sampler sampler) NGF_NOEXCEPT {
  if (sampler) { NGFI_FREE(sampler); }
}

ngf_error
ngf_create_cmd_buffer(const ngf_cmd_buffer_info* info, ngf_cmd_buffer* result) NGF_NOEXCEPT {
  assert(info);
  assert(result);
  return ngfi::generic_create(*info, result);
}

ngf_error
ngf_create_image_view(const ngf_image_view_info* info, ngf_image_view* result) NGF_NOEXCEPT {
  assert(info);
  assert(result);
  return ngfi::generic_create(*info, result);
}

void ngf_destroy_image_view(ngf_image_view view) NGF_NOEXCEPT {
  if (view != nullptr) { NGFI_FREE(view); }
}

ngf_error ngf_create_image(const ngf_image_info* info, ngf_image* result) NGF_NOEXCEPT {
  assert(info);
  assert(result);
  return ngfi::generic_create(*info, result);
}

void ngf_destroy_image(ngf_image image) NGF_NOEXCEPT {
  if (image != nullptr) { NGFI_FREE(image); }
}

void ngf_destroy_cmd_buffer(ngf_cmd_buffer cmd_buffer) NGF_NOEXCEPT {
  if (cmd_buffer != nullptr) { NGFI_FREE(cmd_buffer); }
}
