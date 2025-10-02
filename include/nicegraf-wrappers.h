/**
 * Copyright (c) 2025 nicegraf contributors
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

/**
 * @file
 * \defgroup ngf_wrappers C++ Wrappers
 *
 * This module contains optional C++ wrappers for certain nicegraf structures and routines.
 */

namespace ngf {

namespace detail {

template<class T> struct remove_ref {
  using Type = T;
};
template<class T> struct remove_ref<T&> {
  using Type = T;
};
template<class T> struct remove_ref<T&&> {
  using Type = T;
};

template<class T> using remove_ref_t = typename remove_ref<T>::Type;

template<class T> constexpr T&& fwd(remove_ref_t<T>& x) noexcept {
  return (T&&)x;
}
template<class T> constexpr T&& fwd(remove_ref_t<T>&& x) noexcept {
  return (T&&)x;
}

template<class T> constexpr remove_ref_t<T>&& move(T&& x) noexcept {
  return (remove_ref_t<T>&&)x;
}

}  // namespace detail

/**
 * \ingroup ngf_wrappers
 *
 * A convenience macro to allow easily propagating nicegraf errors. The provided expression must
 * evaluate to a \ref ngf_error. If the result of the expression is not \ref NGF_ERROR_OK, the value
 * is returned from the calling function. Note: the calling function must also return an \ref
 * ngf_error.
 */
#define NGF_RETURN_IF_ERROR(expr)        \
  {                                      \
    const ngf_error tmp = (expr);        \
    if (tmp != NGF_ERROR_OK) return tmp; \
  }

/**
 * \ingroup ngf_wrappers
 *
 * A move-only RAII wrapper over nicegraf handles that provides unique ownership semantics.
 */
template<class T, class ObjectManagementFuncs> class ngf_handle {
  public:
  /** Wraps a raw handle to a nicegraf object. */
  explicit ngf_handle(T raw) : handle_(raw) {
  }

  /** Wraps a null handle. */
  ngf_handle() : handle_(nullptr) {
  }

  ngf_handle(const ngf_handle&) = delete;
  ngf_handle(ngf_handle&& other) : handle_(nullptr) {
    *this = detail::move(other);
  }

  /** Disposes of the owned handle, if it is not null. */
  ~ngf_handle() {
    destroy_if_necessary();
  }

  ngf_handle& operator=(const ngf_handle&) = delete;

  /** Takes ownership of the handle wrapped by another object. */
  ngf_handle& operator=(ngf_handle&& other) noexcept {
    destroy_if_necessary();
    handle_       = other.handle_;
    other.handle_ = nullptr;
    return *this;
  }

  typedef typename ObjectManagementFuncs::InitType init_type;

  /** Creates a new handle using the provided configuration, and takes ownership of it. */
  ngf_error initialize(const typename ObjectManagementFuncs::InitType& info) {
    destroy_if_necessary();
    const ngf_error err = ObjectManagementFuncs::create(&info, &handle_);
    if (err != NGF_ERROR_OK) handle_ = nullptr;
    return err;
  }

  struct make_result {
    ngf_handle      handle;
    const ngf_error error;
  };
  static make_result make(const init_type& info) {
    ngf_handle      handle;
    const ngf_error error = handle.initialize(info);
    return make_result {detail::move(handle), error};
  }

  /** @return The raw handle to the wrapped object. */
  T get() {
    return handle_;
  }

  /** @return The raw handle to the wrapped object. */
  const T get() const {
    return handle_;
  }

  /**
   * Relinquishes ownership of the wrapped object and returns a raw handle to it. After this call
   * completes, it is the responsibility of the calling code to dispose of the handle properly when
   * it is no longer needed.
   */
  T release() {
    T tmp   = handle_;
    handle_ = nullptr;
    return tmp;
  }

  /** Implicit conversion to the raw handle type. */
  operator T() {
    return handle_;
  }

  /** Implicit conversion to the raw handle type. */
  operator const T() const {
    return handle_;
  }

  /**
   * Wraps a raw handle to a nicegraf object.
   */
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

#define NGF_DEFINE_WRAPPER_MANAGEMENT_FUNCS(name)                  \
  struct ngf_##name##_ManagementFuncs {                            \
    using InitType = ngf_##name##_info;                            \
    static ngf_error create(const InitType* info, ngf_##name* r) { \
      return ngf_create_##name(info, r);                           \
    }                                                              \
    static void destroy(ngf_##name handle) {                       \
      ngf_destroy_##name(handle);                                  \
    }                                                              \
  };

#define NGF_DEFINE_WRAPPER_TYPE(name) \
  using name = ngf_handle<ngf_##name, ngf_##name##_ManagementFuncs>;

NGF_DEFINE_WRAPPER_MANAGEMENT_FUNCS(shader_stage);
NGF_DEFINE_WRAPPER_MANAGEMENT_FUNCS(graphics_pipeline);
NGF_DEFINE_WRAPPER_MANAGEMENT_FUNCS(compute_pipeline);
NGF_DEFINE_WRAPPER_MANAGEMENT_FUNCS(image);
NGF_DEFINE_WRAPPER_MANAGEMENT_FUNCS(image_view);
NGF_DEFINE_WRAPPER_MANAGEMENT_FUNCS(sampler);
NGF_DEFINE_WRAPPER_MANAGEMENT_FUNCS(render_target);
NGF_DEFINE_WRAPPER_MANAGEMENT_FUNCS(buffer);
NGF_DEFINE_WRAPPER_MANAGEMENT_FUNCS(texel_buffer_view);
NGF_DEFINE_WRAPPER_MANAGEMENT_FUNCS(context);
NGF_DEFINE_WRAPPER_MANAGEMENT_FUNCS(cmd_buffer);

/**
 * \ingroup ngf_wrappers
 *
 * A RAII wrapper for \ref ngf_shader_stage.
 */
NGF_DEFINE_WRAPPER_TYPE(shader_stage);

/**
 * \ingroup ngf_wrappers
 *
 * A RAII wrapper for \ref ngf_graphics_pipeline.
 */
NGF_DEFINE_WRAPPER_TYPE(graphics_pipeline);

/**
 * \ingroup ngf_wrappers
 *
 * A RAII wrapper for \ref ngf_compute_pipeline.
 */
NGF_DEFINE_WRAPPER_TYPE(compute_pipeline);

/**
 * \ingroup ngf_wrappers
 *
 * A RAII wrapper for \ref ngf_image.
 */
NGF_DEFINE_WRAPPER_TYPE(image);

/**
 * \ingroup ngf_wrappers
 *
 * A RAII wrapper for \ref ngf_image_view.
 */
NGF_DEFINE_WRAPPER_TYPE(image_view);


/**
 * \ingroup ngf_wrappers
 *
 * A RAII wrapper for \ref ngf_sampler.
 */
NGF_DEFINE_WRAPPER_TYPE(sampler);

/**
 * \ingroup ngf_wrappers
 *
 * A RAII wrapper for \ref ngf_render_target.
 */
NGF_DEFINE_WRAPPER_TYPE(render_target);

/**
 * \ingroup ngf_wrappers
 *
 * A RAII wrapper for \ref ngf_buffer.
 */
NGF_DEFINE_WRAPPER_TYPE(buffer);

/**
 * \ingroup ngf_wrappers
 *
 * A RAII wrapper for \ref ngf_texel_buffer_view.
 */
NGF_DEFINE_WRAPPER_TYPE(texel_buffer_view);

/**
 * \ingroup ngf_wrappers
 *
 * A RAII wrapper for \ref ngf_context.
 */
NGF_DEFINE_WRAPPER_TYPE(context);

/**
 * \ingroup ngf_wrappers
 *
 * A RAII wrapper for \ref ngf_cmd_buffer.
 */
NGF_DEFINE_WRAPPER_TYPE(cmd_buffer);

/**
 * \ingroup ngf_wrappers
 *
 * Wraps a render encoder with unique ownership semantics.
 */
class render_encoder {
  public:
  /**
   * Creates a new render encoder for the given command buffer. Has the same semantics as \ref
   * ngf_cmd_begin_render_pass.
   *
   * @param cmd_buf The command buffer to create a new render encoder for.
   * @param pass_info Render pass description.
   */
  explicit render_encoder(ngf_cmd_buffer cmd_buf, const ngf_render_pass_info& pass_info, ngf_gpu_perf_metrics_recorder recorder ) {
    ngf_cmd_begin_render_pass(cmd_buf, &pass_info, &enc_, recorder);
  }

