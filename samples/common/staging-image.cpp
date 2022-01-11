#include "staging-image.h"

#include "check.h"
#include "file-utils.h"
#include "targa-loader.h"

#include <cmath>
#include <vector>

namespace ngf_samples {

staging_image create_staging_image_from_tga(const char* file_name) {
  /* Read in the texture image file.*/
  std::vector<char> texture_tga_data = load_file(file_name);

  /* this call does nothing but quickly get the width & height. */
  uint32_t texture_width, texture_height;
  load_targa(
      texture_tga_data.data(),
      texture_tga_data.size(),
      nullptr,
      0u,
      &texture_width,
      &texture_height);

  /* Create an appropriately sized staging buffer for the texture upload. */
  const size_t texture_size_bytes = texture_width * texture_height * 4u;
  ngf::buffer  staging_buf;
  NGF_SAMPLES_CHECK_NGF_ERROR(staging_buf.initialize(ngf_buffer_info {
      .size         = texture_size_bytes,
      .storage_type = NGF_BUFFER_STORAGE_HOST_READABLE_WRITEABLE,
      .buffer_usage = NGF_BUFFER_USAGE_XFER_SRC}));
  void* mapped_staging_buf = ngf_buffer_map_range(staging_buf.get(), 0, texture_size_bytes);

  /* Decode the loaded targa file, writing RGBA values directly into mapped memory. */
  load_targa(
      texture_tga_data.data(),
      texture_tga_data.size(),
      mapped_staging_buf,
      texture_size_bytes,
      &texture_width,
      &texture_height);

  /* Flush and unmap the staging buffer. */
  ngf_buffer_flush_range(staging_buf.get(), 0, texture_size_bytes);
  ngf_buffer_unmap(staging_buf.get());

  /* Count the number of mipmaps we'll have to generate for trilinear filtering.
     Note that we keep generating mip levels until both dimensions are reduced to 1.
   */
  uint32_t nmips =
      1 + static_cast<uint32_t>(std::floor(std::log2(std::max(texture_width, texture_height))));

  return staging_image {
      .staging_buffer  = std::move(staging_buf),
      .width_px        = texture_width,
      .height_px       = texture_height,
      .nmax_mip_levels = nmips};
}

}  // namespace ngf_samples
