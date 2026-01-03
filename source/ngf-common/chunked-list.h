#pragma once

#include "arena.h"

namespace ngfi {

template <class T, unsigned ChunkCapacity = 10>
class chunked_list {
static_assert(__is_trivially_copyable(T));
private:
  struct chunk {
    chunk* next;
    T* free_slot;
    T slots[ChunkCapacity];
  };
  chunk* last_chunk_ = nullptr;
  
public:
  class iterator {
    friend class chunked_list;

    chunk* first_chunk_ = nullptr;
    chunk* curr_chunk_ = nullptr;
    T* curr_slot_ = nullptr;

    iterator() = default;
    explicit iterator(chunk* first_chunk) : first_chunk_{first_chunk},
     curr_chunk_{first_chunk},
     curr_slot_ { first_chunk->slots } {}

  public:
    iterator& operator++() {
      ++curr_slot_;
      if (curr_chunk_->free_slot == curr_slot_) {
        if (curr_chunk_->next == first_chunk_) {
          first_chunk_ = nullptr;
          curr_chunk_ = nullptr;
          curr_slot_ = nullptr;
        } else {
          curr_chunk_ = curr_chunk_->next;
          curr_slot_ = curr_chunk_->slots;
        }
      }
      return *this;
    }
    const T& operator*() { return *curr_slot_; }
    bool operator==(const iterator& it) const {
      return curr_slot_ == it.curr_slot_ &&
         first_chunk_ == it.first_chunk_ &&
         curr_chunk_ == it.curr_chunk_;
    }
  };

  iterator begin() noexcept { return !last_chunk_ ? end() : iterator{last_chunk_->next}; }
  iterator begin() const noexcept { return !last_chunk_ ? end() : iterator{last_chunk_->next}; }
  iterator end() const noexcept { return iterator{}; }


  T* append(const T& element, arena& a) noexcept {
    const bool need_new_chunk = !last_chunk_ || last_chunk_->free_slot == &last_chunk_->slots[ChunkCapacity];
    if (need_new_chunk) {
      chunk* new_chunk = a.alloc<chunk>();
      if (!new_chunk) return nullptr;
      new_chunk->free_slot = new_chunk->slots;
      if (!last_chunk_) {
        last_chunk_ = new_chunk;
        last_chunk_->next = last_chunk_;
      } else {
        new_chunk->next = last_chunk_->next;
        last_chunk_->next = new_chunk;
        last_chunk_ = new_chunk;
      }
    }
    T* result = last_chunk_->free_slot++;
    *result = element;
    return result;
  }

  void clear() { last_chunk_ = nullptr; }
};

}
