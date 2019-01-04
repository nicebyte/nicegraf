#include "metadata_parser.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
  #pragma comment(lib, "ws2_32.lib")
  #include <winsock2.h>
#else
  #include <arpa/inet.h>
#endif

static const ngf_plmd_alloc_callbacks stdlib_alloc = {
  .alloc = malloc,
  .free = free
};

struct plmd {
  uint8_t *raw_data;
  const ngf_plmd_header *header;
  ngf_plmd_layout layout;
  ngf_plmd_cis_map images_to_cis_map;
  ngf_plmd_cis_map samplers_to_cis_map;
  ngf_plmd_user user;
};

static ngf_plmd_error _create_cis_map(uint8_t *ptr,
                                  const ngf_plmd_alloc_callbacks *cb,
                                  ngf_plmd_cis_map *map) {
  assert(ptr);
  map->nentries = *(uint32_t*)ptr;
  map->entries = cb->alloc(map->nentries * sizeof(ngf_plmd_cis_map_entry*));
  if (map->entries == NULL) {
    return NGF_PLMD_ERROR_OUTOFMEM;
  }

  size_t offset = 4u;
  for (uint32_t e = 0u; e < map->nentries; ++e) {
    map->entries[e] = (ngf_plmd_cis_map_entry*)(ptr + offset);
    offset += 3 * 4u + map->entries[e]->ncombined_ids * sizeof(uint32_t);
  }

  return NGF_PLMD_ERROR_OK;
}

ngf_plmd_error ngf_plmd_load(const void *buf, size_t buf_size,
                     const ngf_plmd_alloc_callbacks *alloc_cb,
                     plmd **result) {
  static const uint32_t START_OF_RAW_BYTE_BLOCK = 0xffffffff;
  static const uint32_t MAGIC_NUMBER = 0xdeadbeef;
  ngf_plmd_error err = NGF_PLMD_ERROR_OK;
  plmd *meta = NULL;
  assert(buf);
  assert(result);

  // Use stdlib malloc/free by default.
  if (alloc_cb == NULL) {
    alloc_cb = &stdlib_alloc;
  }
  
  // Any well-formed pipeline metadata file must contain a multiple of 4 bytes.
  if ((buf_size & 0b11) != 0) {
    err = NGF_PLMD_ERROR_WEIRD_BUFFER_SIZE;
    goto ngf_plmd_load_cleanup;
  }
  const uint32_t nfields = ((uint32_t)buf_size) >> 2u; // number of 4 byte
                                                       // blocks in the buffer.

  // Allocate space for the result.
  meta = alloc_cb->alloc(sizeof(plmd));
  if (result == NULL) {
    err = NGF_PLMD_ERROR_OUTOFMEM;
    goto ngf_plmd_load_cleanup;
  }
  memset(meta, 0u, sizeof(plmd));
  *result = meta;

  // Create a copy of the metadata buffer.
  meta->raw_data = alloc_cb->alloc(buf_size);
  if (meta->raw_data == NULL) {
    err = NGF_PLMD_ERROR_OUTOFMEM;
    goto ngf_plmd_load_cleanup;
  }
  memcpy(meta->raw_data, buf, buf_size);

  // Convert each field from network to host byte order, but skip
  // over raw byte blocks.
  uint32_t *fields = (uint32_t*)meta->raw_data;
  for (uint32_t field_idx = 0u; field_idx < nfields; ++field_idx) {
    const uint32_t field_value = fields[field_idx];
    if (field_value == START_OF_RAW_BYTE_BLOCK) {
      if (field_idx >= nfields - 1u) {
        err = NGF_PLMD_ERROR_BUFFER_TOO_SMALL;
        goto ngf_plmd_load_cleanup;
      }
      field_idx += 1u; // skip over the raw byte block start mark.
      // Convert the length of raw byte block from network to host byte order,
      // and write it back to the buffer.
      const uint32_t raw_blk_size = ntohl(fields[field_idx]);
      fields[field_idx] = raw_blk_size;
      field_idx += raw_blk_size; // skip over the raw byte block contents.
    } else {
      fields[field_idx] = ntohl(field_value);
    }
  }

  // Process header.
  meta->header = (const ngf_plmd_header*)meta->raw_data;
  const ngf_plmd_header *header = meta->header;
  if (header->magic_number != MAGIC_NUMBER) {
    err = NGF_PLMD_ERROR_MAGIC_NUMBER_MISMATCH;
    goto ngf_plmd_load_cleanup;
  }

  // Sanity-check offsets in the header.
  if (header->pipeline_layout_offset >= buf_size ||
      header->image_to_cis_map_offset >= buf_size ||
      header->sampler_to_cis_map_offset >= buf_size ||
      header->user_metadata_offset >= buf_size) {
    err = NGF_PLMD_ERROR_BUFFER_TOO_SMALL;
    goto ngf_plmd_load_cleanup;
  }

  // Process the pipeline layout record.
  const uint8_t *pipeline_layout_ptr =
      &meta->raw_data[header->pipeline_layout_offset];
  const uint32_t nsets = *(const uint32_t*)pipeline_layout_ptr;
  meta->layout.ndescriptor_sets = nsets;
  meta->layout.set_layouts = alloc_cb->alloc(sizeof(void*) * nsets);
  if (meta->layout.set_layouts == NULL) {
    err = NGF_PLMD_ERROR_OUTOFMEM;
    goto ngf_plmd_load_cleanup;
  }
  const uint8_t *set_ptr = pipeline_layout_ptr + sizeof(uint32_t);
  for (uint32_t s = 0u; s < nsets; ++s) {
    meta->layout.set_layouts[s] = (const ngf_plmd_descriptor_set_layout*)set_ptr;
    const uint32_t set_data_size =
        meta->layout.set_layouts[s]->ndescriptors * sizeof(ngf_plmd_descriptor) +
        sizeof(uint32_t);
    set_ptr += set_data_size;
  }

  // Process combined image/sampler maps.
  _create_cis_map((uint8_t*)meta->raw_data + header->image_to_cis_map_offset,
                   alloc_cb,
                   &meta->images_to_cis_map);
  _create_cis_map((uint8_t*)meta->raw_data + header->sampler_to_cis_map_offset,
                   alloc_cb,
                   &meta->samplers_to_cis_map);

  // Process user metadata.
  meta->user.nentries = 
      *(uint32_t*)&meta->raw_data[header->user_metadata_offset];
  meta->user.entries =
      alloc_cb->alloc(sizeof(ngf_plmd_user_entry) * meta->user.nentries);
  if (meta->user.entries == NULL) {
    err = NGF_PLMD_ERROR_OUTOFMEM;
    goto ngf_plmd_load_cleanup;
  }
  uint8_t *blk_ptr = &meta->raw_data[header->user_metadata_offset + 4u];
  for (uint32_t e = 0u; e < meta->user.nentries; ++e) {
    uint32_t *size_ptr = (uint32_t*)(blk_ptr + sizeof(uint32_t));
    blk_ptr += 2 * sizeof(uint32_t);
    meta->user.entries[e].key = (const char*)(blk_ptr);
    blk_ptr += *size_ptr * sizeof(uint32_t);

    size_ptr = (uint32_t*)(blk_ptr + sizeof(uint32_t));
    blk_ptr += 2 * sizeof(uint32_t);
    meta->user.entries[e].value = (const char*)(blk_ptr);
    blk_ptr += *size_ptr * sizeof(uint32_t);
  }

ngf_plmd_load_cleanup:
  if (err != NGF_PLMD_ERROR_OK) {
    ngf_plmd_destroy(meta, alloc_cb);
  }
  return err;
}

