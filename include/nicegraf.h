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

/**
 * @file nicegraf.h
 * @brief Nicegraf declarations.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Error codes.
 * Nicegraf functions report errors via return values. Results are stored in
 * output arguments.
 */
typedef enum {
  NGF_ERROR_OK = 0, /**< No error, operation finished successfully. */
  NGF_ERROR_OUTOFMEM, /**< Host memory allocation failed. */
  NGF_ERROR_FAILED_TO_CREATE_PIPELINE,
  NGF_ERROR_INCOMPLETE_PIPELINE, /**< Some information required to create
                                      a pipeline object was not provided.*/
  NGF_ERROR_CANT_POPULATE_IMAGE, /**< Ulpoading data to image failed. */
  NGF_ERROR_IMAGE_CREATION_FAILED,
  NGF_ERROR_CREATE_SHADER_STAGE_FAILED, /**< On certain back-ends this means
                                             that compiling or linking code for
                                             a shader stage has failed.*/
  NGF_ERROR_INVALID_BINDING,
  NGF_ERROR_INVALID_INDEX_BUFFER_BINDING,
  NGF_ERROR_INVALID_VERTEX_BUFFER_BINDING,
  NGF_ERROR_INCOMPLETE_RENDER_TARGET, /**< Render target didn't have all the
                                           necessary attachments, or some
                                           attachments were invalid. */
  NGF_ERROR_INVALID_RESOURCE_SET_BINDING,
  NGF_ERROR_CONTEXT_CREATION_FAILED,
  NGF_ERROR_INVALID_CONTEXT,
  NGF_ERROR_SWAPCHAIN_CREATION_FAILED,
  NGF_ERROR_INVALID_SURFACE_FORMAT,
  NGF_ERROR_INITIALIZATION_FAILED,
  NGF_ERROR_SURFACE_CREATION_FAILED,
  NGF_ERROR_CANT_SHARE_CONTEXT,
  NGF_ERROR_BEGIN_FRAME_FAILED,
  NGF_ERROR_END_FRAME_FAILED,
  NGF_ERROR_OUT_OF_BOUNDS /**< The operation would have resulted in an out of
                               bounds access. */
} ngf_error ;

/**
 * Device hints.
 */
typedef enum {
  NGF_DEVICE_PREFERENCE_DISCRETE, /**< Prefer discrete GPU. */
  NGF_DEVICE_PREFERENCE_INTEGRATED, /**< Prefer integrated GPU. */
  NGF_DEVICE_PREFERENCE_DONTCARE /**< No GPU preference. */
} ngf_device_preference;

/**
 * Possible present modes.
 */
typedef enum {
  NGF_PRESENTATION_MODE_FIFO, /**< Frames get queued ("wait for vsync") */
  NGF_PRESENTATION_MODE_IMMEDIATE /**< Doesn't wait for vsync */
} ngf_present_mode;

/**
 * Represents a rectangular, axis-aligned 2D region with integer coordinates.
 */
typedef struct {
  int32_t x; /**< X coord of lower-left corner. */
  int32_t y; /**< Y coord of lower-left corner. */
  size_t width;
  size_t height;
} ngf_irect2d;

/**
 * Represents a rectangular, axis-aligned 3D volume.
 */
typedef struct {
  uint32_t width;
  uint32_t height;
  uint32_t depth;
} ngf_extent3d ;

/**
 * Three-dimensional offset.
 */
typedef struct {
  int32_t x;
  int32_t y;
  int32_t z;
} ngf_offset3d ;

/**
 * Shader stage types.
 * Note that some back-ends might not support all of these.
 */
typedef enum {
  NGF_STAGE_VERTEX = 0,
  NGF_STAGE_TESSELLATION_CONTROL,
  NGF_STAGE_TESSELLATION_EVALUATION,
  NGF_STAGE_GEOMETRY,
  NGF_STAGE_FRAGMENT,
  // TODO: compute pipelines
} ngf_stage_type;

/**
 * Describes a programmable shader stage.
 */
typedef struct {
  ngf_stage_type type; /**< Stage type (vert/frag/etc.) */
  const char *content; /**< May be text or binary, depending on the backend.*/
  size_t content_length; /**< Number of bytes in the content buffer. */
  const char *debug_name; /**< Optional name, will appear in debug logs,
                               may be NULL.*/
} ngf_shader_stage_info;

/**
 * Compiled code for a shader stage.
 */
typedef struct ngf_shader_stage ngf_shader_stage;

/**
 * Ways to draw polygons.
 * Some back-ends might not support all of these.
  */
typedef enum {
  NGF_POLYGON_MODE_FILL = 0, /**< Fill entire polyoon.*/
  NGF_POLYGON_MODE_LINE, /**< Outline only.*/
  NGF_POLYGON_MODE_POINT  /**< Vertices only.*/
} ngf_polygon_mode;

/**
 * Which polygons to cull.
 */
