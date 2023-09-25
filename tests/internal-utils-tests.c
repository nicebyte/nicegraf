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

#define NGFI_DICT_TEST_MODE
#define NT_BREAK_ON_ASSERT_FAIL
#include "ngf-common/block-alloc.h"
#include "ngf-common/chunk-list.h"
#include "ngf-common/cmdbuf-state.h"
#include "ngf-common/dict.h"
#include "ngf-common/dynamic-array.h"
#include "ngf-common/frame-token.h"
#include "ngf-common/list.h"
#include "ngf-common/native-binding-map.h"
#include "ngf-common/stack-alloc.h"
#include "ngf-common/macros.h"
#include "nicetest.h"

#include <string.h>
#include <time.h>

extern const ngf_allocation_callbacks NGF_DEFAULT_ALLOC_CB;

static void ngft_bad_hashfn(uintptr_t key, uint32_t seed, void* out) {
  (void)key;
  ((uint32_t*)out)[0] = seed;
  ((uint32_t*)out)[1] = seed;
}

uint32_t allocs_called = 0u;
uint32_t deallocs_called = 0u;
 
void* ngft_counting_alloc(size_t obj_size, size_t nobjs) {
  allocs_called++;
  return malloc(obj_size * nobjs);
}

void ngft_counting_free(void* ptr, size_t obj_size, size_t nobjs) {
  deallocs_called++;
  (void)obj_size;
  (void)nobjs;
  free(ptr);
}

static void ngft_dict_populate_and_iterate(size_t nitems, size_t nslots, float max_load_factor) {
  typedef struct {
    uintptr_t key;
    bool      seen_during_iteration;
  } test_dict_val;
  uintptr_t*     keys = (void*)malloc(sizeof(uintptr_t) * nitems);
  test_dict_val* vals = (test_dict_val*)malloc(sizeof(test_dict_val) * nitems);
  NT_ASSERT(keys != NULL && vals != NULL);
  for (size_t i = 0u; i < nitems; ++i) {
    keys[i] = (uintptr_t)(rand() % RAND_MAX);
    keys[i] <<= 32;
    keys[i] |= (uintptr_t)(rand() % RAND_MAX);
    vals[i].key                   = keys[i];
    vals[i].seen_during_iteration = false;
  }

  ngfi_dict d = ngfi_dict_create(nslots, sizeof(test_dict_val*));
  NT_ASSERT(d);
  if (max_load_factor > 0.f) { ngfi_dict_set_max_load_factor(d, max_load_factor); }

  /* create */
  for (size_t i = 0u; i < nitems; ++i) {
    void* ptr  = &vals[i];
    void* data = ngfi_dict_get(&d, keys[i], &ptr);
    NT_ASSERT(data);
    test_dict_val* val = *(test_dict_val**)data;
    NT_ASSERT(val->key == keys[i]);
  }
  NT_ASSERT(ngfi_dict_count(d) == nitems);

  /* lookup */
  for (size_t i = 0u; i < nitems; ++i) {
    void* data = ngfi_dict_get(&d, keys[i], NULL);
    NT_ASSERT(data);
    test_dict_val* val = *(test_dict_val**)data;
    NT_ASSERT(val->key == keys[i]);
  }

  /* iterate */
  size_t nvals_seen = 0u;
  NGFI_DICT_FOREACH(d, it) {
    void* data = ngfi_dict_itval(d, it);
    NT_ASSERT(data);

    test_dict_val* val         = *(test_dict_val**)data;
    val->seen_during_iteration = true;
    ++nvals_seen;
  }
  NT_ASSERT(nvals_seen == nitems);
  bool seen_all = true;
  for (size_t i = 0u; seen_all && i < nitems; ++i) {
    seen_all = seen_all && vals[i].seen_during_iteration;
  }
  NT_ASSERT(seen_all);

  /* clear */
  ngfi_dict_clear(d);
  for (size_t i = 0u; i < nitems; ++i) {
    void* data = ngfi_dict_get(&d, keys[i], NULL);
    NT_ASSERT(data == NULL);
  }
  nvals_seen = 0u;
  NGFI_DICT_FOREACH(d, it) {
    ++nvals_seen;
  }
  NT_ASSERT(nvals_seen == 0u);

  free(keys);
  free(vals);
}