  /**
   * Creates a new render encoder for the given command buffer. Has the same semantics as \ref
   * ngf_cmd_begin_render_pass_simple.
   *
   * @param cmd_buf The command buffer to create a new render encoder for.
   * @param rt The render target to render into.
   * @param clear_color_r A floating point number between 0.0 and 1.0 specifying the red component
   * of the clear color.
   * @param clear_color_g A floating point number between 0.0 and 1.0 specifying the green component
   * of the clear color.
   * @param clear_color_b A floating point number between 0.0 and 1.0 specifying the blue component
   * of the clear color.
   * @param clear_color_a A floating point number between 0.0 and 1.0 specifying the alpha component
   * of the clear color.
   * @param clear_depth A floating point value to clear the depth attachment to (if the associated
   * render target has one).
   * @param clear_stencil An integer value to clear the stencil buffer to (if the assocuated render
   * taget has one).
   */
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

  /**
   * Finishes the wrapped render pass.
   */
  ~render_encoder() {
    if (enc_.pvt_data_donotuse.d0) ngf_cmd_end_render_pass(enc_);
  }

  render_encoder(render_encoder&& other) noexcept {
    *this = detail::move(other);
  }

  render_encoder& operator=(render_encoder&& other) noexcept {
    enc_                            = other.enc_;
    other.enc_.pvt_data_donotuse.d0 = 0u;
    other.enc_.pvt_data_donotuse.d1 = 0u;
    return *this;
  }