typedef enum {
  NGF_CULL_MODE_BACK = 0, /**< Cull back-facing polygons.*/
  NGF_CULL_MODE_FRONT, /**< Cull front-facing polygons. */
  NGF_CULL_MODE_FRONT_AND_BACK /**< Cull all.*/
} ngf_cull_mode;

/**
 * Ways to determine front-facing polygons.
 */
typedef enum {
  NGF_FRONT_FACE_COUNTER_CLOCKWISE = 0, /**< CCW winding is front-facing.*/
  NGF_FRONT_FACE_CLOCKWISE /**< CW winding is front-facing. */
} ngf_front_face_mode;

/**
 * Rasterization stage parameters.
 */
typedef struct {
  bool discard; /**< Enable/disable rasterizer discard. Use in pipelines that
                     don't write fragment data.*/
  ngf_polygon_mode polygon_mode; /**< How to draw polygons.*/
  ngf_cull_mode cull_mode; /**< Which polygoons to cull.*/
  ngf_front_face_mode front_face; /**< Which winding counts as front-facing.*/
  float line_width; /**< Width of a line.*/
} ngf_rasterization_info ;

/**
 * Compare operations used in depth and stencil tests.
 */
typedef enum {
  NGF_NEVER = 0,
  NGF_LESS,
  NGF_LEQUAL,
  NGF_EQUAL,
  NGF_GEQUAL,
  NGF_GREATER,
  NGF_NEQUAL,
  NGF_ALWAYS
} ngf_compare_op;

/**
 * Operations that can be performed on stencil buffer.
 */
typedef enum {
  NGF_STENCIL_OP_KEEP = 0, /**< Don't touch.*/
  NGF_STENCIL_OP_ZERO, /**< Set to 0.*/
  NGF_STENCIL_OP_REPLACE, /**< Replace with reference value.*/
  NGF_STENCIL_OP_INCR_CLAMP, /**< Increment, clamping to max value.*/
  NGF_STENCIL_OP_INCR_WRAP, /**< Increment, wrapping to 0.*/
  NGF_STENCIL_OP_DECR_CLAMP, /**< Decrement, clamping to 0.*/
  NGF_STENCIL_OP_DECR_WRAP, /**< Decrement, wrapping to max value.*/
  NGF_STENCIL_OP_INVERT /**< Bitwise invert*/
} ngf_stencil_op ;

/**
 * Stencil operation description.
 */
typedef struct {
  ngf_stencil_op fail_op; /**< What to do on stencil test fail.*/
  ngf_stencil_op pass_op; /**< What to do on pass.*/
  ngf_stencil_op depth_fail_op; /**< When depth fails but stencil pass.*/
  ngf_compare_op compare_op; /**< Stencil comparison function.*/
  uint32_t compare_mask; /**< Compare mask.*/
  uint32_t write_mask; /**< Write mask.*/
  uint32_t reference; /**< Reference value (used for REPLACE).*/
} ngf_stencil_info ;

/**
 * Pipeline's depth/stencil state description.
 */
typedef struct {
  float min_depth; /** Near end of the depth range. */
  float max_depth; /** Far end of the depth range. */
  bool depth_test; /**< Enable depth test.*/
  bool depth_write; /**< Enable writes to depth buffer.*/
  ngf_compare_op depth_compare; /**< Depth comparison function.*/
  bool stencil_test; /**< Enable stencil test.*/
  ngf_stencil_info front_stencil; /**< Stencil op for front-facing polys*/
  ngf_stencil_info back_stencil;  /**< Stencil op for back-facing polys*/
} ngf_depth_stencil_info;

/**
 * Blend factors.
 * Result of blend is computed as source*sfactor + dest*dfactor
 * Where `source` is the incoming value, `dest` is the value already in the
 * frame buffer, and `sfactor` and `dfactor` are one of these values. 
 */
typedef enum {
  NGF_BLEND_FACTOR_ZERO = 0,
  NGF_BLEND_FACTOR_ONE,
  NGF_BLEND_FACTOR_SRC_COLOR,
  NGF_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
  NGF_BLEND_FACTOR_DST_COLOR,
  NGF_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
  NGF_BLEND_FACTOR_SRC_ALPHA,
  NGF_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
  NGF_BLEND_FACTOR_DST_ALPHA,
  NGF_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
  NGF_BLEND_FACTOR_CONSTANT_COLOR,
  NGF_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,
  NGF_BLEND_FACTOR_CONSTANT_ALPHA,
  NGF_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,
} ngf_blend_factor;

/**
 * Pipeline's blend state description.
 */
typedef struct {
  bool enable; /**< Enable blending.*/
  ngf_blend_factor sfactor; /**< Source factor.*/
  ngf_blend_factor dfactor; /**< Destination factor.*/
} ngf_blend_info;

/**
 * Vertex attribute's input rate.
 */
typedef enum {
  NGF_INPUT_RATE_VERTEX = 0, /**< attribute changes per-vertex*/
  NGF_INPUT_RATE_INSTANCE /**< attribute changes per-instance*/
} ngf_input_rate;

