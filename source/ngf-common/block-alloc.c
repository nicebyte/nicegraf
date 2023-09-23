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

#include "block-alloc.h"

#include "dynamic-array.h"
#include "list.h"
#include "macros.h"

#include <string.h>
#include <time.h>

#define NGFI_BLKALLOC_MAX_FRAMES_INACTIVE (3u)

typedef struct ngfi_blkalloc_pool_usage {
  uint32_t nactive_blocks;
  uint32_t nframes_inactive;
} ngfi_blkalloc_pool_usage;

typedef struct ngfi_blkalloc_block {  // The block itself.
  ngfi_list_node free_list_node;      // Freelist node.
  uint32_t       parent_pool_idx;     // Index of the block's pool.
#if !defined(NDEBUG)
  uint32_t marker_and_tag;  // Identifies the parent allocator.
#endif
  ngfi_max_align_t padding;
#pragma warning(push)
#pragma warning(disable : 4200)
  uint8_t data[];
#pragma warning(pop)
} ngfi_blkalloc_block;

struct ngfi_block_allocator {
  NGFI_DARRAY_OF(uint8_t*) pools;
  NGFI_DARRAY_OF(ngfi_blkalloc_pool_usage) pool_usage;
  ngfi_list_node* freelist;
  uint32_t        block_size;
  uint32_t        block_size_user;
  uint32_t        nblocks_per_pool;
  uint32_t        nblocks_total;
  uint32_t        nblocks_free;
  uint32_t        tag;
};

#if !defined(NDEBUG)

static const uint32_t IN_USE_BLOCK_MARKER_MASK = (1u << 31);

static bool ngfi_blkalloc_is_block_free(const ngfi_blkalloc_block* b) {
  return !(b->marker_and_tag & IN_USE_BLOCK_MARKER_MASK);
}

static void ngfi_blkalloc_mark_block_inuse(ngfi_blkalloc_block* b) {
  b->marker_and_tag |= IN_USE_BLOCK_MARKER_MASK;
}

static void ngfi_blkalloc_mark_block_free(ngfi_blkalloc_block* b) {
  b->marker_and_tag &= (~IN_USE_BLOCK_MARKER_MASK);
}

static uint32_t ngfi_blkalloc_block_tag(const ngfi_blkalloc_block* b) {
  return (~IN_USE_BLOCK_MARKER_MASK) & b->marker_and_tag;
}
#endif

static void ngfi_blkalloc_add_pool(ngfi_block_allocator* allocator) {
  const size_t pool_size = allocator->block_size * allocator->nblocks_per_pool;
  uint8_t*     pool      = NGFI_ALLOCN(uint8_t, pool_size);
  if (pool != NULL) {
    NGFI_DARRAY_APPEND(allocator->pools, pool);
    static ngfi_blkalloc_pool_usage fresh_pool_usage = {0u, 0u};
    NGFI_DARRAY_APPEND(allocator->pool_usage, fresh_pool_usage);
    const uint32_t pool_idx = NGFI_DARRAY_SIZE(allocator->pools) - 1u;
    for (uint32_t b = 0u; b < allocator->nblocks_per_pool; ++b) {
      ngfi_blkalloc_block* blk = (ngfi_blkalloc_block*)(pool + allocator->block_size * b);
      ngfi_list_init(&blk->free_list_node);
      blk->parent_pool_idx = pool_idx;
      if (allocator->freelist) {
        ngfi_list_append(&blk->free_list_node, allocator->freelist);
      } else {
        allocator->freelist = &blk->free_list_node;
      }
#if !defined(NDEBUG)
      blk->marker_and_tag = allocator->tag;
      ngfi_blkalloc_mark_block_free(blk);
#endif
      allocator->nblocks_total++;
      allocator->nblocks_free++;
    }
  }
}

ngfi_block_allocator* ngfi_blkalloc_create(uint32_t requested_block_size, uint32_t nblocks) {
#if !defined(NDEBUG)
  static NGFI_THREADLOCAL uint32_t next_tag = 0u;
  if (next_tag == 0u) {
    srand((unsigned int)time(NULL));
    uint32_t threadid = (uint32_t)rand();
    next_tag          = (~IN_USE_BLOCK_MARKER_MASK) & (threadid << 16);
  }
#endif

  ngfi_block_allocator* allocator = NGFI_ALLOC(ngfi_block_allocator);
  if (allocator == NULL) { return NULL; }
  memset(allocator, 0, sizeof(*allocator));

  const uint32_t unaligned_block_size = requested_block_size + sizeof(ngfi_blkalloc_block);
  const uint32_t aligned_block_size =
      unaligned_block_size +
      (uint32_t)(NGFI_MAX_ALIGNMENT - (unaligned_block_size & (NGFI_MAX_ALIGNMENT - 1u)));

  allocator->block_size       = aligned_block_size;
  allocator->block_size_user  = requested_block_size;
  allocator->nblocks_per_pool = nblocks;
#if !defined(NDEBUG)
  allocator->tag = (~IN_USE_BLOCK_MARKER_MASK) & (next_tag++);
#endif
  NGFI_DARRAY_RESET(allocator->pools, 8u);
  NGFI_DARRAY_RESET(allocator->pool_usage, 8u);
  allocator->freelist = NULL;
  ngfi_blkalloc_add_pool(allocator);
  return allocator;
}

