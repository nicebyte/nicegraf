#include "handle_pool.h"

#include "dynamic_array.h"
#include "nicegraf_internal.h"

typedef struct ngfi_handle_pool_t {
  NGFI_DARRAY_OF(uint64_t) handles;
  pthread_mutex_t         lock;
  size_t                  size;
  ngfi_handle_pool_info   info;
} ngfi_handle_pool_t;

ngfi_handle_pool ngfi_create_handle_pool(const ngfi_handle_pool_info* info) {
  bool             err    = false;
  ngfi_handle_pool pool = NGFI_ALLOC(ngfi_handle_pool_t);

  if (pool == NULL) {
    err = true;
    goto ngfi_handle_pool_cleanup;
  }

  pthread_mutex_init(&pool->lock, 0);

  NGFI_DARRAY_RESET(pool->handles, info->initial_size);
  pool->size = info->initial_size;
  pool->info = *info;
  
  for (size_t i = 0; i < pool->info.initial_size; ++i) {
    const uint64_t handle = pool->info.allocator(pool->info.allocator_userdata);
    if (handle == 0u) {
      goto ngfi_handle_pool_cleanup;
    }
    NGFI_DARRAY_APPEND(pool->handles, handle);
  }

ngfi_handle_pool_cleanup:
  if (err) {
    ngfi_destroy_handle_pool(pool);
    return NULL;
  } else {
    return pool;
  }
}

void ngfi_destroy_handle_pool(ngfi_handle_pool pool) {
  if (pool != NULL) {
    const size_t pool_size = NGFI_DARRAY_SIZE(pool->handles);
    for(size_t i = 0; i < pool_size && pool->info.deallocator; ++i) {
      pool->info.deallocator(NGFI_DARRAY_AT(pool->handles, i), pool->info.deallocator_userdata);
    }
    NGFI_DARRAY_DESTROY(pool->handles);
    pthread_mutex_destroy(&pool->lock);
    NGFI_FREE(pool);
  }
}

uint64_t ngfi_handle_pool_alloc(ngfi_handle_pool pool) {
  assert(pool);
  uint64_t result = 0u;
  pthread_mutex_lock(&pool->lock);
  const size_t navailable_handles = NGFI_DARRAY_SIZE(pool->handles);
  if (navailable_handles > 0) {
    result = *NGFI_DARRAY_BACKPTR(pool->handles);
    NGFI_DARRAY_POP(pool->handles);
  } else {
    result = pool->info.allocator(pool->info.allocator_userdata);
    if (result != 0u) {
      ++pool->size;
    }
  }
  pthread_mutex_unlock(&pool->lock);
  return result;
}

void ngfi_handle_pool_free(ngfi_handle_pool pool, uint64_t handle) {
  assert(pool);
  pthread_mutex_lock(&pool->lock);
  NGFI_DARRAY_APPEND(pool->handles, handle);
  pthread_mutex_unlock(&pool->lock);
}

