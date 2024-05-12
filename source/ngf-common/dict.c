/**
 * Copyright (c) 2023 nicegraf contributors
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

#include "dict.h"

#include "macros.h"

#include <stdint.h>
#include <string.h>

void (*ngfi_dict_hashfn)(uintptr_t, uint32_t, void*) = NULL;
static const uint32_t ngfi_dict_hashseed             = 0x9e3779b9;

struct ngfi_dict_t {
  size_t nslots;
  size_t nitems;
  size_t val_size;
  size_t slot_size;
  float  max_load_factor;
  char   data[];
};

typedef struct ngfi_dict_slot {
  uintptr_t key;
  size_t    idx;
  char      val[];
} ngfi_dict_slot;

ngfi_dict ngfi_dict_create(size_t nslots, size_t val_size) {
  const size_t slot_size = ngfi_align_size(sizeof(ngfi_dict_slot) + val_size);
  const size_t all_size  = sizeof(struct ngfi_dict_t) + slot_size * nslots;
  ngfi_dict    result    = (ngfi_dict)NGFI_ALLOCN(char, all_size);
  if (result) {
    result->nslots          = nslots;
    result->val_size        = val_size;
    result->slot_size       = slot_size;
    result->max_load_factor = 0.675f;
    ngfi_dict_clear(result);
  }
  return result;
}

void ngfi_dict_destroy(ngfi_dict dict) {
  if (dict) {
    const size_t all_size = sizeof(struct ngfi_dict_t) + dict->slot_size * dict->nslots;
    NGFI_FREEN(dict, all_size);
  }
}

void ngfi_dict_clear(ngfi_dict dict) {
  assert(dict);
  memset(dict->data, 0xff, dict->slot_size * dict->nslots);
  dict->nitems = 0u;
}

static ngfi_dict_slot* ngfi_dict_get_slot(ngfi_dict dict, size_t slot) {
  assert(dict);
  if (slot < dict->nslots) {
    return (ngfi_dict_slot*)(&dict->data[slot * dict->slot_size]);
  } else {
    return NULL;
  }
}

static void* ngfi_dict_rehash(ngfi_dict* d, uintptr_t k) {
  // TODO: adjust growth factor to take into the account the current max load factor.
  ngfi_dict new_d = ngfi_dict_create((*d)->nitems * 4u, // new load factor will be 0.25
      (*d)->val_size);
  ngfi_dict_set_max_load_factor(new_d, (*d)->max_load_factor);
  void* result = NULL;
  NGFI_DICT_FOREACH(*d, it) {
    size_t          idx  = *(size_t*)it;
    ngfi_dict_slot* s    = ngfi_dict_get_slot(*d, idx);
    void*           data = ngfi_dict_get(&new_d, s->key, s->val, NULL, NULL);
    if (k != NGFI_DICT_INVALID_KEY && s->key == k) { result = data; }
  }
  assert(ngfi_dict_count(*d) == ngfi_dict_count(new_d));
  ngfi_dict old_d = *d;
  *d              = new_d;
  ngfi_dict_destroy(old_d);
  return result;
}

void* ngfi_dict_get(
    ngfi_dict*         dict,
    ngfi_dict_key      key,
    void*              default_val,
    bool*              new_entry,
    ngfi_dict_keyhash* keyhash_out) {
  assert(dict);
  assert(*dict);
  assert(key != NGFI_DICT_INVALID_KEY);

  uint64_t mmh3_out[2] = {0, 0};
  if (ngfi_dict_hashfn == NULL) {
    ngfi_mmh3_x64_128(key, ngfi_dict_hashseed, mmh3_out);
  } else {
    ngfi_dict_hashfn(key, ngfi_dict_hashseed, mmh3_out);
  }
  const uint64_t    hash    = mmh3_out[0] ^ mmh3_out[1];
  ngfi_dict_keyhash keyhash = {.key = key, .hash = hash};
  if (keyhash_out) *keyhash_out = keyhash;

  return ngfi_dict_get_prehashed(dict, &keyhash, default_val, new_entry);
}

void* ngfi_dict_get_prehashed(
    ngfi_dict*               dict,
    const ngfi_dict_keyhash* keyhash,
    void*                    default_val,
    bool*                    new_entry) {
  size_t hash_slot_idx = keyhash->hash % (*dict)->nslots;

  ngfi_dict_slot* slot     = NULL;
  size_t          slot_idx = NGFI_DICT_INVALID_KEY;
  for (size_t slot_idx_offset = 0u; !slot && slot_idx_offset < (*dict)->nslots; ++slot_idx_offset) {
    const size_t    curr_slot_idx  = (hash_slot_idx + slot_idx_offset) % (*dict)->nslots;
    ngfi_dict_slot* candidate_slot = ngfi_dict_get_slot(*dict, curr_slot_idx);
    assert(candidate_slot);
    if (candidate_slot->key == keyhash->key || candidate_slot->key == NGFI_DICT_INVALID_KEY) {
      slot     = candidate_slot;
      slot_idx = curr_slot_idx;
    }
  }
  
  if (!slot) {
    assert(false);
    return NULL;
  }

  if (default_val && slot->key == NGFI_DICT_INVALID_KEY) {
    if (new_entry) *new_entry = true;
    /* inserting a new value */
    slot->key = keyhash->key;
    memcpy(slot->val, default_val, (*dict)->val_size);
    ngfi_dict_slot* idx_slot = ngfi_dict_get_slot(*dict, (*dict)->nitems++);
    idx_slot->idx            = slot_idx;
    const float load_factor  = (float)(*dict)->nitems / (float)(*dict)->nslots;
    if (load_factor >= (*dict)->max_load_factor) {
      return ngfi_dict_rehash(dict, keyhash->key);
    } else {
      return slot->val;
    }
  } else if (slot->key == NGFI_DICT_INVALID_KEY) {
    return NULL;
  } else {
    if (new_entry) *new_entry = false;
    return slot->val;
  }
}

ngfi_dict_iter ngfi_dict_itstart(ngfi_dict dict) {
  return dict->nitems > 0u ? &((ngfi_dict_slot*)dict->data)->idx : NULL;
}

ngfi_dict_iter ngfi_dict_itnext(ngfi_dict dict, ngfi_dict_iter iter) {
  if (iter) {
    ngfi_dict_iter start = (char*)ngfi_dict_itstart(dict);
    const bool     last_slot =
        (size_t)((char*)iter - (char*)start) / dict->slot_size == dict->nslots - 1u;
    if (last_slot) {
      return NULL;
    } else {
      iter             = (ngfi_dict_iter)((char*)iter + dict->slot_size);
      const size_t idx = *(size_t*)iter;
      return idx == NGFI_DICT_INVALID_KEY ? NULL : iter;
    }
  } else {
    return NULL;
  }
}

void* ngfi_dict_itval(ngfi_dict dict, ngfi_dict_iter iter) {
  if (iter) {
    size_t idx = *(size_t*)iter;
    return idx != NGFI_DICT_INVALID_KEY ? ngfi_dict_get_slot(dict, idx)->val : NULL;
  } else {
    return NULL;
  }
}

size_t ngfi_dict_count(ngfi_dict dict) {
  assert(dict);
  return dict->nitems;
}

size_t ngfi_dict_nslots(ngfi_dict dict) {
  assert(dict);
  return dict->nslots;
}

float ngfi_dict_max_load_factor(ngfi_dict dict) {
  assert(dict);
  return dict->max_load_factor;
}

void ngfi_dict_set_max_load_factor(ngfi_dict dict, float a) {
  assert(dict);
  assert(a > 0.f);
  dict->max_load_factor = a;
}

