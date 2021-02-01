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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "nicegraf_internal.h"

#include "dynamic_array.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

ngf_diagnostic_info ngfi_diag_info = {
    .verbosity = NGF_DIAGNOSTICS_VERBOSITY_DEFAULT,
    .userdata  = NULL,
    .callback  = NULL};

// Default allocation callbacks.
void* ngf_default_alloc(size_t obj_size, size_t nobjs) {
  return malloc(obj_size * nobjs);
}

void ngf_default_free(void* ptr, size_t s, size_t n) {
  NGFI_IGNORE_VAR(s);
  NGFI_IGNORE_VAR(n);
  free(ptr);
}

const ngf_allocation_callbacks NGF_DEFAULT_ALLOC_CB = {ngf_default_alloc, ngf_default_free};

const ngf_allocation_callbacks* NGF_ALLOC_CB = &NGF_DEFAULT_ALLOC_CB;

void ngf_set_allocation_callbacks(const ngf_allocation_callbacks* callbacks) {
  if (callbacks == NULL) {
    NGF_ALLOC_CB = &NGF_DEFAULT_ALLOC_CB;
  } else {
    NGF_ALLOC_CB = callbacks;
  }
}

const ngfi_native_binding*
ngfi_binding_map_lookup(const ngfi_native_binding_map binding_map, uint32_t set, uint32_t binding) {
  const ngfi_native_binding* set_map = binding_map[set];
  uint32_t                   b_idx   = 0u;
  while (set_map[b_idx].ngf_binding_id != binding &&
         set_map[b_idx].ngf_binding_id != (uint32_t)(-1))
    ++b_idx;
  if (set_map[b_idx].ngf_binding_id == (uint32_t)(-1)) { return NULL; }
  return &set_map[b_idx];
}

ngf_error ngfi_create_native_binding_map(
    const ngf_pipeline_layout_info* layout,
    const ngf_plmd_cis_map*         images_to_cis,
    const ngf_plmd_cis_map*         samplers_to_cis,
    ngfi_native_binding_map*        result) {
  ngf_error               err          = NGF_ERROR_OK;
  uint32_t                nmap_entries = layout->ndescriptor_set_layouts + 1;
  ngfi_native_binding_map map          = NGFI_ALLOCN(ngfi_native_binding*, nmap_entries);
  *result                              = map;
  if (map == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto _ngf_create_native_binding_map_cleanup;
  }
  memset(map, 0, sizeof(ngfi_native_binding*) * (nmap_entries));
  uint32_t total_c[NGF_DESCRIPTOR_TYPE_COUNT] = {0u};
  for (uint32_t set = 0u; set < layout->ndescriptor_set_layouts; ++set) {
    const ngf_descriptor_set_layout_info* set_layout = &layout->descriptor_set_layouts[set];
    map[set] = NGFI_ALLOCN(ngfi_native_binding, set_layout->ndescriptors + 1u);
    if (map[set] == NULL) {
      err = NGF_ERROR_OUT_OF_MEM;
      goto _ngf_create_native_binding_map_cleanup;
    }
    map[set][set_layout->ndescriptors].ngf_binding_id = (uint32_t)(-1);
    for (uint32_t b = 0u; b < set_layout->ndescriptors; ++b) {
      const ngf_descriptor_info* desc_info = &set_layout->descriptors[b];
      const ngf_descriptor_type  desc_type = desc_info->type;
      ngfi_native_binding*       mapping   = &map[set][b];
      mapping->ngf_binding_id              = desc_info->id;
      mapping->native_binding_id           = total_c[desc_type]++;
      if ((desc_info->type == NGF_DESCRIPTOR_SAMPLER && samplers_to_cis) ||
          (desc_info->type == NGF_DESCRIPTOR_TEXTURE && images_to_cis)) {
        const ngf_plmd_cis_map* cis_map =
            desc_info->type == NGF_DESCRIPTOR_SAMPLER ? samplers_to_cis : images_to_cis;
        const ngf_plmd_cis_map_entry* combined_list = NULL;
        for (uint32_t i = 0u; i < cis_map->nentries; ++i) {
          if (set == cis_map->entries[i]->separate_set_id &&
              desc_info->id == cis_map->entries[i]->separate_binding_id) {
            combined_list = cis_map->entries[i];
            break;
          }
        }
        if (combined_list) {
          mapping->cis_bindings = NGFI_ALLOCN(uint32_t, combined_list->ncombined_ids);
          if (mapping->cis_bindings == NULL) {
            err = NGF_ERROR_OUT_OF_MEM;
            goto _ngf_create_native_binding_map_cleanup;
          }
          memcpy(
              mapping->cis_bindings,
              combined_list->combined_ids,
              sizeof(uint32_t) * combined_list->ncombined_ids);
          mapping->ncis_bindings = combined_list->ncombined_ids;
        } else {
          mapping->cis_bindings  = NULL;
          mapping->ncis_bindings = 0u;
        }
      } else {
        mapping->cis_bindings  = NULL;
        mapping->ncis_bindings = 0u;
      }
    }
  }

_ngf_create_native_binding_map_cleanup:
  if (err != NGF_ERROR_OK) { ngfi_destroy_binding_map(map); }
  return err;
}