/**
 * Vertex attribute component type.
 */
typedef enum {
  NGF_TYPE_INT8 = 0,
  NGF_TYPE_UINT8,
  NGF_TYPE_INT16,
  NGF_TYPE_UINT16,
  NGF_TYPE_INT32,
  NGF_TYPE_UINT32,
  NGF_TYPE_FLOAT,
  NGF_TYPE_HALF_FLOAT,
  NGF_TYPE_DOUBLE
} ngf_type;

/**
 * Specifies an attribute binding.
 */
typedef struct {
  uint32_t binding;/**< Index of the binding that this structure describes.*/
  uint32_t stride; /**< Number of bytes between consecutive attribute values.*/
  ngf_input_rate input_rate; /**< Whether attributes read from this binding
                                  change per-vetex or per-instance.*/
} ngf_vertex_buf_binding_desc;

/**
 * Specifies information about a vertex attribute.
 */
typedef struct {
  uint32_t location; /**< Attribute index. */
  uint32_t binding; /**< Which vertex buffer binding to use.*/
  uint32_t offset; /**< Offset in the buffer at which attribute data starts.*/
  ngf_type type; /**< Type of attribute component.*/
  uint32_t size; /**< Number of attribute components.*/
  bool normalized; /**< Whether attribute values should be normalized.*/
} ngf_vertex_attrib_desc;

/**
 * Specifies information about the pipeline's vertex input.
 */
typedef struct {
  /**Pointer to array of structures describing the vertex buffer binding used.*/
  const ngf_vertex_buf_binding_desc *vert_buf_bindings; 
  uint32_t nvert_buf_bindings; /**< Number of vertex buffer bindings
                                         used.*/
  const ngf_vertex_attrib_desc *attribs; /**< Ptr to array of attrib descriptions.*/
  uint32_t nattribs; /**< Number of attribute descriptions.*/
} ngf_vertex_input_info;

/**
 * Specifies state of multisampling. 
 */
typedef struct {
  bool multisample; /**< Whether to enable multisampling.*/
  bool alpha_to_coverage; /**< Whether alpha-to-coverage is enabled.*/
} ngf_multisample_info;

/** Specifies tessellation-related state.
 * Only has meaning for back-ends that support tessellation.
 */
typedef struct {
  uint32_t patch_vertices; /**< Number of verts per tessellation patch.*/
} ngf_tessellation_info;

/**
 * Possible image types.
 */
typedef enum {
  NGF_IMAGE_TYPE_IMAGE_2D = 0,
  NGF_IMAGE_TYPE_IMAGE_3D
} ngf_image_type;

/**
 * Image formats.
 * Some backends may not support all of those.
 * Using an sRGB format in a color attachment or swapchain image means that all
 * color values output by the fragment stage are interpreted as being in linear
 * color space, and an appropriate transfer function is applied to them to
 * covert them to the sRGB colorspace before writing them to the target.
 * Using an sRGB format in a sampled image means that all color values stored in
 * the image are interpreted to be in the linear color space, and all read
 * operations automatically apply a transfer function to convert the values from
 * sRGB to linear color space.
 */
typedef enum {
  NGF_IMAGE_FORMAT_R8 = 0,
  NGF_IMAGE_FORMAT_RG8,
  NGF_IMAGE_FORMAT_RGB8,
  NGF_IMAGE_FORMAT_RGBA8,
  NGF_IMAGE_FORMAT_SRGB8,
  NGF_IMAGE_FORMAT_SRGBA8,
  NGF_IMAGE_FORMAT_BGR8,
  NGF_IMAGE_FORMAT_BGRA8,
  NGF_IMAGE_FORMAT_BGR8_SRGB,
  NGF_IMAGE_FORMAT_BGRA8_SRGB,
  NGF_IMAGE_FORMAT_R32F,
  NGF_IMAGE_FORMAT_RG32F,
  NGF_IMAGE_FORMAT_RGB32F,
  NGF_IMAGE_FORMAT_RGBA32F,
  NGF_IMAGE_FORMAT_R16F,
  NGF_IMAGE_FORMAT_RG16F,
  NGF_IMAGE_FORMAT_RGB16F,
  NGF_IMAGE_FORMAT_RGBA16F,
  NGF_IMAGE_FORMAT_DEPTH32,
  NGF_IMAGE_FORMAT_DEPTH16,
  NGF_IMAGE_FORMAT_DEPTH24_STENCIL8,
  NGF_IMAGE_FORMAT_UNDEFINED
} ngf_image_format;

/**
 * Swapchain configuration.
 */
