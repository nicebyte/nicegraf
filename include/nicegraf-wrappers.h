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

#include "nicegraf-util.h"
#include "nicegraf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <utility>

namespace ngf {

/**
 * Convenience macro to allow easily propagating nicegraf errors.
 */
#define NGF_RETURN_IF_ERROR(expr)        \
  {                                      \
    const ngf_error tmp = (expr);        \
    if (tmp != NGF_ERROR_OK) return tmp; \
  }

/**
 * A move-only RAII wrapper over nicegraf handles that provides
 * unique ownership semantics
 */
template<class T, class ObjectManagementFuncs> class ngf_handle {
  public:
  explicit ngf_handle(T raw) : handle_(raw) {
  }
  ngf_handle() : handle_(nullptr) {
  }
  ngf_handle(const ngf_handle&) = delete;
  ngf_handle(ngf_handle&& other) : handle_(nullptr) {
    *this = std::move(other);
  }
  ~ngf_handle() {
    destroy_if_necessary();
  }

  ngf_handle& operator=(const ngf_handle&) = delete;
  ngf_handle& operator                     =(ngf_handle&& other) noexcept {
    destroy_if_necessary();
    handle_       = other.handle_;
    other.handle_ = nullptr;
    return *this;
  }

  typedef typename ObjectManagementFuncs::InitType init_type;

  ngf_error initialize(const typename ObjectManagementFuncs::InitType& info) {
    destroy_if_necessary();
    return ObjectManagementFuncs::create(&info, &handle_);
  }

  T get() {
    return handle_;
  }
  const T get() const {
    return handle_;
  }
  T release() {
    T tmp   = handle_;
    handle_ = nullptr;
    return tmp;
  }
  operator T() {
    return handle_;
  }
  operator const T() const {
    return handle_;
  }

  void reset(T new_handle) {
    destroy_if_necessary();
    handle_ = new_handle;
  }

  private:
  void destroy_if_necessary() {
    if (handle_) {
      ObjectManagementFuncs::destroy(handle_);
      handle_ = nullptr;
    }
  }

  T handle_;
};

#define NGF_DEFINE_WRAPPER_TYPE(name)                              \
  struct ngf_##name##_ManagementFuncs {                            \
    using InitType = ngf_##name##_info;                            \
    static ngf_error create(const InitType* info, ngf_##name* r) { \
      return ngf_create_##name(info, r);                           \
    }                                                              \
    static void destroy(ngf_##name handle) {                       \
      ngf_destroy_##name(handle);                                  \
    }                                                              \
  };                                                               \
  using name = ngf_handle<ngf_##name, ngf_##name##_ManagementFuncs>;

NGF_DEFINE_WRAPPER_TYPE(shader_stage);
NGF_DEFINE_WRAPPER_TYPE(graphics_pipeline);
NGF_DEFINE_WRAPPER_TYPE(image);
NGF_DEFINE_WRAPPER_TYPE(sampler);
NGF_DEFINE_WRAPPER_TYPE(render_target);
NGF_DEFINE_WRAPPER_TYPE(buffer);
NGF_DEFINE_WRAPPER_TYPE(context);
NGF_DEFINE_WRAPPER_TYPE(cmd_buffer);

class render_encoder {
  public:
  explicit render_encoder(ngf_cmd_buffer cmd_buf, const ngf_pass_info& pass_info) {
    ngf_cmd_begin_render_pass(cmd_buf, &pass_info, &enc_);
  }

  explicit render_encoder(
      ngf_cmd_buffer    cmd_buf,
      ngf_render_target rt,
      float             clear_color_r,
      float             clear_color_g,
      float             clear_color_b,
      float             clear_color_a,
      float             clear_depth,
      uint32_t          clear_stencil) {
    ngf_cmd_begin_render_pass_simple(
        cmd_buf,
        rt,
        clear_color_r,
        clear_color_g,
        clear_color_b,
        clear_color_a,
        clear_depth,
        clear_stencil,
        &enc_);
  }

  ~render_encoder() {
    if (enc_.__handle) ngf_cmd_end_render_pass(enc_);
  }

  render_encoder(render_encoder&& other) noexcept {
    *this = std::move(other);
  }

  render_encoder& operator=(render_encoder&& other) noexcept {
    enc_                = other.enc_;
    other.enc_.__handle = 0u;
    return *this;
  }

  render_encoder(const render_encoder&) = delete;
  render_encoder& operator=(const render_encoder&) = delete;

  operator ngf_render_encoder() {
    return enc_;
  }

  private:
  ngf_render_encoder enc_ {};
};

class xfer_encoder {
  public:
  explicit xfer_encoder(ngf_cmd_buffer cmd_buf) {
    ngf_cmd_begin_xfer_pass(cmd_buf, &enc_);
  }

  ~xfer_encoder() {
    if (enc_.__handle) ngf_cmd_end_xfer_pass(enc_);
  }

  xfer_encoder(xfer_encoder&& other) noexcept {
    *this = std::move(other);
  }

  xfer_encoder& operator=(xfer_encoder&& other) noexcept {
    enc_                = other.enc_;
    other.enc_.__handle = 0u;
    return *this;
  }

  xfer_encoder(const xfer_encoder&) = delete;
  xfer_encoder& operator=(const xfer_encoder&) = delete;

  operator ngf_xfer_encoder() {
    return enc_;
  }

  private:
  ngf_xfer_encoder enc_;
};

