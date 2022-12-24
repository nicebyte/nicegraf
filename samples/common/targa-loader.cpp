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

#include "targa-loader.h"

#include <algorithm>
#include <math.h>
#include <stdexcept>

namespace ngf_samples {
namespace tga {
/* image type constants */
enum class img_type : uint8_t {
  none                = 0,
  color_mapped        = 1,
  true_color          = 2,
  black_and_white     = 3,
  color_mapped_rle    = 9,
  true_color_rle      = 10,
  black_and_white_rle = 11
};

/* targa structures */
#pragma pack(push, 1)
struct cmap {
  uint16_t first_entry_idx;
  uint16_t num_entries;
  uint8_t  bits_per_entry;
};

struct image {
  uint16_t x_origin;
  uint16_t y_origin;
  uint16_t width;
  uint16_t height;
  uint8_t  bitsperpel;
  uint8_t  descriptor;
};

struct header {
  uint8_t  id_length;
  uint8_t  has_cmap;
  img_type type;
  cmap     cmap_entry;
  image    img;
};

struct footer {
  uint32_t ext_offset;
  uint32_t dev_offset;
  char     sig[18];
};
#pragma pack(pop)

}  // namespace tga

namespace {

float srgb_to_linear(uint8_t srgb_value) {
  const float srgb_valuef = (float)srgb_value / 255.0f;
  return srgb_valuef <= 0.04045f ? (srgb_valuef / 12.92f)
                                 : powf(((srgb_valuef + 0.055f) / 1.055f), 2.4f);
}

uint8_t linear_to_srgb(float linear_value) {
  const float srgb_valuef = linear_value <= 0.0031308f
                                ? (12.92f * linear_value)
                                : (1.055f * powf(linear_value, 1.0f / 2.4f) - 0.055f);
  return (uint8_t)(std::min(1.0f, srgb_valuef) * 255.0f);
}

}  // namespace

void load_targa(
    const void* in_buf,
    size_t      in_buf_size,
    void*       out_buf,
    size_t      out_buf_size,
    uint32_t*   width_px,
    uint32_t*   height_px) {
  auto in_bytes  = (const char*)in_buf;
  auto out_bytes = (char*)out_buf;

  /* obtain header and footer data. */
  auto hdr = (const tga::header*)in_buf;
  auto ftr = (const tga::footer*)&in_bytes[in_buf_size - sizeof(tga::footer)];

  /* write width and height outputs. */
  *width_px  = hdr->img.width;
  *height_px = hdr->img.height;

  /* if the output buffer pointer is null, we're done. */
  if (out_buf == nullptr) { return; }

  /* compute expected output size and check if it fits into the provided
     output buffer. */
  const size_t expected_output_size = 4u * hdr->img.width * hdr->img.height;
  if (expected_output_size > out_buf_size) { throw std::runtime_error("buffer overflow"); }

  /* verify that footer is valid. */
  const char* expected_sig = "TRUEVISION-XFILE.";
  for (size_t si = 0; si < sizeof(ftr->sig); ++si) {
    if (ftr->sig[si] != expected_sig[si]) { throw std::runtime_error("tga signature not found"); }
  }

  /* only rle-encoded true-color images are allowed. */
  if (hdr->type != tga::img_type::true_color_rle) {
    throw std::runtime_error("unsupported tga feature detected");
  }
  const bool has_alpha = (hdr->img.descriptor & 0x08) != 0;

  /* obtain extension data offset. */
  const size_t ext_offset = ftr->ext_offset;

  /* read 'attributes type' field to determine whether alpha is
     premultiplied.
     if no extension section is present, assume non-premultiplied
     alpha. */
  const char attr_type = !has_alpha || ext_offset == 0 ? 3 : in_bytes[ext_offset + 494];
  if (attr_type != 3 && attr_type != 4) { throw std::runtime_error("invalid attribute type"); }
  const bool is_premul_alpha = attr_type == 4;

  /* read and decode image data, writing result to output. */
  const char*  img_data       = in_bytes + sizeof(tga::header) + hdr->id_length;
  size_t written_pixels = 0;
  const size_t bytes_per_pel  = has_alpha ? 4 : 3;
  while (written_pixels < hdr->img.width * hdr->img.height &&
         img_data - in_bytes < (ptrdiff_t)in_buf_size) {
    const char   packet_hdr    = *img_data;
    const bool   is_rle_packet = packet_hdr & 0x80;
    const size_t packet_length = 1u + (packet_hdr & 0x7f);
    ++img_data; /* advance img. data to point to start of packet data. */
    for (size_t p = 0u; p < packet_length; ++p) {
      /* pixel data is stored as BGRA. */
      const uint8_t  a     = has_alpha ? (uint8_t)img_data[3] : 0xff;
      const float   af     = (float)a / 255.0f;
      auto        premul = [&](uint8_t v) {
        if (is_premul_alpha || !has_alpha)
          return v;
        else {
          /* need to convert from sRGB to linear, premultiply then convert back. */
          const float linear        = srgb_to_linear(v);
          const float linear_premul = linear * af;
          return linear_to_srgb(linear_premul);
        }
      };
      const uint8_t b = premul((uint8_t)img_data[0]), g = premul((uint8_t)img_data[1]), r = premul((uint8_t)img_data[2]);
      out_bytes[written_pixels * 4u + 0] = (char)r;
      out_bytes[written_pixels * 4u + 1] = (char)g;
      out_bytes[written_pixels * 4u + 2] = (char)b;
      out_bytes[written_pixels * 4u + 3] = (char)a;
      ++written_pixels;
      if (!is_rle_packet) img_data += bytes_per_pel;
    }
    if (is_rle_packet) img_data += bytes_per_pel;
  }

  if (img_data - in_bytes >= (ptrdiff_t)in_buf_size) {
    throw std::runtime_error("buffer overflow");
  }
}

}  // namespace ngf_samples
