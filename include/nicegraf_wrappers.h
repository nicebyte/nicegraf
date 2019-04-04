/**
 * Copyright (c) 2019 nicegraf contributors
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
#include <optional>
#include <string.h>
#include <tuple>
#include <utility>
#include <queue>

namespace ngf {

/**
 * A move-only RAII wrapper over Nicegraf objects that provides
 * unique ownership semantics
 */ 
template <class T, class ObjectManagementFuncs>
class ngf_handle {
public:
  explicit ngf_handle(T *raw) : handle_(raw) {}
  ngf_handle() : handle_(nullptr) {}
  ngf_handle(const ngf_handle&) = delete;
  ngf_handle(ngf_handle &&other) : handle_(nullptr) {
    *this = std::move(other);
  }
  ~ngf_handle() { destroy_if_necessary(); }

  ngf_handle& operator=(const ngf_handle&) = delete;
  ngf_handle& operator=(ngf_handle&& other) {
    destroy_if_necessary();
    handle_ = other.handle_;
    other.handle_ = nullptr;
    return *this;
  }

  typedef typename ObjectManagementFuncs::InitType init_type;

  ngf_error initialize(const typename ObjectManagementFuncs::InitType &info) {
    destroy_if_necessary();
    return ObjectManagementFuncs::create(&info, &handle_);
  }

  T* get() { return handle_; }
  const T* get() const { return handle_; }
  T* release() {
    T *tmp = handle_;
    handle_ = nullptr;
    return tmp;
  }
  operator T*() { return handle_; }
  operator const T*() const { return handle_; }

  void reset(T *new_handle) { destroy_if_necessary(); handle_ = new_handle; }

private:
  void destroy_if_necessary() {
    if(handle_) {
      ObjectManagementFuncs::destroy(handle_);
      handle_ = nullptr;
    }
  }

  T *handle_;
};

#define NGF_DEFINE_WRAPPER_TYPE(name) \
  struct ngf_##name##_ManagementFuncs { \
    using InitType = ngf_##name##_info; \
    static ngf_error create(const InitType *info, ngf_##name **r) { \
      return ngf_create_##name(info, r); \
    } \
    static void destroy(ngf_##name *handle) { ngf_destroy_##name(handle); } \
  }; \
  using name = ngf_handle<ngf_##name, ngf_##name##_ManagementFuncs>;

NGF_DEFINE_WRAPPER_TYPE(shader_stage);
NGF_DEFINE_WRAPPER_TYPE(graphics_pipeline);
NGF_DEFINE_WRAPPER_TYPE(image);
NGF_DEFINE_WRAPPER_TYPE(sampler);
NGF_DEFINE_WRAPPER_TYPE(render_target);
NGF_DEFINE_WRAPPER_TYPE(attrib_buffer);
NGF_DEFINE_WRAPPER_TYPE(index_buffer);
NGF_DEFINE_WRAPPER_TYPE(uniform_buffer);
NGF_DEFINE_WRAPPER_TYPE(pixel_buffer);
NGF_DEFINE_WRAPPER_TYPE(context);
NGF_DEFINE_WRAPPER_TYPE(cmd_buffer);

template <uint32_t S>
struct descriptor_set {
  template <uint32_t B>
  struct binding {
    static ngf_resource_bind_op texture(const ngf_image *image) {
      ngf_resource_bind_op op;
      op.type = NGF_DESCRIPTOR_TEXTURE;
      op.target_binding = B;
      op.target_set = S;
      op.info.image_sampler.image_subresource.image = image;
      return op;
    }

    static ngf_resource_bind_op uniform_buffer(const ngf_uniform_buffer *buf,
                                               size_t offset, size_t range) {
      ngf_resource_bind_op op;
      op.type = NGF_DESCRIPTOR_UNIFORM_BUFFER;
      op.target_binding = B;
      op.target_set = S;
      op.info.uniform_buffer.buffer = buf;
      op.info.uniform_buffer.offset = offset;
      op.info.uniform_buffer.range = range;
      return op;
    }

