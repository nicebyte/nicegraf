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
 * This module contains optional C++ wrappers for nicegraf structures and routines.
 * The \ref ngf namespace contains aliases for most types without the `ngf_` prefix
 * (i.e. `ngf_extent3d` becomes `ngf::extent3d`).
 * Most functions are wrapped using static inline wrappers.
 */

namespace ngf {

#define NGF_POD_TYPE_ALIAS(name)    using name = ngf_##name;
#define NGF_OPAQUE_TYPE_ALIAS(name) using unowned_##name = ngf_##name;

NGF_POD_TYPE_ALIAS(diagnostic_log_verbosity)
NGF_POD_TYPE_ALIAS(diagnostic_message_type)
NGF_POD_TYPE_ALIAS(renderdoc_info)
NGF_POD_TYPE_ALIAS(diagnostic_callback)
NGF_POD_TYPE_ALIAS(diagnostic_info)
NGF_POD_TYPE_ALIAS(allocation_callbacks)
NGF_POD_TYPE_ALIAS(device_handle)
NGF_POD_TYPE_ALIAS(device_performance_tier)
NGF_POD_TYPE_ALIAS(init_info)
NGF_POD_TYPE_ALIAS(error)
NGF_POD_TYPE_ALIAS(irect2d)
NGF_POD_TYPE_ALIAS(extent3d)
NGF_POD_TYPE_ALIAS(offset3d)
NGF_POD_TYPE_ALIAS(stage_type)
NGF_POD_TYPE_ALIAS(shader_stage_info)
NGF_POD_TYPE_ALIAS(polygon_mode)
NGF_POD_TYPE_ALIAS(cull_mode)
NGF_POD_TYPE_ALIAS(front_face_mode)
NGF_POD_TYPE_ALIAS(rasterization_info)
NGF_POD_TYPE_ALIAS(compare_op)
NGF_POD_TYPE_ALIAS(stencil_op)
NGF_POD_TYPE_ALIAS(stencil_info)
NGF_POD_TYPE_ALIAS(depth_stencil_info)
NGF_POD_TYPE_ALIAS(blend_factor)
NGF_POD_TYPE_ALIAS(blend_op)
NGF_POD_TYPE_ALIAS(color_write_mask_bit)
NGF_POD_TYPE_ALIAS(blend_info)
NGF_POD_TYPE_ALIAS(type)
NGF_POD_TYPE_ALIAS(vertex_input_rate)
NGF_POD_TYPE_ALIAS(vertex_buf_binding_desc)
NGF_POD_TYPE_ALIAS(vertex_attrib_desc)
NGF_POD_TYPE_ALIAS(vertex_input_info)
NGF_POD_TYPE_ALIAS(sample_count)
NGF_POD_TYPE_ALIAS(multisample_info)
NGF_POD_TYPE_ALIAS(image_format)
NGF_POD_TYPE_ALIAS(attachment_type)
NGF_POD_TYPE_ALIAS(attachment_description)
NGF_POD_TYPE_ALIAS(attachment_descriptions)
NGF_POD_TYPE_ALIAS(primitive_topology)
NGF_POD_TYPE_ALIAS(constant_specialization)
NGF_POD_TYPE_ALIAS(specialization_info)
NGF_POD_TYPE_ALIAS(input_assembly_info)
NGF_POD_TYPE_ALIAS(graphics_pipeline_info)
NGF_POD_TYPE_ALIAS(compute_pipeline_info)
NGF_POD_TYPE_ALIAS(descriptor_type)
NGF_POD_TYPE_ALIAS(sampler_filter)
NGF_POD_TYPE_ALIAS(sampler_wrap_mode)
NGF_POD_TYPE_ALIAS(sampler_info)
NGF_POD_TYPE_ALIAS(image_usage)
NGF_POD_TYPE_ALIAS(image_type)
NGF_POD_TYPE_ALIAS(image_info)
NGF_POD_TYPE_ALIAS(cubemap_face)
NGF_POD_TYPE_ALIAS(image_ref)
NGF_POD_TYPE_ALIAS(image_view_info)
NGF_POD_TYPE_ALIAS(clear)
NGF_POD_TYPE_ALIAS(attachment_load_op)
NGF_POD_TYPE_ALIAS(attachment_store_op)
NGF_POD_TYPE_ALIAS(render_pass_info)
NGF_POD_TYPE_ALIAS(xfer_pass_info)
NGF_POD_TYPE_ALIAS(compute_pass_info)
NGF_POD_TYPE_ALIAS(buffer_storage_type)
NGF_POD_TYPE_ALIAS(buffer_usage)
NGF_POD_TYPE_ALIAS(buffer_info)
NGF_POD_TYPE_ALIAS(buffer_slice)
NGF_POD_TYPE_ALIAS(texel_buffer_view_info)
NGF_POD_TYPE_ALIAS(buffer_bind_info)
NGF_POD_TYPE_ALIAS(image_sampler_bind_info)
NGF_POD_TYPE_ALIAS(resource_bind_op)
NGF_POD_TYPE_ALIAS(present_mode)
NGF_POD_TYPE_ALIAS(colorspace)
NGF_POD_TYPE_ALIAS(swapchain_info)
NGF_POD_TYPE_ALIAS(context_info)
NGF_POD_TYPE_ALIAS(cmd_buffer_info)
NGF_POD_TYPE_ALIAS(frame_token)
NGF_POD_TYPE_ALIAS(device_capabilities)
NGF_POD_TYPE_ALIAS(device)
NGF_POD_TYPE_ALIAS(image_write)
NGF_OPAQUE_TYPE_ALIAS(shader_stage)
NGF_OPAQUE_TYPE_ALIAS(graphics_pipeline)
NGF_OPAQUE_TYPE_ALIAS(compute_pipeline)
NGF_OPAQUE_TYPE_ALIAS(sampler)
NGF_OPAQUE_TYPE_ALIAS(image)
NGF_OPAQUE_TYPE_ALIAS(image_view)
NGF_OPAQUE_TYPE_ALIAS(render_target_info)
NGF_OPAQUE_TYPE_ALIAS(render_target)
NGF_OPAQUE_TYPE_ALIAS(render_encoder)
NGF_OPAQUE_TYPE_ALIAS(compute_encoder)
NGF_OPAQUE_TYPE_ALIAS(xfer_encoder)
NGF_OPAQUE_TYPE_ALIAS(buffer)
NGF_OPAQUE_TYPE_ALIAS(texel_buffer_view)
NGF_OPAQUE_TYPE_ALIAS(context)
NGF_OPAQUE_TYPE_ALIAS(cmd_buffer)

static inline error get_device_list(const device** devices, uint32_t* ndevices) noexcept {
  return ngf_get_device_list(devices, ndevices);
}

static inline error initialize(const init_info* init_info) noexcept {
  return ngf_initialize(init_info);
}

static inline void shutdown() noexcept {
  ngf_shutdown();
}

static inline error
resize_context(unowned_context ctx, uint32_t new_width, uint32_t new_height) noexcept {
  return ngf_resize_context(ctx, new_width, new_height);
}

static inline error set_context(unowned_context ctx) noexcept {
  return ngf_set_context(ctx);
}

static inline unowned_context get_context() noexcept {
  return ngf_get_context();
}

static inline error begin_frame(frame_token* token) noexcept {
  return ngf_begin_frame(token);
}

static inline error end_frame(frame_token token) noexcept {
  return ngf_end_frame(token);
}

static inline error get_current_swapchain_image(frame_token token, unowned_image* result) noexcept {
  return ngf_get_current_swapchain_image(token, result);
}

static inline const device_capabilities* get_device_capabilities() noexcept {
  return ngf_get_device_capabilities();
}

static inline unowned_render_target default_render_target() noexcept {
  return ngf_default_render_target();
}

static inline const attachment_descriptions* default_render_target_attachment_descs() noexcept {
  return ngf_default_render_target_attachment_descs();
}

static inline void* buffer_map_range(unowned_buffer buf, size_t offset, size_t size) noexcept {
  return ngf_buffer_map_range(buf, offset, size);
}

static inline void buffer_flush_range(unowned_buffer buf, size_t offset, size_t size) noexcept {
  ngf_buffer_flush_range(buf, offset, size);
}

static inline void buffer_unmap(unowned_buffer buf) noexcept {
  ngf_buffer_unmap(buf);
}

static inline void finish() noexcept {
  ngf_finish();
}

static inline error start_cmd_buffer(unowned_cmd_buffer buf, frame_token token) noexcept {
  return ngf_start_cmd_buffer(buf, token);
}

static inline error submit_cmd_buffers(uint32_t nbuffers, unowned_cmd_buffer* bufs) noexcept {
  return ngf_submit_cmd_buffers(nbuffers, bufs);
}

static inline void
cmd_bind_gfx_pipeline(unowned_render_encoder buf, unowned_graphics_pipeline pipeline) noexcept {
  ngf_cmd_bind_gfx_pipeline(buf, pipeline);
}

static inline void
cmd_bind_compute_pipeline(unowned_compute_encoder buf, unowned_compute_pipeline pipeline) noexcept {
  ngf_cmd_bind_compute_pipeline(buf, pipeline);
}

static inline void cmd_viewport(unowned_render_encoder buf, const irect2d* r) noexcept {
  ngf_cmd_viewport(buf, r);
}

static inline void cmd_scissor(unowned_render_encoder enc, const irect2d* r) noexcept {
  ngf_cmd_scissor(enc, r);
}

static inline void
cmd_stencil_reference(unowned_render_encoder enc, uint32_t front, uint32_t back) noexcept {
  ngf_cmd_stencil_reference(enc, front, back);
}

static inline void
cmd_stencil_compare_mask(unowned_render_encoder enc, uint32_t front, uint32_t back) noexcept {
  ngf_cmd_stencil_compare_mask(enc, front, back);
}

static inline void
cmd_stencil_write_mask(unowned_render_encoder enc, uint32_t front, uint32_t back) noexcept {
  ngf_cmd_stencil_write_mask(enc, front, back);
}

static inline void cmd_set_depth_bias(
    unowned_render_encoder enc,
    float                  const_scale,
    float                  slope_scale,
    float                  clamp) noexcept {
  ngf_cmd_set_depth_bias(enc, const_scale, slope_scale, clamp);
}

static inline void cmd_bind_resources(
    unowned_render_encoder  enc,
    const resource_bind_op* bind_operations,
    uint32_t                nbind_operations) noexcept {
  ngf_cmd_bind_resources(enc, bind_operations, nbind_operations);
}

static inline void cmd_bind_compute_resources(
    unowned_compute_encoder enc,
    const resource_bind_op* bind_operations,
    uint32_t                nbind_operations) noexcept {
  ngf_cmd_bind_compute_resources(enc, bind_operations, nbind_operations);
}

static inline void cmd_bind_attrib_buffer(
    unowned_render_encoder enc,
    unowned_buffer         vbuf,
    uint32_t               binding,
    size_t                 offset) noexcept {
  ngf_cmd_bind_attrib_buffer(enc, vbuf, binding, offset);
}

static inline void cmd_bind_index_buffer(
    unowned_render_encoder enc,
    unowned_buffer         idxbuf,
    size_t                 offset,
    type                   index_type) noexcept {
  ngf_cmd_bind_index_buffer(enc, idxbuf, offset, index_type);
}

static inline void cmd_draw(
    unowned_render_encoder enc,
    bool                   indexed,
    uint32_t               first_element,
    uint32_t               nelements,
    uint32_t               ninstances) noexcept {
  ngf_cmd_draw(enc, indexed, first_element, nelements, ninstances);
}

static inline void cmd_dispatch(
    unowned_compute_encoder enc,
    uint32_t                x_threadgroups,
    uint32_t                y_threadgroups,
    uint32_t                z_threadgroups) noexcept {
  ngf_cmd_dispatch(enc, x_threadgroups, y_threadgroups, z_threadgroups);
}

static inline void cmd_copy_buffer(
    unowned_xfer_encoder enc,
    unowned_buffer       src,
    unowned_buffer       dst,
    size_t               size,
    size_t               src_offset,
    size_t               dst_offset) noexcept {
  ngf_cmd_copy_buffer(enc, src, dst, size, src_offset, dst_offset);
}

static inline void cmd_write_image(
    unowned_xfer_encoder enc,
    unowned_buffer       src,
    unowned_image        dst,
    const image_write*   writes,
    uint32_t             nwrites) noexcept {
  ngf_cmd_write_image(enc, src, dst, writes, nwrites);
}

static inline void cmd_copy_image_to_buffer(
    unowned_xfer_encoder enc,
    const image_ref      src,
    offset3d             src_offset,
    extent3d             src_extent,
    uint32_t             nlayers,
    unowned_buffer       dst,
    size_t               dst_offset) noexcept {
  ngf_cmd_copy_image_to_buffer(enc, src, src_offset, src_extent, nlayers, dst, dst_offset);
}

static inline error cmd_generate_mipmaps(unowned_xfer_encoder xfenc, unowned_image img) noexcept {
  return ngf_cmd_generate_mipmaps(xfenc, img);
}

static inline void cmd_begin_debug_group(unowned_cmd_buffer cmd_buffer, const char* name) noexcept {
  ngf_cmd_begin_debug_group(cmd_buffer, name);
}

static inline void cmd_end_current_debug_group(unowned_cmd_buffer cmd_buffer) noexcept {
  ngf_cmd_end_current_debug_group(cmd_buffer);
}

static inline void renderdoc_capture_next_frame() noexcept {
  ngf_renderdoc_capture_next_frame();
}

static inline void renderdoc_capture_begin() noexcept {
  ngf_renderdoc_capture_begin();
}

static inline void renderdoc_capture_end() noexcept {
  ngf_renderdoc_capture_end();
}

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
template<class T, class ObjectManagementFuncs> class unique_handle {
  public:
  /** Wraps a raw handle to a nicegraf object. */
  explicit unique_handle(T raw) : handle_(raw) {
  }

  /** Wraps a null handle. */
  unique_handle() : handle_(nullptr) {
  }

  unique_handle(const unique_handle&) = delete;
  unique_handle(unique_handle&& other) : handle_(nullptr) {
    *this = detail::move(other);
  }

  /** Disposes of the owned handle, if it is not null. */
  ~unique_handle() {
    destroy_if_necessary();
  }

  unique_handle& operator=(const unique_handle&) = delete;

  /** Takes ownership of the handle wrapped by another object. */
  unique_handle& operator=(unique_handle&& other) noexcept {
    destroy_if_necessary();
    handle_       = other.handle_;
    other.handle_ = nullptr;
    return *this;
  }

  typedef typename ObjectManagementFuncs::InitType init_type;

  static unique_handle create(const typename ObjectManagementFuncs::InitType& info, error* err = nullptr) {
    unique_handle h;
    auto e = h.initialize(info);
    if (err) *err = e;
    return h;
  }

  /** Creates a new handle using the provided configuration, and takes ownership of it. */
  ngf_error initialize(const typename ObjectManagementFuncs::InitType& info) {
    destroy_if_necessary();
    const ngf_error err = ObjectManagementFuncs::create(&info, &handle_);
    if (err != NGF_ERROR_OK) handle_ = nullptr;
    return err;
  }

  struct make_result {
    unique_handle   handle;
    const ngf_error error;
  };
  static make_result make(const init_type& info) {
    unique_handle   handle;
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
  using name = unique_handle<ngf_##name, ngf_##name##_ManagementFuncs>;

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
 * A RAII wrapper for \ref unowned_image.
 */
NGF_DEFINE_WRAPPER_TYPE(image);

/**
 * \ingroup ngf_wrappers
 *
 * A RAII wrapper for \ref unowned_image_view.
 */
NGF_DEFINE_WRAPPER_TYPE(image_view);

/**
 * \ingroup ngf_wrappers
 *
 * A RAII wrapper for \ref unowned_sampler.
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
 * A RAII wrapper for \ref unowned_buffer.
 */
NGF_DEFINE_WRAPPER_TYPE(buffer);

/**
 * \ingroup ngf_wrappers
 *
 * A RAII wrapper for \ref unowned_texel_buffer_view.
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
  explicit render_encoder(ngf_cmd_buffer cmd_buf, const ngf_render_pass_info& pass_info) {
    ngf_cmd_begin_render_pass(cmd_buf, &pass_info, &enc_);
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
      unowned_cmd_buffer    cmd_buf,
      unowned_render_target rt,
      float                 clear_color_r,
      float                 clear_color_g,
      float                 clear_color_b,
      float                 clear_color_a,
      float                 clear_depth,
      uint32_t              clear_stencil) {
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
   * Implicit conversion to \ref unowned_render_encoder.
   */
  operator unowned_render_encoder() {
    return enc_;
  }

  private:
  unowned_render_encoder enc_ {};
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
  explicit xfer_encoder(unowned_cmd_buffer cmd_buf, const xfer_pass_info& pass_info) {
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
  operator unowned_xfer_encoder() {
    return enc_;
  }

  private:
  unowned_xfer_encoder enc_;
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
  explicit compute_encoder(ngf_cmd_buffer cmd_buf, const ngf_compute_pass_info& pass_info) {
    ngf_cmd_begin_compute_pass(cmd_buf, &pass_info, &enc_);
  }

  /**
   * Creates a new compute encoder for the given command buffer that doesn't execute any
   * synchronization
   *
   * @param cmd_buf The command buffer to create a new compute encoder for.
   */
  explicit compute_encoder(ngf_cmd_buffer cmd_buf) {
    ngf_cmd_begin_compute_pass(cmd_buf, nullptr, &enc_);
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
   * Implicit conversion to \ref unowned_compute_encoder.
   */
  operator unowned_compute_encoder() {
    return enc_;
  }

  private:
  unowned_compute_encoder enc_ {};
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
     * Creates a \ref resource_bind_op for a \ref unowned_image.
     *
     * @param image The image to bind.
     * @param array_index If the descriptor is an array, specifies the index of the array element to
     * bind the object to.
     */
    static resource_bind_op texture(const unowned_image image, uint32_t array_index = 0u) {
      resource_bind_op op;
      op.type                              = NGF_DESCRIPTOR_IMAGE;
      op.target_binding                    = B;
      op.target_set                        = S;
      op.info.image_sampler.is_image_view  = false;
      op.info.image_sampler.resource.image = image;
      op.array_index                       = array_index;
      return op;
    }

    /**
     * Creates a \ref resource_bind_op for an \ref unowned_image that is to be used as a storage
     * image
     *
     * @param image The image to bind.
     */
    static resource_bind_op storage_image(const unowned_image image, uint32_t array_index = 0u) {
      resource_bind_op op;
      op.type                              = NGF_DESCRIPTOR_STORAGE_IMAGE;
      op.target_binding                    = B;
      op.target_set                        = S;
      op.info.image_sampler.is_image_view  = false;
      op.info.image_sampler.resource.image = image;
      op.array_index                       = array_index;
      return op;
    }

    /**
     * Creates a \ref resource_bind_op for a \ref unowned_image_view.
     *
     * @param view The view to bind.
     * @param array_index If the descriptor is an array, specifies the index of the array element to
     * bind the object to.
     */
    static resource_bind_op texture(const unowned_image_view view, uint32_t array_index = 0u) {
      resource_bind_op op;
      op.type                             = NGF_DESCRIPTOR_IMAGE;
      op.target_binding                   = B;
      op.target_set                       = S;
      op.info.image_sampler.is_image_view = true;
      op.info.image_sampler.resource.view = view;
      op.array_index                      = array_index;
      return op;
    }

    /**
     * Creates a \ref resource_bind_op for an \ref unowned_image_view that is to be used as a
     * storage image
     *
     * @param image The image to bind.
     */
    static resource_bind_op
    storage_image(const unowned_image_view view, uint32_t array_index = 0u) {
      resource_bind_op op;
      op.type                             = NGF_DESCRIPTOR_STORAGE_IMAGE;
      op.target_binding                   = B;
      op.target_set                       = S;
      op.info.image_sampler.is_image_view = true;
      op.info.image_sampler.resource.view = view;
      op.array_index                      = array_index;
      return op;
    }

    /**
     * Creates a \ref resource_bind_op for an storage buffer.
     *
     * @param buf The buffer to bind as a storage buffer.
     * @param offset The offset at which to bind the buffer.
     * @param range The extent of the bound memory.
     */
    static resource_bind_op storage_buffer(
        const unowned_buffer buf,
        size_t               offset,
        size_t               range,
        uint32_t             array_index = 0u) {
      resource_bind_op op;
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
     * Creates a \ref resource_bind_op for an uniform buffer.
     *
     * @param buf The buffer to bind as a uniform buffer.
     * @param offset The offset at which to bind the buffer.
     * @param range The extent of the bound memory.
     */
    static resource_bind_op uniform_buffer(
        const unowned_buffer buf,
        size_t               offset,
        size_t               range,
        uint32_t             array_index = 0u) {
      resource_bind_op op;
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
     * Creates a \ref resource_bind_op for a texel buffer.
     *
     * @param buf The buffer to bind as a texel buffer.
     * @param offset The offset at which to bind the buffer.
     * @param range The extent of the bound memory.
     * @param fmt The texel format expected by the shader.
     */
    static resource_bind_op
    texel_buffer(const unowned_texel_buffer_view buf_view, uint32_t array_index = 0u) {
      resource_bind_op op;
      op.type                   = NGF_DESCRIPTOR_TEXEL_BUFFER;
      op.target_binding         = B;
      op.target_set             = S;
      op.info.texel_buffer_view = buf_view;
      op.array_index            = array_index;
      return op;
    }

    /**
     * Creates a \ref resource_bind_op for a sampler.
     *
     * @param sampler The sampler to use.
     */
    static resource_bind_op sampler(const unowned_sampler sampler, uint32_t array_index = 0u) {
      resource_bind_op op;
      op.type                       = NGF_DESCRIPTOR_SAMPLER;
      op.target_binding             = B;
      op.target_set                 = S;
      op.info.image_sampler.sampler = sampler;
      op.array_index                = array_index;
      return op;
    }

    /**
     * Creates a \ref resource_bind_op for a combined image + sampler.
     *
     * @param image The image part of the combined image + sampler.
     * @param sampler The sampler part of the combined image + sampler.
     */
    static resource_bind_op texture_and_sampler(
        const unowned_image   image,
        const unowned_sampler sampler,
        uint32_t              array_index = 0u) {
      resource_bind_op op;
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
template<class... Args> void cmd_bind_resources(unowned_render_encoder enc, const Args&&... args) {
  const resource_bind_op ops[] = {detail::fwd<const Args>(args)...};
  ngf_cmd_bind_resources(enc, ops, sizeof(ops) / sizeof(resource_bind_op));
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
template<class... Args> void cmd_bind_resources(unowned_compute_encoder enc, const Args&&... args) {
  const resource_bind_op ops[] = {detail::fwd<const Args>(args)...};
  ngf_cmd_bind_compute_resources(enc, ops, sizeof(ops) / sizeof(resource_bind_op));
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
    NGF_RETURN_IF_ERROR(buf_.initialize(buffer_info {
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

  resource_bind_op bind_op_at_current_offset(
      uint32_t set,
      uint32_t binding,
      size_t   additional_offset = 0,
      size_t   range             = 0) const {
    resource_bind_op op {};
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
