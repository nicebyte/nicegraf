/**
Copyright © 2018 nicegraf contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the “Software”), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "nicegraf.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Contains all the data describing a graphics pipeline, with the exception
 * of shader stages and pipeline layout.
 * Use the `pipeline_info` member of this struct to initialize a new pipeline
 * object.
 */
typedef struct ngf_util_graphics_pipeline_data {
  ngf_graphics_pipeline_info pipeline_info ;
  ngf_blend_info blend_info;
  ngf_depth_stencil_info depth_stencil_info;
  ngf_vertex_input_info vertex_input_info;
  ngf_multisample_info multisample_info;
  ngf_rasterization_info rasterization_info;
  ngf_irect2d scissor;
  ngf_irect2d viewport;
  ngf_tessellation_info tessellation_info;
  ngf_specialization_info spec_info;
  ngf_pipeline_layout_info layout_info;
} ngf_util_graphics_pipeline_data;

/**
 * Creates configuration data for a graphics pipeline with a given layout.
 * The state is set to match OpenGL defaults and can be adjusted later.
 * @param viewport If not NULL, the pipeline's viewport and scissor regions
                   will be configured to this area. If NULL, the viewport and
                   scissor will be possible to adjust dynamically.
 * @param result Pipeline configuration data will be stored here.
 */
void ngf_util_create_default_graphics_pipeline_data(
    const ngf_irect2d *viewport,
    ngf_util_graphics_pipeline_data *result);

/**
 * Creates a simple pipeline layout with just a single descriptor set.
 * @param desc pointer to an array of descriptor configurations. All of these
 *  descriptors will be added to the set.
 * @param ndesc number of descriptors in the array.
 * @param result a pointer to a `ngf_pipeline_layout_info` structyre that will
 *               be populated by this function. The descriptor set within it
 *               must be freed by the caller when it is no longer necessary.
 */
ngf_error ngf_util_create_simple_layout(const ngf_descriptor_info *desc,
                                        uint32_t ndesc,
                                        ngf_pipeline_layout_info *result);

/**
 * Creates a pipeline layout from shader metadata produced by ngf_shaderc.
 * @param stage_layouts pointer to an array of pointers to buffers containing
 *  shader metadata
 * @param nstages number of elements in the array pointed to bt `stage_layouts`
 * @param result a pointer to a `ngf_pipeline_layout_info` structyre that will
 *               be populated by this function. The descriptor sets within it
 *               must be freed by the caller when it is no longer necessary.
 */
ngf_error ngf_util_create_layout(uint32_t **stage_layouts,
                                 uint32_t nstages,
                                 ngf_pipeline_layout_info *result);
#ifdef __cplusplus
}
#endif
