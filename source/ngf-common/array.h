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

#pragma once

#include "macros.h"
#include "util.h"

#include <stddef.h>
#include <string.h>

namespace ngfi {

/**
 * A simple dynamic array for trivially-copyable types.
 * Similar to std::vector but uses NGFI allocation callbacks.
 */
template<class T, class AllocT = configured_alloc_callbacks, bool FixedSize = false>
class array {
private:
  T*     data_     = nullptr;
  size_t size_     = 0;
  size_t capacity_ = 0;

  static constexpr size_t MIN_CAPACITY = 8;

public:
  using value_type      = T;
  using iterator        = T*;
  using const_iterator  = const T*;
  using reference       = T&;
  using const_reference = const T&;

  array() noexcept = default;
  explicit array(size_t size) : data_ {ngfi::allocn<T>(size)}, size_ {size}, capacity_ {size} {
  }
  array(array&& other) noexcept {
    *this = ngfi::move(other);
  }
  ~array() noexcept { destroy(); }

  array& operator=(array&& other ) noexcept {
    destroy();
    data_ = other.data_;
    size_ = other.size_;
    capacity_ = other.capacity_;

    other.data_     = nullptr;
    other.size_     = 0;
    other.capacity_ = 0;

    return *this;
  }
  
  size_t size() const noexcept { return size_; }
  size_t capacity() const noexcept { return capacity_; }
  bool empty() const noexcept { return size_ == 0; }

  T& operator[](size_t idx) noexcept { return data_[idx]; }
  const T& operator[](size_t idx) const noexcept { return data_[idx]; }

  T& front() noexcept { return data_[0]; }
  const T& front() const noexcept { return data_[0]; }
  T& back() noexcept { return data_[size_ - 1]; }
  const T& back() const noexcept { return data_[size_ - 1]; }

  T* data() noexcept { return data_; }
  const T* data() const noexcept { return data_; }

  T* push_back(const T& value) noexcept {
    static_assert(!FixedSize);
    if (!ensure_capacity(size_ + 1)) {
      return nullptr;
    }
    if constexpr (__is_trivially_copyable(T)) {
        memcpy(&data_[size_], &value, sizeof(T));
    } else {
        data_[size_] = value;
    }
    ++size_;
    return &data_[size_ - 1];
  }

  template <class... Args> T* emplace_back(Args... args) {
    static_assert(!FixedSize);
    if (!ensure_capacity(size_ + 1)) { return nullptr; }
    new (&data_[size_]) T {ngfi::forward<Args>(args)...};
    ++size_;
    return &data_[size_ - 1];
  }

  void pop_back() noexcept {
    static_assert(!FixedSize);
    if (size_ > 0) {
      --size_;
    }
  }

  void clear() noexcept {
    static_assert(!FixedSize);
    size_ = 0;
  }

  
  bool resize(size_t new_size) noexcept {
    static_assert(!FixedSize);
    if (new_size > capacity_) {
      if (!reserve(new_size)) {
        return false;
      }
    }
    size_ = new_size;
    return true;
  }

  bool reserve(size_t new_capacity) noexcept {
    static_assert(!FixedSize);
    if (new_capacity <= capacity_) {
      return true;
    }
    return grow_to(new_capacity);
  }

  iterator begin() noexcept { return data_; }
  const_iterator begin() const noexcept { return data_; }
  iterator end() noexcept { return data_ + size_; }
  const_iterator end() const noexcept { return data_ + size_; }

private:
  void destroy() noexcept {
    if (data_ != nullptr) {
      AllocT::freen(data_, capacity_);
      data_     = nullptr;
      size_     = 0;
      capacity_ = 0;
    }
  }

  array(const array&)            = delete;
  array& operator=(const array&) = delete;
 
  bool ensure_capacity(size_t required) noexcept {
    if (required <= capacity_) {
      return true;
    }
    size_t new_capacity = capacity_ == 0 ? MIN_CAPACITY : capacity_ * 2;
    while (new_capacity < required) {
      new_capacity *= 2;
    }
    return grow_to(new_capacity);
  }

  bool grow_to(size_t new_capacity) noexcept {
    T* new_data = AllocT::template allocn<T>(new_capacity);
    if (new_data == nullptr) {
      return false;
    }

    if (data_ != nullptr) {
      if (size_ > 0) {
        if constexpr (__is_trivially_copyable(T)) {
            memcpy(new_data, data_, size_ * sizeof(T));
        } else {
            for (size_t i = 0u; i < size_; ++i) {
                new (&new_data[i]) T {ngfi::move(data_[i])};
            }
        }
      }
      AllocT::freen(data_, capacity_);
    }

    data_     = new_data;
    capacity_ = new_capacity;
    return true;
  }
};

template <class T, class AllocT = configured_alloc_callbacks>
using fixed_array = array<T, AllocT, true>;

}  // namespace ngfi
