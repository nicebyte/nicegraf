/**
Copyright © 2018 nicegraf contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the “Software”), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "nicegraf_internal.h"
#include <stdlib.h>

void* ngf_default_alloc(size_t obj_size, size_t nobjs) {
  return malloc(obj_size * nobjs);
}

void ngf_default_free(void *ptr, size_t s, size_t n) {
  free(ptr);
}

const ngf_allocation_callbacks NGF_DEFAULT_ALLOC_CB = {
  ngf_default_alloc,
  ngf_default_free
};

const ngf_allocation_callbacks *NGF_ALLOC_CB =
    &NGF_DEFAULT_ALLOC_CB;

void ngf_set_allocation_callbacks(const ngf_allocation_callbacks *callbacks) {
  if (callbacks == NULL) {
    NGF_ALLOC_CB = &NGF_DEFAULT_ALLOC_CB;
  } else {
    NGF_ALLOC_CB = callbacks;
  }
}

_ngf_block_allocator* _ngf_blkalloc_create(uint32_t block_size, uint32_t nblocks) {
  _ngf_block_allocator *alloc = NGF_ALLOC(_ngf_block_allocator);
  const uint32_t total_unaligned_block_size =
      block_size + sizeof(_ngf_blkalloc_block);
  const uint32_t total_aligned_block_size =
      total_unaligned_block_size +
      (total_unaligned_block_size % sizeof(_ngf_blkalloc_block*));
  alloc->chunk = NGF_ALLOCN(uint8_t, block_size * nblocks);
  alloc->block_size = block_size;
  alloc->nblocks = nblocks;
  alloc->freelist = (_ngf_blkalloc_block*)alloc->chunk;
  for (uint32_t b = 0u; b < nblocks - 1u; ++b) {
    _ngf_blkalloc_block *blk =
        (_ngf_blkalloc_block*)(alloc->chunk + total_aligned_block_size * b);
    _ngf_blkalloc_block *next_blk =
        (_ngf_blkalloc_block*)(alloc->chunk + total_aligned_block_size * (b + 1u));
    blk->next_free = next_blk;
  }
  alloc->freelist = (_ngf_blkalloc_block*)alloc->chunk;
  return alloc;
}

void _ngf_blkalloc_destroy(_ngf_block_allocator *alloc) {
  NGF_FREEN(alloc->chunk, alloc->block_size * alloc->nblocks);
  NGF_FREE(alloc);
}

void* _ngf_blkalloc_alloc(_ngf_block_allocator *alloc) {
  _ngf_blkalloc_block *blk = alloc->freelist;
  alloc->freelist = blk->next_free;
  return blk->data;
}

void _ngf_blkalloc_free(_ngf_block_allocator *alloc, void *ptr) {
  _ngf_blkalloc_block *blk =
      (_ngf_blkalloc_block*)((uint8_t*)ptr - sizeof(_ngf_blkalloc_block*));
  blk->next_free = alloc->freelist;
  alloc->freelist = blk;
}