template<uint32_t S> struct descriptor_set {
  template<uint32_t B> struct binding {
    static ngf_resource_bind_op texture(const ngf_image image) {
      ngf_resource_bind_op op;
      op.type                     = NGF_DESCRIPTOR_IMAGE;
      op.target_binding           = B;
      op.target_set               = S;
      op.info.image_sampler.image = image;
      return op;
    }

    static ngf_resource_bind_op uniform_buffer(const ngf_buffer buf, size_t offset, size_t range) {
      ngf_resource_bind_op op;
      op.type               = NGF_DESCRIPTOR_UNIFORM_BUFFER;
      op.target_binding     = B;
      op.target_set         = S;
      op.info.buffer.buffer = buf;
      op.info.buffer.offset = offset;
      op.info.buffer.range  = range;
      return op;
    }

    static ngf_resource_bind_op
    texel_buffer(const ngf_buffer buf, size_t offset, size_t range, ngf_image_format fmt) {
      ngf_resource_bind_op op;
      op.type               = NGF_DESCRIPTOR_TEXEL_BUFFER;
      op.target_binding     = B;
      op.target_set         = S;
      op.info.buffer.buffer = buf;
      op.info.buffer.offset = offset;
      op.info.buffer.range  = range;
      op.info.buffer.format = fmt;
      return op;
    }

    static ngf_resource_bind_op sampler(const ngf_sampler sampler) {
      ngf_resource_bind_op op;
      op.type                       = NGF_DESCRIPTOR_SAMPLER;
      op.target_binding             = B;
      op.target_set                 = S;
      op.info.image_sampler.sampler = sampler;
      return op;
    }

    static ngf_resource_bind_op
    texture_and_sampler(const ngf_image image, const ngf_sampler sampler) {
      ngf_resource_bind_op op;
      op.type                       = NGF_DESCRIPTOR_IMAGE_AND_SAMPLER;
      op.target_binding             = B;
      op.target_set                 = S;
      op.info.image_sampler.image   = image;
      op.info.image_sampler.sampler = sampler;
      return op;
    }
  };
};

template<class... Args> void cmd_bind_resources(ngf_render_encoder enc, const Args&&... args) {
  const ngf_resource_bind_op ops[] = {args...};
  ngf_cmd_bind_resources(enc, ops, sizeof(ops) / sizeof(ngf_resource_bind_op));
}

inline ngf_image_ref image_ref(const ngf_image img) {
  return {img, 0, 0, NGF_CUBEMAP_FACE_POSITIVE_X};
}

inline ngf_image_ref image_ref(const ngf_image img, uint32_t mip) {
  return {img, mip, 0, NGF_CUBEMAP_FACE_POSITIVE_X};
}

inline ngf_image_ref image_ref(const ngf_image img, uint32_t mip, uint32_t layer) {
  return {img, mip, layer, NGF_CUBEMAP_FACE_POSITIVE_X};
}

inline ngf_image_ref image_ref(const ngf_image img, uint32_t mip, ngf_cubemap_face face) {
  return {img, mip, 0, face};
}

inline ngf_image_ref
image_ref(const ngf_image img, uint32_t mip, uint32_t layer, ngf_cubemap_face face) {
  return {img, mip, layer, face};
}

/**
 * A convenience class for dynamically updated structured uniform data.
 */
template<typename T> class uniform_multibuffer {
  public:
  uniform_multibuffer() = default;
  uniform_multibuffer(uniform_multibuffer&& other) {
    *this = std::move(other);
  }
  uniform_multibuffer(const uniform_multibuffer&) = delete;

  uniform_multibuffer& operator=(uniform_multibuffer&& other) = default;
  uniform_multibuffer& operator=(const uniform_multibuffer&) = delete;

  ngf_error initialize(const uint32_t frames) {
    const size_t alignment    = ngf_get_device_capabilities()->uniform_buffer_offset_alignment;
    const size_t aligned_size = ngf_util_align_size(sizeof(T), alignment);
    NGF_RETURN_IF_ERROR(buf_.initialize(ngf_buffer_info {
        aligned_size * frames,
        NGF_BUFFER_STORAGE_HOST_WRITEABLE,
        NGF_BUFFER_USAGE_UNIFORM_BUFFER}));
    nframes_                = frames;
    aligned_per_frame_size_ = aligned_size;
    return NGF_ERROR_OK;
  }

  void write(const T& data) {
    current_offset_  = (frame_)*aligned_per_frame_size_;
    void* mapped_buf = ngf_buffer_map_range(buf_.get(), current_offset_, aligned_per_frame_size_);
    memcpy(mapped_buf, (void*)&data, sizeof(T));
    ngf_buffer_flush_range(buf_.get(), 0, aligned_per_frame_size_);
    ngf_buffer_unmap(buf_.get());
    frame_ = (frame_ + 1u) % nframes_;
  }

  ngf_resource_bind_op bind_op_at_current_offset(
      uint32_t set,
      uint32_t binding,
      size_t   additional_offset = 0,
      size_t   range             = 0) const {
    ngf_resource_bind_op op;
    op.type               = NGF_DESCRIPTOR_UNIFORM_BUFFER;
    op.target_binding     = binding;
    op.target_set         = set;
    op.info.buffer.buffer = buf_.get();
    op.info.buffer.offset = current_offset_ + additional_offset;
    op.info.buffer.range  = (range == 0) ? aligned_per_frame_size_ : range;
    return op;
  }

  private:
  buffer   buf_;
  uint32_t frame_                  = 0;
  size_t   current_offset_         = 0;
  size_t   aligned_per_frame_size_ = 0;
  uint32_t nframes_                = 0;
};

}  // namespace ngf
