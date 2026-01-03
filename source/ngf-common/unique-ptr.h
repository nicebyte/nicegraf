#pragma once

#include "macros.h"

namespace ngfi {

template<class T> class unique_ptr {
  private:
  T* obj_ = nullptr;

  public:
  unique_ptr() noexcept = default;
  unique_ptr(T* obj) : obj_ {obj} {
  }
  ~unique_ptr() noexcept {
    destroy();
  }
  unique_ptr(unique_ptr&& other) {
    *this = ngfi::move(other);
  }
  unique_ptr(const unique_ptr&) = delete;

  unique_ptr& operator=(unique_ptr&& other) {
    destroy();
    obj_       = other.obj_;
    other.obj_ = nullptr;
    return *this;
  }
  unique_ptr& operator=(const unique_ptr&) = delete;

  T* release() noexcept {
    auto r = obj_;
    obj_   = nullptr;
    return r;
  }
  T* get() noexcept {
    return obj_;
  }
  const T* get() const noexcept {
    return obj_;
  }
  T* operator->() noexcept {
    return get();
  }
  operator bool() const noexcept {
    return obj_ != nullptr;
  }

  template<class... Args> static unique_ptr make(Args&&... args) {
    return unique_ptr {ngfi::alloc<T>(ngfi::forward<Args>(args)...)};
  }

  private:
  void destroy() noexcept {
    if (obj_) ngfi::free<T>(obj_);
  }
};

}  // namespace ngfi
