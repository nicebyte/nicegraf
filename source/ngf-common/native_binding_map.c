#include "native_binding_map.h"
#include "nicegraf_internal.h"
#include <string.h>

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