typedef struct {
  ngf_image_format cfmt; /**< Swapchain image format. */
  ngf_image_format dfmt; /**< Format to use for the depth buffer, if set to
                              NGF_IMAGE_FORMAT_UNDEFINED, no depth buffer will
                              be created. */
  int nsamples; /**< Number of samples per pixel (0 for non-multisampled) */
  size_t capacity_hint; /**< Number of images in swapchain (may get ignored) */
  size_t width; /**< Width of swapchain images in pixels. */
  size_t height;/**< Height of swapchain images in pixels. */
  uintptr_t native_handle;/**< HWND, ANativeWindow, NSWindow, etc. */
  ngf_present_mode present_mode; /**< Desired present mode. */
} ngf_swapchain_info;

/**
 * Image usage flags.
 */
typedef enum {
  NGF_IMAGE_USAGE_SAMPLE_FROM = 0x01, /**< Can be read from in a shader.*/
  NGF_IMAGE_USAGE_ATTACHMENT = 0x02   /**< Can be used as an attachment for a
                                           render target.*/
} ngf_image_usage;

/**
 * Describes an image.
 */
typedef struct {
  ngf_image_type type;
  ngf_extent3d extent; /**< Width, height and depth (for 3d images) or no. of
                            layers (for layered images).*/
  uint32_t nmips; /**< Number of mip levels.*/
  ngf_image_format format; /**< Internal format.*/
  uint32_t nsamples; /**< Number of samples. 0 indicates non-multisampled
                          images.*/
  uint32_t usage_hint; /**< How the client intends to use the image. Must be a
                            combination of image usage flags.*/
} ngf_image_info;

typedef struct ngf_image ngf_image;

/**
 * Reference to an image part.
 */
typedef struct {
  const ngf_image *image; /**< Image being referred to.*/
  uint32_t mip_level; /**< Mip level within the image.*/
  uint32_t layer; /**< Layer within the image.*/
  bool layered; /**< Whether the referred image is layered.*/
} ngf_image_ref;

/**
 *  Min/mag filters.
 */
typedef enum {
  NGF_FILTER_NEAREST = 0,
  NGF_FILTER_LINEAR,
  NGF_FILTER_LINEAR_MIPMAP_NEAREST,
  NGF_FILTER_LINEAR_MIPMAP_LINEAR
} ngf_sampler_filter;

/**
 * What to do when sampling out-of-bounds.
 */
typedef enum {
  NGF_WRAP_MODE_CLAMP_TO_EDGE = 0,
  NGF_WRAP_MODE_CLAMP_TO_BORDER,
  NGF_WRAP_MODE_REPEAT,
  NGF_WRAP_MODE_MIRRORED_REPEAT
} ngf_sampler_wrap_mode;

/**
 * Describes a sampler object.
 */
typedef struct {
  ngf_sampler_filter min_filter; /**< Minification filter.*/
  ngf_sampler_filter mag_filter; /**< Magnification filter.*/
  // TODO: anisotropic filtering
  ngf_sampler_wrap_mode wrap_s; /**< Horizontal wrap mode. */
  ngf_sampler_wrap_mode wrap_t;
  ngf_sampler_wrap_mode wrap_r;
  float lod_max; /**< Max mip level.*/
  float lod_min; /**< Min mip level.*/
  float lod_bias; /**< Level bias.*/
  float border_color[4]; /**< Border color.*/
} ngf_sampler_info;

/**
 * Sampler object.
 */
typedef struct ngf_sampler ngf_sampler;

/** Available descriptor types.
 * Not that some back-ends may not support all of the listed descriptor types.
 */
typedef enum {
  NGF_DESCRIPTOR_UNIFORM_BUFFER = 0,
  NGF_DESCRIPTOR_STORAGE_BUFFER,
  NGF_DESCRIPTOR_LOADSTORE_IMAGE,
  NGF_DESCRIPTOR_TEXTURE,
  NGF_DESCRIPTOR_SAMPLER,
  NGF_DESCRIPTOR_TEXTURE_AND_SAMPLER,
  NGF_DESCRIPTOR_TYPE_COUNT
} ngf_descriptor_type;

/**
 * Descriptor configuration.
 * A descriptor is an opaque object through which resources such as buffers and
 * textures can be accessed.
 * This structure specifies details about a descriptor.
 */
typedef struct {
  ngf_descriptor_type type; /**< Type of the descriptor.*/
  uint32_t id; /**< Associated binding point.*/
} ngf_descriptor_info;

/**
 * Descriptor set layout configuration.
 * Descriptors are bound in groups called descriptor sets.
 * The number of descriptors in such a group, and their types and
 * associated bindings are called a "descriptor set layout".
 * Think of it as a schema for a descriptor set.
 */
typedef struct {
  ngf_descriptor_info *descriptors;
  uint32_t ndescriptors;
} ngf_descriptors_layout_info;

/**
 * Descriptor layout object.
 */
typedef struct ngf_descriptors_layout ngf_descriptors_layout;

/**
 * Descriptor set object.
 */
typedef struct ngf_descriptor_set ngf_descriptor_set;

/**
 * Pipeline layout description.
 * Specifies layouts for descriptor sets that are required to be bound by a
 * pipeline.
 */