  render_encoder(const render_encoder&)            = delete;
  render_encoder& operator=(const render_encoder&) = delete;

  /**
   * Implicit conversion to \ref ngf_render_encoder.
   */
  operator ngf_render_encoder() {
    return enc_;
  }

  private:
  ngf_render_encoder enc_ {};
};

/**
 * \ingroup ngf_wrappers
 *
 * Wraps a transfer encoder with unique ownership semantics.
 */
class xfer_encoder {
  public:
  /**
   * Creates a new transfer encoder for the given command buffer.
   *
   * @param cmd_buf The command buffer to create the transfer encoder for.
   */
  explicit xfer_encoder(ngf_cmd_buffer cmd_buf, const ngf_xfer_pass_info& pass_info) {
    ngf_cmd_begin_xfer_pass(cmd_buf, &pass_info, &enc_);
  }

  /**
   * Ends the wrapped transfer pass.
   */
  ~xfer_encoder() {
    if (enc_.pvt_data_donotuse.d0) ngf_cmd_end_xfer_pass(enc_);
  }

  xfer_encoder(xfer_encoder&& other) noexcept {
    *this = detail::move(other);
  }

  xfer_encoder& operator=(xfer_encoder&& other) noexcept {
    enc_                            = other.enc_;
    other.enc_.pvt_data_donotuse.d0 = 0u;
    other.enc_.pvt_data_donotuse.d1 = 0u;
    return *this;
  }

  xfer_encoder(const xfer_encoder&)            = delete;
  xfer_encoder& operator=(const xfer_encoder&) = delete;

  /**
   * Implicit conversion to \ref ngf_xfer_encoder.
   */
  operator ngf_xfer_encoder() {
    return enc_;
  }

  private:
  ngf_xfer_encoder enc_;
};

/**
 * \ingroup ngf_wrappers
 *
 * Wraps a compute encoder with unique ownership semantics.
 */
class compute_encoder {
  public:
  /**
   * Creates a new compute encoder for the given command buffer. Has the same semantics as \ref
   * ngf_cmd_begin_compute_pass.
   *
   * @param cmd_buf The command buffer to create a new compute encoder for.
   */
  explicit compute_encoder(ngf_cmd_buffer cmd_buf, const ngf_compute_pass_info& pass_info, ngf_gpu_perf_metrics_recorder recorder) {
    ngf_cmd_begin_compute_pass(cmd_buf, &pass_info, &enc_, recorder);
  }

  /**
   * Creates a new compute encoder for the given command buffer that doesn't execute any
   * synchronization
   *
   * @param cmd_buf The command buffer to create a new compute encoder for.
   */
  explicit compute_encoder(ngf_cmd_buffer cmd_buf) {
    ngf_cmd_begin_compute_pass(cmd_buf, nullptr, &enc_, nullptr);
  }

  /**
   * Finishes the wrapped compute pass.
   */
  ~compute_encoder() {
    if (enc_.pvt_data_donotuse.d0) ngf_cmd_end_compute_pass(enc_);
  }

  compute_encoder(compute_encoder&& other) noexcept {
    *this = detail::move(other);
  }

  compute_encoder& operator=(compute_encoder&& other) noexcept {
    enc_                            = other.enc_;
    other.enc_.pvt_data_donotuse.d0 = 0u;
    other.enc_.pvt_data_donotuse.d1 = 0u;
    return *this;
  }