void ngf_plmd_destroy(plmd *m, const ngf_plmd_alloc_callbacks *alloc_cb) {
  if (alloc_cb == NULL) {
    alloc_cb = &stdlib_alloc;
  }
  if (m != NULL) {
    if (m->raw_data != NULL) {
      alloc_cb->free(m->raw_data);
    }
    if (m->layout.set_layouts != NULL) {
      alloc_cb->free((void*)m->layout.set_layouts);
    }
    if (m->images_to_cis_map.entries != NULL) {
      alloc_cb->free((void*)m->images_to_cis_map.entries);
    }
    if (m->samplers_to_cis_map.entries != NULL) {
      alloc_cb->free((void*)m->samplers_to_cis_map.entries);
    }
    if (m->user.entries != NULL) {
      alloc_cb->free((void*)m->user.entries);
    }
    alloc_cb->free(m);
  }
}

const ngf_plmd_layout* ngf_plmd_get_layout(const plmd *m) {
  return &m->layout;
}

const ngf_plmd_cis_map* ngf_plmd_get_image_to_cis_map(const plmd *m) {
  return &m->images_to_cis_map;
}

const ngf_plmd_cis_map* ngf_plmd_get_sampler_to_cis_map(const plmd *m) {
  return &m->samplers_to_cis_map;
}

const ngf_plmd_user* ngf_plmd_get_user(const plmd *m) {
  return &m->user;
}

const ngf_plmd_header* ngf_plmd_get_header(const plmd *m) {
  return m->header;
}