typedef struct {
  uint32_t ndescriptors_layouts;
  const ngf_descriptors_layout **descriptors_layouts;
} ngf_pipeline_layout_info;

/**
 * Buffer object
 */
typedef struct ngf_buffer ngf_buffer;

/**
 * Specifies a buffer bind operation.
 */
typedef struct {
  const ngf_buffer *buffer; /**< Which buffer to bind.*/
  size_t offset; /**< Offset at which to bind the buffer.*/
  size_t range;  /**< Bound range.*/
} ngf_descriptor_write_buffer;

/**
 * Specifies an image bind operation.
 */
typedef struct {
  ngf_image_ref image_subresource; /**< Image portion to bind.*/
  const ngf_sampler *sampler; /**< Sampler to use.*/
} ngf_descriptor_write_image_sampler;

/**
 * Specifies a write to a descriptor set.
 */
typedef struct {
  uint32_t binding; /**< Number of the binding being changed.*/
  ngf_descriptor_type type; /**< Type of the descriptor being written.*/

  /* Resource-specific information.*/
  union {
    ngf_descriptor_write_buffer buffer_bind;
    ngf_descriptor_write_image_sampler image_sampler_bind;
  } op;
} ngf_descriptor_write;

/**
 * Graphics pipeline object.
 */
typedef struct ngf_graphics_pipeline ngf_graphics_pipeline;

/** 
 * Flags for specifying which aspects of the pipeline are to be configured
 * as dynamic.
 */
typedef enum {
  NGF_DYNAMIC_STATE_VIEWPORT = 0x01,
  NGF_DYNAMIC_STATE_SCISSOR = 0x02,
  NGF_DYNAMIC_STATE_LINE_WIDTH = 0x04,
  NGF_DYNAMIC_STATE_BLEND_CONSTANTS = 0x08,
  NGF_DYNAMIC_STATE_STENCIL_REFERENCE = 0x10,
  NGF_DYNAMIC_STATE_STENCIL_COMPARE_MASK = 0x20,
  NGF_DYNAMIC_STATE_STENCIL_WRITE_MASK = 0x40
} ngf_dynamic_state_flags;

/**
 * A command affecting dynamic state of the pipeline.
 */
typedef struct {
  ngf_dynamic_state_flags state; /**< Which aspect is affected.*/
  union {
    ngf_irect2d viewport;
    ngf_irect2d scissor;
    float line_width;
    struct {
      ngf_blend_factor sfactor;
      ngf_blend_factor dfactor;
    } blend_factors;
    struct {
      uint32_t front;
      uint32_t back;
    } stencil_reference;
    struct {
      uint32_t front;
      uint32_t back;
    } stencil_write_mask;
    struct {
      uint32_t front;
      uint32_t back;
    } stencil_compare_mask;
  };
} ngf_dynamic_state_command;

/**
 * Primitive types to use for draw operations.
 * Some back-ends may not support all of the primitive types.
 */
typedef enum {
  NGF_PRIMITIVE_TYPE_TRIANGLE_LIST = 0,
  NGF_PRIMITIVE_TYPE_TRIANGLE_STRIP,
  NGF_PRIMITIVE_TYPE_TRIANGLE_FAN,
  NGF_PRIMITIVE_TYPE_LINE_LIST,
  NGF_PRIMITIVE_TYPE_LINE_STRIP,
  NGF_PRIMITIVE_TYPE_PATCH_LIST
} ngf_primitive_type;

/**
 * Specifies information for creation of a graphics pipeline.
 */
typedef struct {
  const ngf_shader_stage *shader_stages[5];
  uint32_t nshader_stages;
  const ngf_irect2d *viewport;
  const ngf_irect2d *scissor;
  const ngf_rasterization_info *rasterization;
  const ngf_multisample_info *multisample;
  const ngf_depth_stencil_info *depth_stencil;
  const ngf_blend_info *blend;
  uint32_t dynamic_state_mask;
  const ngf_tessellation_info *tessellation;
  const ngf_vertex_input_info *input_info;
  ngf_primitive_type primitive_type;
  const ngf_pipeline_layout_info *layout;
} ngf_graphics_pipeline_info;

/**
 * Rendertarget attachment types.
 */
typedef enum {
  NGF_ATTACHMENT_COLOR = 0,
  NGF_ATTACHMENT_DEPTH,
  NGF_ATTACHMENT_STENCIL,
  NGF_ATTACHMENT_DEPTH_STENCIL
} ngf_attachment_type;

/**
 * What to do on attachment load.
 */
typedef enum {
  NGF_LOAD_OP_DONTCARE = 0,
  NGF_LOAD_OPKEEP,
  NGF_LOAD_OP_CLEAR
} ngf_attachment_load_op;

/**
 * Specifies information about a rendertarget attachment.
 */
typedef struct {
  ngf_image_ref image_ref; /**< Associated image subresource.*/
  ngf_attachment_type type; /**< Attachment type. */
} ngf_attachment;

