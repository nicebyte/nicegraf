/**
 * Copyright (c) 2021 nicegraf contributors
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
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "ngf-common/native-binding-map.h"

#include "ngf-common/dynamic-array.h"
#include "ngf-common/macros.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* ngfi_find_serialized_native_binding_map(const char* input) {
  const char   magic[]         = "NGF_NATIVE_BINDING_MAP";
  const char   comment_start[] = "/*";
  const char   comment_end[]   = "*/";
  const size_t magic_len       = sizeof(magic) - 1;
  bool         in_comment      = false;
  for (const char* i = input; *i; ++i) {
    if (!in_comment) {
      if (strncmp(i, comment_start, 2) == 0) { in_comment = true; }
    } else {
      if (strncmp(i, comment_end, 2) == 0) {
        in_comment = false;
      } else if (strncmp(i, magic, magic_len) == 0) {
        return i + magic_len;
      }
    }
  }
  return NULL;
}

typedef struct ngfi_set_map {
  uint32_t* bindings;
  uint32_t  nbindings;
} ngfi_set_map;

struct ngfi_native_binding_map {
  uint32_t      nsets;
  ngfi_set_map* sets;
};

struct native_binding_entry {
  int set;
  int binding;
  int native_binding;
};

static int binding_entry_comparator(const void* a, const void* b) {
  const struct native_binding_entry* e_a = a;
  const struct native_binding_entry* e_b = b;
  if (e_a->set < e_b->set)
    return -1;
  else {
    if (e_a->set == e_b->set) {
      if (e_a->binding < e_b->binding)
        return -1;
      else if (e_a->binding == e_b->binding)
        return 0;
      return 1;
    }
  }
  return 1;
}

ngfi_native_binding_map* ngfi_parse_serialized_native_binding_map(const char* serialized_map) {
  int                         n              = 0;
  int                         consumed_bytes = 0;
  struct native_binding_entry current_entry;
  NGFI_DARRAY_OF(struct native_binding_entry) entries;
  NGFI_DARRAY_RESET(entries, 16);

  while (sscanf(
             serialized_map,
             " ( %d %d ) : %d%n",
             &current_entry.set,
             &current_entry.binding,
             &current_entry.native_binding,
             &n) == 3) {
    serialized_map += n;
    consumed_bytes += n;
    if (current_entry.set == -1 && current_entry.binding == -1 &&
        current_entry.native_binding == -1) {
      break;
    }
    NGFI_DARRAY_APPEND(entries, current_entry);
  }

  /**
   * If we didn't consume any input at all, it was ill-formed.
   */
  if (consumed_bytes <= 0) { return NULL; }

  if (NGFI_DARRAY_SIZE(entries) > 0) {
    qsort(
        entries.data,
        NGFI_DARRAY_SIZE(entries) - 1,
        sizeof(struct native_binding_entry),
        binding_entry_comparator);
  }

  ngfi_native_binding_map* map = NGFI_ALLOC(ngfi_native_binding_map);
  map->nsets                   = 0;
  map->sets                    = NULL;
  for (int i = (int)NGFI_DARRAY_SIZE(entries) - 1; i >= 0; --i) {
    const struct native_binding_entry* entry = &(NGFI_DARRAY_AT(entries, i));
    if (map->sets == NULL) {
      /* since we're starting at the far end of a sorted array, op->target_set
         would be the largest set ID at this point, indicating the max required
         number of sets. */
      map->nsets = (uint32_t)entry->set + 1u;
      map->sets  = NGFI_ALLOCN(ngfi_set_map, map->nsets);
      memset(map->sets, 0, sizeof(ngfi_set_map) * map->nsets);
    }
    const uint32_t current_set = (uint32_t)entry->set;
    if (map->sets[current_set].nbindings == 0) {
      map->sets[current_set].nbindings = (uint32_t)entry->binding + 1;
      map->sets[current_set].bindings  = NGFI_ALLOCN(uint32_t, map->sets[current_set].nbindings);
      memset(
          map->sets[current_set].bindings,
          ~0,
          sizeof(uint32_t) * (map->sets[current_set].nbindings));
    }
  }

  /* Assign binding ids. */
  NGFI_DARRAY_FOREACH(entries, i) {
    const struct native_binding_entry* entry       = &NGFI_DARRAY_AT(entries, i);
    map->sets[entry->set].bindings[entry->binding] = (uint32_t)entry->native_binding;
  }

  return map;
}

uint32_t
ngfi_native_binding_map_lookup(const ngfi_native_binding_map* map, uint32_t set, uint32_t binding) {
  if (set >= map->nsets) return ~0u;
  if (binding >= map->sets[set].nbindings) return ~0u;
  return map->sets[set].bindings[binding];
}

void ngfi_destroy_native_binding_map(ngfi_native_binding_map* map) {
  for (uint32_t i = 0; i < map->nsets; ++i) {
    NGFI_FREEN(map->sets[i].bindings, map->sets[i].nbindings);
  }
  NGFI_FREEN(map->sets, map->nsets);
  NGFI_FREE(map);
}

