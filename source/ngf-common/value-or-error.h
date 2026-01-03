#pragma once

#include "ngf-common/util.h"

#include <nicegraf.h>

namespace ngfi {

template<class ErrorT> ErrorT missing_value_error() noexcept;

template<class ErrorT> ErrorT non_error() noexcept;

template<class ValueT, class ErrorT> class value_or_error {
  private:
  alignas(ValueT) char value_[sizeof(ValueT)];
  ErrorT error_;

  public:
  value_or_error(ValueT&& v) noexcept : error_{non_error<ErrorT>()} {
    new (value_) ValueT {ngfi::move(v)};
  }
  value_or_error(ErrorT err) noexcept : error_ {err} {
    if (error_ == non_error<ErrorT>()) abort();
  }
  value_or_error(value_or_error&& other) {
    *this = ngfi::move(other);
  }
  value_or_error(const value_or_error&) = delete;
  ~value_or_error() noexcept {
    maybe_destroy_value();
  }

  bool has_error() const noexcept {
    return error_ != non_error<ErrorT>();
  }
  ErrorT error() const noexcept {
    return error_;
  }

  const ValueT& value() const noexcept {
    if (has_error()) { abort(); }
    return *((const ValueT*)value_);
  }
  ValueT& value() noexcept {
    if (has_error()) abort();
    return *((ValueT*)value_);
  }

  value_or_error& operator=(value_or_error&& other) {
    maybe_destroy_value();
    if (other.has_error()) {
      error_ = other.error();
    } else {
      new (value_) ValueT {ngfi::move(other.value())};
      error_ = non_error<ErrorT>();
      other.error_ = missing_value_error<ErrorT>();
    }
    return *this;
  }

  value_or_error& operator=(const value_or_error&) = delete;

  private:
  void maybe_destroy_value() noexcept {
    if (!has_error()) {
      ((ValueT*)value_)->~ValueT();
      error_ = missing_value_error<ErrorT>();
    }
  }
};

template<class ValueT> using value_or_ngferr = value_or_error<ValueT, ngf_error>;

template<> ngf_error missing_value_error<ngf_error>() noexcept {
  return NGF_ERROR_INVALID_OPERATION;
}

template<> ngf_error non_error<ngf_error>() noexcept {
  return NGF_ERROR_OK;
}

}  // namespace ngfi
