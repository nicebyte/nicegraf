#include "stack_alloc.h"

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

_ngf_sa* _ngf_sa_create(size_t capacity) {
  _ngf_sa* result = malloc(capacity + sizeof(_ngf_sa));
  if (result) {
    result->capacity = capacity;
    result->ptr      = result->data;
  }
  return result;
}

void* _ngf_sa_alloc(_ngf_sa *allocator, size_t nbytes) {
  assert(allocator);
  void           *result             = NULL;
  const ptrdiff_t consumed_capacity  = allocator->ptr - allocator->data;
  const ptrdiff_t available_capacity = allocator->capacity - consumed_capacity;
  if (available_capacity >= nbytes) {
    result = allocator->ptr;
    allocator->ptr += nbytes;
  } 
  return result;
}

void _ngf_sa_reset(_ngf_sa *allocator) {
  assert(allocator);
  allocator->ptr = allocator->data;
}

void _ngf_sa_destroy(_ngf_sa *allocator) {
  assert(allocator);
  free(allocator);
}