/**
 * Specifies information about a rendertarget.
 */
typedef struct {
  const ngf_attachment *attachments;
  uint32_t nattachments;
} ngf_render_target_info;

/**
 * Framebuffer
 */
typedef struct ngf_render_target ngf_render_target;

/**
 * Types of buffer objects.
 */
typedef enum {
  NGF_BUFFER_TYPE_VERTEX = 0,
  NGF_BUFFER_TYPE_INDEX,
  NGF_BUFFER_TYPE_UNIFORM
} ngf_buffer_type;

// How frequently the buffer is accessed.
typedef enum {
  NGF_BUFFER_USAGE_STATIC = 0, // Written once, read many times.
  NGF_BUFFER_USAGE_DYNAMIC, // Written many times, read many times.
  NGF_BUFFER_USAGE_STREAM, // Written once, read a few times.
} ngf_buffer_access_freq;

// How the buffer is being used on the GPU side.
typedef enum {
  NGF_BUFFER_ACCESS_DRAW = 0, // Consumed by draw operations.
  NGF_BUFFER_ACCESS_READ, // Application reads from the buffer.
  NGF_BUFFER_ACCESS_COPY // Copy destination.
} ngf_buffer_access_type;

/**
 * Specifies information about a buffer object.
 */
typedef struct {
  size_t size; /**< Size in bytes. */
  ngf_buffer_type type; /**< Type of buffer (vertex/uniform/etc.)*/
  ngf_buffer_access_freq access_freq; /**< Access frequency.*/
  ngf_buffer_access_type access_type; /**< Access type.*/
} ngf_buffer_info;

/**
 * Vertex buffer bind operation parameters.
 */
typedef struct {
  const ngf_buffer *buffer; /**< Buffer object to bind.*/
  uint32_t offset; /**<  Offset at which to bind the buffer.*/
  uint32_t binding;  /**< Index of the binding.*/
} ngf_vertex_buf_bind_op;

/**
 * Index buffer binding parameter.
 */
typedef struct {
  const ngf_buffer *buffer; /**< Buffer object to bind.*/
  ngf_type type; /**< Type of elements in the index buffer. */
} ngf_index_buf_bind_op;

/**
 * Types of draw operations.
 */
typedef enum {
  NGF_DRAW_MODE_DIRECT = 0,
  NGF_DRAW_MODE_INDEXED,
  NGF_DRAW_MODE_DIRECT_INSTANCED,
  NGF_DRAW_MODE_INDEXED_INSTANCED
} ngf_draw_mode;

typedef struct {
  ngf_descriptor_set *set;
  uint32_t slot;
} ngf_descriptor_set_bind_op;

/**
 * Draw sub-operation sets up and makes a draw call.
 */
typedef struct {
  const ngf_dynamic_state_command *dynamic_state_cmds;
  uint32_t ndynamic_state_cmds;
  uint32_t ndescriptor_set_bind_ops;
  const ngf_descriptor_set_bind_op *descriptor_set_bind_ops;
  const ngf_vertex_buf_bind_op *vertex_buf_bind_ops;
  uint32_t nvertex_buf_bind_ops;
  ngf_draw_mode mode;
  const ngf_index_buf_bind_op *index_buf_bind_op;
  uint32_t first_element;
  uint32_t nelements;
  uint32_t ninstances;
} ngf_draw_subop_info; 

/**
 * Draw operation binds a pipeline and executes several drawcalls with it.
 */
typedef struct {
  const ngf_graphics_pipeline *pipeline;  /**< Pipeline to bind.*/
  const ngf_draw_subop_info *subops;  /**< Drawcalls to execute with the pipeline.*/
  uint32_t nsubops;  /**< Number of drawcalls.*/
} ngf_draw_op_info;

typedef struct ngf_draw_op ngf_draw_op;

/**
 * Specifies a rendertarget clear operation.
 */
typedef struct {
  float clear_color[4];
  float clear_depth;
  uint32_t clear_stencil;
} ngf_clear_info;

/**
 * Specifies a renderpass.
 * A renderpass is a series of drawcalls affecting a rendertarget, preceded by
 * an operation performed on all attachments of the rendertarget.
 */
typedef struct {
  const ngf_clear_info *clears; /**< An array of clear operation descriptions.
                                     Each entry in the array  corresponds to a
                                     rendertarget attachment. If the load op is
                                     not CLEAR, the entry is ignored, otherwise
                                     it is used to specify the details of the
                                     clear operation, such as the clear color
                                     for color attachments.*/
  const ngf_attachment_load_op *loadops; /**< Operation to perform on each
                                              corresponding attachment of the
                                              rendertarget*/
  uint32_t nloadops; /**< Number of attachments.*/
} ngf_pass_info;

/**
 * Renderpass object.
 */
typedef struct ngf_pass ngf_pass;

/**
 * Specifies host memory allocation callbacks for the library's internal needs.
 */
