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

#include "ngf-common/native-binding-map.h"
#include "ngf-common/list.h"
#include "ngf-common/dynamic_array.h"
#include "macros.h"

#include <string.h>

/* Native binding map for a single descriptor set. */
typedef struct ngfi_set_native_binding_map {
  uint32_t* map;
  uint32_t nbindings;
} ngfi_set_native_binding_map;

/* Maps (set, binding) pair to a platform's native binding id. */
struct ngfi_native_binding_map {
  ngfi_set_native_binding_map* set_maps;
  uint32_t nsets;
};

static int ngfi_bind_op_sorter(const void* a_void, const void* b_void) {
  const ngfi_pending_bind_op* a = a_void;
  const ngfi_pending_bind_op* b = b_void;
  if (a->op.target_set < b->op.target_set) return -1;
  else if (a->op.target_set == b->op.target_set) {
    if (a->op.target_binding < b->op.target_binding) return -1;
    else if (a->op.target_binding == b->op.target_binding) return 0;
    else return 1;
  } else return 1;
}

ngfi_native_binding_map* ngfi_create_native_binding_map_from_pending_bind_ops(
  const ngfi_pending_bind_op* pending_bind_ops_list) {
  /* Create a sorted array of bind ops from the given list.
     Sorting is necessary to ensure we process bind ops in the same order
     regardless of the order they were added to the list - this ensures
     consistency of the mapping. */
  NGFI_DARRAY_OF(ngfi_pending_bind_op) bind_ops_array;
  NGFI_DARRAY_RESET(bind_ops_array, 8);
  NGFI_LIST_FOR_EACH_CONST(
    &pending_bind_ops_list->pending_ops_list_node, node) {
    const ngfi_pending_bind_op* bind_op =
      NGFI_LIST_CONTAINER_OF(node, ngfi_pending_bind_op, pending_ops_list_node);
    NGFI_DARRAY_APPEND(bind_ops_array, *bind_op);
  }
  qsort(bind_ops_array.data, NGFI_DARRAY_SIZE(bind_ops_array),
        sizeof(ngfi_pending_bind_op), ngfi_bind_op_sorter);
  
  /* Allocate entries in the lookup table. */
  ngfi_native_binding_map* map = NGFI_ALLOC(ngfi_native_binding_map);
  map->nsets = 0;
  map->set_maps = NULL;
  for (int i = (int)(NGFI_DARRAY_SIZE(bind_ops_array) - 1); i >= 0; --i) {
    const ngf_resource_bind_op* op = &(NGFI_DARRAY_AT(bind_ops_array, i).op);
    if (map->set_maps == NULL) {
      NGFI_CHECK_FATAL(i == NGFI_DARRAY_SIZE(bind_ops_array) - 1, "unexpected error");
      /* since we're starting at the far end of a sorted array, op->target_set
         would be the largest set ID at this point, indicating the max required
         number of sets. */
      map->nsets = op->target_set + 1;
      map->set_maps = NGFI_ALLOCN(ngfi_set_native_binding_map, map->nsets);
      memset(map->set_maps, 0, sizeof(ngfi_set_native_binding_map) * map->nsets);
    }
    const uint32_t current_set = op->target_set;
    if (map->set_maps[current_set].nbindings == 0) {
      map->set_maps[current_set].nbindings = op->target_binding + 1;
      map->set_maps[current_set].map = NGFI_ALLOCN(uint32_t, map->set_maps[current_set].nbindings);
      memset(map->set_maps[current_set].map, ~0,
             sizeof(uint32_t) * (map->set_maps[current_set].nbindings));
    }
  }
  
  /* Assign binding ids. */
  uint32_t num_descriptors_of_type[NGF_DESCRIPTOR_TYPE_COUNT] = {0};
  NGFI_DARRAY_FOREACH(bind_ops_array, i) {
    const ngf_resource_bind_op* op = &NGFI_DARRAY_AT(bind_ops_array, i).op;
    map->set_maps[op->target_set].map[op->target_binding] =
    num_descriptors_of_type[op->type]++;
  }
  NGFI_DARRAY_DESTROY(bind_ops_array);
  return map;
}

void ngfi_destroy_native_binding_map(ngfi_native_binding_map* map) {
  for (uint32_t i = 0; i < map->nsets; ++i) {
    NGFI_FREEN(map->set_maps[i].map, map->set_maps[i].nbindings);
  }
  NGFI_FREEN(map->set_maps, map->nsets);
  NGFI_FREE(map);
}

uint32_t ngfi_native_binding_map_lookup(const ngfi_native_binding_map* map,
                                        uint32_t set, uint32_t binding) {
  if (set < map->nsets && binding < map->set_maps[set].nbindings) {
    return map->set_maps[set].map[binding];
  } else {
    return ~0u;
  }
}
