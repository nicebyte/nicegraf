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
#define _CRT_SECURE_NO_WARNINGS
#include "mesh-loader.h"

#include "check.h"

#include <stdint.h>
#include <stdio.h>

namespace ngf_samples {

mesh load_mesh_from_file(const char* mesh_file_name, ngf_xfer_encoder xfenc) {
  mesh  result;
  FILE* mesh_file = fopen(mesh_file_name, "rb");
  NGF_SAMPLES_ASSERT(mesh_file != NULL);

  /**
   * Read the "file header" - 4-byte field with the lowest bit indicating
   * the presence of normals, and the second-lowest bit indicating the
   * presence of UV coordinates (position attribute is always assumed).
   */
  uint32_t header        = 0u;
  size_t   read_elements = 0u;
  read_elements          = fread(&header, sizeof(header), 1u, mesh_file);
  NGF_SAMPLES_ASSERT(read_elements == 1u);
  result.have_normals = header & 1;
  result.have_uvs     = header & 2;

  /**
   * Read the total size of the vertex data, create a staging buffer, and
   * read the vertex data directly into the staging buffer.
   */
  uint32_t vertex_data_size = 0u;
  read_elements             = fread(&vertex_data_size, sizeof(vertex_data_size), 1u, mesh_file);
  NGF_SAMPLES_ASSERT(read_elements == 1u);
  const ngf_buffer_info vertex_data_staging_buffer_info = {
      .size         = vertex_data_size,
      .storage_type = NGF_BUFFER_STORAGE_HOST_WRITEABLE,
      .buffer_usage = NGF_BUFFER_USAGE_XFER_SRC,
  };
  ngf::buffer vertex_data_staging_buffer;
  NGF_SAMPLES_CHECK_NGF_ERROR(
      vertex_data_staging_buffer.initialize(vertex_data_staging_buffer_info));
  void* mapped_vertex_data_staging_buffer =
      ngf_buffer_map_range(vertex_data_staging_buffer.get(), 0u, vertex_data_size);
  read_elements =
      fread(mapped_vertex_data_staging_buffer, sizeof(char), vertex_data_size, mesh_file);
  NGF_SAMPLES_ASSERT(read_elements == vertex_data_size);
  ngf_buffer_flush_range(vertex_data_staging_buffer.get(), 0, vertex_data_size);
  ngf_buffer_unmap(vertex_data_staging_buffer.get());

  /**
   * Read the number of indices in the mesh. If number of indices is 0, the
   * mesh is considered to not have an index buffer, and a non-indexed draw call
   * should be used to render it.
   */
  read_elements = fread(&result.num_indices, sizeof(uint32_t), 1, mesh_file);
  NGF_SAMPLES_ASSERT(read_elements == 1u);

  /**
   * Allocate a staging buffer for the index data, and read the index data
   * directly into that buffer.
   */

  ngf::buffer index_data_staging_buffer;
  if (result.num_indices > 0) {
    const ngf_buffer_info index_data_staging_buffer_info = {
        .size         = sizeof(uint32_t) * result.num_indices,
        .storage_type = NGF_BUFFER_STORAGE_HOST_WRITEABLE,
        .buffer_usage = NGF_BUFFER_USAGE_XFER_SRC,
    };
    NGF_SAMPLES_CHECK_NGF_ERROR(
        index_data_staging_buffer.initialize(index_data_staging_buffer_info));
    void* mapped_index_data_staging_buffer = ngf_buffer_map_range(
        index_data_staging_buffer.get(),
        0,
        index_data_staging_buffer_info.size);
    read_elements =
        fread(mapped_index_data_staging_buffer, sizeof(uint32_t), result.num_indices, mesh_file);
    NGF_SAMPLES_ASSERT(read_elements == result.num_indices);
    ngf_buffer_flush_range(index_data_staging_buffer.get(), 0, index_data_staging_buffer_info.size);
    ngf_buffer_unmap(index_data_staging_buffer.get());
  }

  /**
   * Create the GPU buffers mesh buffers, and record commands to upload the data
   * from staging buffers into them.
   */
  const ngf_buffer_info vertex_data_buffer_info = {
      .size         = vertex_data_staging_buffer_info.size,
      .storage_type = NGF_BUFFER_STORAGE_PRIVATE,
      .buffer_usage = NGF_BUFFER_USAGE_VERTEX_BUFFER | NGF_BUFFER_USAGE_XFER_DST,
  };
  NGF_SAMPLES_CHECK_NGF_ERROR(result.vertex_data.initialize(vertex_data_buffer_info));
  ngf_cmd_copy_buffer(
      xfenc,
      vertex_data_staging_buffer.get(),
      result.vertex_data.get(),
      vertex_data_buffer_info.size,
      0u,
      0u);
  if (result.num_indices > 0) {
    const ngf_buffer_info index_data_buffer_info = {
        .size         = sizeof(uint32_t) * result.num_indices,
        .storage_type = NGF_BUFFER_STORAGE_PRIVATE,
        .buffer_usage = NGF_BUFFER_USAGE_INDEX_BUFFER | NGF_BUFFER_USAGE_XFER_DST,
    };
    NGF_SAMPLES_CHECK_NGF_ERROR(result.index_data.initialize(index_data_buffer_info));
    ngf_cmd_copy_buffer(
        xfenc,
        index_data_staging_buffer.get(),
        result.index_data.get(),
        index_data_buffer_info.size,
        0u,
        0u);
  }

  return result;
}

}  // namespace ngf_samples
