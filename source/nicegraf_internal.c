/**
 * Copyright (c) 2019 nicegraf contributors
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "nicegraf_internal.h"
#include "dynamic_array.h"
#include <stdlib.h>

// Default allocation callbacks.
void* ngf_default_alloc(size_t obj_size, size_t nobjs) {
  return malloc(obj_size * nobjs);
}

void ngf_default_free(void *ptr, size_t s, size_t n) {
  _NGF_FAKE_USE(s, n);
  free(ptr);
}

const ngf_allocation_callbacks NGF_DEFAULT_ALLOC_CB = {
  ngf_default_alloc,
  ngf_default_free
};

const ngf_allocation_callbacks *NGF_ALLOC_CB = &NGF_DEFAULT_ALLOC_CB;

void ngf_set_allocation_callbacks(const ngf_allocation_callbacks *callbacks) {
  if (callbacks == NULL) {
    NGF_ALLOC_CB = &NGF_DEFAULT_ALLOC_CB;
  } else {
    NGF_ALLOC_CB = callbacks;
  }
}

/**
 * The block allocator. doles out memory in fixed-size blocks from a pool.
 * A block allocator is created with a certain initial capacity. If the block
 * capacity is exceeded, an additional block pool is allocated.
 */

typedef struct _ngf_blkalloc_block  _ngf_blkalloc_block;

typedef struct _ngf_blkalloc_block_header { // Block metadata.
 _ngf_blkalloc_block *next_free;      // Points to the next free block in list.
  uint32_t            marker_and_tag; // Identifies the parent allocator.
} _ngf_blkalloc_block_header;

struct _ngf_blkalloc_block { // The block itself.
 _ngf_blkalloc_block_header header;
  uint8_t                   data[];
};

struct _ngf_block_allocator {
 _NGF_DARRAY_OF(uint8_t*)  pools;
 _ngf_blkalloc_block      *freelist;
  size_t                   block_size;
  uint32_t                 nblocks;
  uint32_t                 tag;
};

static const uint32_t MARKER_MASK = (1u << 31);

static void _ngf_blkallock_add_pool(_ngf_block_allocator *alloc) {
 _ngf_blkalloc_block *old_freelist = alloc->freelist;
  const size_t        pool_size    = alloc->block_size * alloc->nblocks;
  uint8_t            *pool         = NGF_ALLOCN(uint8_t, pool_size);

  alloc->freelist = (_ngf_blkalloc_block*)pool;
  for (uint32_t b = 0u; b < alloc->nblocks; ++b) {
    _ngf_blkalloc_block *blk =
        (_ngf_blkalloc_block*)(pool + alloc->block_size * b);
    _ngf_blkalloc_block *next_blk =
        (_ngf_blkalloc_block*)(pool + alloc->block_size * (b + 1u));
    blk->header.next_free =
        (b < alloc->nblocks - 1u) ? next_blk : old_freelist;
    blk->header.marker_and_tag = (~MARKER_MASK) & alloc->tag;
  }
  _NGF_DARRAY_APPEND(alloc->pools, pool);
}

_ngf_block_allocator* _ngf_blkalloc_create(uint32_t requested_block_size,
                                           uint32_t nblocks) {
  static NGF_THREADLOCAL uint32_t next_tag = 0u;
  if (next_tag == 0u) {
    uint32_t threadid = (uint32_t)_ngf_cur_thread_id();
    next_tag = (~MARKER_MASK) & (threadid << 16);
  }

  _ngf_block_allocator *alloc = NGF_ALLOC(_ngf_block_allocator);
  if (alloc == NULL) { return NULL; }

  const size_t unaligned_block_size =
      requested_block_size + sizeof(_ngf_blkalloc_block);
  const size_t aligned_block_size =
      unaligned_block_size +
     (unaligned_block_size % sizeof(_ngf_blkalloc_block*));

  alloc->block_size = aligned_block_size;
  alloc->nblocks    = nblocks;
  alloc->tag        = next_tag++;
  _NGF_DARRAY_RESET(alloc->pools, 8u);
  alloc->freelist = NULL;
  _ngf_blkallock_add_pool(alloc);
  return alloc;
}

void _ngf_blkalloc_destroy(_ngf_block_allocator *alloc) {
  for (uint32_t i = 0u; i < _NGF_DARRAY_SIZE(alloc->pools); ++i) {
    uint8_t *pool = _NGF_DARRAY_AT(alloc->pools, i);
    if (pool) { NGF_FREEN(pool, alloc->block_size * alloc->nblocks); }
  }
  _NGF_DARRAY_DESTROY(alloc->pools);
  NGF_FREE(alloc);
}

void* _ngf_blkalloc_alloc(_ngf_block_allocator *alloc) {
  if (alloc->freelist) {
    _ngf_blkallock_add_pool(alloc);
  }
  _ngf_blkalloc_block *blk = alloc->freelist;
  void *result = NULL;
  if (blk != NULL) {
    alloc->freelist = blk->header.next_free;
    result = blk->data;
    blk->header.marker_and_tag |= MARKER_MASK;
  }
  return result;
}

_ngf_blkalloc_error _ngf_blkalloc_free(_ngf_block_allocator *alloc,
                                        void *ptr) {
  _ngf_blkalloc_error result = _NGF_BLK_NO_ERROR;
  if (ptr != NULL) {
    _ngf_blkalloc_block *blk = 
        (_ngf_blkalloc_block*)((uint8_t*)ptr -
                                offsetof(_ngf_blkalloc_block, data));

    uint32_t blk_tag = (~MARKER_MASK) & blk->header.marker_and_tag;
    uint32_t my_tag = alloc->tag;
    uint32_t blk_marker = MARKER_MASK & blk->header.marker_and_tag;
    if (blk_marker == 0u) {
      result = _NGF_BLK_DOUBLE_FREE;
    } else if (my_tag == blk_tag) {
      blk->header.next_free = alloc->freelist;
      blk->header.marker_and_tag &= (~MARKER_MASK);
      alloc->freelist = blk;
    } else {
      result = _NGF_BLK_WRONG_ALLOCATOR;
    }
  }
  return result;
}