typedef struct {
  //TODO: specify alignments?
  void* (*allocate)(size_t obj_size, size_t nobjs);
  void (*free)(void *ptr, size_t obj_size, size_t nobjs);
} ngf_allocation_callbacks;

/**
 * Nicegraf rendering context.
 */
typedef struct ngf_context ngf_context;

/**
 * Configures a Nicegraf context.
 */
typedef struct {
  const ngf_swapchain_info *swapchain_info; /**< Configures the swapchain that the
                                                 context will be presenting to. This
                                                 can be NULL if all rendering is done
                                                 off-screen.*/
  const ngf_context *shared_context; /**< A reference to another context; the newly
                                    created context will have access to the
                                    other one's resources (such as buffers and
                                    textures) and vice versa. Can be NULL.*/
  bool debug; /**< Whether to enable debug features. */
} ngf_context_info;

/**
 * Set the memory allocation callbacks that the library will use for its
 * internal needs.
 * By default, stdlib's malloc and free are used.
 */
void ngf_set_allocation_callbacks(const ngf_allocation_callbacks *callbacks);

void ngf_debug_message_callback(void *userdata,
                                void (*callback)(const char*, const void*));

void ngf_insert_log_message(const char *message);

void ngf_begin_debug_group(const char *title);

void ngf_end_debug_group();

/**
 * Creates a shader stage from its description.
 * For certain back-ends that means compiling shaders from source.
 * On back-ends that compile shaders from source, the debug message callback
 * will be invoked on a compile or link error, if the callback had previously
 * been set.
 * On some back-ends, the full compile/link step might be deferred until later,
 * meaning any errors will be reported via callback when attempting to actually
 * use the shader stage.
 * @param stages A `ngf_shader_stage_info`s storing information (code,type) for
 *  the stage.
 * @param result Newly created stage will be stored here.
 * @return 
 */
ngf_error ngf_create_shader_stage(const ngf_shader_stage_info *info,
                                  ngf_shader_stage **result);

/**
 * Detsroys a given shader stage.
 */
void ngf_destroy_shader_stage(ngf_shader_stage *stage);

/**
 * Creates a new descriptor set layout object that can be used when constructing
 * a pipeline layout.
 * @param info shall point to a structure specifying the details of descriptor
 *  set layout.
 * @param result shall be initialized to point to a descriptor set layout object
 *  if the function succeeds.
 */
ngf_error ngf_create_descriptors_layout(const ngf_descriptors_layout_info *info,
                                        ngf_descriptors_layout **result);

/**
 * Destroy the given descriptor set layout object.
 */
void ngf_destroy_descriptors_layout(ngf_descriptors_layout *layout);

/**
 * Create a new descriptor set conforming to the given layout.
 */
ngf_error ngf_create_descriptor_set(const ngf_descriptors_layout *layout,
                                    ngf_descriptor_set **set);

/**
 * Destroy the given descriptor set.
 * @param set The descriptor set object to destroy.
 */
void ngf_destroy_descriptor_set(ngf_descriptor_set *set);

/**
 * Applies bind operations to a given descriptor set.
 * @param ops shall be a pointer to an array of bind operations.
 * @param nops shall be the number of elements in the array pointed to by `ops'.
 * @param set shall point to the dscriptor set object to which the bind
 *  operations need to be applied.
 */
ngf_error ngf_apply_descriptor_writes(const ngf_descriptor_write *writes,
                                      const uint32_t nwrites,
                                      ngf_descriptor_set *set);

/**
 * Creates a graphics pipeline object.
 * @param info Configuration for the graphics pipeline.
 */
ngf_error ngf_create_graphics_pipeline(const ngf_graphics_pipeline_info *info,
                                       ngf_graphics_pipeline **result);

/**
 * Destroys the given graphics pipeline object.
 */
void ngf_destroy_graphics_pipeline(ngf_graphics_pipeline *p);

/**
 * Creates a new image object.
 * @param info Configuration of the image.
 */
ngf_error ngf_create_image(const ngf_image_info *info, ngf_image **result);


/**
 * Upload data to the specified image subresource.
 * @param image the image to modify
 * @param level mip level to modify
 * @param offset within the mip level at which to write data
 * @param dimensions width and height of the written chunk
 * @param data buffer with the image data
 */
ngf_error ngf_populate_image(ngf_image *image,
                             uint32_t level,
                             ngf_offset3d offset,
                             ngf_extent3d dimensions,
                             const void *data);

/**
 * Destroy the given image object.
 */
void ngf_destroy_image(ngf_image *image);

/**
 * Create a sampler object with the given configuration.
 */
ngf_error ngf_create_sampler(const ngf_sampler_info *info,
                             ngf_sampler **result);

/**
 * Destroy a given sampler object.
 */
void ngf_destroy_sampler(ngf_sampler *sampler);

/**
 * Obtain a pointer to the default render target for the current context
 * (i.e. swapchain image).
 */
ngf_error ngf_default_render_target(ngf_render_target **result);

/**
 * Create a new rendertarget with the given configuration.
 */
