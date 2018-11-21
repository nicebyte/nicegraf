/**
Copyright © 2018 nicegraf contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the “Software”), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "nicegraf.h"
#include <utility>

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
NGF_DEFINE_WRAPPER_TYPE(descriptor_set_layout);
NGF_DEFINE_WRAPPER_TYPE(graphics_pipeline);
NGF_DEFINE_WRAPPER_TYPE(image);
NGF_DEFINE_WRAPPER_TYPE(sampler);
NGF_DEFINE_WRAPPER_TYPE(render_target);
NGF_DEFINE_WRAPPER_TYPE(buffer);
NGF_DEFINE_WRAPPER_TYPE(context);

// special case for descriptor sets
struct ngf_descriptor_set_ManagementFuncs {
  using InitType = ngf_descriptor_set_layout;
  static ngf_error create(const InitType *layout, ngf_descriptor_set **set) {
    return ngf_create_descriptor_set(layout, set);
  }
  static void destroy(ngf_descriptor_set *handle) {
    ngf_destroy_descriptor_set(handle);
  }
};
using descriptor_set = ngf_handle<ngf_descriptor_set,
                                  ngf_descriptor_set_ManagementFuncs>;
}  // namespace ngf
