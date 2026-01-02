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

#pragma once

#include "block-alloc.h"
#include "list.h"

#include <stddef.h>
#include <string.h>

typedef struct ngfi_chnk_hdr {
  ngfi_list_node clnode;
  uint32_t       bytes_used;
  uint32_t       bytes_total;
} ngfi_chnk_hdr;

typedef struct ngfi_chnklist {
  ngfi_block_allocator* blkalloc;
  ngfi_chnk_hdr*        firstchnk;
} ngfi_chnklist;

static inline void* ngfi_chnk_data(ngfi_chnk_hdr* hdr, uint32_t offset) {
  return (void*)((char*)hdr + sizeof(*hdr) + offset);
}

static inline void* ngfi_chnklist_append(ngfi_chnklist* list, const void* data, uint32_t data_size) {
  assert(list);
  assert(data);
  assert(list->blkalloc);
  const uint32_t blksize = ngfi_blkalloc_blksize(list->blkalloc);
  if (blksize <= sizeof(ngfi_chnk_hdr) || blksize - sizeof(ngfi_chnk_hdr) < data_size) {
    return NULL;
  }
  ngfi_chnk_hdr* tail =
      list->firstchnk ? NGFI_LIST_CONTAINER_OF(&list->firstchnk->clnode, ngfi_chnk_hdr, clnode)
                      : NULL;
  if (tail == NULL || tail->bytes_total - tail->bytes_used <= data_size) {
    ngfi_chnk_hdr* new_chnk = (ngfi_chnk_hdr*)ngfi_blkalloc_alloc(list->blkalloc);
    if (!new_chnk) { return NULL; }
    ngfi_list_init(&new_chnk->clnode);
    new_chnk->bytes_used  = 0u;
    new_chnk->bytes_total = (uint32_t)blksize - (uint32_t)sizeof(ngfi_chnk_hdr);
    if (tail != NULL) {
      ngfi_list_append(&new_chnk->clnode, &tail->clnode);
    } else {
      list->firstchnk = new_chnk;
    }
    tail = new_chnk;
  }
  void* chnk_data = ngfi_chnk_data(tail, tail->bytes_used);
  memcpy(chnk_data, data, data_size);
  tail->bytes_used += data_size;
  return chnk_data;
}

#define NGFI_CHNK_FROM_NODE(node) (NGFI_LIST_CONTAINER_OF(node, ngfi_chnk_hdr, clnode))

static inline void ngfi_chnklist_clear(ngfi_chnklist* list) {
  assert(list);
  assert(list->blkalloc);
  while (list->firstchnk) {
    ngfi_chnk_hdr* old_hdr = list->firstchnk;
    list->firstchnk        = list->firstchnk->clnode.next == &list->firstchnk->clnode
                                 ? NULL
                                 : NGFI_CHNK_FROM_NODE(list->firstchnk->clnode.next);
    ngfi_list_remove(&old_hdr->clnode);
    ngfi_blkalloc_free(list->blkalloc, old_hdr);
  }
}

#define NGFI_CHNK_FOR_EACH(chnk, elem_type, ptrname) \
  for (elem_type* ptrname = (elem_type*)ngfi_chnk_data((chnk), 0);          \
       ptrname - (elem_type*)ngfi_chnk_data((chnk), 0) <        \
       (ptrdiff_t)((chnk)->bytes_used / sizeof(elem_type)); \
       ptrname++)

#define NGFI_CHNKLIST_FOR_EACH(chnklist, elem_type, ptrname)                                 \
  if ((chnklist).firstchnk) NGFI_LIST_FOR_EACH(&(chnklist).firstchnk->clnode, ptrname##_node)    \
    NGFI_CHNK_FOR_EACH(NGFI_CHNK_FROM_NODE(ptrname##_node), elem_type, ptrname)

typedef struct ngfi_chnk_range {
  ngfi_chnk_hdr* chnk;
  uint32_t       start;
  uint32_t       size;
} ngfi_chnk_range;