    static ngf_resource_bind_op sampler(const ngf_sampler *sampler) {
      ngf_resource_bind_op op;
      op.type = NGF_DESCRIPTOR_SAMPLER;
      op.target_binding = B;
      op.target_set = S;
      op.info.image_sampler.sampler = sampler;
      return op;
    }

    static ngf_resource_bind_op texture_and_sampler(
        const ngf_image *image,
        const ngf_sampler *sampler) {
      ngf_resource_bind_op op;
      op.type = NGF_DESCRIPTOR_TEXTURE_AND_SAMPLER;
      op.target_binding = B;
      op.target_set = S;
      op.info.image_sampler.image_subresource.image = image;
      op.info.image_sampler.sampler = sampler;
      return op;
    }
  };
};

template <class ...Args>
void cmd_bind_resources(ngf_cmd_buffer *buf, const Args&&... args) {
  const ngf_resource_bind_op ops[] = { args... };
  ngf_cmd_bind_resources(buf, ops, sizeof(ops)/sizeof(ngf_resource_bind_op));
}

inline void* buffer_map_range(ngf_attrib_buffer *buf,
                              size_t offset,
                              size_t size,
                              uint32_t flags) {
  return ngf_attrib_buffer_map_range(buf, offset, size, flags);
}

inline void* buffer_map_range(ngf_index_buffer *buf,
                              size_t offset,
                              size_t size,
                              uint32_t flags) {
  return ngf_index_buffer_map_range(buf, offset, size, flags);
}

inline void* buffer_map_range(ngf_uniform_buffer *buf,
                              size_t offset,
                              size_t size,
                              uint32_t flags) {
  return ngf_uniform_buffer_map_range(buf, offset, size, flags);
}

inline void* buffer_map_range(ngf_pixel_buffer *buf,
                              size_t offset,
                              size_t size,
                              uint32_t flags) {
  return ngf_pixel_buffer_map_range(buf, offset, size, flags);
}

inline void buffer_flush_range(ngf_attrib_buffer *buf,
                               size_t offset, size_t size) {
  ngf_attrib_buffer_flush_range(buf, offset, size);
}

inline void buffer_flush_range(ngf_index_buffer *buf,
                               size_t offset, size_t size) {
  ngf_index_buffer_flush_range(buf, offset, size);
}

inline void buffer_flush_range(ngf_uniform_buffer *buf,
                               size_t offset, size_t size) {
  ngf_uniform_buffer_flush_range(buf, offset, size);
}

inline void buffer_flush_range(ngf_pixel_buffer *buf,
                               size_t offset, size_t size) {
  ngf_pixel_buffer_flush_range(buf, offset, size);
}

inline void buffer_unmap(ngf_attrib_buffer *buf) {
  ngf_attrib_buffer_unmap(buf);
}

inline void buffer_unmap(ngf_index_buffer *buf) {
  ngf_index_buffer_unmap(buf);
}

inline void buffer_unmap(ngf_uniform_buffer *buf) {
  ngf_uniform_buffer_unmap(buf);
}

inline void buffer_unmap(ngf_pixel_buffer *buf) {
  ngf_pixel_buffer_unmap(buf);
}

/**
 * A convenience class for streaming uniform data.
 */
template <typename T>
class streamed_uniform {
  // TODO: replace 256 with real alignment
  static constexpr uint32_t ALIGNED_SIZE = sizeof(T) + (256u - sizeof(T)%256u);
public:
  streamed_uniform() = default;
  streamed_uniform(streamed_uniform &&other) { *this = std::move(other); }
  streamed_uniform(const streamed_uniform&) = delete;

  streamed_uniform& operator=(streamed_uniform &&other) {
    buf_ = std::move(other.buf_);
    frame_ = other.frame_;
    other.frame_ = 0u;
    current_offset_ = other.current_offset_;
    other.current_offset_ = 0u;
    nframes_ = other.nframes_;
    other.nframes_ = 0u;
    return *this;
  }

  streamed_uniform& operator=(const streamed_uniform&) = delete;

