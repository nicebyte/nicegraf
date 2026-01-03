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
#include <stdint.h>

namespace ngfi {

namespace detail {

/**
 * murmur3 hash function implementation.
 * This is a simplified version for keys 8 bytes in length.
 */

inline uint64_t rotl64(uint64_t x, int8_t r) {
  return (x << r) | (x >> (64 - r));
}

inline uint64_t fmix64(uint64_t k) {
  k ^= k >> 33;
  k *= 0xff51afd7ed558ccdLLU;
  k ^= k >> 33;
  k *= 0xc4ceb9fe1a85ec53LLU;
  k ^= k >> 33;
  return k;
}

inline void mmh3_x64_128(uintptr_t key, uint32_t seed, uint64_t* out) {
  const auto* data = reinterpret_cast<const uint8_t*>(&key);

  uint64_t h1 = seed;
  uint64_t h2 = seed;
  uint64_t c1 = 0x87c37b91114253d5LLU;
  uint64_t c2 = 0x4cf5ad432745937fLLU;
  uint64_t k1 = 0;

  k1 ^= static_cast<uint64_t>(data[7]) << 56;
  k1 ^= static_cast<uint64_t>(data[6]) << 48;
  k1 ^= static_cast<uint64_t>(data[5]) << 40;
  k1 ^= static_cast<uint64_t>(data[4]) << 32;
  k1 ^= static_cast<uint64_t>(data[3]) << 24;
  k1 ^= static_cast<uint64_t>(data[2]) << 16;
  k1 ^= static_cast<uint64_t>(data[1]) << 8;
  k1 ^= static_cast<uint64_t>(data[0]) << 0;
  k1 *= c1;
  k1 = rotl64(k1, 31);
  k1 *= c2;
  h1 ^= k1;

  h1 ^= sizeof(key);
  h2 ^= sizeof(key);

  h1 += h2;
  h2 += h1;

  h1 = fmix64(h1);
  h2 = fmix64(h2);

  h1 += h2;
  h2 += h1;

  out[0] = h1;
  out[1] = h2;
}

}  // namespace detail

/**
 * A hash table with open addressing (linear probing).
 * Keys are 64-bit unsigned integers, values can be any trivially copyable type.
 * Does not support individual element deletion - only full clear.
 */
template<class V, class AllocT = configured_alloc_callbacks> class hashtable {
  static_assert(
      __is_trivially_copyable(V),
      "hashtable only supports trivially copyable value types");

  public:
  using key_type = uint64_t;

  static constexpr key_type EMPTY_KEY = ~key_type {0};

  struct keyhash {
    key_type key;
    uint64_t hash;
  };

  struct entry {
    key_type key;
    V        value;
  };

  private:
  static constexpr uint32_t HASH_SEED       = 0x9e3779b9u;
  static constexpr float    MAX_LOAD_FACTOR = 0.7f;

  entry* slots_            = nullptr;
  size_t capacity_         = 0;
  size_t initial_capacity_ = 100u;
  size_t size_             = 0;

  public:
  hashtable() noexcept = default;
  explicit hashtable(size_t capacity) : initial_capacity_ {capacity} {
  }
  hashtable(hashtable&& other) noexcept {
    *this = ngfi::move(other);
  }
  ~hashtable() noexcept {
    destroy();
  }

  hashtable& operator=(hashtable&& other) noexcept {
    destroy();
    slots_          = other.slots_;
    capacity_       = other.capacity_;
    size_           = other.size_;
    other.slots_    = nullptr;
    other.capacity_ = 0;
    other.size_     = 0;
    return *this;
  }

  size_t size() const noexcept {
    return size_;
  }
  size_t capacity() const noexcept {
    return capacity_;
  }
  bool empty() const noexcept {
    return size_ == 0;
  }

  static keyhash compute_hash(key_type key) noexcept {
    uint64_t mmh3_out[2] = {0, 0};
    detail::mmh3_x64_128(static_cast<uintptr_t>(key), HASH_SEED, mmh3_out);
    return keyhash {key, mmh3_out[0] ^ mmh3_out[1]};
  }

  V* get(key_type key) noexcept {
    return get_prehashed(compute_hash(key));
  }
  const V* get(key_type key) const noexcept {
    return get_prehashed(compute_hash(key));
  }

  V* get_prehashed(const keyhash& kh) noexcept {
    if (!slots_) { return nullptr; }
    const size_t start_idx = kh.hash % capacity_;
    for (size_t offset = 0; offset < capacity_; ++offset) {
      const size_t idx = (start_idx + offset) % capacity_;
      if (slots_[idx].key == kh.key) { return &slots_[idx].value; }
      if (slots_[idx].key == EMPTY_KEY) {
        return nullptr;  // Key not found
      }
    }
    return nullptr;  // Table is full and key not found
  }

  const V* get_prehashed(const keyhash& kh) const noexcept {
    return const_cast<hashtable*>(this)->get_prehashed(kh);
  }

  V* insert(key_type key, const V& value) noexcept {
    return insert_prehashed(compute_hash(key), value);
  }

  V* insert_prehashed(const keyhash& kh, const V& value) noexcept {
    // Check if we need to rehash
    if (capacity_ == 0 ||
        static_cast<float>(size_ + 1) / static_cast<float>(capacity_) > MAX_LOAD_FACTOR) {
      if (!rehash(capacity_ ? capacity_ * 2 : initial_capacity_)) { return nullptr; }
    }

    return insert_internal(kh, value);
  }

  V* get_or_insert(key_type key, const V& default_value, bool& is_new) noexcept {
    return get_or_insert_prehashed(compute_hash(key), default_value, is_new);
  }

  V* get_or_insert_prehashed(const keyhash& kh, const V& default_value, bool& is_new) noexcept {
    if (!slots_) {
      is_new = true;
      return insert_prehashed(kh, default_value);
    } else {
      // First try to find existing entry
      const size_t start_idx = kh.hash % capacity_;
      for (size_t offset = 0; offset < capacity_; ++offset) {
        const size_t idx = (start_idx + offset) % capacity_;
        if (slots_[idx].key == kh.key) {
          is_new = false;
          return &slots_[idx].value;
        }
        if (slots_[idx].key == EMPTY_KEY) {
          // Key not found, insert new entry
          // Check if we need to rehash first
          is_new = true;
          return insert_prehashed(kh, default_value);
        }
      }
      return nullptr;  // Table is full (should not happen with proper load factor)
    }
  }

  void clear() noexcept {
    if (slots_) {
      for (size_t i = 0; i < capacity_; ++i) { slots_[i].key = EMPTY_KEY; }
      size_ = 0;
    }
  }

  class iterator {
    public:
    using value_type      = entry;
    using pointer         = entry*;
    using reference       = entry&;
    using difference_type = std::ptrdiff_t;

    iterator() noexcept = default;

    reference operator*() const noexcept {
      return *slot_;
    }

    pointer operator->() const noexcept {
      return slot_;
    }

    iterator& operator++() noexcept {
      ++slot_;
      advance_to_valid();
      return *this;
    }

    iterator operator++(int) noexcept {
      iterator tmp = *this;
      ++(*this);
      return tmp;
    }

    bool operator==(const iterator& other) const noexcept {
      return slot_ == other.slot_;
    }

    bool operator!=(const iterator& other) const noexcept {
      return slot_ != other.slot_;
    }

    private:
    friend class hashtable;

    iterator(entry* slot, entry* end) noexcept : slot_(slot), end_(end) {
      advance_to_valid();
    }

    void advance_to_valid() noexcept {
      while (slot_ != end_ && slot_->key == EMPTY_KEY) { ++slot_; }
    }

    entry* slot_ = nullptr;
    entry* end_  = nullptr;
  };

  iterator begin() noexcept {
    return !slots_ ? end() : iterator(slots_, slots_ + capacity_);
  }

  iterator end() noexcept {
    return !slots_ ? iterator(nullptr, nullptr) : iterator {slots_ + capacity_, slots_ + capacity_};
  }

  private:
  void destroy() noexcept {
    if (slots_ != nullptr) {
      ngfi::freen(slots_, capacity_);
      slots_    = nullptr;
      capacity_ = 0;
      size_     = 0;
    }
  }

  // Prevent copying
  hashtable(const hashtable&)            = delete;
  hashtable& operator=(const hashtable&) = delete;

  /**
   * Internal insert without load factor check.
   */
  V* insert_internal(const keyhash& kh, const V& value) noexcept {
    const size_t start_idx = kh.hash % capacity_;
    for (size_t offset = 0; offset < capacity_; ++offset) {
      const size_t idx = (start_idx + offset) % capacity_;
      if (slots_[idx].key == kh.key) {
        // Update existing
        memcpy(&slots_[idx].value, &value, sizeof(V));
        return &slots_[idx].value;
      }
      if (slots_[idx].key == EMPTY_KEY) {
        // Insert new
        slots_[idx].key = kh.key;
        memcpy(&slots_[idx].value, &value, sizeof(V));
        ++size_;
        return &slots_[idx].value;
      }
    }
    return nullptr;  // Should not happen if load factor is maintained
  }

  /**
   * Rehash the table to a new capacity.
   */
  bool rehash(size_t new_capacity) noexcept {
    entry* old_slots    = slots_;
    size_t old_capacity = capacity_;

    slots_ = AllocT::template allocn<entry>(new_capacity);
    if (slots_ == nullptr) {
      slots_ = old_slots;
      return false;
    }

    capacity_ = new_capacity;
    size_     = 0;

    // Initialize new slots as empty
    for (size_t i = 0; i < capacity_; ++i) { slots_[i].key = EMPTY_KEY; }

    // Reinsert all existing entries
    for (size_t i = 0; i < old_capacity; ++i) {
      if (old_slots[i].key != EMPTY_KEY) {
        insert_internal(compute_hash(old_slots[i].key), old_slots[i].value);
      }
    }

    AllocT::freen(old_slots, old_capacity);
    return true;
  }
};

}  // namespace ngfi