  compute_encoder(const compute_encoder&)            = delete;
  compute_encoder& operator=(const compute_encoder&) = delete;

  /**
   * Implicit conversion to \ref ngf_compute_encoder.
   */
  operator ngf_compute_encoder() {
    return enc_;
  }

  private:
  ngf_compute_encoder enc_ {};
};

/**
 * \ingroup ngf_wrappers
 *
 * Convenience wrapper for binding resources. See \ref cmd_bind_resources for details.
 */
template<uint32_t S> struct descriptor_set {
  /**
   * Convenience wrapper for binding resources. See \ref cmd_bind_resources for details.
   */
  template<uint32_t B> struct binding {
    /**
     * Creates a \ref ngf_resource_bind_op for a \ref ngf_image.
     *
     * @param image The image to bind.
     * @param array_index If the descriptor is an array, specifies the index of the array element to
     * bind the object to.
     */
    static ngf_resource_bind_op texture(const ngf_image image, uint32_t array_index = 0u) {
      ngf_resource_bind_op op;
      op.type                              = NGF_DESCRIPTOR_IMAGE;
      op.target_binding                    = B;
      op.target_set                        = S;
      op.info.image_sampler.is_image_view  = false;
      op.info.image_sampler.resource.image = image;
      op.array_index                       = array_index;
      return op;
    }

    /**
     * Creates a \ref ngf_resource_bind_op for an \ref ngf_image that is to be used as a storage
     * image
     *
     * @param image The image to bind.
     */
    static ngf_resource_bind_op storage_image(const ngf_image image, uint32_t array_index = 0u) {
      ngf_resource_bind_op op;
      op.type                              = NGF_DESCRIPTOR_STORAGE_IMAGE;
      op.target_binding                    = B;
      op.target_set                        = S;
      op.info.image_sampler.is_image_view  = false;
      op.info.image_sampler.resource.image = image;
      op.array_index                       = array_index;
      return op;
    }

    /**
     * Creates a \ref ngf_resource_bind_op for a \ref ngf_image_view.
     *
     * @param view The view to bind.
     * @param array_index If the descriptor is an array, specifies the index of the array element to
     * bind the object to.
     */
    static ngf_resource_bind_op texture(const ngf_image_view view, uint32_t array_index = 0u) {
      ngf_resource_bind_op op;
      op.type                             = NGF_DESCRIPTOR_IMAGE;
      op.target_binding                   = B;
      op.target_set                       = S;
      op.info.image_sampler.is_image_view = true;
      op.info.image_sampler.resource.view = view;
      op.array_index                      = array_index;
      return op;
    }

    /**
     * Creates a \ref ngf_resource_bind_op for an \ref ngf_image_view that is to be used as a
     * storage image
     *
     * @param image The image to bind.
     */
    static ngf_resource_bind_op
    storage_image(const ngf_image_view view, uint32_t array_index = 0u) {
      ngf_resource_bind_op op;
      op.type                             = NGF_DESCRIPTOR_STORAGE_IMAGE;
      op.target_binding                   = B;
      op.target_set                       = S;
      op.info.image_sampler.is_image_view = true;
      op.info.image_sampler.resource.view = view;
      op.array_index                      = array_index;
      return op;
    }

    /**
     * Creates a \ref ngf_resource_bind_op for an storage buffer.
     *
     * @param buf The buffer to bind as a storage buffer.
     * @param offset The offset at which to bind the buffer.
     * @param range The extent of the bound memory.
     */
    static ngf_resource_bind_op
    storage_buffer(const ngf_buffer buf, size_t offset, size_t range, uint32_t array_index = 0u) {
      ngf_resource_bind_op op;
      op.type               = NGF_DESCRIPTOR_STORAGE_BUFFER;
      op.target_binding     = B;
      op.target_set         = S;
      op.info.buffer.buffer = buf;
      op.info.buffer.offset = offset;
      op.info.buffer.range  = range;
      op.array_index        = array_index;
      return op;
    }

    /**
     * Creates a \ref ngf_resource_bind_op for an uniform buffer.
     *
     * @param buf The buffer to bind as a uniform buffer.
     * @param offset The offset at which to bind the buffer.
     * @param range The extent of the bound memory.
     */
    static ngf_resource_bind_op
    uniform_buffer(const ngf_buffer buf, size_t offset, size_t range, uint32_t array_index = 0u) {
      ngf_resource_bind_op op;
      op.type               = NGF_DESCRIPTOR_UNIFORM_BUFFER;
      op.target_binding     = B;
      op.target_set         = S;
      op.info.buffer.buffer = buf;
      op.info.buffer.offset = offset;
      op.info.buffer.range  = range;
      op.array_index        = array_index;
      return op;
    }

    /**
     * Creates a \ref ngf_resource_bind_op for a texel buffer.
     *
     * @param buf The buffer to bind as a texel buffer.
     * @param offset The offset at which to bind the buffer.
     * @param range The extent of the bound memory.
     * @param fmt The texel format expected by the shader.
     */
    static ngf_resource_bind_op
    texel_buffer(const ngf_texel_buffer_view buf_view, uint32_t array_index = 0u) {
      ngf_resource_bind_op op;
      op.type                   = NGF_DESCRIPTOR_TEXEL_BUFFER;
      op.target_binding         = B;
      op.target_set             = S;
      op.info.texel_buffer_view = buf_view;
      op.array_index            = array_index;
      return op;
    }

    /**
     * Creates a \ref ngf_resource_bind_op for a sampler.
     *
     * @param sampler The sampler to use.
     */
    static ngf_resource_bind_op sampler(const ngf_sampler sampler, uint32_t array_index = 0u) {
      ngf_resource_bind_op op;
      op.type                       = NGF_DESCRIPTOR_SAMPLER;
      op.target_binding             = B;
      op.target_set                 = S;
      op.info.image_sampler.sampler = sampler;
      op.array_index                = array_index;
      return op;
    }

    /**
     * Creates a \ref ngf_resource_bind_op for a combined image + sampler.
     *
     * @param image The image part of the combined image + sampler.
     * @param sampler The sampler part of the combined image + sampler.
     */
    static ngf_resource_bind_op texture_and_sampler(
        const ngf_image   image,
        const ngf_sampler sampler,
        uint32_t          array_index = 0u) {
      ngf_resource_bind_op op;
      op.type                              = NGF_DESCRIPTOR_IMAGE_AND_SAMPLER;
      op.target_binding                    = B;
      op.target_set                        = S;
      op.info.image_sampler.is_image_view  = false;
      op.info.image_sampler.resource.image = image;
      op.info.image_sampler.sampler        = sampler;
      op.array_index                       = array_index;
      return op;
    }
  };
};

/**
 * \ingroup ngf_wrappers
 *
 * A convenience function for binding many resources at once to the shader. Example usage:
 *
 * ```
 * ngf::cmd_bind_resources(your_render_encoder,
 *                         ngf::descriptor_set<0>::binding<0>::image(your_image),
 *                         ngf::descriptor_set<0>::binding<1>::sampler(your_sampler),
 *                         ngf::descriptor_set<1>::binding<0>::uniform_buffer(your_buffer));
 * ```
 */
template<class... Args> void cmd_bind_resources(ngf_render_encoder enc, const Args&&... args) {
  const ngf_resource_bind_op ops[] = {detail::fwd<const Args>(args)...};
  ngf_cmd_bind_resources(enc, ops, sizeof(ops) / sizeof(ngf_resource_bind_op));
}

/**
 * \ingroup ngf_wrappers
 *
 * A convenience function for binding many resources at once to the shader. Example usage:
 *
 * ```
 * ngf::cmd_bind_resources(your_compute_encoder,
 *                         ngf::descriptor_set<0>::binding<0>::image(your_image),
 *                         ngf::descriptor_set<0>::binding<1>::sampler(your_sampler),
 *                         ngf::descriptor_set<1>::binding<0>::uniform_buffer(your_buffer));
 * ```
 *
 */
template<class... Args> void cmd_bind_resources(ngf_compute_encoder enc, const Args&&... args) {
  const ngf_resource_bind_op ops[] = {detail::fwd<const Args>(args)...};
  ngf_cmd_bind_compute_resources(enc, ops, sizeof(ops) / sizeof(ngf_resource_bind_op));
}

/**
 * \ingroup ngf_wrappers
 *
 * A convenience class for dynamically updated structured uniform data.
 */
template<typename T> class uniform_multibuffer {
  public:
  uniform_multibuffer() = default;
  uniform_multibuffer(uniform_multibuffer&& other) {
    *this = detail::move(other);
  }
  uniform_multibuffer(const uniform_multibuffer&) = delete;

  uniform_multibuffer& operator=(uniform_multibuffer&& other) = default;
  uniform_multibuffer& operator=(const uniform_multibuffer&)  = delete;

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
    ngf_resource_bind_op op {};
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