ngf_error ngf_create_render_target(const ngf_render_target_info *info,
                                   ngf_render_target **result);

/**
 * Blit pixels from one render target to another. If the source render target
 * is multisampled, this will trigger an MSAA resolve.
 * @param src source rt
 * @param dst destination rt
 * @param src_rect the region to copy from the source rt
 */
ngf_error ngf_resolve_render_target(const ngf_render_target *src,
                                    ngf_render_target *dst,
                                    const ngf_irect2d *src_rect);

/**
 * Destroy the given render target.
 */
void ngf_destroy_render_target(ngf_render_target *target);

/**
 * Create a new buffer object.
 */
ngf_error ngf_create_buffer(const ngf_buffer_info *info, ngf_buffer **result);


/**
 * Upload data to the given buffer.
 * @param buf the buffer object to modify.
 * @param offset offset at which the data should be written.
 * @param size size of the data being uploaded in bytes.
 * @param data pointer to a buffer holding the data.
 */
ngf_error ngf_populate_buffer(ngf_buffer *buf,
                              size_t offset,
                              size_t size,
                              const void *data);

/**
 * Destroy the given buffer.
 */
void ngf_destroy_buffer(ngf_buffer *buffer);

/**
 * Create a new draw operation object.
 */
ngf_error ngf_create_draw_op(const ngf_draw_op_info *info,
                             ngf_draw_op **result);

/**
 * Destroy a given draw operation.
 */
void ngf_destroy_draw_op(ngf_draw_op *op);

/**
 * Create a render pass object.
 * @param info render pass configuration
 */
ngf_error ngf_create_pass(const ngf_pass_info *info, ngf_pass **result);

/**
 * Destroy a given pass.
 * @param pass the render pass object to destroy.
 */
void ngf_destroy_pass(ngf_pass *pass);

/**
 * Wait for all pending rendering commands to complete.
 */
void ngf_finish();

/**
 * Execute draw calls.
 * @param pass The render pass object that specifies load operation for all of
 *  the render target's attachments.
 * @param rt The rendertarget that will be affected by the draw calls. It must
 *  be compatible with the render pass.
 * @param drawops Pointer to an array of draw operation specifications.
 * @param ndrawops Number of elements in the array of draw operations.
 */
ngf_error ngf_execute_pass(const ngf_pass *pass,
                           const ngf_render_target *rt,
                           ngf_draw_op **drawops,
                           const uint32_t ndrawops);

/**
 * Initialize Nicegraf.
 * @param dev_pref specifies what type of GPU to prefer. Note that this setting
 *  might be ignored, depending on the backend.
 * @return Error codes: NGF_ERROR_INITIALIZATION_FAILED
 */
ngf_error ngf_initialize(ngf_device_preference dev_pref);

/**
 * Create a new Nicegraf context. Creates a new Nicegraf context that
 * can optionally present to a window surface and mutually share objects
 * (such as images and buffers) with another context.
 * @param info context configuration
 * @return Error codes: NGF_ERROR_CANT_SHARE_CONTEXT,
 *  NGF_ERROR_OUTOFMEM, NGF_ERROR_CONTEXT_CREATION_FAILED,
 *  NGF_ERROR_SWAPCHAIN_CREATION_FAILED
 */
ngf_error ngf_create_context(const ngf_context_info *info,
                             ngf_context **result);

/**
 * Destroy a Nicegraf context.
 * @param ctx The context to deallocate.
 */
void ngf_destroy_context(ngf_context *ctx);

/**
 * Adjust the size of a context's associated swapchain. This function must be
 * called every time the swapchain's underlying window is resized.
 * @param ctx The context to operate on
 * @param new_width New window width in pixels
 * @param new_height New window height in pixels
 * @return Error codes: NGF_ERROR_SWAPCHAIN_CREATION_FAILED, NGF_ERROR_OUTOFMEM
 */
ngf_error ngf_resize_context(ngf_context *ctx,
                             size_t new_width,
                             size_t new_height);

/**
 * Set a given Nicegraf context as current for the calling thread. All
 * subsequent rendering operations invoked from the calling thread will affect
 * the given context.
 * @param ctx The context to set as current.
 */
ngf_error ngf_set_context(ngf_context *ctx);

/**
 * Begin a frame of rendering. This functions starts a frame of rendering in
 * the given context. It acquires an image from the context's associated
 * swap chain. 
 * @param ctx The context to operate on.
 * @return Error codes: NGF_ERROR_BEGIN_FRAME_FAILED
 */
ngf_error ngf_begin_frame(ngf_context *ctx);

/**
 * End a frame of rendering. This function releases the image that was
 * previously acquired from the context's associated swapchain by
 * ngf_begin_frame. 
 * @param ctx The context to operate on.
 * @return Error codes: NGF_ERROR_END_FRAME_FAILED
 */
ngf_error ngf_end_frame(ngf_context *ctx);

#ifdef __cplusplus
}
#endif
