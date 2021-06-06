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

#pragma once

#include <stddef.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A circular, intrusive, doubly-linked list.
 * This data type is not thread-safe.
 */
typedef struct ngfi_list_node {
  struct ngfi_list_node* next;
  struct ngfi_list_node* prev;
} ngfi_list_node;

/**
 * Appends a new node at the end of the list represented by `head_node`.
 */
static inline void ngfi_list_append(ngfi_list_node* new_node, ngfi_list_node* head_node) {
  assert(new_node->next == new_node->prev);
  assert(new_node->next == new_node);
  ngfi_list_node* old_tail = head_node->prev;
  old_tail->next = new_node;
  new_node->next = head_node;
  new_node->prev = old_tail;
  head_node->prev = new_node;
}

/**
 * Removes the given node from the list it's currently in.
 */
static inline void ngfi_list_remove(ngfi_list_node* node) {
  assert(node->prev->next == node);
  assert(node->next->prev == node);
  node->prev->next = node->next;
  node->next->prev = node->prev;
  node->prev = node;
  node->next = node;
}

/**
 * Initializes an isolated node that isn't a member of a list.
 */
static inline void ngfi_list_init(ngfi_list_node* node) {
  node->next = node;
  node->prev = node;
}

/**
 * Obtains ptr to the structure containing the given list node field.
 */
#define NGFI_LIST_CONTAINER_OF(ptr, type, node_name) (type*)((char*)((ptr)-offsetof(type, node_name)))

/**
 * Iterates over all the elements in the list in order, starting at the given one.
 */
#define NGFI_LIST_FOR_EACH(list, node_name) for (ngfi_list_node* node_name = (list), *node_name##_prev = NULL; \
                                                 (!node_name##_prev || node_name##_prev->next != (list)); \
                                                 node_name##_prev = node_name, node_name = node_name->next)

#ifdef __cplusplus
}
#endif

