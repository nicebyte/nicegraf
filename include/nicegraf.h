/**
Copyright (c) 2018 nicegraf contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
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

#include "metadata_parser.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma region ngf_type_declarations

/**
 * Error codes.
 *
 * nicegraf functions report errors via return values. Results are stored in
 * output arguments.
 */
typedef enum ngf_error {
  NGF_ERROR_OK = 0, /**< No error, operation finished successfully. */
  NGF_ERROR_OUTOFMEM, /**< Host memory allocation failed. */
  NGF_ERROR_FAILED_TO_CREATE_PIPELINE,
  NGF_ERROR_INCOMPLETE_PIPELINE, /**< Some information required to create
                                      a pipeline object was not provided.*/
  NGF_ERROR_CANT_POPULATE_IMAGE, /**< Uploading data to image failed. */
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
  NGF_ERROR_OUT_OF_BOUNDS, /**< The operation would have resulted in an out of
                               bounds access. */
  /**
   * An attempt was made to start recording a command buffer that was already
   * in a recording state.
   */
  NGF_ERROR_CMD_BUFFER_ALREADY_RECORDING,

  /**
   * An attempt was made to finish recording a cmd buffer that was not
   * recording.
   */
  NGF_ERROR_CMD_BUFFER_WAS_NOT_RECORDING,
  NGF_ERROR_CONTEXT_ALREADY_CURRENT,
  NGF_ERROR_CALLER_HAS_CURRENT_CONTEXT,

  /** Specialization parameters were passed for a pipeline that uses shader
   stages created from binaries on a backend that does not support
   specialization constants natively. */
  NGF_ERROR_CANNOT_SPECIALIZE_SHADER_STAGE_BINARY,

  /** The specified binary format is not supported by the system.*/
  NGF_ERROR_SHADER_STAGE_INVALID_BINARY_FORMAT,

  /** Current backend does not support reading back shader stage binaries.*/
  NGF_ERROR_CANNOT_READ_BACK_SHADER_STAGE_BINARY,

  /** Current context has no default render target*/
  NGF_ERROR_NO_DEFAULT_RENDER_TARGET,

  /** Failed to acquire frame from the swapchain.*/
  NGF_ERROR_NO_FRAME,

  /** There was an attempt to write too little or too much data into a uniform
      buffer. */
  NGF_ERROR_UNIFORM_BUFFER_SIZE_MISMATCH,

  /** Indicates that the given image format is not supported by the active
      backend.*/
  NGF_ERROR_INVALID_IMAGE_FORMAT,

  /** Indicates that the given image format is not a valid depth format for
      the active backend.*/
  NGF_ERROR_INVALID_DEPTH_FORMAT,

  /** Given vertex attribute format is not supported by the active backend.*/
  NGF_ERROR_INVALID_VERTEX_ATTRIB_FORMAT,
  
  /** Give sampler address mode is not supported by the active backend. */
  NGF_ERROR_INVALID_SAMPLER_ADDRESS_MODE
  /*..add new errors above this line */
} ngf_error ;

/**
 * Device hints.
 */
typedef enum ngf_device_preference {
  NGF_DEVICE_PREFERENCE_DISCRETE, /**< Prefer discrete GPU. */
  NGF_DEVICE_PREFERENCE_INTEGRATED, /**< Prefer integrated GPU. */
  NGF_DEVICE_PREFERENCE_DONTCARE /**< No GPU preference. */
} ngf_device_preference;

/**
 * Possible present modes.
 */
typedef enum ngf_present_mode {
  NGF_PRESENTATION_MODE_FIFO, /**< Frames get queued ("wait for vsync") */
  NGF_PRESENTATION_MODE_IMMEDIATE /**< Doesn't wait for vsync */
} ngf_present_mode;

/**
 * Represents a rectangular, axis-aligned 2D region with integer coordinates.
 */
typedef struct ngf_irect2d {
  int32_t x; /**< X coord of lower-left corner. */
  int32_t y; /**< Y coord of lower-left corner. */
  uint32_t width;
  uint32_t height;
} ngf_irect2d;

/**
 * Represents a rectangular, axis-aligned 3D volume.
 */
typedef struct ngf_extent3d {
  uint32_t width;
  uint32_t height;
  uint32_t depth;
} ngf_extent3d ;

/**
 * Three-dimensional offset.
 */
typedef struct ngf_offset3d {
  int32_t x;
  int32_t y;
  int32_t z;
} ngf_offset3d ;

/**
 * Shader stage types.
 * Note that some back-ends might not support all of these.
 */
typedef enum ngf_stage_type {
  NGF_STAGE_VERTEX = 0,
  NGF_STAGE_FRAGMENT,
  // TODO: compute pipelines
  NGF_STAGE_COUNT
} ngf_stage_type;

/**
 * Describes a programmable shader stage.
 */
