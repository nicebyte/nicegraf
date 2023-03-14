/**
 * Copyright (c) 2023 nicegraf contributors
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

#include "imgui.h"
#include "nicegraf-wrappers.h"
#include "nicegraf.h"

namespace ngf_samples {

/**
 * This is a nicegraf-based rendering backend for ImGui.
 * It's used to render the UI for samples.
 */
class ngf_imgui {
  public:
  /**
   * Initializes the internal state of the ImGui rendering backend, and uploads
   * the font texture by recording the appropriate commands into the given
   * transfer encoder.
   */
  ngf_imgui(
      ngf_xfer_encoder     font_xfer_encoder,
      ngf_sample_count     main_render_target_sample_count,
      const unsigned char* font_atlast_bytes,
      uint32_t             font_atlas_width,
      uint32_t             font_atlas_height);

  /**
   * Records commands for rendering the contents ofteh current ImGui draw data into the
   * given render encoder.
   */
  void record_rendering_commands(ngf_render_encoder enc);

  private:
  struct uniform_data {
    float ortho_projection[4][4];
  };

#if !defined(NGF_NO_IMGUI)
  ngf::graphics_pipeline                 pipeline_;
  ngf::uniform_multibuffer<uniform_data> uniform_data_;
  ngf::image                             font_texture_;
  ngf::sampler                           tex_sampler_;
  ngf::buffer                            attrib_buffer_;
  ngf::buffer                            index_buffer_;
  ngf::buffer                            texture_data_;
  ngf::shader_stage                      vertex_stage_;
  ngf::shader_stage                      fragment_stage_;
  ngf::render_target                     default_rt_;
#endif
};

}  // namespace ngf_samples
