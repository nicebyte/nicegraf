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

#include "shader-loader.h"

#include "file-utils.h"
#include "check.h"

#include <fstream>
#include <string>

namespace ngf_samples {

#if defined(NGF_SAMPLES_BACKEND_NICEGRAF_VK)
#define SHADER_EXTENSION ".spv"
#elif defined(NGF_SAMPLES_BACKEND_NICEGRAF_MTL) || defined(NGF_SAMPLES_BACKEND_NICEGRAF_MTL_CPP)
#define SHADER_EXTENSION ".21.msl"
#else
#error "build system needs to define samples backend"
#endif

ngf::shader_stage
load_shader_stage(const char* shader_file_name, const char* entry_point_name, ngf_stage_type type) {
  constexpr const char* shaders_root_dir        = "shaders" NGF_SAMPLES_PATH_SEPARATOR;
  constexpr const char* stage_to_file_ext_map[] = {"vs", "ps", "cs"};

  const std::string file_name = shaders_root_dir + std::string(shader_file_name) + "." +
                                stage_to_file_ext_map[type] + SHADER_EXTENSION;
  const std::vector<char> content    = load_file(file_name.c_str());
  ngf_shader_stage_info      stage_info = {
      .type             = type,
      .content          = reinterpret_cast<const uint8_t*>(content.data()),
      .content_length   = (uint32_t)content.size(),
      .debug_name       = "",
      .entry_point_name = entry_point_name};

  ngf::shader_stage stage;
  NGF_SAMPLES_CHECK_NGF_ERROR(stage.initialize(stage_info));

  return stage;
}

}  // namespace ngf_samples