typedef struct ngf_shader_stage_info {
  ngf_stage_type type; /**< Stage type (vert/frag/etc.) */
  const char *content; /**< May be text or binary, depending on the backend.*/
  uint32_t content_length; /**< Number of bytes in the content buffer. */
  const char *debug_name; /**< Optional name, will appear in debug logs,
                               may be NULL.*/
  bool is_binary; /**< Indicates whether `content` is the source code, or a
                       binary blob. Backends that only support binaries (e.g.
                       Vulkan) ignore this flag. */

  /** 
   * Entry point name for this shader stage. On platforms that have fixed
   * entry point names (GL), this field gets ignored.
   */
  const char *entry_point_name;

  /** 
   * Indicates the binary format.The value is backend-specific:
   *   - On Vulkan, it should always be 0;
   *   - On Metal, 0 indicates a macOS binary, 1 indicates an iOS binary;
   *   - On OpenGL, it should be whatever `ngf_get_binary_shader_stage`
   *     returned when reading back the binary.
   */
  uint32_t binary_format;
} ngf_shader_stage_info;

/**
 * A programmable stage of the rendering pipeline.
 *
 * Programmable stages are specified using backend-specific blobs of
 * data. In general, there are two kinds of blobs - text and binary.
 * Backends may support either or both.
 *
 * Text blobs require a compilation step which may produce compile errors.
 * The detailed information about compile errors is reported via the debug
 * callback mechanism.
 * 
 * On some back-ends, the full compile/link step may be repeated during 
 * pipeline * creation (if using constant specialization). This does not apply
 * to back-ends that support specialization natively with no extensions (i.e.
 * Vulkan and Metal).
 *
 * Binary blobs have a notion of format, which is backend-specific. For
 * example, Metal binaries may come in iOS or macOS flavors.
 *
 * Some backends allow you to obtain the binary representation of the shader
 * stage even if it was specified using a text blob. If this binary is cached,
 * compilation can be skipped during the next run. Keep in mind that OpenGL
 * binaries are not transferable across machines.
 *
 * Note that on back-ends that do not support constant specialization natively
 * (i.e. OpenGL), it is impossible to specialize constants for a shader stage
 * that was created from a binary - it only works if the source code is
 * available.
 */
typedef struct ngf_shader_stage ngf_shader_stage;

/**
 * Ways to draw polygons.
 * Some back-ends might not support all of these.
  */
typedef enum ngf_polygon_mode {
  NGF_POLYGON_MODE_FILL = 0, /**< Fill entire polyoon.*/
  NGF_POLYGON_MODE_LINE, /**< Outline only.*/
  NGF_POLYGON_MODE_POINT, /**< Vertices only.*/
  NGF_POLYGON_MODE_COUNT
} ngf_polygon_mode;

/**
 * Which polygons to cull.
 */
typedef enum ngf_cull_mode {
  NGF_CULL_MODE_BACK = 0, /**< Cull back-facing polygons.*/
  NGF_CULL_MODE_FRONT, /**< Cull front-facing polygons. */
  NGF_CULL_MODE_FRONT_AND_BACK, /**< Cull all.*/
  NGF_CULL_MODE_NONE, /**< Never cull triangles. */
  NGF_CULL_MODE_COUNT
} ngf_cull_mode;

/**
 * Ways to determine front-facing polygons.
 */
typedef enum ngf_front_face_mode {
  NGF_FRONT_FACE_COUNTER_CLOCKWISE = 0, /**< CCW winding is front-facing.*/
  NGF_FRONT_FACE_CLOCKWISE, /**< CW winding is front-facing. */
  NGF_FRONT_FACE_COUNT
} ngf_front_face_mode;

/**
 * Rasterization stage parameters.
 */
