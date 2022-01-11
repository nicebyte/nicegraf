#pragma once

#include "nicegraf-wrappers.h"

namespace ngf_samples {

/**
 * This is a helper type used by the samples to upload image data to the rendering device.
 * Usually the samples create a staging buffer that is just enough to upload a given image. The raw
 * RGBA data is loaded directly into the staging buffer, which the sample can then use to populate
 * an image. After that, the staging buffer is discarded. This simple method works for the sample
 * code, but more advanced applications will require a different approach.
 */
struct staging_image {
  ngf::buffer staging_buffer; /** Staging buffer containing raw image data. */
  uint32_t    width_px;       /** Image width in pixels. */
  uint32_t    height_px;      /** Image height in pixels. */
  uint32_t nmax_mip_levels; /** Maximum number of mip level that may be generated for this image. */
};

/**
 * Creates a staging_image populated with the raw RGBA data from the given Targa file.
 */
staging_image create_staging_image_from_tga(const char* file_name);

}  // namespace ngf_samples