void ngfi_destroy_binding_map(ngfi_native_binding_map map) {
  if (map != NULL) {
    for (uint32_t i = 0; map[i] != NULL; ++i) {
      ngfi_native_binding* set = map[i];
      if (set->ngf_binding_id != (uint32_t)-1 && set->cis_bindings) {
        NGFI_FREEN(set->cis_bindings, set->ncis_bindings);
      }
      NGFI_FREE(set);
    }
    NGFI_FREE(map);
  }
}

static struct {
  ngf_device_capabilities caps;
  bool                    initialized;
  pthread_mutex_t         mut;
} ngfi_device_caps;

void ngfi_device_caps_create(void) {
  pthread_mutex_init(&ngfi_device_caps.mut, NULL);
}

const ngf_device_capabilities* ngfi_device_caps_read(void) {
  const ngf_device_capabilities* result = NULL;
  pthread_mutex_lock(&ngfi_device_caps.mut);
  if (ngfi_device_caps.initialized) { result = &ngfi_device_caps.caps; }
  pthread_mutex_unlock(&ngfi_device_caps.mut);
  return result;
}

ngf_device_capabilities* ngfi_device_caps_lock(void) {
  pthread_mutex_lock(&ngfi_device_caps.mut);
  return ngfi_device_caps.initialized ? NULL : &ngfi_device_caps.caps;
}

void ngfi_device_caps_unlock(ngf_device_capabilities* ptr) {
  if (ptr == &ngfi_device_caps.caps) {
    ngfi_device_caps.initialized = true;
    pthread_mutex_unlock(&ngfi_device_caps.mut);
  }
}

extern ngf_diagnostic_info ngfi_diag_info;

ngf_error ngfi_transition_cmd_buf(
    ngfi_cmd_buffer_state* cur_state,
    bool                   has_active_renderpass,
    ngfi_cmd_buffer_state  new_state) {
  switch (new_state) {
  case NGFI_CMD_BUFFER_NEW:
    NGFI_DIAG_ERROR("command buffer cannot go back to a `new` state");
    return NGF_ERROR_INVALID_OPERATION;
  case NGFI_CMD_BUFFER_READY:
    if (*cur_state != NGFI_CMD_BUFFER_SUBMITTED && *cur_state != NGFI_CMD_BUFFER_READY &&
        *cur_state != NGFI_CMD_BUFFER_NEW) {
      NGFI_DIAG_ERROR("command buffer not in a startable state.");
      return NGF_ERROR_INVALID_OPERATION;
    }
    break;
  case NGFI_CMD_BUFFER_RECORDING:
    if (!NGFI_CMD_BUF_RECORDABLE(*cur_state)) {
      NGFI_DIAG_ERROR("command buffer not in a recordable state.");
      return NGF_ERROR_INVALID_OPERATION;
    }
    break;
  case NGFI_CMD_BUFFER_AWAITING_SUBMIT:
    if (*cur_state != NGFI_CMD_BUFFER_RECORDING) {
      NGFI_DIAG_ERROR("command buffer is not actively recording.");
      return NGF_ERROR_INVALID_OPERATION;
    }
    if (has_active_renderpass) {
      NGFI_DIAG_ERROR("cannot finish render encoder with an unterminated render pass.");
      return NGF_ERROR_INVALID_OPERATION;
    }
    break;
  case NGFI_CMD_BUFFER_SUBMITTED:
    if (*cur_state != NGFI_CMD_BUFFER_AWAITING_SUBMIT) {
      NGFI_DIAG_ERROR("command buffer not in a submittable state");
      return NGF_ERROR_INVALID_OPERATION;
    }
  }
  *cur_state = new_state;
  return NGF_ERROR_OK;
}