typedef struct ngf_rasterization_info {
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
typedef enum ngf_compare_op {
  NGF_COMPARE_OP_NEVER = 0, /**< Comparison test never succeeds. */
  NGF_COMPARE_OP_LESS, /**< Comparison test succeeds if A < B. */
  NGF_COMPARE_OP_LEQUAL, /**< Comparison test succeeds if A <= B. */
  NGF_COMPARE_OP_EQUAL, /**< Comparison test succeeds if A == B. */
  NGF_COMPARE_OP_GEQUAL, /**< Comparison test succeeds if A >= B. */
  NGF_COMPARE_OP_GREATER, /**< Comparison test succeeds if A > B. */
  NGF_COMPARE_OP_NEQUAL, /**< Comparison test succeeds if A != B. */
  NGF_COMPARE_OP_ALWAYS, /**< Comparison test always succeeds. */
  NGF_COMPARE_OP_COUNT
} ngf_compare_op;

/**
 * Operations that can be performed on stencil buffer.
 */
typedef enum ngf_stencil_op {
  NGF_STENCIL_OP_KEEP = 0, /**< Don't touch.*/
  NGF_STENCIL_OP_ZERO, /**< Set to 0.*/
  NGF_STENCIL_OP_REPLACE, /**< Replace with reference value.*/
  NGF_STENCIL_OP_INCR_CLAMP, /**< Increment, clamping to max value.*/
  NGF_STENCIL_OP_INCR_WRAP, /**< Increment, wrapping to 0.*/
  NGF_STENCIL_OP_DECR_CLAMP, /**< Decrement, clamping to 0.*/
  NGF_STENCIL_OP_DECR_WRAP, /**< Decrement, wrapping to max value.*/
  NGF_STENCIL_OP_INVERT, /**< Bitwise invert*/
  NGF_STENCIL_OP_COUNT
} ngf_stencil_op ;

/**
 * Stencil operation description.
 */
typedef struct ngf_stencil_info {
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
typedef struct ngf_depth_stencil_info {
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
typedef enum ngf_blend_factor {
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
  NGF_BLEND_FACTOR_COUNT
} ngf_blend_factor;

/**
 * Pipeline's blend state description.
 */
typedef struct ngf_blend_info {
  bool enable; /**< Enable blending.*/
  ngf_blend_factor sfactor; /**< Source factor.*/
  ngf_blend_factor dfactor; /**< Destination factor.*/
} ngf_blend_info;

/**
 * Vertex attribute's input rate.
 */
typedef enum ngf_input_rate {
  NGF_INPUT_RATE_VERTEX = 0, /**< attribute changes per-vertex*/
  NGF_INPUT_RATE_INSTANCE, /**< attribute changes per-instance*/
  NGF_INPUT_RATE_COUNT
} ngf_input_rate;

/**
 * Vertex attribute component type.
 */
typedef enum ngf_type {
  NGF_TYPE_INT8 = 0,
  NGF_TYPE_UINT8,
  NGF_TYPE_INT16,
  NGF_TYPE_UINT16,
  NGF_TYPE_INT32,
  NGF_TYPE_UINT32,
  NGF_TYPE_FLOAT,
  NGF_TYPE_HALF_FLOAT,
  NGF_TYPE_DOUBLE,
  NGF_TYPE_COUNT
} ngf_type;

/**
 * Specifies an attribute binding.
 */
typedef struct ngf_vertex_buf_binding_desc {
  uint32_t binding;/**< Index of the binding that this structure describes.*/
  uint32_t stride; /**< Number of bytes between consecutive attribute values.*/
  ngf_input_rate input_rate; /**< Whether attributes read from this binding
                                  change per-vetex or per-instance.*/
} ngf_vertex_buf_binding_desc;

/**
 * Specifies information about a vertex attribute.
 */
typedef struct ngf_vertex_attrib_desc {
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
typedef struct ngf_vertex_input_info {
  /**
   * Pointer to array of structures describing the vertex buffer binding
   * used.
   */
  const ngf_vertex_buf_binding_desc *vert_buf_bindings; 
  uint32_t nvert_buf_bindings; /**< Number of vertex buffer bindings used.*/
  const ngf_vertex_attrib_desc *attribs; /**< Ptr to attrib descriptions.*/
  uint32_t nattribs; /**< Number of attribute descriptions.*/
} ngf_vertex_input_info;

/**
 * Specifies state of multisampling. 
 */
typedef struct ngf_multisample_info {
  bool multisample; /**< Whether to enable multisampling.*/
  bool alpha_to_coverage; /**< Whether alpha-to-coverage is enabled.*/
} ngf_multisample_info;

/**
 * Vertex data usage hints.
 *
 * These values hint the library how the vertex data (attributes and indices)
 * is intended to be consumed on the graphics device side.
 */
typedef enum ngf_vertex_data_usage {
  /**
   * This hint indicates that the vertex data is intended to be read many times
   * on the graphics device. An example usage scenario would be a character
   * mesh.
   */
  NGF_VERTEX_DATA_USAGE_STATIC,

  /**
   * This hint indicates that the vertex data is intended to be consumed once,
   * or very few times on the graphics device. An example would be displaying
   * "immediate-mode" GUI.
   */
  NGF_VERTEX_DATA_USAGE_TRANSIENT
 } ngf_vertex_data_usage;

 /**
  * Buffers can be populated directly by user-provided callbacks, potentially
  * obviating the need for additional host-side memory allocations.
  * The callback receives three parameters:
  *   - a pointer to a region of host-visible device memory, into which the
  *     user must write data (do not read from this buffer);
  *   - the size of the buffer;
  *   - a pointer to arbitrary user-provided data.
  */
 typedef void(*ngf_buffer_populate_callback)(volatile void *buffer, size_t, 
                                             void *userdata);

 /**
  * Vertex data description.
  *
  * This structure provides information necessary to create and populate a
  * vertex attribute or index buffer.
  */
typedef struct ngf_vertex_data_info {
  size_t buffer_size; /**< Buffer size in bytes. */

  /**
   * Pointer to the data that the buffer will be populated with. Set this to
   * `NULL` to populate the buffer using a callback instead.
   */
  const void *buffer_ptr;

  /**
   * This callback will be invoked to populate the buffer with data if
   * the \ref buffer_ptr field is set to `NULL` (otherwise this field is
   * ignored).
   */
  ngf_buffer_populate_callback fill_callback;

  /**
   * This pointer shall be passed to \ref fill_callback as its third parameter.
   */
  void *fill_callback_userdata;
  ngf_vertex_data_usage usage_hint;
 } ngf_vertex_data_info;

 typedef ngf_vertex_data_info ngf_attrib_buffer_info;
 typedef ngf_vertex_data_info ngf_index_buffer_info;

 /**
  * A vertex attribute buffer.
  */
 typedef struct ngf_attrib_buffer ngf_attrib_buffer;

 /**
  * A vertex index buffer.
  */
 typedef struct ngf_index_buffer ngf_index_buffer;

 /**
  * Specifies the data necessary for the creation of a uniform buffer.
  */
 typedef struct ngf_uniform_buffer_info {
   size_t size; /**< Size of the buffer in bytes. */
 } ngf_uniform_buffer_info;

 /**
  * A uniform buffer.
  */
 typedef struct ngf_uniform_buffer ngf_uniform_buffer;
/**
 * Possible image types.
 */
typedef enum ngf_image_type {
  NGF_IMAGE_TYPE_IMAGE_2D = 0,
  NGF_IMAGE_TYPE_IMAGE_3D,
  NGF_IMAGE_TYPE_COUNT
} ngf_image_type;

/**
 * Image formats.
 * Some backends may not support all of those.
 * Using an sRGB format in a color attachment or swapchain image means that all
 * color values output by the fragment stage are interpreted as being in linear
 * color space, and an appropriate transfer function is applied to them to
 * covert them to the sRGB colorspace before writing them to the target.
 * Using an sRGB format in a sampled image means that all color values stored
 * in the image are interpreted to be in the sRGB color space, and all read
 * operations automatically apply a transfer function to convert the values
 * from sRGB to linear color space.
 */
typedef enum ngf_image_format {
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
  NGF_IMAGE_FORMAT_UNDEFINED,
  NGF_IMAGE_FORMAT_COUNT
} ngf_image_format;

/**
 * Image usage flags.
 */
typedef enum ngf_image_usage {
  NGF_IMAGE_USAGE_SAMPLE_FROM = 0x01, /**< Can be read from in a shader.*/
  NGF_IMAGE_USAGE_ATTACHMENT = 0x02   /**< Can be used as an attachment for a
                                           render target.*/
} ngf_image_usage;

/**
 * Describes an image.
 */
typedef struct ngf_image_info {
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
 * Rendertarget attachment types.
 */
typedef enum ngf_attachment_type {
  NGF_ATTACHMENT_COLOR = 0,
  NGF_ATTACHMENT_DEPTH,
  NGF_ATTACHMENT_STENCIL,
  NGF_ATTACHMENT_DEPTH_STENCIL
} ngf_attachment_type;

/**
 * What to do on attachment load.
 */
typedef enum ngf_attachment_load_op {
  NGF_LOAD_OP_DONTCARE = 0,
  NGF_LOAD_OP_KEEP,
  NGF_LOAD_OP_CLEAR,
  NGF_LOAD_OP_COUNT
} ngf_attachment_load_op;

/**
 * Specifies a rendertarget clear operation.
 */
typedef union ngf_clear_info {
  float clear_color[4];
  float clear_depth;
  uint32_t clear_stencil;
} ngf_clear;

/**
 * Specifies information about a rendertarget attachment.
 */
typedef struct ngf_attachment {
  ngf_image_ref image_ref; /**< Associated image subresource.*/
  ngf_attachment_type type; /**< Attachment type. */

  /**
   * Indicates what to do with this attachment at the beginning of a render
   * pass.
   */
  ngf_attachment_load_op load_op;

  /**
   * If the `load_op` field  is NGF_LOAD_OP_CLEAR, contains the value(s) to
   * clear the attachment to, otherwise this field is ignored.
   */
  ngf_clear clear;
 } ngf_attachment;

/**
 * Specifies information about a rendertarget.
 */
typedef struct ngf_render_target_info {
  const ngf_attachment *attachments;
  uint32_t nattachments;
} ngf_render_target_info;

/**
 * Framebuffer
 */
typedef struct ngf_render_target ngf_render_target;


/**
 * Swapchain configuration.
 */
typedef struct ngf_swapchain_info {
  ngf_image_format cfmt; /**< Swapchain image format. */
  ngf_image_format dfmt; /**< Format to use for the depth buffer, if set to
                              NGF_IMAGE_FORMAT_UNDEFINED, no depth buffer will
                              be created. */
  int nsamples; /**< Number of samples per pixel (0 for non-multisampled) */
  uint32_t capacity_hint; /**< Number of images in swapchain (may be ignored)*/
  uint32_t width; /**< Width of swapchain images in pixels. */
  uint32_t height;/**< Height of swapchain images in pixels. */
  uintptr_t native_handle;/**< HWND, ANativeWindow, NSWindow, etc. */
  ngf_present_mode present_mode; /**< Desired present mode. */
} ngf_swapchain_info;

/**
 *  Min/mag filters.
 */
typedef enum {
  NGF_FILTER_NEAREST = 0,
  NGF_FILTER_LINEAR,
  NGF_FILTER_COUNT
} ngf_sampler_filter;

/**
 * What to do when sampling out-of-bounds.
 */
typedef enum {
  NGF_WRAP_MODE_CLAMP_TO_EDGE = 0,
  NGF_WRAP_MODE_CLAMP_TO_BORDER,
  NGF_WRAP_MODE_REPEAT,
  NGF_WRAP_MODE_MIRRORED_REPEAT,
  NGF_WRAP_MODE_COUNT
} ngf_sampler_wrap_mode;

/**
 * Describes a sampler object.
 */
typedef struct {
  ngf_sampler_filter min_filter; /**< Minification filter.*/
  ngf_sampler_filter mag_filter; /**< Magnification filter.*/
  ngf_sampler_filter mip_filter; /**< Mipmap filter. */
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
  NGF_DESCRIPTOR_TEXTURE,
  NGF_DESCRIPTOR_SAMPLER,
  NGF_DESCRIPTOR_TEXTURE_AND_SAMPLER,
  NGF_DESCRIPTOR_TYPE_COUNT
} ngf_descriptor_type;

/**
 * Descriptor visibility flags.
 */
typedef enum ngf_descriptor_stage {
  NGF_DESCRIPTOR_VERTEX_STAGE_BIT = 0x01,
  NGF_DESCRIPTOR_FRAGMENT_STAGE_BIT = 0x02,
} ngf_descriptor_stage_flags;

/**
 * Descriptor configuration.
 * A descriptor is an opaque object through which resources such as buffers and
 * textures can be accessed.
 * This structure specifies details about a descriptor.
 */
typedef struct {
  ngf_descriptor_type type; /**< Type of the descriptor.*/
  uint32_t id; /**< Associated binding point.*/

  /**
   * Specifies which stage(s) the descriptor is accessible from.
   * This needs to be a combination of \ref ngf_descriptor_stage flags.
   */
  uint32_t stage_flags; 
} ngf_descriptor_info;

/**
 * Descriptor set layout configuration.
 * Descriptors are bound in groups called descriptor sets.
 * The number of descriptors in such a group, and their types and
 * associated bindings are called a "descriptor set layout".
 * Think of it as a schema for a descriptor set.
 */
typedef struct {
  const ngf_descriptor_info *descriptors;
  uint32_t ndescriptors;
} ngf_descriptor_set_layout_info;

/**
 * Pipeline layout description.
 * Specifies layouts for descriptor sets that are required to be bound by a
 * pipeline.
 */
typedef struct {
  uint32_t ndescriptor_set_layouts;
  ngf_descriptor_set_layout_info *descriptor_set_layouts;
} ngf_pipeline_layout_info;

/**
 * Specifies a buffer bind operation.
 */
typedef struct {
  const ngf_uniform_buffer *buffer; /**< Which buffer to bind.*/
  size_t offset; /**< Offset at which to bind the buffer.*/
  size_t range;  /**< Bound range.*/
} ngf_uniform_buffer_bind_info;

/**
 * Specifies an image bind operation.
 */
typedef struct {
  ngf_image_ref image_subresource; /**< Image portion to bind.*/
  const ngf_sampler *sampler; /**< Sampler to use.*/
} ngf_image_sampler_bind_info;

typedef struct {
  uint32_t target_set;
  uint32_t target_binding;
  ngf_descriptor_type type;
  union {
    ngf_uniform_buffer_bind_info uniform_buffer;
    ngf_image_sampler_bind_info image_sampler;
  } info;
} ngf_resource_bind_op;

/**
 * Graphics pipeline object.
 */
typedef struct ngf_graphics_pipeline ngf_graphics_pipeline;

/** 
 * Flags for specifying which aspects of the pipeline are to be configured
 * as dynamic.
 */
typedef enum ngf_dynamic_state_flags {
  NGF_DYNAMIC_STATE_VIEWPORT = 0x01,
  NGF_DYNAMIC_STATE_SCISSOR = 0x02,
  NGF_DYNAMIC_STATE_LINE_WIDTH = 0x04,
  NGF_DYNAMIC_STATE_BLEND_CONSTANTS = 0x08,
  NGF_DYNAMIC_STATE_STENCIL_REFERENCE = 0x10,
  NGF_DYNAMIC_STATE_STENCIL_COMPARE_MASK = 0x20,
  NGF_DYNAMIC_STATE_STENCIL_WRITE_MASK = 0x40,
  NGF_DYNAMIC_STATE_VIEWPORT_AND_SCISSOR = NGF_DYNAMIC_STATE_SCISSOR |
                                           NGF_DYNAMIC_STATE_VIEWPORT
} ngf_dynamic_state_flags;

/**
 * Primitive types to use for draw operations.
 * Some back-ends may not support all of the primitive types.
 */
typedef enum ngf_primitive_type {
  NGF_PRIMITIVE_TYPE_TRIANGLE_LIST = 0,
  NGF_PRIMITIVE_TYPE_TRIANGLE_STRIP,
  NGF_PRIMITIVE_TYPE_TRIANGLE_FAN,
  NGF_PRIMITIVE_TYPE_LINE_LIST,
  NGF_PRIMITIVE_TYPE_LINE_STRIP,
  NGF_PRIMITIVE_TYPE_COUNT
} ngf_primitive_type;

/**
 * A constant specialization entry, sets the value for a single
 * specialization constant.
 */
typedef struct ngf_constant_specialization {
  uint32_t constant_id; /**< ID of the specialization constant used in the
                             shader stage */
  uint32_t offset; /**< Offset at which the user-provided value is stored in
                        the specialization buffer. */
  ngf_type type; /**< Type of the specialization constant. */
} ngf_constant_specialization;

/**
 * Sets specialization constant values for a pipeline.
 */
typedef struct ngf_specialization_info {
  ngf_constant_specialization  *specializations; /**< List of specialization
                                                      entries. */
  uint32_t nspecializations; /**< Number of specialization entries. */
  void *value_buffer; /**< Pointer to a buffer containing the values for the
                           specialization constants. */
} ngf_specialization_info;

/**
 * Specifies information for creation of a graphics pipeline.
 */
typedef struct ngf_graphics_pipeline_info {
  const ngf_shader_stage *shader_stages[5];
  uint32_t nshader_stages;
  const ngf_irect2d *viewport;
  const ngf_irect2d *scissor;
  const ngf_rasterization_info *rasterization;
  const ngf_multisample_info *multisample;
  const ngf_depth_stencil_info *depth_stencil;
  const ngf_blend_info *blend;
  uint32_t dynamic_state_mask;
  const ngf_vertex_input_info *input_info;
  ngf_primitive_type primitive_type;
  const ngf_pipeline_layout_info *layout;
  const ngf_specialization_info *spec_info;
  const ngf_render_target *compatible_render_target;
  const ngf_plmd_cis_map *image_to_combined_map;
  const ngf_plmd_cis_map *sampler_to_combined_map;
} ngf_graphics_pipeline_info;

/**
 * Specifies host memory allocation callbacks for the library's internal needs.
 */
typedef struct ngf_allocation_callbacks {
  //TODO: specify alignments?
  void* (*allocate)(size_t obj_size, size_t nobjs);
  void (*free)(void *ptr, size_t obj_size, size_t nobjs);
} ngf_allocation_callbacks;

/**
 * Nicegraf rendering context.
 * 
 * A context represents the internal state of the library that is required for
 * performing most of the library's functionality. This includes, but is not
 * limited to: presenting rendered content in a window; creating and managing
 * resources, such as images, buffers and command buffers; recording and
 * submitting command buffers.
 *
 * Most operations, with the exception of `ngf_init` and context management
 * functions, require a context to be "current" on the calling thread.
 *
 * Invoking `ngf_set_context` will make a context current on the calling
 * thread. Once a context is made current on a thread, it cannot be migrated to
 * another thread.
 *
 * The results of using resources created within one context, in another 
 * context are undefined, unless the two contexts are explicitly configured to
 * share data. When contexts are configured as shared, resources created in one
 * can be used in the other, and vice versa. Notably, command buffers created
 * and recorded in one context, can be submitted in another, shared context.
 *
 * A context mainatins exclusive ownership of its swapchain (if it has one),
 * and even shared contexts cannot acquire, present or render to images from
 * that swapchain.
 */
typedef struct ngf_context ngf_context;

/**
 * Configures a Nicegraf context.
 */
typedef struct ngf_context_info {
  /**
   * Configures the swapchain that the context will be presenting to. This
   * can be NULL if all rendering is done off-screen.
   */
  const ngf_swapchain_info *swapchain_info;

  /**
   * A reference to another context; the newly created context will have access
   * to the other one's resources (such as buffers and textures) and vice versa
   * Can be NULL.
   */
  const ngf_context *shared_context;
  bool debug; /**< Whether to enable debug features. */
} ngf_context_info;

typedef struct ngf_cmd_buffer_info {
  uint32_t flags; /**< Reserved for future use. */
} ngf_cmd_buffer_info;

/**
 * Encodes a series of rendering commands.
 *
 * Internally, a command buffer may be in any of the following four states:
 *   - reset;
 *   - recording;
 *   - ready;
 *   - submitted.
 *
 * Every newly created command buffer is in the "reset" state.
 * When a command buffer is in the "reset" state, it is ready for a new
 * series of rendering commands to be recorded into it.
 *
 * Once the recording begins (via a call to \ref ngf_cmd_buffer_start), the
 * command buffer transitions into the "recording" state.
 *
 * Recording commands into a command buffer is done by calling `ngf_cmd_*` 
 * (i.e. \ref ngf_cmd_bind_pipeline etc.). All command-recording functions
 * require the command buffer to be in the "recording" state.
 *
 * Once all the commands have been recorded, the buffer needs to be
 * transitioned into the "ready" state via a call to \ref ngf_cmd_buffer_end.
 *
 * Only command buffers in the "ready" state may be submitted for execution
 * (via a call to \ref ngf_cmd_buffer_submit), which transitions them into the
 * "submitted" state.
 *
 * The results of re-submitting a command buffer that is already in the
 * "submitted" state are undefined.
 *
 * Once a command buffer is in either "ready" or "submitted" state, it is
 * impossible to append new commands to it. It is, however, possible to begin
 * recording a new, separate batch of commands by calling
 * \ref ngf_cmd_buffer_start which implicitly transitions the buffer to the
 * "reset" state if it is ready or submitted.
 *
 * Command buffers may be recorded in parrallel on different threads. Recording
 * and destroying a command buffer must always be done by the same thread that
 * created it.
 *
 * Submitting a command buffer for execution may be done by a different thread,
 * as long as the submitting and recording threads have shared contexts.
 */
typedef struct ngf_cmd_buffer ngf_cmd_buffer;

#pragma endregion

#pragma region ngf_function_declarations 
/**
 * Set the memory allocation callbacks that the library will use for its
 * internal needs.
 * By default, stdlib's malloc and free are used.
 */
void ngf_set_allocation_callbacks(const ngf_allocation_callbacks *callbacks);

void ngf_debug_message_callback(void *userdata,
                                void (*callback)(const char*, const void*));

/**
 * Create a shader stage from its description.
 *
 * @param stages A `ngf_shader_stage_info` storing the content and other data
 *  for the stage.
 * @param result Newly created stage will be stored here.
 * @return 
 */
ngf_error ngf_create_shader_stage(const ngf_shader_stage_info *info,
                                  ngf_shader_stage **result);

/**
 * Obtain the size (in bytes) of the shader stage's corresponding binary.
 * 
 * @param stage The stage to obtain the size of.
 * @param size Result will be written here.
 */
ngf_error ngf_get_binary_shader_stage_size(const ngf_shader_stage *stage,
                                           size_t *size);

/**
 * Obtain the shader stage's binary.
 * 
 * @param stage The stage to obtain the binary for.
 * @param buf_size Maximum amount of bytes to write into `buffer`.
 * @param buffer A pointer to the beginning of the buffer into which the bytes
 *  will be written. The buffer must be able to store at least `buf_size`
 *  bytes.
 * @param format A backend-specific format code will be written here (see
 *   comments for `ngf_shader_stage_info`.
 */
 ngf_error ngf_get_binary_shader_stage(const ngf_shader_stage *stage,
                                       size_t buf_size, void *buffer,
                                       uint32_t *format);
/**
 * Detsroys a given shader stage.
 */
void ngf_destroy_shader_stage(ngf_shader_stage *stage);

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
 * Obtain a render target associated with the the current context's
 * swapchain. If the current context does not have a swapchain, the result will
 * be a null pointer. Otherwise, it will be a render target that has a
 * color attachment associated with the context's swapchain. If the swapchain
 * was created with an accompanying depth buffer, the render target will
 * have an attachment for that as well.
 *
 * It is the responsibility of the caller to free the returned render target 
 * object. Doing so does not affect the swapchain.
 *
 * @param color_load_op specifies the operation to perform on the color
 *  attachment the render target at the beginning of a rend render pass.
 * @param depth_load_op if the context's swap chain has an accompanying depth
 *  buffer, specifies the operation to perform on it at the beginning of a
 *  render pass. Otherwise, this value is ignored.
 * @param clear_color If `color_load_op` is `NGF_LOAD_OP_CLEAR`, specifies the
 *  color to clear the color attachment of the render target to. Otherwise,
    this value is ignored.
 * @param clear_depth If the current context's swapchain has an accompanying
 *   depth buffer AND the `depth_load_op` parameter of this function was set to
 *   `NGF_LOAD_OP_CLEAR`, this paramener specifies the value to clear the depth
 *   buffer to. Otherwise, this parameter is ignored.
 *
 */
ngf_error ngf_default_render_target(
  ngf_attachment_load_op color_load_op,
  ngf_attachment_load_op depth_load_op,
  const ngf_clear *clear_color,
  const ngf_clear *clear_depth,
  ngf_render_target **result);

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
 * Creates a new vertex attribute buffer.
 * @param info see \ref ngf_attrib_buffer_info
 * @param result the new buffer handle will be stored here.
 */
ngf_error ngf_create_attrib_buffer(const ngf_attrib_buffer_info *info,
                                   ngf_attrib_buffer **result);

/**
 * Discards the given vertex attribute buffer.
 */
void ngf_destroy_attrib_buffer(ngf_attrib_buffer *buffer);

/**
 * Creates a new vertex index buffer.
 * @param info see \ref ngf_index_buffer_info
 * @param result the new buffer handle will be stored here.
 */
ngf_error ngf_create_index_buffer(const ngf_index_buffer_info *info,
                                  ngf_index_buffer **result);
/**
 * Discards the given vertex index buffer.
 */
void ngf_destroy_index_buffer(ngf_index_buffer *buffer);

/**
 * Creates a new uniform buffer.
 * @param info see \ref ngf_uniform_buffer_info
 * @param result the new buffer handle will be stored here.
 */
ngf_error ngf_create_uniform_buffer(const ngf_uniform_buffer_info *info,
                                     ngf_uniform_buffer **result);
/**
 * Discards the given uniform buffer.
 */
void ngf_destroy_uniform_buffer(ngf_uniform_buffer *buf);

/**
 * Fill the given uniform buffer with data.
 * @param buffer the target uniform buffer.
 * @param data A pointer to the source buffer. Exaclty `N` bytes will be read
               from here, where `N` is the size of the target uniform buffer.
 * @param size This parameter is for sanity checking. If it is not equal to the
 *             size of the target uniform buffer, no data will be written and
 *             an error will be generated.
 */
ngf_error ngf_write_uniform_buffer(ngf_uniform_buffer *buffer,
                                   const void *data,
                                   size_t size);

/**
 * Wait for all pending rendering commands to complete.
 */
void ngf_finish();

/**
 * Creates a new command buffer that is in the "reset" state.
 */
ngf_error ngf_create_cmd_buffer(const ngf_cmd_buffer_info *info,
                                ngf_cmd_buffer **result);

/**
 * Frees resources associated with the given command buffer.
 * Any work previously submitted via this command buffer will be finished
 * asynchronously.
 */
void ngf_destroy_cmd_buffer(ngf_cmd_buffer *buffer);

/**
 * Begin recording a new batch of commands into a given command buffer,
 * transitioning it into "recording" state.
 * The command buffer must be either "reset", "ready" or "submitted".
 * If the command buffer is either "ready" or "submitted", its state is
 * implicitly transitioned to "reset" before becoming "recording".
 * Note that if the buffer was "ready", the commands previously recorded into
 * it will be lost and never executed.
 */
ngf_error ngf_start_cmd_buffer(ngf_cmd_buffer *buf);

/**
 * Finishes recording a batch of commands into a buffer and transitions it into
 * the "ready" state.
 * The command buffer needs to be in the "recording" state.
 */
ngf_error ngf_end_cmd_buffer(ngf_cmd_buffer *buf);

/**
 * Submits the commands recorded in the given command buffer for execution.
 * The command buffer must be in the "ready" state, and will be transitioned to
 * the "submitted" state.
 */
ngf_error ngf_submit_cmd_buffer(uint32_t nbuffers, ngf_cmd_buffer **bufs);

void ngf_cmd_bind_pipeline(ngf_cmd_buffer *buf,
                           const ngf_graphics_pipeline *pipeline);
void ngf_cmd_viewport(ngf_cmd_buffer *buf, const ngf_irect2d *r);
void ngf_cmd_scissor(ngf_cmd_buffer *buf, const ngf_irect2d *r);
void ngf_cmd_stencil_reference(ngf_cmd_buffer *buf, uint32_t front,
                               uint32_t back);
void ngf_cmd_stencil_compare_mask(ngf_cmd_buffer *buf, uint32_t front,
                                  uint32_t back);
void ngf_cmd_stencil_write_mask(ngf_cmd_buffer *buf, uint32_t front,
                                uint32_t back);
void ngf_cmd_line_width(ngf_cmd_buffer *buf, float line_width);
void ngf_cmd_blend_factors(ngf_cmd_buffer *buf,
                           ngf_blend_factor sfactor,
                           ngf_blend_factor dfactor);
void ngf_cmd_bind_resources(ngf_cmd_buffer *buf,
                            const ngf_resource_bind_op *bind_operations,
                            uint32_t nbind_operations);
void ngf_cmd_bind_attrib_buffer(ngf_cmd_buffer *buf,
                                const ngf_attrib_buffer *vbuf,
                                uint32_t binding, uint32_t offset);
void ngf_cmd_bind_index_buffer(ngf_cmd_buffer *buf,
                               const ngf_index_buffer *idxbuf,
                               ngf_type index_type);
void ngf_cmd_begin_pass(ngf_cmd_buffer *buf, const ngf_render_target *target);
void ngf_cmd_end_pass(ngf_cmd_buffer *buf);
void ngf_cmd_draw(ngf_cmd_buffer *buf, bool indexed,
                  uint32_t first_element, uint32_t nelements,
                  uint32_t ninstances);

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
                             uint32_t new_width,
                             uint32_t new_height);

/**
 * Set a given Nicegraf context as current for the calling thread. All
 * subsequent rendering operations invoked from the calling thread will affect
 * the given context.
 *
 * Once a context has been set as current on a thread, it cannot be migrated to
 * another thread.
 *
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
#pragma endregion

#ifdef __cplusplus
}
#endif
