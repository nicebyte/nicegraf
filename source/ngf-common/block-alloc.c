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

#include "block-alloc.h"

#include "dynamic_array.h"

#include <time.h>

/**
 * The block allocator. doles out memory in fixed-size blocks from a pool.
 * A block allocator is created with a certain initial capacity. If the block
 * capacity is exceeded, an additional block pool is allocated.
 */

typedef struct ngfi_blkalloc_block ngfi_blkalloc_block;

typedef struct ngfi_blkalloc_block_header {  // Block metadata.
  ngfi_blkalloc_block* next_free;            // Points to the next free block in list.
  uint32_t             marker_and_tag;       // Identifies the parent allocator.
} ngfi_blkalloc_block_header;

struct ngfi_blkalloc_block {  // The block itself.
  ngfi_blkalloc_block_header header;
  uint8_t                    data[];
};

struct ngfi_block_allocator {
  NGFI_DARRAY_OF(uint8_t*) pools;
  ngfi_blkalloc_block* freelist;
  size_t               block_size;
  uint32_t             nblocks;
  uint32_t             tag;
};

static const uint32_t FREE_BLOCK_MARKER_MASK = (1u << 31);

static bool ngfi_blkalloc_is_block_free(const ngfi_blkalloc_block* b) {
  return !(b->header.marker_and_tag & FREE_BLOCK_MARKER_MASK);
}

static void ngfi_blkalloc_mark_block_inuse(ngfi_blkalloc_block* b) {
  b->header.marker_and_tag |= FREE_BLOCK_MARKER_MASK;
}

static void ngfi_blkalloc_mark_block_free(ngfi_blkalloc_block* b) {
  b->header.marker_and_tag &= (~FREE_BLOCK_MARKER_MASK);
}

static uint32_t ngfi_blkalloc_block_tag(const ngfi_blkalloc_block* b) {
  return (~FREE_BLOCK_MARKER_MASK) & b->header.marker_and_tag;
}

static void ngfi_blkalloc_add_pool(ngfi_block_allocator* alloc) {
  ngfi_blkalloc_block* old_freelist = alloc->freelist;
  const size_t         pool_size    = alloc->block_size * alloc->nblocks;
  uint8_t*             pool         = NGFI_ALLOCN(uint8_t, pool_size);

  alloc->freelist = (ngfi_blkalloc_block*)pool;
  for (uint32_t b = 0u; b < alloc->nblocks; ++b) {
    ngfi_blkalloc_block* blk      = (ngfi_blkalloc_block*)(pool + alloc->block_size * b);
    ngfi_blkalloc_block* next_blk = (ngfi_blkalloc_block*)(pool + alloc->block_size * (b + 1u));
    blk->header.next_free         = (b < alloc->nblocks - 1u) ? next_blk : old_freelist;
    blk->header.marker_and_tag    = alloc->tag;
    ngfi_blkalloc_mark_block_free(blk);
    blk->header.marker_and_tag |= 0;
  }
  NGFI_DARRAY_APPEND(alloc->pools, pool);
}

ngfi_block_allocator* ngfi_blkalloc_create(uint32_t requested_block_size, uint32_t nblocks) {
  static NGFI_THREADLOCAL uint32_t next_tag = 0u;
  if (next_tag == 0u) {
    srand((unsigned int)time(NULL));
    uint32_t threadid = (uint32_t)rand();
    next_tag          = (~FREE_BLOCK_MARKER_MASK) & (threadid << 16);
  }

  ngfi_block_allocator* alloc = NGFI_ALLOC(ngfi_block_allocator);
  if (alloc == NULL) { return NULL; }

  const size_t unaligned_block_size = requested_block_size + sizeof(ngfi_blkalloc_block);
  const size_t aligned_block_size =
      unaligned_block_size + (unaligned_block_size % sizeof(ngfi_blkalloc_block*));

  alloc->block_size = aligned_block_size;
  alloc->nblocks    = nblocks;
  alloc->tag        = (~FREE_BLOCK_MARKER_MASK) & (next_tag++);
  NGFI_DARRAY_RESET(alloc->pools, 8u);
  alloc->freelist = NULL;
  ngfi_blkalloc_add_pool(alloc);
  return alloc;
}

void ngfi_blkalloc_destroy(ngfi_block_allocator* alloc) {
  for (uint32_t i = 0u; i < NGFI_DARRAY_SIZE(alloc->pools); ++i) {
    uint8_t* pool = NGFI_DARRAY_AT(alloc->pools, i);
    if (pool) { NGFI_FREEN(pool, alloc->block_size * alloc->nblocks); }
  }
  NGFI_DARRAY_DESTROY(alloc->pools);
  NGFI_FREE(alloc);
}

void* ngfi_blkalloc_alloc(ngfi_block_allocator* alloc) {
  if (alloc->freelist == NULL) { ngfi_blkalloc_add_pool(alloc); }
  ngfi_blkalloc_block* blk    = alloc->freelist;
  void*                result = NULL;
  if (blk != NULL) {
    alloc->freelist = blk->header.next_free;
    result          = blk->data;
    ngfi_blkalloc_mark_block_inuse(blk);
  }
  return result;
}

ngfi_blkalloc_error ngfi_blkalloc_free(ngfi_block_allocator* alloc, void* ptr) {
  ngfi_blkalloc_error result = NGFI_BLK_NO_ERROR;
  if (ptr != NULL) {
    ngfi_blkalloc_block* blk =
        (ngfi_blkalloc_block*)((uint8_t*)ptr - offsetof(ngfi_blkalloc_block, data));
#if !defined(NDEBUG)
    uint32_t blk_tag = ngfi_blkalloc_block_tag(blk);
    uint32_t my_tag  = alloc->tag;
    if (ngfi_blkalloc_is_block_free(blk)) {
      result = NGFI_BLK_DOUBLE_FREE;
    } else if (my_tag == blk_tag) {
      blk->header.next_free = alloc->freelist;
      ngfi_blkalloc_mark_block_free(blk);
      alloc->freelist = blk;
    } else {
      result = NGFI_BLK_WRONG_ALLOCATOR;
    }
#else
    blk->header.next_free = alloc->freelist;
    alloc->freelist       = blk;
#endif
  }
  return result;
}