void ngfi_blkalloc_destroy(ngfi_block_allocator* allocator) {
  NGFI_DARRAY_FOREACH(allocator->pools, i) {
    uint8_t* pool = NGFI_DARRAY_AT(allocator->pools, i);
    if (pool) { NGFI_FREEN(pool, allocator->block_size * allocator->nblocks_per_pool); }
  }
  NGFI_DARRAY_DESTROY(allocator->pools);
  NGFI_DARRAY_DESTROY(allocator->pool_usage);
  NGFI_FREE(allocator);
}

void* ngfi_blkalloc_alloc(ngfi_block_allocator* alloc) {
  if (alloc->freelist == NULL) { ngfi_blkalloc_add_pool(alloc); }
  void* result = NULL;
  if (alloc->freelist != NULL) {
    ngfi_blkalloc_block* blk =
        NGFI_LIST_CONTAINER_OF(alloc->freelist, ngfi_blkalloc_block, free_list_node);
    ngfi_blkalloc_pool_usage* pool_usage = &NGFI_DARRAY_AT(alloc->pool_usage, blk->parent_pool_idx);
    ngfi_list_node*           new_head   = alloc->freelist->next;
    ngfi_list_remove(alloc->freelist);
    alloc->freelist = new_head == alloc->freelist ? NULL : new_head;
    result          = blk->data;
    alloc->nblocks_free--;
    pool_usage->nactive_blocks++;
    pool_usage->nframes_inactive = 0u;
#if !defined(NDEBUG)
    ngfi_blkalloc_mark_block_inuse(blk);
#endif
  }
  return result;
}

ngfi_blkalloc_error ngfi_blkalloc_free(ngfi_block_allocator* alloc, void* ptr) {
  ngfi_blkalloc_error result = NGFI_BLK_NO_ERROR;
  if (ptr != NULL) {
    ngfi_blkalloc_block* blk =
        (ngfi_blkalloc_block*)((uint8_t*)ptr - offsetof(ngfi_blkalloc_block, data));
    ngfi_blkalloc_pool_usage* pool_usage = &NGFI_DARRAY_AT(alloc->pool_usage, blk->parent_pool_idx);
#if !defined(NDEBUG)
    uint32_t blk_tag = ngfi_blkalloc_block_tag(blk);
    uint32_t my_tag  = alloc->tag;
    if (ngfi_blkalloc_is_block_free(blk)) {
      result = NGFI_BLK_DOUBLE_FREE;
    } else if (my_tag != blk_tag) {
      result = NGFI_BLK_WRONG_ALLOCATOR;
    } else {
      ngfi_blkalloc_mark_block_free(blk);
    }
#endif
    if (result == NGFI_BLK_NO_ERROR) {
      if (alloc->freelist) {
        ngfi_list_append(&blk->free_list_node, alloc->freelist);
      } else {
        alloc->freelist = &blk->free_list_node;
      }
      alloc->nblocks_free++;
      pool_usage->nactive_blocks--;
    }
  }
  return result;
}

uint32_t ngfi_blkalloc_blksize(ngfi_block_allocator* alloc) {
  assert(alloc);
  return alloc->block_size_user;
}

void ngfi_blkalloc_cleanup(ngfi_block_allocator* alloc) {
  NGFI_DARRAY_FOREACH(alloc->pools, i) {
    ngfi_blkalloc_pool_usage* pool_usage = &NGFI_DARRAY_AT(alloc->pool_usage, i);
    uint8_t*                  pool       = NGFI_DARRAY_AT(alloc->pools, i);
    if (i > 0u && pool_usage->nframes_inactive >= NGFI_BLKALLOC_MAX_FRAMES_INACTIVE && pool) {
      NGFI_FREEN(pool, alloc->block_size * alloc->nblocks_per_pool);
      NGFI_DARRAY_AT(alloc->pools, i) = NULL;
    } else if (pool_usage->nactive_blocks == 0u) {
      NGFI_DARRAY_AT(alloc->pool_usage, i).nframes_inactive++;
    }
  }
}

void ngfi_blkalloc_dump_dbgstats(ngfi_block_allocator* alloc, FILE* out) {
  fprintf(out, "Debug stats for block allocator 0x%p\n", alloc);
  fprintf(
      out,
      "Block size:\t%ld (requested)\t%ld (effective)\n",
      alloc->block_size_user,
      alloc->block_size);
  fprintf(out, "Total mem:\t%zu\n", alloc->block_size * alloc->nblocks_total);
  fprintf(out, "Avail mem:\t%zu\n", alloc->block_size * alloc->nblocks_free);
  fprintf(
      out,
      "Used mem:\t%zu\n",
      alloc->block_size * (alloc->nblocks_total - alloc->nblocks_free));
  fprintf(out, "Active pools:\t%zu\n", NGFI_DARRAY_SIZE(alloc->pools));
  fprintf(out, "Blks per pool:\t%zu\n", alloc->nblocks_per_pool);
}