  static std::tuple<std::optional<streamed_uniform>, ngf_error> create(
      const uint32_t frames) {
    const ngf_buffer_info buffer_info = {
      ALIGNED_SIZE * frames,
      NGF_BUFFER_STORAGE_HOST_READABLE_WRITEABLE
    };
    ngf::uniform_buffer buf;
    ngf_error err = buf.initialize(buffer_info);
    if (err != NGF_ERROR_OK) {
      return std::make_tuple(std::nullopt, err);
    }
    return std::make_tuple(streamed_uniform(std::move(buf), frames), err);
  }

  void write(const T &data) {
    current_offset_ = (frame_) * ALIGNED_SIZE;
    const uint32_t flags =
      (current_offset_ == 0u)
          ? (NGF_BUFFER_MAP_WRITE_BIT | NGF_BUFFER_MAP_DISCARD_BIT)
          :  NGF_BUFFER_MAP_WRITE_BIT;
    void *mapped_buf = ngf_uniform_buffer_map_range(buf_.get(),
                                                    current_offset_,
                                                    ALIGNED_SIZE,
                                                    flags);
    memcpy(mapped_buf, (void*)&data, sizeof(T));
    ngf_uniform_buffer_flush_range(buf_.get(), 0, ALIGNED_SIZE);
    ngf_uniform_buffer_unmap(buf_.get());
    frame_ = (frame_ + 1u) % nframes_;
  }

  ngf_resource_bind_op bind_op_at_current_offset(
      uint32_t set,
      uint32_t binding,
      size_t   additional_offset = 0) const {
    ngf_resource_bind_op op;
    op.type = NGF_DESCRIPTOR_UNIFORM_BUFFER;
    op.target_binding = binding;
    op.target_set = set;
    op.info.uniform_buffer.buffer = buf_.get();
    op.info.uniform_buffer.offset = current_offset_ + additional_offset;
    op.info.uniform_buffer.range = ALIGNED_SIZE;
    return op;
  }

private:
  streamed_uniform(ngf::uniform_buffer buf, uint32_t nframes) :
    buf_(std::move(buf)),
    frame_(0u),
    current_offset_(0u),
    nframes_(nframes) {}

  uniform_buffer buf_;
  uint32_t frame_;
  size_t current_offset_;
  uint32_t nframes_;
};

// This is a helper for disposing of staging buffers at an appropriate time.
// After a buffer is enqueued, it will be destroyed once it is no
// longer needed.
class resource_dispose_queue {
  template <class ResHandleType>
  struct entry {
    uint32_t frame;
    ResHandleType handle;
  };
public:

  // Disposes of previously enqueued buffers that are no longer needed by
  // any commands.
  // Do not call this more than once per frame.
  void update() {
    update_queue(idx_buf_queue_);
    update_queue(attr_buf_queue_);
    update_queue(uniform_buf_queue_);
    update_queue(pixel_buf_queue_);
    frame_++;
  }

  void enqueue(index_buffer buf) { 
    idx_buf_queue_.emplace(entry<index_buffer>{frame_, std::move(buf)});
  }
   
  void enqueue(attrib_buffer buf) { 
    attr_buf_queue_.emplace(entry<attrib_buffer>{frame_, std::move(buf)});
  }

  void enqueue(uniform_buffer buf) { 
    uniform_buf_queue_.emplace(entry<uniform_buffer>{frame_, std::move(buf)});
  }

  void enqueue(pixel_buffer buf) { 
    pixel_buf_queue_.emplace(entry<pixel_buffer>{frame_, std::move(buf)});
  }

private:
  template <class T>
  void update_queue(std::queue<entry<T>> &q) {
    while (!q.empty() && frame_ > q.front().frame) {
      entry<T> e = std::move(q.front());
      q.pop();
      e.handle.reset(nullptr);
    }
  }

  std::queue<entry<index_buffer>> idx_buf_queue_;
  std::queue<entry<attrib_buffer>> attr_buf_queue_;
  std::queue<entry<uniform_buffer>> uniform_buf_queue_;
  std::queue<entry<pixel_buffer>> pixel_buf_queue_;
  uint32_t frame_ = 0u;
};

}  // namespace ngf