NT_TESTSUITE {
  ngfi_set_allocation_callbacks(NULL);

  /* frame token tests */

  NT_TESTCASE("frame token: encode-decode") {
    const uint16_t  test_ctx_id              = 65534u;
    const uint8_t   test_max_inflight_frames = 3u, test_frame_id = 255u;
    const uintptr_t test_token =
        ngfi_encode_frame_token(test_ctx_id, test_max_inflight_frames, test_frame_id);
    NT_ASSERT(test_ctx_id == ngfi_frame_ctx_id(test_token));
    NT_ASSERT(test_max_inflight_frames == ngfi_frame_max_inflight_frames(test_token));
    NT_ASSERT(test_frame_id == ngfi_frame_id(test_token));
  }

  /* native binding map tests */

  NT_TESTCASE("native binding map: magic") {
    const char  test_string[] = "nothing /* NGF_NATIVE_BINDING_MAP */ more of nothing";
    const char* result        = ngfi_find_serialized_native_binding_map(test_string);
    NT_ASSERT(result != NULL);
    NT_ASSERT(result - test_string == 33);

    const char  test_string2[] = "nothing /**\nNGF_NATIVE_BINDING_MAP\n**/\nmore of nothing";
    const char* result2        = ngfi_find_serialized_native_binding_map(test_string2);
    NT_ASSERT(result2 != NULL);
    NT_ASSERT(result2 - test_string2 == 34);
  }

  NT_TESTCASE("native binding map: missing magic") {
    const char  test_string[] = "/*NGF_NATIVE_";
    const char* result        = ngfi_find_serialized_native_binding_map(test_string);
    NT_ASSERT(result == NULL);

    const char  test_string2[] = "";
    const char* result2        = ngfi_find_serialized_native_binding_map(test_string2);
    NT_ASSERT(result2 == NULL);
  }

  NT_TESTCASE("native binding map: magic not in comment") {
    const char  test_string[] = "*/nothing NGF_NATIVE_BINDING_MAP */ more of nothing";
    const char* result        = ngfi_find_serialized_native_binding_map(test_string);
    NT_ASSERT(result == NULL);
  }

  NT_TESTCASE("native binding map: first occuring magic") {
    const char  test_string[] = "0123456/*NGF_NATIVE_BINDING_MAP*/012/*NGF_NATIVE_BINDING_MAP*/";
    const char* result1       = ngfi_find_serialized_native_binding_map(test_string);
    NT_ASSERT(result1 != NULL);
    NT_ASSERT(result1 - test_string == 31);
    const char* result2 = ngfi_find_serialized_native_binding_map(result1);
    NT_ASSERT(result2 != NULL);
    NT_ASSERT(result2 - test_string == sizeof(test_string) - 3);
  }

  NT_TESTCASE("native binding map: parsing") {
    const char               test_string[] = "( 0 1 \n) : 2\n(2 0) : 3\naaaa";
    ngfi_native_binding_map* map           = ngfi_parse_serialized_native_binding_map(test_string);
    NT_ASSERT(ngfi_native_binding_map_lookup(map, 0, 1) == 2u);
    NT_ASSERT(ngfi_native_binding_map_lookup(map, 2, 0) == 3u);
    NT_ASSERT(ngfi_native_binding_map_lookup(map, 2, 1) == ~0u);
    NT_ASSERT(ngfi_native_binding_map_lookup(map, 0, 0) == ~0u);
    NT_ASSERT(ngfi_native_binding_map_lookup(map, 3, 0) == ~0u);
    NT_ASSERT(ngfi_native_binding_map_lookup(map, 3, 2) == ~0u);
    ngfi_destroy_native_binding_map(map);
  }

  NT_TESTCASE("native binding map: ill-formed") {
    const char               test_string1[] = "(0 1 : 2\n(2 0) : 3\naaaa";
    ngfi_native_binding_map* map1 = ngfi_parse_serialized_native_binding_map(test_string1);
    NT_ASSERT(map1 == NULL);
  }

  NT_TESTCASE("native binding map: parsing stops at error") {
    const char               test_string2[] = "(0 1 ): 2\n2 0) : 3\naaaa";
    ngfi_native_binding_map* map2 = ngfi_parse_serialized_native_binding_map(test_string2);
    NT_ASSERT(map2 != NULL);
    NT_ASSERT(ngfi_native_binding_map_lookup(map2, 0, 1) == 2u);
    NT_ASSERT(ngfi_native_binding_map_lookup(map2, 2, 0) == ~0u);
    ngfi_destroy_native_binding_map(map2);
  }

  NT_TESTCASE("native binding map: parsing stops at (-1 -1 -1)") {
    const char               test_string2[] = "(0 1): 2\n(-1 -1):-1 (2 0) : 3\naaaa";
    ngfi_native_binding_map* map2 = ngfi_parse_serialized_native_binding_map(test_string2);
    NT_ASSERT(map2 != NULL);
    NT_ASSERT(ngfi_native_binding_map_lookup(map2, 0, 1) == 2u);
    NT_ASSERT(ngfi_native_binding_map_lookup(map2, 2, 0) == ~0u);
    ngfi_destroy_native_binding_map(map2);
  }

  /* stack allocator tests */

  NT_TESTCASE("stack alloc: exhaust-reset-exhaust cycle") {
    const uint32_t value   = 0xdeadbeef;
    const uint32_t nvalues = 10;
    ngfi_sa*       sa      = ngfi_sa_create(sizeof(value) * nvalues);
    NT_ASSERT(sa != NULL);
    for (uint32_t i = 0; i < nvalues + 1; ++i) {
      uint32_t* target = (uint32_t*)ngfi_sa_alloc(sa, sizeof(value));
      *target          = value;
      NT_ASSERT(*target == value);
    }
    ngfi_sa_reset(sa);
    for (uint32_t i = 0; i < nvalues + 1; ++i) {
      uint32_t* target = (uint32_t*)ngfi_sa_alloc(sa, sizeof(value));
      *target          = value;
      NT_ASSERT(*target == value);
    }
    ngfi_sa_destroy(sa);
  }

  NT_TESTCASE("stack alloc: allocate beyond available capacity") {
    const uint32_t value   = 0xdeadbeef;
    const uint32_t nvalues = 10;
    ngfi_sa*       sa      = ngfi_sa_create(sizeof(value) * nvalues);
    NT_ASSERT(sa != NULL);
    for (uint32_t i = 0; i < nvalues + 1; ++i) {
      uint32_t* target = (uint32_t*)ngfi_sa_alloc(sa, sizeof(value));
      *target          = value;
      NT_ASSERT(*target == value);
    }

    // another block should have been allocated
    NT_ASSERT(sa->active_block != NULL);

    // the next block of the base allocator should be the next free block
    // since only two total block have been allocated
    NT_ASSERT(sa->next_block == sa->active_block);

    ngfi_sa_reset(sa);

    ngfi_sa_alloc(sa, sizeof(value) * (nvalues - 1));

    size_t   alloc_size = sizeof(value) + 1;
    uint8_t* x          = ngfi_sa_alloc(sa, alloc_size);

    // another block should have been allocated
    NT_ASSERT(sa->active_block != NULL);
    NT_ASSERT(x != NULL);

    // the next block of the base allocator should be the next free block
    // since only two total block have been allocated
    NT_ASSERT(sa->next_block == sa->active_block);

    ngfi_sa_destroy(sa);
  }

  NT_TESTCASE("stack alloc: allocate beyond base capacity") {
    size_t   size = 32;
    ngfi_sa* sa   = ngfi_sa_create(size);

    uint8_t* x = ngfi_sa_alloc(sa, size + 1);

    // another block should have been allocated
    NT_ASSERT(sa->active_block != NULL);
    NT_ASSERT(x == sa->active_block->ptr - (size + 1));

    // the next block of the base allocator should be the next free block
    // since only two total block have been allocated
    NT_ASSERT(sa->next_block == sa->active_block);

    ngfi_sa_destroy(sa);
  }

  NT_TESTCASE("stack alloc: allocate more than 1 new block") {
    size_t   size = 32;
    ngfi_sa* sa   = ngfi_sa_create(size);

    uint8_t* x = ngfi_sa_alloc(sa, size + 1);

    // another block should have been allocated
    NT_ASSERT(sa->active_block != NULL);
    NT_ASSERT(x == sa->active_block->ptr - (size + 1));

    // the next block of the base allocator should be the next free block
    // since only two total block have been allocated
    NT_ASSERT(sa->next_block == sa->active_block);

    ngfi_sa* old_free_block = sa->active_block;

    size = sa->active_block->capacity + 1;
    x    = ngfi_sa_alloc(sa, size);

    // another block should have been allocated
    NT_ASSERT(sa->active_block != NULL);
    NT_ASSERT(sa->active_block != old_free_block);
    NT_ASSERT(old_free_block->next_block == sa->active_block);
    NT_ASSERT(x == sa->active_block->ptr - size);

    // the next block of the base allocator should be old_free_block
    NT_ASSERT(sa->next_block == old_free_block);

    ngfi_sa_destroy(sa);
  }

  /* block allocator tests */

  typedef struct test_data {
    uint8_t b1;
    float   f1;
    void *  p1, *p2;
  } test_data;

#define num_max_entries (1000u)

  NT_TESTCASE("block alloc: basic") {
    ngfi_block_allocator* allocator = ngfi_blkalloc_create(sizeof(test_data), num_max_entries);
    NT_ASSERT(allocator != NULL);
    test_data* data[num_max_entries] = {NULL};
    for (uint32_t i = 0u; i < num_max_entries; ++i) {
      data[i] = (test_data*)ngfi_blkalloc_alloc(allocator);
      NT_ASSERT(data[i] != NULL);
      data[i]->p2 = (void*)data[i];
    }
    // Max. number of entries reached, try adding a pool.
    test_data* null_blk = (test_data*)ngfi_blkalloc_alloc(allocator);
    NT_ASSERT(null_blk != NULL);
    for (uint32_t i = 0u; i < num_max_entries; ++i) {
      NT_ASSERT(data[i]->p2 == (void*)data[i]);
      ngfi_blkalloc_error err = ngfi_blkalloc_free(allocator, data[i]);
      NT_ASSERT(err == NGFI_BLK_NO_ERROR);
    }
    test_data* blk = (test_data*)ngfi_blkalloc_alloc(allocator);
    NT_ASSERT(blk != NULL);
#if !defined(NDEBUG)
    ngfi_block_allocator* alloc2 = ngfi_blkalloc_create(sizeof(test_data), num_max_entries);
    ngfi_blkalloc_error   err    = ngfi_blkalloc_free(alloc2, blk);
    NT_ASSERT(err == NGFI_BLK_WRONG_ALLOCATOR);
    err = ngfi_blkalloc_free(allocator, blk);
    NT_ASSERT(err == NGFI_BLK_NO_ERROR);
    err = ngfi_blkalloc_free(allocator, blk);
    NT_ASSERT(err == NGFI_BLK_DOUBLE_FREE);
    ngfi_blkalloc_destroy(alloc2);
#endif
    ngfi_blkalloc_destroy(allocator);
  }

#define frand() ((float)rand() / (float)RAND_MAX)

  NT_TESTCASE("block alloc: fuzz test") {
    const uint32_t ntests = 30000u;
    srand((unsigned)time(NULL));
    uint32_t              active_blocks         = 0u;
    test_data*            data[num_max_entries] = {NULL};
    ngfi_block_allocator* allocator = ngfi_blkalloc_create(sizeof(test_data), num_max_entries);
    for (uint32_t t = 0u; t < ntests; ++t) {
      bool allocate = frand() < 0.5f;
      if (allocate) {
        test_data* x = (test_data*)ngfi_blkalloc_alloc(allocator);
        if (active_blocks == num_max_entries) {
          NT_ASSERT(x != NULL);
          ngfi_blkalloc_free(allocator, x);
        } else {
          NT_ASSERT(x != NULL);
          uint32_t idx = 0;
          while (data[idx] != NULL && idx++ < num_max_entries)
            ;
          NT_ASSERT(idx < num_max_entries);
          data[idx] = x;
          x->p1     = (test_data*)x;
          active_blocks++;
        }
      } else if (active_blocks > 0u) {
        uint32_t idx = 0u;
        idx          = (uint32_t)((num_max_entries - 1u) * frand());
        if (data[idx] != NULL) {
          --active_blocks;
          NT_ASSERT(data[idx]->p1 == (test_data*)data[idx]);
          ngfi_blkalloc_error err = ngfi_blkalloc_free(allocator, data[idx]);
          NT_ASSERT(err == NGFI_BLK_NO_ERROR);
          data[idx] = NULL;
        }
      }
    }
  }

  NT_TESTCASE("block alloc: cleanup") {
    const uint32_t blocks_per_pool = 3u;
    ngf_allocation_callbacks counting_alloc_callbacks = {ngft_counting_alloc, ngft_counting_free};
    NGF_ALLOC_CB                                      = &counting_alloc_callbacks;
    allocs_called = deallocs_called = 0u;
    ngfi_block_allocator* alloc = ngfi_blkalloc_create(sizeof(test_data), blocks_per_pool);
    allocs_called -= 1u; // discount the allocator struct itself.
    NT_ASSERT(allocs_called == 1u);
    NT_ASSERT(alloc != NULL);
    test_data*            blocks[3 * 3] = { NULL };
    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); ++i) {
      blocks[i] = ngfi_blkalloc_alloc(alloc);
      NT_ASSERT(blocks[i] != NULL);
    }
    NT_ASSERT(allocs_called == 3u);
    NT_ASSERT(deallocs_called == 0u);
    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]) - 3u; ++i) {
      ngfi_blkalloc_free(alloc, blocks[i]);
    }
    NT_ASSERT(deallocs_called == 0u);
    for (size_t i = 0; i < 4u; ++i) { ngfi_blkalloc_cleanup(alloc); }
    NT_ASSERT(deallocs_called == 2u);

    test_data* td = ngfi_blkalloc_alloc(alloc);
    NT_ASSERT(allocs_called == 4u);
    ngfi_blkalloc_free(alloc, td);

    for (size_t i = sizeof(blocks) / sizeof(blocks[0]) - 3u; i < sizeof(blocks) / sizeof(blocks[0]);
         ++i) {
      ngfi_blkalloc_free(alloc, blocks[i]);
    }
    for (size_t i = 0; i < 4u; ++i) { ngfi_blkalloc_cleanup(alloc); }
    NT_ASSERT(deallocs_called == 3u);

    for (size_t i = 0; i < 4u; ++i) { ngfi_blkalloc_cleanup(alloc); }
    NT_ASSERT(deallocs_called == 3u);

    ngfi_blkalloc_destroy(alloc);
    deallocs_called -= 1u; // discount the allocator struct itself.
    NT_ASSERT(deallocs_called == 4u);
    NGF_ALLOC_CB                = &NGF_DEFAULT_ALLOC_CB;
  }

  /* dynamic array tests */

  typedef struct point {
    float x, y;
  } point;

  NT_TESTCASE("dynamic array: create") {
    NGFI_DARRAY_OF(point) pt_array;
    NGFI_DARRAY_RESET(pt_array, 100u);
    NT_ASSERT(pt_array.data != NULL);
    NT_ASSERT(pt_array.data == pt_array.endptr);
    NT_ASSERT(pt_array.capacity == 100u);
    NGFI_DARRAY_DESTROY(pt_array);
  }

  NT_TESTCASE("dynamic array: populate") {
    NGFI_DARRAY_OF(point) pt_array;
    point check_array[1100u];
    NGFI_DARRAY_RESET(pt_array, 100u);
    uint32_t ntests = sizeof(check_array) / sizeof(check_array[0]);
    for (uint32_t i = 0u; i < ntests; ++i) {
      point p = {frand(), frand()};
      NGFI_DARRAY_APPEND(pt_array, p);
      check_array[i] = p;
    }
    NT_ASSERT(NGFI_DARRAY_SIZE(pt_array) == ntests);
    for (uint32_t i = 0; i < ntests; ++i) {
      NT_ASSERT(check_array[i].x == NGFI_DARRAY_AT(pt_array, i).x);
      NT_ASSERT(check_array[i].y == NGFI_DARRAY_AT(pt_array, i).y);
    }

    NGFI_DARRAY_CLEAR(pt_array);
    NT_ASSERT(NGFI_DARRAY_SIZE(pt_array) == 0u);

    for (uint32_t i = 0u; i < ntests; ++i) {
      point p = {frand(), frand()};
      NGFI_DARRAY_APPEND(pt_array, p);
      check_array[i] = p;
    }
    NT_ASSERT(NGFI_DARRAY_SIZE(pt_array) == ntests);
    for (uint32_t i = 0; i < ntests; ++i) {
      NT_ASSERT(check_array[i].x == NGFI_DARRAY_AT(pt_array, i).x);
      NT_ASSERT(check_array[i].y == NGFI_DARRAY_AT(pt_array, i).y);
    }

    NGFI_DARRAY_DESTROY(pt_array);
  }

  NT_TESTCASE("dynamic array: foreach") {
    NGFI_DARRAY_OF(point) pt_array;
    point          check_array[10];
    const uint32_t array_size = sizeof(check_array) / sizeof(check_array[0]);
    NGFI_DARRAY_RESET(pt_array, array_size);
    for (size_t i = 0; i < array_size; ++i) {
      point p = {frand(), frand()};
      NGFI_DARRAY_APPEND(pt_array, p);
      check_array[i] = p;
    }
    int prev_i = -1;
    NGFI_DARRAY_FOREACH(pt_array, i) {
      NT_ASSERT(i == prev_i + 1);
      NT_ASSERT(NGFI_DARRAY_AT(pt_array, i).x == check_array[i].x);
      NT_ASSERT(NGFI_DARRAY_AT(pt_array, i).y == check_array[i].y);
      prev_i = (int)i;
    }
    NT_ASSERT(prev_i == (int)array_size - 1);
  }

  /* list tests */

  typedef struct test_struct {
    ngfi_list_node test_list;
    int            tag;
  } test_struct;

  NT_TESTCASE("list: container_of returns the container struct") {
    test_struct s;
    ngfi_list_init(&s.test_list);
    s.tag                            = 0xbadbeef;
    const test_struct* container_ptr = NGFI_LIST_CONTAINER_OF(&s.test_list, test_struct, test_list);
    NT_ASSERT(container_ptr->tag == s.tag);
    NT_ASSERT(container_ptr == &s);
  }

  NT_TESTCASE("list: single-element list iteration") {
    test_struct s;
    ngfi_list_init(&s.test_list);
    s.tag         = 0xbadbeef;
    int num_iters = 0u;
    NGFI_LIST_FOR_EACH(&s.test_list, n) {
      const test_struct* s_ptr = NGFI_LIST_CONTAINER_OF(n, test_struct, test_list);
      NT_ASSERT(s_ptr->tag == s.tag);
      NT_ASSERT(num_iters++ < 1);
    }
  }

  NT_TESTCASE("list: foreach") {
    test_struct elements[80];
    const int   num_elements = sizeof(elements) / sizeof(elements[0]);
    for (int i = 0; i < num_elements; ++i) {
      ngfi_list_init(&elements[i].test_list);
      elements[i].tag = i;
    }

    for (int i = 1; i < num_elements; ++i) {
      ngfi_list_append(&elements[i].test_list, &elements[0].test_list);
    }

    int num_iters = 0;
    NGFI_LIST_FOR_EACH(&elements[0].test_list, n) {
      const test_struct* s_ptr = NGFI_LIST_CONTAINER_OF(n, test_struct, test_list);
      NT_ASSERT(s_ptr->tag == num_iters);
      ++num_iters;
    }
    NT_ASSERT(num_iters == num_elements);
  }

  NT_TESTCASE("list: remove and append") {
    test_struct elements[3];
    const int   num_elements = sizeof(elements) / sizeof(elements[0]);
    for (int i = 0; i < num_elements; ++i) {
      ngfi_list_init(&elements[i].test_list);
      elements[i].tag = i;
    }

    for (int i = 1; i < num_elements; ++i) {
      ngfi_list_append(&elements[i].test_list, &elements[0].test_list);
    }

    const int expected_tags_before_remove[] = {0, 1, 2};
    int       i                             = 0;
    NGFI_LIST_FOR_EACH(&elements[0].test_list, n) {
      const test_struct* s_ptr = NGFI_LIST_CONTAINER_OF(n, test_struct, test_list);
      NT_ASSERT(s_ptr->tag == expected_tags_before_remove[i++]);
    }
    NT_ASSERT(i == 3);

    ngfi_list_remove(&elements[1].test_list);

    const int expected_tags_after_remove[] = {0, 2};
    i                                      = 0;
    NGFI_LIST_FOR_EACH(&elements[0].test_list, n) {
      const test_struct* s_ptr = NGFI_LIST_CONTAINER_OF(n, test_struct, test_list);
      NT_ASSERT(s_ptr->tag == expected_tags_after_remove[i++]);
    }
    NT_ASSERT(i == 2);

    ngfi_list_append(&elements[1].test_list, &elements[0].test_list);

    const int expected_tags_after_append[] = {0, 2, 1};
    i                                      = 0;
    NGFI_LIST_FOR_EACH(&elements[0].test_list, n) {
      const test_struct* s_ptr = NGFI_LIST_CONTAINER_OF(n, test_struct, test_list);
      NT_ASSERT(s_ptr->tag == expected_tags_after_append[i++]);
    }
    NT_ASSERT(i == 3);
  }

  NT_TESTCASE("list: empty foreach") {
    size_t          niters = 0u;
    ngfi_list_node* n      = NULL;
    NGFI_LIST_FOR_EACH(n, a) {
      ++niters;
    }
    NGFI_LIST_FOR_EACH_CONST(n, b) {
      ++niters;
    }
    NT_ASSERT(niters == 0u);
  }

  /* cmd buffer state transitions tests */
  NT_TESTCASE("cmdbuf transitions: valid") {
    ngfi_cmd_buffer_state s = NGFI_CMD_BUFFER_NEW;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, false, NGFI_CMD_BUFFER_READY) == NGF_ERROR_OK);
    s = NGFI_CMD_BUFFER_SUBMITTED;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, false, NGFI_CMD_BUFFER_READY) == NGF_ERROR_OK);

    s = NGFI_CMD_BUFFER_READY;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, false, NGFI_CMD_BUFFER_RECORDING) == NGF_ERROR_OK);
    s = NGFI_CMD_BUFFER_PENDING;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, false, NGFI_CMD_BUFFER_RECORDING) != NGF_ERROR_OK);

    s = NGFI_CMD_BUFFER_RECORDING;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, false, NGFI_CMD_BUFFER_PENDING) != NGF_ERROR_OK);

    s = NGFI_CMD_BUFFER_PENDING;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, false, NGFI_CMD_BUFFER_SUBMITTED) == NGF_ERROR_OK);
  }

  NT_TESTCASE("cmdbuf transitions: invalid") {
    ngfi_cmd_buffer_state s = NGFI_CMD_BUFFER_NEW;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, false, NGFI_CMD_BUFFER_NEW) != NGF_ERROR_OK);
    s = NGFI_CMD_BUFFER_PENDING;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, false, NGFI_CMD_BUFFER_READY) != NGF_ERROR_OK);

    s = NGFI_CMD_BUFFER_NEW;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, false, NGFI_CMD_BUFFER_RECORDING) != NGF_ERROR_OK);
    s = NGFI_CMD_BUFFER_RECORDING;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, false, NGFI_CMD_BUFFER_RECORDING) != NGF_ERROR_OK);
    s = NGFI_CMD_BUFFER_SUBMITTED;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, false, NGFI_CMD_BUFFER_RECORDING) != NGF_ERROR_OK);

    s = NGFI_CMD_BUFFER_NEW;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, false, NGFI_CMD_BUFFER_PENDING) != NGF_ERROR_OK);
    s = NGFI_CMD_BUFFER_PENDING;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, false, NGFI_CMD_BUFFER_PENDING) != NGF_ERROR_OK);
    s = NGFI_CMD_BUFFER_READY;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, false, NGFI_CMD_BUFFER_PENDING) != NGF_ERROR_OK);
    s = NGFI_CMD_BUFFER_RECORDING;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, true, NGFI_CMD_BUFFER_PENDING) != NGF_ERROR_OK);

    s = NGFI_CMD_BUFFER_READY;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, false, NGFI_CMD_BUFFER_SUBMITTED) != NGF_ERROR_OK);
    s = NGFI_CMD_BUFFER_RECORDING;
    NT_ASSERT(ngfi_transition_cmd_buf(&s, false, NGFI_CMD_BUFFER_SUBMITTED) != NGF_ERROR_OK);
  }

  /* hashtable tests */
  NT_TESTCASE("dict: create and destroy") {
    ngfi_dict d = ngfi_dict_create(10u, 4u);
    NT_ASSERT(d);
    NT_ASSERT(ngfi_dict_count(d) == 0u);
    NT_ASSERT(ngfi_dict_nslots(d) == 10u);
    ngfi_dict_destroy(d);
  }

  NT_TESTCASE("dict: lookup-clear-lookup") {
    uintptr_t key = 0xdeadbeef, value = 0xbadf00d;
    ngfi_dict d = ngfi_dict_create(10u, sizeof(value));
    NT_ASSERT(d);
    void* val0 = ngfi_dict_get(&d, key, &value);
    NT_ASSERT(val0);
    NT_ASSERT(memcmp(val0, &value, sizeof(value)) == 0);
    NT_ASSERT(ngfi_dict_count(d) == 1u);
    void* val1 = ngfi_dict_get(&d, key, NULL);
    NT_ASSERT(memcmp(val1, &value, sizeof(value)) == 0);
    ngfi_dict_clear(d);
    NT_ASSERT(ngfi_dict_count(d) == 0u);
    NT_ASSERT(ngfi_dict_get(&d, key, NULL) == NULL);
    NT_ASSERT(ngfi_dict_count(d) == 0u);
    ngfi_dict_destroy(d);
  }

  NT_TESTCASE("dict: iteration") {
    ngft_dict_populate_and_iterate(50u, 500u, 0.f);
  }

  NT_TESTCASE("dict: fill all slots") {
    ngft_dict_populate_and_iterate(500u, 500u, 2.0f);
  }

  NT_TESTCASE("dict: collisions") {
    ngfi_dict_hashfn = ngft_bad_hashfn;
    ngft_dict_populate_and_iterate(50u, 500u, 0.f);
    ngfi_dict_hashfn = NULL;
  }

  NT_TESTCASE("dict: rehash") {
    ngft_dict_populate_and_iterate(500u, 100u, 0.5f);
  }

  /* chunk list tests */

  NT_TESTCASE("chunklist: multiple chunks and clear") {
    const uint32_t nelems_per_chunk = 10u;
    const uint32_t nchunks          = 10u;
    const uint32_t nelems_total     = nelems_per_chunk * nchunks;
    bool         elem_seen[100] = {false};
    ngfi_block_allocator* blkalloc = ngfi_blkalloc_create((uint32_t)(sizeof(ngfi_chnk_hdr) + sizeof(size_t) * nelems_per_chunk), 1u);
    ngfi_chnklist  cl = {blkalloc, NULL};
    for (size_t i = 0u; i < nelems_total; ++i) { 
      void* e = ngfi_chnklist_append(&cl, &i, sizeof(i));
      NT_ASSERT(e != NULL);
      NT_ASSERT(*((size_t*)e) == i);
    }

    size_t niters = 0u;
    NGFI_CHNKLIST_FOR_EACH(cl, size_t, i) {
        elem_seen[*i] = true;
        ++niters;
    }
    NT_ASSERT(niters == 100);
    bool all_seen = true;
    for (size_t i = 0u; all_seen && i < 100u; ++i) all_seen = all_seen && elem_seen[i];
    NT_ASSERT(all_seen);
    ngfi_chnklist_clear(&cl);
    ngfi_blkalloc_destroy(blkalloc);
  }

}
