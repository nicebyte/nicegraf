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
typedef struct {
  ngf_graphics_pipeline_info pipeline_info ;
  ngf_blend_info blend_info;
  ngf_depth_stencil_info depth_stencil_info;
  ngf_vertex_input_info vertex_input_info;
  ngf_multisample_info multisample_info;
  ngf_rasterization_info rasterization_info;
  ngf_irect2d scissor;
  ngf_irect2d viewport;
  ngf_tessellation_info tessellation_info;
} ngf_util_graphics_pipeline_data;

/**
 * Creates configuration data for a graphics pipeline with a given layout.
 * The state is set to match OpenGL defaults and can be adjusted later.
 */
void ngf_util_create_default_graphics_pipeline_data(
    ngf_pipeline_layout_info *layout,
    const ngf_irect2d *viewport,
    ngf_util_graphics_pipeline_data *result);

/**
 * Stores data for a simple pipeline layout with a single descriptor set.
 */
typedef struct {
  ngf_pipeline_layout_info pipeline_layout;
  ngf_descriptors_layout **descriptors_layouts;
  uint32_t ndescriptors_layouts;
} ngf_util_layout_data;

/**
 * Creates a simple pipeline layout with just a single descriptor set.
 * @param desc pointer to an array of descriptor configurations. All of these
 *  descriptors will be added to the set.
 * @param ndesc number of descriptors in the array.
 */
ngf_error ngf_util_create_simple_layout_data(
    ngf_descriptor_info *desc,
    uint32_t ndesc,
    ngf_util_layout_data *result);

/**
 * Creates a pipeline layout from shader metadata produced by ngf_shaderc.
 * @param stage_layouts pointer to an array of pointers to buffers containing
 *  shader metadata
 * @param nstages number of elements in the array pointed to bt `stage_layouts`
 * @param result data for the newly created layout shall be stored here.
 */
ngf_error ngf_util_create_layout_data(uint32_t **stage_layouts,
                                      uint32_t nstages,
                                      ngf_util_layout_data *result);
#ifdef __cplusplus
}
#endif
