#include "emulated_descriptor_set.h"
#include "nicegraf_internal.h"
#include <assert.h>
#include <string.h>

ngf_error ngf_create_descriptor_set_layout(const ngf_descriptor_set_layout_info *info,
                                           ngf_descriptor_set_layout **result) {
  assert(info);
  assert(result);
  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_descriptor_set_layout);
  ngf_descriptor_set_layout *layout = *result;
  if (layout == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_descriptor_set_layout_cleanup;
  }

  layout->info.ndescriptors = info->ndescriptors;
  ngf_descriptor_info *descriptors = NGF_ALLOCN(ngf_descriptor_info,
                                                info->ndescriptors);
  layout->info.descriptors = descriptors;
  if (layout->info.descriptors == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_descriptor_set_layout_cleanup;
  }
  memcpy(descriptors,
         info->descriptors,
         sizeof(ngf_descriptor_info) * info->ndescriptors);

ngf_create_descriptor_set_layout_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_descriptor_set_layout(layout);
  }
  return err;
}

void ngf_destroy_descriptor_set_layout(ngf_descriptor_set_layout *layout) {
  if (layout != NULL) {
    if (layout->info.ndescriptors > 0 &&
        layout->info.descriptors) {
        NGF_FREEN(layout->info.descriptors, layout->info.ndescriptors);
    }
    NGF_FREE(layout);
  }
}

ngf_error ngf_create_descriptor_set(const ngf_descriptor_set_layout *layout,
                                    ngf_descriptor_set **result) {
  assert(layout);
  assert(result);

  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_descriptor_set);
  ngf_descriptor_set *set = *result;
  if (set == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_descriptor_set_cleanup;
  }

  set->nslots = layout->info.ndescriptors;
  set->bind_ops = NGF_ALLOCN(ngf_descriptor_write, layout->info.ndescriptors);
  if (set->bind_ops == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_descriptor_set_cleanup;
  }

  set->descriptors = NGF_ALLOCN(ngf_descriptor_info, set->nslots);
  if (set->descriptors == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_descriptor_set_cleanup;
  }
  memcpy(set->descriptors,
         layout->info.descriptors,
         sizeof(ngf_descriptor_info) * set->nslots);

  for (size_t s = 0; s < set->nslots; ++s) {
    set->bind_ops[s].type = layout->info.descriptors[s].type;
  }

ngf_create_descriptor_set_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_descriptor_set(set);
  }

  return err;
}

void ngf_destroy_descriptor_set(ngf_descriptor_set *set) {
  if (set != NULL) {
    if (set->nslots > 0 && set->bind_ops) {
      NGF_FREEN(set->bind_ops, set->nslots);
    }
    if (set->nslots > 0 && set->descriptors) {
      NGF_FREEN(set->descriptors, set->nslots);
    }
    NGF_FREE(set);
  }
}

ngf_error ngf_apply_descriptor_writes(const ngf_descriptor_write *writes,
                                      const uint32_t nwrites,
                                      ngf_descriptor_set *set) {
  for (size_t w = 0; w < nwrites; ++w) {
    const ngf_descriptor_write *write = &(writes[w]);
    bool found_binding = false;
    for (uint32_t s = 0u; s < set->nslots; ++s) {
      if (set->bind_ops[s].type == write->type &&
          set->descriptors[s].id == write->binding) {
        set->bind_ops[s] = *write;
        found_binding = true;
        break;
      }
    }
    if (!found_binding) return NGF_ERROR_INVALID_BINDING;
  }
  return NGF_ERROR_OK;
}
