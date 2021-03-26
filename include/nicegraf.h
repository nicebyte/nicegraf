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

/**
 * @file nicegraf.h
 * @brief nicegraf declarations.
 */
#pragma once

#include "metadata_parser.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#define NGF_NOEXCEPT noexcept
#else
#define NGF_NOEXCEPT
#endif

#define NGF_VER_MAJ 0
#define NGF_VER_MIN 0

#ifdef _MSC_VER
#pragma region ngf_type_declarations
#endif

/**
 * Contains information about various device features, limits, etc.
 */
typedef struct ngf_device_capabilities {
  /**
   * This flag is set to `true` if the platform supports [0; 1]
   * range for the clip-space z coordinate. We enforce clip-space
   * z to be in this range on all platforms that support it.
   */
  bool clipspace_z_zero_to_one;

  /**
   * When binding uniform buffers, the specified offset must be
   * a multiple of this number.
   */
  size_t uniform_buffer_offset_alignment;
} ngf_device_capabilities;

/**
 * Device hints.
 * TODO: API for choosing device explicitly.
 */
typedef enum ngf_device_preference {
  NGF_DEVICE_PREFERENCE_DISCRETE,   /**< Prefer discrete GPU. */
  NGF_DEVICE_PREFERENCE_INTEGRATED, /**< Prefer integrated GPU. */
  NGF_DEVICE_PREFERENCE_DONTCARE    /**< No GPU preference. */
} ngf_device_preference;

/**
 * Verbosity levels for the diagnostic message log.
 */
typedef enum ngf_diagnostic_log_verbosity {
  NGF_DIAGNOSTICS_VERBOSITY_DEFAULT, /**< Normal level, reports only severe
                                        errors. */
  NGF_DIAGNOSTICS_VERBOSITY_DETAILED /**< Recommended for debug builds, may
                                        induce perf overhead. */
} ngf_diagnostic_log_verbosity;

/**
 * Type of a diagnostic log entry.
 */
typedef enum ngf_diagnostic_message_type {
  NGF_DIAGNOSTIC_INFO,    /**< Informational message, not actionalble. */
  NGF_DIAGNOSTIC_WARNING, /**< Message warns of a potential issue with an API
                             call. */
  NGF_DIAGNOSTIC_ERROR    /**< Message provides details of an API call failure or a
                             severe performance issue. */
} ngf_diagnostic_message_type;

/**
 * Detailed information about errors may vary from platform to platform.
 * nicegraf reports errors through a combination of generic return codes and
 * detailed human-readable information passed to a user-provided callback
 * function. The callback function must accept the diagnostic message type, an
 * arbitrary void pointer (which the user may specify), a printf-style format
 * string, and an arbitrary number of arguments specifying the data for the
 * format-string.
 */
typedef void (*ngf_diagnostic_callback)(ngf_diagnostic_message_type, void*, const char*, ...);

/**
 * Diagnostic configuration.
 */
typedef struct ngf_diagnostic_info {
  ngf_diagnostic_log_verbosity verbosity; /**< Diagnostic log verbosity. */
  void*                        userdata;  /**< Arbitrary pointer that will
                                               be passed as-is to the
                                               callback. */
  ngf_diagnostic_callback callback;       /**< Pointer to the diagnostic
                                               message callback function.*/
} ngf_diagnostic_info;

/**
 * nicegraf initialization parameters.
 */
typedef struct ngf_init_info {
  ngf_device_preference device_pref; /**< Which type of device to prefer.
                                          May be ignored, depending on the backend.
                                      */
  ngf_diagnostic_info diag_info;     /**< Diagnostic log configuration. */
} ngf_init_info;

/**
 * Error codes.
 *
 * nicegraf functions report errors via return values. Results are stored in
 * output arguments.
 */
typedef enum ngf_error {
  NGF_ERROR_OK = 0,     /**< No error, operation finished successfully. */
  NGF_ERROR_OUT_OF_MEM, /**< Host memory allocation failed. */
  NGF_ERROR_OBJECT_CREATION_FAILED,
  NGF_ERROR_OUT_OF_BOUNDS, /**< The operation would have resulted in an out of
                              bounds access. */
  NGF_ERROR_INVALID_FORMAT,
  NGF_ERROR_INVALID_SIZE,
  NGF_ERROR_INVALID_ENUM,
  NGF_ERROR_INVALID_OPERATION
  /*..add new errors above this line */
} ngf_error;

/**
 * Possible present modes.
 */
typedef enum ngf_present_mode {
  NGF_PRESENTATION_MODE_FIFO,     /**< Frames get queued ("wait for vsync") */
  NGF_PRESENTATION_MODE_IMMEDIATE /**< Doesn't wait for vsync */
} ngf_present_mode;

/**
 * Represents a rectangular, axis-aligned 2D region with integer coordinates.
 */
typedef struct ngf_irect2d {
  int32_t  x; /**< X coord of lower-left corner. */
  int32_t  y; /**< Y coord of lower-left corner. */
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
} ngf_extent3d;

/**
 * Three-dimensional offset.
 */
typedef struct ngf_offset3d {
  int32_t x;
  int32_t y;
  int32_t z;
} ngf_offset3d;

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
  ngf_stage_type type;           /**< Stage type (vert/frag/etc.) */
  const void*    content;        /**< May be text or binary, depending on the backend.*/
  uint32_t       content_length; /**< Number of bytes in the content buffer. */
  const char*    debug_name;     /**< Optional name, will appear in debug logs,
                                      may be NULL.*/
  /**
   * Entry point name for this shader stage. On platforms that have fixed
   * entry point names (GL), this field gets ignored.
   */
  const char* entry_point_name;
} ngf_shader_stage_info;

/**
 * A programmable stage of the rendering pipeline.
 *
 * Programmable stages are specified using backend-specific blobs of
 * data.
 *
 * On platforms that require a compilation step, details about compile errors
 * are reported via the debug callback mechanism.
 *
 * On some back-ends, the full compile/link step may be repeated during
 * pipeline creation (if using constant specialization). This does not apply
 * to back-ends that support specialization natively with no extensions (i.e.
 * Vulkan and Metal).
 */
typedef struct ngf_shader_stage_t* ngf_shader_stage;

/**
 * Ways to draw polygons.
 * Some back-ends might not support all of these.
 */
typedef enum ngf_polygon_mode {
  NGF_POLYGON_MODE_FILL = 0, /**< Fill entire polyoon.*/
  NGF_POLYGON_MODE_LINE,     /**< Outline only.*/
  NGF_POLYGON_MODE_POINT,    /**< Vertices only.*/
  NGF_POLYGON_MODE_COUNT
} ngf_polygon_mode;

/**
 * Which polygons to cull.
 */
typedef enum ngf_cull_mode {
  NGF_CULL_MODE_BACK = 0,       /**< Cull back-facing polygons.*/
  NGF_CULL_MODE_FRONT,          /**< Cull front-facing polygons. */
  NGF_CULL_MODE_FRONT_AND_BACK, /**< Cull all.*/
  NGF_CULL_MODE_NONE,           /**< Never cull triangles. */
  NGF_CULL_MODE_COUNT
} ngf_cull_mode;

/**
 * Ways to determine front-facing polygons.
 */
typedef enum ngf_front_face_mode {
  NGF_FRONT_FACE_COUNTER_CLOCKWISE = 0, /**< CCW winding is front-facing.*/
  NGF_FRONT_FACE_CLOCKWISE,             /**< CW winding is front-facing. */
  NGF_FRONT_FACE_COUNT
} ngf_front_face_mode;

/**
 * Rasterization stage parameters.
 */
typedef struct ngf_rasterization_info {
  bool discard;                     /**< Enable/disable rasterizer discard. Use in pipelines that
                                         don't write fragment data.*/
  ngf_polygon_mode    polygon_mode; /**< How to draw polygons.*/
  ngf_cull_mode       cull_mode;    /**< Which polygons to cull.*/
  ngf_front_face_mode front_face;   /**< Which winding counts as front-facing.*/
} ngf_rasterization_info;

/**
 * Compare operations used in depth and stencil tests.
 */
typedef enum ngf_compare_op {
  NGF_COMPARE_OP_NEVER = 0, /**< Comparison test never succeeds. */
  NGF_COMPARE_OP_LESS,      /**< Comparison test succeeds if A < B. */
  NGF_COMPARE_OP_LEQUAL,    /**< Comparison test succeeds if A <= B. */
  NGF_COMPARE_OP_EQUAL,     /**< Comparison test succeeds if A == B. */
  NGF_COMPARE_OP_GEQUAL,    /**< Comparison test succeeds if A >= B. */
  NGF_COMPARE_OP_GREATER,   /**< Comparison test succeeds if A > B. */
  NGF_COMPARE_OP_NEQUAL,    /**< Comparison test succeeds if A != B. */
  NGF_COMPARE_OP_ALWAYS,    /**< Comparison test always succeeds. */
  NGF_COMPARE_OP_COUNT
} ngf_compare_op;

/**
 * Operations that can be performed on stencil buffer.
 */
typedef enum ngf_stencil_op {
  NGF_STENCIL_OP_KEEP = 0,   /**< Don't touch.*/
  NGF_STENCIL_OP_ZERO,       /**< Set to 0.*/
  NGF_STENCIL_OP_REPLACE,    /**< Replace with reference value.*/
  NGF_STENCIL_OP_INCR_CLAMP, /**< Increment, clamping to max value.*/
  NGF_STENCIL_OP_INCR_WRAP,  /**< Increment, wrapping to 0.*/
  NGF_STENCIL_OP_DECR_CLAMP, /**< Decrement, clamping to 0.*/
  NGF_STENCIL_OP_DECR_WRAP,  /**< Decrement, wrapping to max value.*/
  NGF_STENCIL_OP_INVERT,     /**< Bitwise invert*/
  NGF_STENCIL_OP_COUNT
} ngf_stencil_op;

/**
 * Stencil operation description.
 */
typedef struct ngf_stencil_info {
  ngf_stencil_op fail_op;       /**< What to do on stencil test fail.*/
  ngf_stencil_op pass_op;       /**< What to do on pass.*/
  ngf_stencil_op depth_fail_op; /**< When depth fails but stencil pass.*/
  ngf_compare_op compare_op;    /**< Stencil comparison function.*/
  uint32_t       compare_mask;  /**< Compare mask.*/
  uint32_t       write_mask;    /**< Write mask.*/
  uint32_t       reference;     /**< Reference value (used for REPLACE).*/
} ngf_stencil_info;

/**
 * Pipeline's depth/stencil state description.
 */
typedef struct ngf_depth_stencil_info {
  float            min_depth;     /** Near end of the depth range. */
  float            max_depth;     /** Far end of the depth range. */
  bool             depth_test;    /**< Enable depth test.*/
  bool             depth_write;   /**< Enable writes to depth buffer.*/
  ngf_compare_op   depth_compare; /**< Depth comparison function.*/
  bool             stencil_test;  /**< Enable stencil test.*/
  ngf_stencil_info front_stencil; /**< Stencil op for front-facing polys*/
  ngf_stencil_info back_stencil;  /**< Stencil op for back-facing polys*/
} ngf_depth_stencil_info;

/**
 * Blend factors.
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
 * Blend operations.
 */
typedef enum ngf_blend_op {
  NGF_BLEND_OP_ADD,
  NGF_BLEND_OP_SUB,
  NGF_BLEND_OP_REV_SUB,
  NGF_BLEND_OP_MIN,
  NGF_BLEND_OP_MAX,
  NGF_BLEND_OP_COUNT
} ngf_blend_op;

/**
 * Pipeline's blend state description.
 * The result of blend is computed for color and alpha separately,
 * according to the following equation:
 *  source*sfactor OP dest*dfactor
 * Where:
 *  - `source` is the incoming value, `dest` is the value already in the frame
 *     buffer.
 *  - `OP` is one of the supported blend operations.
 *  - `sfactor` and `dfactor` are one of the supported blend factors.
 */
typedef struct ngf_blend_info {
  bool             enable;                 /**< Enable blending.*/
  ngf_blend_op     blend_op_color;         /**< Blend operation to perform for color. */
  ngf_blend_op     blend_op_alpha;         /**< Blend operation to perform for alpha. */
  ngf_blend_factor src_color_blend_factor; /**< Source blend factor for color. */
  ngf_blend_factor dst_color_blend_factor; /**< Destination blend factor for color. */
  ngf_blend_factor src_alpha_blend_factor; /**< Source blend factor for alpha. */
  ngf_blend_factor dst_alpha_blend_factor; /**< Destination blend factor for alpha. */
  float            blend_color[4];         /**< Blend color. */
} ngf_blend_info;

/**
 * Vertex attribute's input rate.
 */
typedef enum ngf_input_rate {
  NGF_INPUT_RATE_VERTEX = 0, /**< attribute changes per-vertex*/
  NGF_INPUT_RATE_INSTANCE,   /**< attribute changes per-instance*/
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
  uint32_t       binding;    /**< Index of the binding that this structure describes.*/
  uint32_t       stride;     /**< Number of bytes between consecutive attribute values.*/
  ngf_input_rate input_rate; /**< Whether attributes read from this binding
                                  change per-vetex or per-instance.*/
} ngf_vertex_buf_binding_desc;

/**
 * Specifies information about a vertex attribute.
 */
typedef struct ngf_vertex_attrib_desc {
  uint32_t location;   /**< Attribute index. */
  uint32_t binding;    /**< Which vertex buffer binding to use.*/
  uint32_t offset;     /**< Offset in the buffer at which attribute data starts.*/
  ngf_type type;       /**< Type of attribute component.*/
  uint32_t size;       /**< Number of attribute components.*/
  bool     normalized; /**< Whether attribute values should be normalized.*/
} ngf_vertex_attrib_desc;

/**
 * Specifies information about the pipeline's vertex input.
 */
typedef struct ngf_vertex_input_info {
  /**
   * Pointer to array of structures describing the vertex buffer binding
   * used.
   */
  const ngf_vertex_buf_binding_desc* vert_buf_bindings;
  uint32_t                      nvert_buf_bindings; /**< Number of vertex buffer bindings used.*/
  const ngf_vertex_attrib_desc* attribs;            /**< Ptr to attrib descriptions.*/
  uint32_t                      nattribs;           /**< Number of attribute descriptions.*/
} ngf_vertex_input_info;

typedef enum ngf_sample_count {
  NGF_SAMPLE_COUNT_1 = 1,
  NGF_SAMPLE_COUNT_2 = 2,
  NGF_SAMPLE_COUNT_4 = 4,
  NGF_SAMPLE_COUNT_8 = 8,
  NGF_SAMPLE_COUNT_16 = 16,
  NGF_SAMPLE_COUNT_32 = 32,
  NGF_SAMPLE_COUNT_64 = 64,
} ngf_sample_count;

/**
 * Specifies state of multisampling.
 */
typedef struct ngf_multisample_info {
  ngf_sample_count sample_count;      /**< MSAA sample count. */
  bool             alpha_to_coverage; /**< Whether alpha-to-coverage is enabled.*/
} ngf_multisample_info;

/**
 * Types of memory backing a buffer object.
 */
typedef enum ngf_buffer_storage_type {
  /**
   * Memory that can be read by the host.
   */
  NGF_BUFFER_STORAGE_HOST_READABLE,

  /**
   * Memory that can be written to by the host.
   */
  NGF_BUFFER_STORAGE_HOST_WRITEABLE,
  /**
   * Memory that can be both read from and written to by the
   * host.
   */
  NGF_BUFFER_STORAGE_HOST_READABLE_WRITEABLE,

  /**
   * Private memory cannot be accessed by the host directly. The contents of a
   * buffer backed by this type of memory can only be modified by executing a
   * `ngf_cmd_copy_xxxxx_buffer`.
   */
  NGF_BUFFER_STORAGE_PRIVATE
} ngf_buffer_storage_type;

/**
 * Flags for clarifying how the buffer is intended to be used.
 */
typedef enum ngf_buffer_usage {
  NGF_BUFFER_USAGE_XFER_SRC = 0x01, /** < Buffer may be used as a source for
                                          transfer operations.
                                      */
  NGF_BUFFER_USAGE_XFER_DST = 0x02, /** < Buffer may be used as a destination
                                          for transfer operations.
                                      */
} ngf_buffer_usage;

/**
 * Information required for buffer creation.
 */
typedef struct ngf_buffer_info {
  size_t                  size;
  ngf_buffer_storage_type storage_type;
  uint32_t                buffer_usage;
} ngf_buffer_info;

/**
 * Options for mapping a buffer to host memory.
 */
typedef enum ngf_buffer_map_flags {
  NGF_BUFFER_MAP_READ_BIT  = 0x01, /** < Mapped memory is readable. */
  NGF_BUFFER_MAP_WRITE_BIT = 0x02, /** < Mapped memory is writeable. */
} ngf_buffer_map_flags;

/**
 * Specifies the data necessary for the creation of a uniform buffer.
 */
typedef ngf_buffer_info ngf_uniform_buffer_info;

/**
 * A uniform buffer.
 */
typedef struct ngf_uniform_buffer_t* ngf_uniform_buffer;

/**
 * Possible usages of a pixel buffer object.
 */
typedef enum ngf_pixel_buffer_usage {
  // TODO: NGF_PIXEL_BUFFER_USAGE_READ
  /**
   * This value indicates that a pixel buffer object will be used by the
   * application to transfer pixel data to the device.
   */
  NGF_PIXEL_BUFFER_USAGE_WRITE
} ngf_pixel_buffer_usage;

/**
 * Information required to create a pixel buffer object.
 */
typedef struct ngf_pixel_buffer_info {
  size_t                 size;  /**< Size in bytes. */
  ngf_pixel_buffer_usage usage; /**< Intended usage. */
} ngf_pixel_buffer_info;

/**
 * Pixel buffers are used to transfer pixel data between the host and the
 * device. They can be memory-mapped, similar to other types of buffers backed
 * by host-visible storage, and their contents can be transferred to images via
 * \ref ngf_cmd_write_image.
 */
typedef struct ngf_pixel_buffer_t* ngf_pixel_buffer;

/**
 * Possible image types.
 */
typedef enum ngf_image_type {
  NGF_IMAGE_TYPE_IMAGE_2D = 0,
  NGF_IMAGE_TYPE_IMAGE_3D,
  NGF_IMAGE_TYPE_CUBE,
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
  NGF_IMAGE_FORMAT_RG11B10F,
  NGF_IMAGE_FORMAT_R16_UNORM,
  NGF_IMAGE_FORMAT_R16_SNORM,
  NGF_IMAGE_FORMAT_R16U,
  NGF_IMAGE_FORMAT_R16S,
  NGF_IMAGE_FORMAT_RG16U,
  NGF_IMAGE_FORMAT_RGB16U,
  NGF_IMAGE_FORMAT_RGBA16U,
  NGF_IMAGE_FORMAT_R32U,
  NGF_IMAGE_FORMAT_RG32U,
  NGF_IMAGE_FORMAT_RGB32U,
  NGF_IMAGE_FORMAT_RGBA32U,
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
  NGF_IMAGE_USAGE_ATTACHMENT  = 0x02, /**< Can be used as an attachment for a
                                           render target.*/
  NGF_IMAGE_USAGE_XFER_DST = 0x04 /**< Can be used as a destination for a transfer operation. **/
} ngf_image_usage;

/**
 * Describes an image.
 */
typedef struct ngf_image_info {
  ngf_image_type type;
  ngf_extent3d   extent;         /**< Width, height and depth (for 3d images) or no. of
                                      layers (for layered images).*/
  uint32_t         nmips;        /**< Number of mip levels.*/
  ngf_image_format format;       /**< Internal format.*/
  ngf_sample_count sample_count; /**< Number of samples. **/
  uint32_t         usage_hint;   /**< How the client intends to use the image. Must be a
                                      combination of image usage flags.*/
} ngf_image_info;

typedef struct ngf_image_t* ngf_image;

/**
 * Indicates the face of a cubemap.
 */
typedef enum ngf_cubemap_face {
  NGF_CUBEMAP_FACE_POSITIVE_X,
  NGF_CUBEMAP_FACE_NEGATIVE_X,
  NGF_CUBEMAP_FACE_POSITIVE_Y,
  NGF_CUBEMAP_FACE_NEGATIVE_Y,
  NGF_CUBEMAP_FACE_POSITIVE_Z,
  NGF_CUBEMAP_FACE_NEGATIVE_Z,
  NGF_CUBEMAP_FACE_COUNT
} ngf_cubemap_face;

/**
 * Reference to a part of an image.
 */
typedef struct {
  ngf_image        image;        /**< Image being referred to.*/
  uint32_t         mip_level;    /**< Mip level within the image.*/
  uint32_t         layer;        /**< Layer within the image.*/
  ngf_cubemap_face cubemap_face; /**< Face of the cubemap, ignored for
                                      non-cubemap textures.*/
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
  NGF_LOAD_OP_DONTCARE = 0, /**< Don't care what happens. */
  NGF_LOAD_OP_KEEP,         /**< Preserve the prior contents of the attachment. */
  NGF_LOAD_OP_CLEAR,        /**< Clear attachment. */
  NGF_LOAD_OP_COUNT
} ngf_attachment_load_op;

/**
 * What to do on attachment store.
 */
typedef enum ngf_attachment_store_op {
  /**
   * Don't care what happens. Use this if you don't plan on reading back the
   * contents of the attachment in any shaders or presenting it to screen.
   */
  NGF_STORE_OP_DONTCARE = 0,

  /**
   * Make sure the contents is written out to system memory. Use this if you
   * plan on reading the contents of the attachment in any shaders or
   * presenting it to screen.
   */
  NGF_STORE_OP_STORE,

  NGF_STORE_OP_COUNT
} ngf_attachment_store_op;

/**
 * Specifies a rendertarget clear operation.
 */
typedef union ngf_clear_info {
  float    clear_color[4];
  struct {
    float    clear_depth;
    uint32_t clear_stencil;
  } clear_depth_stencil;
} ngf_clear;

/**
 * Specifies information about a rendertarget attachment.
 */
typedef struct ngf_attachment {
  ngf_image_ref       image_ref; /**< Associated image subresource.*/
  ngf_attachment_type type;      /**< Attachment type. */

  /**
   * Indicates what to do with this attachment at the beginning of a render
   * pass.
   */
  ngf_attachment_load_op load_op;

  /**
   * Indicates what to do with this attachment at the end of a render pass.
   */
  ngf_attachment_store_op store_op;

  /**
   * If the `load_op` field  is NGF_LOAD_OP_CLEAR, contains the value(s) to
   * clear the attachment to, otherwise this field is ignored.
   */
  ngf_clear clear;
} ngf_attachment;

/**
 * Describes the type and format of a render target attachment. 
 */
typedef struct ngf_attachment_description {
  ngf_attachment_type type;         /**< What the attachment shall be used for. */
  ngf_image_format    format;       /**< Format of the associated image. */
  ngf_sample_count    sample_count; /**< Number of samples per pixel in the associated image. */
  bool                is_sampled;   /**< Whether this attachment's associated image is sampled from a
                                         shader at any point. */
} ngf_attachment_description;

/**
 * A list of attachment descriptions.
 */
typedef struct ngf_attachment_descriptions {
  uint32_t                          nattachments;
  const ngf_attachment_description* attachment_descriptions;
} ngf_attachment_descriptions;

/**
 * Specifies information about a rendertarget.
 */
typedef struct ngf_render_target_info {
  const ngf_attachment_descriptions* attachment_descriptions; /**< List of attachment descriptions
                                                                   for this render target. */
  const ngf_image_ref*               attachment_image_refs;   /**< Image references corresponding
                                                                   to each attachment in the list. */
} ngf_render_target_info;

/**
 * Framebuffer
 */
typedef struct ngf_render_target_t* ngf_render_target;

/**
 * Information about a render pass.
 */
typedef struct ngf_pass_info {
  ngf_render_target              render_target;
  const ngf_attachment_load_op*  load_ops;
  const ngf_attachment_store_op* store_ops;
  const ngf_clear*               clears;
} ngf_pass_info;


/**
 * Swapchain configuration.
 */
typedef struct ngf_swapchain_info {
  ngf_image_format color_format;  /**< Swapchain image format. */
  ngf_image_format depth_format;  /**< Format to use for the depth buffer, if set to NGF_IMAGE_FORMAT_UNDEFINED, no depth buffer will be created. */
  ngf_sample_count sample_count;  /**< Number of samples per pixel (0 for non-multisampled) */
  uint32_t         capacity_hint; /**< Number of images in swapchain (may be ignored)*/
  uint32_t         width;         /**< Width of swapchain images in pixels. */
  uint32_t         height;        /**< Height of swapchain images in pixels. */
  uintptr_t        native_handle; /**< HWND, ANativeWindow, NSWindow, etc. */
  ngf_present_mode present_mode;  /**< Desired present mode. */
} ngf_swapchain_info;

/**
 *  Min/mag filters.
 */
typedef enum { NGF_FILTER_NEAREST = 0, NGF_FILTER_LINEAR, NGF_FILTER_COUNT } ngf_sampler_filter;

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
  ngf_sampler_filter    min_filter; /**< Minification filter.*/
  ngf_sampler_filter    mag_filter; /**< Magnification filter.*/
  ngf_sampler_filter    mip_filter; /**< Mipmap filter. */
  ngf_sampler_wrap_mode wrap_s;     /**< Horizontal wrap mode. */
  ngf_sampler_wrap_mode wrap_t;
  ngf_sampler_wrap_mode wrap_r;
  float                 lod_max;         /**< Max mip level.*/
  float                 lod_min;         /**< Min mip level.*/
  float                 lod_bias;        /**< Level bias.*/
  float                 border_color[4]; /**< Border color.*/
  /** Max number of samples allowed for anisotropic filtering.*/
  float max_anisotropy;
  bool  enable_anisotropy; /**< Whether to allow anisotropic filtering. */
} ngf_sampler_info;

/**
 * Sampler object.
 */
typedef struct ngf_sampler_t* ngf_sampler;

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
  NGF_DESCRIPTOR_VERTEX_STAGE_BIT   = 0x01,
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
  uint32_t            id;   /**< Associated binding point.*/

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
  const ngf_descriptor_info* descriptors;
  uint32_t                   ndescriptors;
} ngf_descriptor_set_layout_info;

/**
 * Pipeline layout description.
 * Specifies layouts for descriptor sets that are required to be bound by a
 * pipeline.
 */
typedef struct {
  uint32_t                        ndescriptor_set_layouts;
  ngf_descriptor_set_layout_info* descriptor_set_layouts;
} ngf_pipeline_layout_info;

/**
 * Specifies a buffer bind operation.
 */
typedef struct {
  ngf_uniform_buffer buffer; /**< Which buffer to bind.*/
  size_t             offset; /**< Offset at which to bind the buffer.*/
  size_t             range;  /**< Bound range.*/
} ngf_uniform_buffer_bind_info;

/**
 * Specifies an image bind operation.
 */
typedef struct {
  ngf_image_ref image_subresource; /**< Image portion to bind.*/
  ngf_sampler   sampler;           /**< Sampler to use.*/
} ngf_image_sampler_bind_info;

typedef struct {
  uint32_t            target_set;
  uint32_t            target_binding;
  ngf_descriptor_type type;
  union {
    ngf_uniform_buffer_bind_info uniform_buffer;
    ngf_image_sampler_bind_info  image_sampler;
  } info;
} ngf_resource_bind_op;

/**
 * Graphics pipeline object.
 */
typedef struct ngf_graphics_pipeline_t* ngf_graphics_pipeline;

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
  uint32_t offset;      /**< Offset at which the user-provided value is stored in
                             the specialization buffer. */
  ngf_type type;        /**< Type of the specialization constant. */
} ngf_constant_specialization;

/**
 * Sets specialization constant values for a pipeline.
 */
typedef struct ngf_specialization_info {
  ngf_constant_specialization* specializations; /**< List of specialization
                                                     entries. */
  uint32_t nspecializations;                    /**< Number of specialization entries. */
  void*    value_buffer; /**< Pointer to a buffer containing the values for the
                              specialization constants. */
} ngf_specialization_info;

/**
 * Specifies information for creation of a graphics pipeline.
 */
typedef struct ngf_graphics_pipeline_info {
  ngf_shader_stage                   shader_stages[5];
  uint32_t                           nshader_stages;
  const ngf_rasterization_info*      rasterization;
  const ngf_multisample_info*        multisample;
  const ngf_depth_stencil_info*      depth_stencil;
  const ngf_blend_info*              blend;
  uint32_t                           dynamic_state_mask;
  const ngf_vertex_input_info*       input_info;
  ngf_primitive_type                 primitive_type;
  const ngf_pipeline_layout_info*    layout;
  const ngf_specialization_info*     spec_info;
  const ngf_attachment_descriptions* compatible_render_target;
  const ngf_plmd_cis_map*            image_to_combined_map;
  const ngf_plmd_cis_map*            sampler_to_combined_map;
} ngf_graphics_pipeline_info;

/**
 * Specifies host memory allocation callbacks for the library's internal needs.
 */
typedef struct ngf_allocation_callbacks {
  // TODO: specify alignments?
  void* (*allocate)(size_t obj_size, size_t nobjs);
  void (*free)(void* ptr, size_t obj_size, size_t nobjs);
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
typedef struct ngf_context_t* ngf_context;

/**
 * Configures a Nicegraf context.
 */
typedef struct ngf_context_info {
  /**
   * Configures the swapchain that the context will be presenting to. This
   * can be NULL if all rendering is done off-screen.
   */
  const ngf_swapchain_info* swapchain_info;

  /**
   * A reference to another context; the newly created context will have access
   * to the other one's resources (such as buffers and textures) and vice versa
   * Can be NULL.
   */
  const ngf_context shared_context;
} ngf_context_info;

typedef struct ngf_cmd_buffer_info {
  uint32_t flags; /**< Reserved for future use. */
} ngf_cmd_buffer_info;

/**
 * Encodes a series of rendering commands.
 *
 * Internally, a command buffer may be in any of the following five states:
 *   - new;
 *   - ready;
 *   - recording;
 *   - awaiting submission;
 *   - submitted.
 *
 * Every newly created command buffer is in the "new" state. It can be transitioned
 * to the "ready" state by calling `ngf_start_cmd_buffer` on it.
 * 
 * When a command buffer is in the "ready" state, you may begin recording a new
 * series of rendering commands into it.
 *
 * Recording commands into a command buffer is performed using command
 * encoders. There are a few different types of encoders, supporting different
 * types of commands.
 *
 * A new encoder may be created for a command buffer only if the command buffer
 * is in either the "ready" or the "awaiting submission" state.
 *
 * Creating a new encoder for a command buffer transitions that command buffer
 * to the "recording" state.
 *
 * Finishing and disposing of an active encoder transitions its corresponding
 * command buffer into the "awaiting submission" state.
 *
 * The three rules above mean that a command buffer may not have more than
 * one encoder active at a given time.
 *
 * Once all of the desired commands have been recorded, and the command buffer is
 * in the "awaiting submission" state, the command buffer may be
 * submitted for execution via a call to \ref ngf_cmd_buffer_submit, which
 * transitions it into the "submitted" state.

 * Submission may only be performed on command buffers that are in the
 * "awaiting submission" state.
 *
 * Once a command buffer is in the "submitted" state, it is
 * impossible to append any new commands to it.
 * It is, however, possible to begin recording a new, completely separate batch
 * of commands by calling \ref ngf_cmd_buffer_start which implicitly
 * transitions the buffer to the "ready" state if it is already "submitted".
 * This does not affect any previously submitted commands. 
 *
 * Calling a command buffer function on a buffer that is in a state not
 * expected by that function will result in an error. For example, calling
 * \ref ngf_cmd_buffer_submit would produce an error on a buffer that is in
 * the "ready" state, since, according to the rules outlined above,
 * \ref ngf_cmd_buffer_submit expects command buffers to be in the "awaiting
 * submission" state.
 *
 * Command buffers may be recorded in parrallel on different threads. Recording
 * and destroying a command buffer must always be done by the same thread that
 * created it.
 *
 * Submitting a command buffer for execution may be done by a different thread,
 * as long as the submitting and recording threads have shared contexts.
 *
 * Access to command buffer objects from different threads must be synchronized
 * by the application. In other words, it falls on the application to ensure
 * that no two threads ever access the same command buffer simultaneously.
 */
typedef struct ngf_cmd_buffer_t* ngf_cmd_buffer;

/**
 * A render encoder records rendering commands (such as draw calls) into its
 * corresponding command buffer.
 */
typedef struct {
  uintptr_t __handle;
} ngf_render_encoder;

/**
 * A transfer encoder records transfer commands (i.e. copying buffer contents)
 * into its corresponding command buffer.
 */
typedef struct {
  uintptr_t __handle;
} ngf_xfer_encoder;

typedef ngf_buffer_info ngf_attrib_buffer_info;
typedef ngf_buffer_info ngf_index_buffer_info;

/**
 * A vertex attribute buffer.
 */
typedef struct ngf_attrib_buffer_t* ngf_attrib_buffer;

/**
 * A vertex index buffer.
 */
typedef struct ngf_index_buffer_t* ngf_index_buffer;

/**
 * A token identifying a frame of rendering.
 */
typedef uint32_t ngf_frame_token;

#ifdef _MSC_VER
#pragma endregion

#pragma region ngf_function_declarations
#endif

/**
 * Initialize nicegraf.
 * @param init_info Initialization parameters.
 * @return Error codes: NGF_ERROR_INITIALIZATION_FAILED
 */
ngf_error ngf_initialize(const ngf_init_info* init_info) NGF_NOEXCEPT;

/**
 * Create a new nicegraf context. Creates a new Nicegraf context that
 * can optionally present to a window surface and mutually share objects
 * (such as images and buffers) with another context.
 * @param info context configuration
 * @return Error codes: NGF_ERROR_CANT_SHARE_CONTEXT,
 *  NGF_ERROR_OUTOFMEM, NGF_ERROR_CONTEXT_CREATION_FAILED,
 *  NGF_ERROR_SWAPCHAIN_CREATION_FAILED
 */
ngf_error ngf_create_context(const ngf_context_info* info, ngf_context* result) NGF_NOEXCEPT;

/**
 * Destroy a nicegraf context.
 * @param ctx The context to deallocate.
 */
void ngf_destroy_context(ngf_context ctx) NGF_NOEXCEPT;

/**
 * Adjust the size of a context's associated swapchain. This function must be
 * called every time the swapchain's underlying window is resized.
 * @param ctx The context to operate on
 * @param new_width New window width in pixels
 * @param new_height New window height in pixels
 * @return Error codes: NGF_ERROR_SWAPCHAIN_CREATION_FAILED, NGF_ERROR_OUTOFMEM
 */
ngf_error ngf_resize_context(ngf_context ctx, uint32_t new_width, uint32_t new_height) NGF_NOEXCEPT;

/**
 * Set a given nicegraf context as current for the calling thread. All
 * subsequent rendering operations invoked from the calling thread will affect
 * the given context.
 *
 * Once a context has been set as current on a thread, it cannot be migrated to
 * another thread.
 *
 * @param ctx The context to set as current.
 */
ngf_error ngf_set_context(ngf_context ctx) NGF_NOEXCEPT;

/**
 * Begin a frame of rendering.
 * This function starts a frame of rendering in
 * the calling thread's current context. It generates a frame token, which
 * is required for recording command buffers.
 * @param token The generated frame token shall be returned here.
 * @return Error codes: NGF_ERROR_BEGIN_FRAME_FAILED, NGF_ERROR_NO_FRAME
 */
ngf_error ngf_begin_frame(ngf_frame_token* token) NGF_NOEXCEPT;

/**
 * End a frame of rendering on the calling thread's context.
 * @param token The frame token generated by the corresponding call to ngf_begin_frame.
 * @return Error codes: NGF_ERROR_END_FRAME_FAILED
 */
ngf_error ngf_end_frame(ngf_frame_token token) NGF_NOEXCEPT;

/**
 * Get a pointer to the device capabilities structure.
 * @return NULL if no context is present on the calling thread.
 */
const ngf_device_capabilities* ngf_get_device_capabilities(void) NGF_NOEXCEPT;

/**
 * Set the memory allocation callbacks that the library will use for its
 * internal needs.
 * By default, stdlib's malloc and free are used.
 */
void ngf_set_allocation_callbacks(const ngf_allocation_callbacks* callbacks) NGF_NOEXCEPT;

/**
 * Create a shader stage from its description.
 *
 * @param stages A `ngf_shader_stage_info` storing the content and other data
 *  for the stage.
 * @param result Newly created stage will be stored here.
 * @return
 */
ngf_error
ngf_create_shader_stage(const ngf_shader_stage_info* info, ngf_shader_stage* result) NGF_NOEXCEPT;

/**
 * Detsroys a given shader stage.
 */
void ngf_destroy_shader_stage(ngf_shader_stage stage) NGF_NOEXCEPT;

/**
 * Creates a graphics pipeline object.
 * @param info Configuration for the graphics pipeline.
 */
ngf_error ngf_create_graphics_pipeline(
    const ngf_graphics_pipeline_info* info,
    ngf_graphics_pipeline*            result) NGF_NOEXCEPT;

/**
 * Destroys the given graphics pipeline object.
 */
void ngf_destroy_graphics_pipeline(ngf_graphics_pipeline p) NGF_NOEXCEPT;

/**
 * Creates a new image object.
 * @param info Configuration of the image.
 */
ngf_error ngf_create_image(const ngf_image_info* info, ngf_image* result) NGF_NOEXCEPT;

/**
 * Destroy the given image object.
 */
void ngf_destroy_image(ngf_image image) NGF_NOEXCEPT;

/**
 * Create a sampler object with the given configuration.
 */
ngf_error ngf_create_sampler(const ngf_sampler_info* info, ngf_sampler* result) NGF_NOEXCEPT;

/**
 * Destroy a given sampler object.
 */
void ngf_destroy_sampler(ngf_sampler sampler) NGF_NOEXCEPT;

/**
 * Obtain a render target associated with the the current context's
 * swapchain. If the current context does not have a swapchain, the result shall
 * be a null pointer. Otherwise, it shall be a render target that has a
 * color attachment associated with the context's swapchain. If the swapchain
 * was created with an accompanying depth buffer, the render target shall
 * have an attachment for that as well.
 *
 * Calling this function multiple times within the same context returns the handle
 * to the same object.
 * 
 * The caller should not attempt to destroy the returned render target. It shall
 * be destroyed automatically, together with the parent context.
 */
ngf_error ngf_default_render_target(ngf_render_target* result) NGF_NOEXCEPT;

/**
 * Create a new rendertarget with the given configuration.
 */
ngf_error ngf_create_render_target(const ngf_render_target_info* info, ngf_render_target* result)
    NGF_NOEXCEPT;

/**
 * Blit pixels from one render target to another. If the source render target
 * is multisampled, this will trigger an MSAA resolve.
 * @param src source rt
 * @param dst destination rt
 * @param src_rect the region to copy from the source rt
 */
ngf_error ngf_resolve_render_target(
    const ngf_render_target src,
    ngf_render_target       dst,
    const ngf_irect2d*      src_rect) NGF_NOEXCEPT;

/**
 * Destroy the given render target.
 */
void ngf_destroy_render_target(ngf_render_target target) NGF_NOEXCEPT;

/**
 * Creates a new vertex attribute buffer.
 * @param info see \ref ngf_attrib_buffer_info
 * @param result the new buffer handle will be stored here.
 */
ngf_error ngf_create_attrib_buffer(const ngf_attrib_buffer_info* info, ngf_attrib_buffer* result)
    NGF_NOEXCEPT;

/**
 * Discards the given vertex attribute buffer.
 */
void ngf_destroy_attrib_buffer(ngf_attrib_buffer buffer) NGF_NOEXCEPT;

/**
 * Map a region of a given buffer to host memory.
 * It is an error to bind a mapped buffer using any command. If a buffer that
 * needs to be bound is mapped, first call \ref ngf_buffer_flush_range to ensure
 * any new data in the mapped range becomes visible to the subsequent commands,
 * then call \ref ngf_buffer_unmap. Then it shall be safe to bind the buffer.
 * Writing into any region that could be in use by previously submitted commands
 * results in undefined behavior.
 * @param buf The buffer to be mapped.
 * @param offset Offset at which the mapped region starts, in bytes. It must
 *               satisfy platform-specific alignment requirements.
 * @param size  Size of the mapped region in bytes.
 * @param flags A combination of flags from \ref ngf_buffer_map_flags.
 * @return A pointer to the mapped memory, or NULL if the buffer could not be
 *         mapped.
 */
void* ngf_attrib_buffer_map_range(ngf_attrib_buffer buf, size_t offset, size_t size, uint32_t flags)
    NGF_NOEXCEPT;
/**
 * Ensure that any new data in the mapped range will be visible to subsequently
 * submitted commands.
 * @param ptr A \ref ngf_buffer_ptr object wrapping a handle to the buffer that
 *            needs to be flushed.
 * @param offset Offset, relative to the start of the mapped range, at which
 *               the flushed region starts, in bytes.
 * @param size  Size of the flushed region in bytes.
 */
void ngf_attrib_buffer_flush_range(ngf_attrib_buffer buf, size_t offset, size_t size) NGF_NOEXCEPT;

/**
 * Unmaps a previously mapped buffer. The pointer returned for that buffer
 * by the corresponding call to \ref ngf_buffer_map_range becomes invalid.
 * @param buf The buffer that needs to be unmapped.
 */
void ngf_attrib_buffer_unmap(ngf_attrib_buffer buf) NGF_NOEXCEPT;

/**
 * Creates a new vertex index buffer.
 * @param info see \ref ngf_index_buffer_info
 * @param result the new buffer handle will be stored here.
 */
ngf_error
ngf_create_index_buffer(const ngf_index_buffer_info* info, ngf_index_buffer* result) NGF_NOEXCEPT;

/**
 * Discards the given vertex index buffer.
 */
void ngf_destroy_index_buffer(ngf_index_buffer buffer) NGF_NOEXCEPT;

/**
 * Similar to \ref ngf_attrib_buffer_map_range, but for index buffers.
 */
void* ngf_index_buffer_map_range(ngf_index_buffer buf, size_t offset, size_t size, uint32_t flags)
    NGF_NOEXCEPT;
/**
 * Similar to \ref ngf_attrib_buffer_flush_range, but for index buffers.
 */
void ngf_index_buffer_flush_range(ngf_index_buffer buf, size_t offset, size_t size) NGF_NOEXCEPT;

/**
 * Similar to \ref ngf_attrib_buffer_unmap, but for index buffers.
 */
void ngf_index_buffer_unmap(ngf_index_buffer buf) NGF_NOEXCEPT;

/**
 * Creates a new uniform buffer.
 * @param info see \ref ngf_uniform_buffer_info
 * @param result the new buffer handle will be stored here.
 */
ngf_error ngf_create_uniform_buffer(const ngf_uniform_buffer_info* info, ngf_uniform_buffer* result)
    NGF_NOEXCEPT;

/**
 * Discards the given uniform buffer.
 */
void ngf_destroy_uniform_buffer(ngf_uniform_buffer buf) NGF_NOEXCEPT;

/**
 * Similar to \ref ngf_attrib_buffer_map_range, but for uniform buffers.
 */
void* ngf_uniform_buffer_map_range(
    ngf_uniform_buffer buf,
    size_t             offset,
    size_t             size,
    uint32_t           flags) NGF_NOEXCEPT;
/**
 * Similar to \ref ngf_attrib_buffer_flush_range, but for uniform buffers.
 */
void ngf_uniform_buffer_flush_range(ngf_uniform_buffer buf, size_t offset, size_t size)
    NGF_NOEXCEPT;

/**
 * Similar to \ref ngf_attrib_buffer_unmap, but for uniform buffers.
 */
void ngf_uniform_buffer_unmap(ngf_uniform_buffer buf) NGF_NOEXCEPT;

/**
 * Creates a new pixel buffer.
 * @param info see \ref ngf_pixel_buffer_info.
 * @param result the new buffer handle will be stored in here.
 */
ngf_error
ngf_create_pixel_buffer(const ngf_pixel_buffer_info* info, ngf_pixel_buffer* result) NGF_NOEXCEPT;
/**
 * Discards a given pixel buffer.
 */
void ngf_destroy_pixel_buffer(ngf_pixel_buffer buf) NGF_NOEXCEPT;

/**
 * Similar to \ref ngf_attrib_buffer_map_range, but for pixel buffers.
 */
void* ngf_pixel_buffer_map_range(ngf_pixel_buffer buf, size_t offset, size_t size, uint32_t flags)
    NGF_NOEXCEPT;
/**
 * Similar to \ref ngf_attrib_buffer_flush_range, but for pixel buffers.
 */
void ngf_pixel_buffer_flush_range(ngf_pixel_buffer buf, size_t offset, size_t size) NGF_NOEXCEPT;

/**
 * Similar to \ref ngf_attrib_buffer_unmap, but for pixel buffers.
 */
void ngf_pixel_buffer_unmap(ngf_pixel_buffer buf) NGF_NOEXCEPT;

/**
 * Wait for all pending rendering commands to complete.
 */
void ngf_finish(void) NGF_NOEXCEPT;

/**
 * Creates a new command buffer that is in the "ready" state.
 */
ngf_error
ngf_create_cmd_buffer(const ngf_cmd_buffer_info* info, ngf_cmd_buffer* result) NGF_NOEXCEPT;

/**
 * Frees resources associated with the given command buffer.
 * Any work previously submitted via this command buffer will be finished
 * asynchronously.
 */
void ngf_destroy_cmd_buffer(ngf_cmd_buffer buffer) NGF_NOEXCEPT;

/**
 * Erases all the commands previously recorded into the given command buffer,
 * and prepares it for recording commands to be submitted within the frame
 * identified by the specified token.
 * The command buffer is required to be in the "ready" state.
 * @param buf The command buffer to operate on
 * @param token The token for the frame within which the recorded commands are going 
 *              to be submitted. 
 */
ngf_error ngf_start_cmd_buffer(ngf_cmd_buffer buf, ngf_frame_token token) NGF_NOEXCEPT;

/**
 * Submits the commands recorded in the given command buffers for execution.
 * All command buffers must be in the "ready" state, and will be transitioned
 * to the "submitted" state.
 */
ngf_error ngf_submit_cmd_buffers(uint32_t nbuffers, ngf_cmd_buffer* bufs) NGF_NOEXCEPT;

/**
 * Starts a new encoder for rendering commands associated with the given
 * command buffer.
 * @param buf The buffer to create the encoder for. Must be in the "ready"
 *            state, will be transitioned to the "recording" state.
 */
ngf_error ngf_cmd_buffer_start_render(ngf_cmd_buffer buf, ngf_render_encoder* enc) NGF_NOEXCEPT;

/**
 * Starts a new encoder for transfer commands associated with the given
 * command buffer.
 * @param buf The buffer to create the encoder for. Must be in the "ready"
 *            state, will be transitioned to the "recording" state.
 */
ngf_error ngf_cmd_buffer_start_xfer(ngf_cmd_buffer buf, ngf_xfer_encoder* enc) NGF_NOEXCEPT;

/**
 * Disposes of the given render cmd encoder, transitioning its corresponding
 * command buffer to the "ready" state.
 */
ngf_error ngf_render_encoder_end(ngf_render_encoder enc) NGF_NOEXCEPT;

/**
 * Disposes of the given transfer cmd encoder, transitioning its corresponding
 * command buffer to the "ready" state.
 */
ngf_error ngf_xfer_encoder_end(ngf_xfer_encoder enc) NGF_NOEXCEPT;

void ngf_cmd_bind_gfx_pipeline(ngf_render_encoder buf, const ngf_graphics_pipeline pipeline)
    NGF_NOEXCEPT;
void ngf_cmd_viewport(ngf_render_encoder buf, const ngf_irect2d* r) NGF_NOEXCEPT;
void ngf_cmd_scissor(ngf_render_encoder buf, const ngf_irect2d* r) NGF_NOEXCEPT;
void ngf_cmd_stencil_reference(ngf_render_encoder buf, uint32_t front, uint32_t back) NGF_NOEXCEPT;
void ngf_cmd_stencil_compare_mask(ngf_render_encoder buf, uint32_t front, uint32_t back)
    NGF_NOEXCEPT;
void ngf_cmd_stencil_write_mask(ngf_render_encoder buf, uint32_t front, uint32_t back) NGF_NOEXCEPT;
void ngf_cmd_line_width(ngf_render_encoder buf, float line_width) NGF_NOEXCEPT;
void ngf_cmd_bind_gfx_resources(
    ngf_render_encoder          buf,
    const ngf_resource_bind_op* bind_operations,
    uint32_t                    nbind_operations) NGF_NOEXCEPT;

void ngf_cmd_bind_attrib_buffer(
    ngf_render_encoder      buf,
    const ngf_attrib_buffer vbuf,
    uint32_t                binding,
    uint32_t                offset) NGF_NOEXCEPT;
void ngf_cmd_bind_index_buffer(
    ngf_render_encoder     buf,
    const ngf_index_buffer idxbuf,
    ngf_type               index_type) NGF_NOEXCEPT;
void ngf_cmd_begin_pass(ngf_render_encoder buf, const ngf_pass_info* pass_info) NGF_NOEXCEPT;
void ngf_cmd_end_pass(ngf_render_encoder buf) NGF_NOEXCEPT;
void ngf_cmd_draw(
    ngf_render_encoder buf,
    bool               indexed,
    uint32_t           first_element,
    uint32_t           nelements,
    uint32_t           ninstances) NGF_NOEXCEPT;
void ngf_cmd_copy_attrib_buffer(
    ngf_xfer_encoder        enc,
    const ngf_attrib_buffer src,
    ngf_attrib_buffer       dst,
    size_t                  size,
    size_t                  src_offset,
    size_t                  dst_offset) NGF_NOEXCEPT;
void ngf_cmd_copy_index_buffer(
    ngf_xfer_encoder       buf,
    const ngf_index_buffer src,
    ngf_index_buffer       dst,
    size_t                 size,
    size_t                 src_offset,
    size_t                 dst_offset) NGF_NOEXCEPT;
void ngf_cmd_copy_uniform_buffer(
    ngf_xfer_encoder         enc,
    const ngf_uniform_buffer src,
    ngf_uniform_buffer       dst,
    size_t                   size,
    size_t                   src_offset,
    size_t                   dst_offset) NGF_NOEXCEPT;
void ngf_cmd_write_image(
    ngf_xfer_encoder       buf,
    const ngf_pixel_buffer src,
    size_t                 src_offset,
    ngf_image_ref          dst,
    const ngf_offset3d*    offset,
    const ngf_extent3d*    extent) NGF_NOEXCEPT;

#ifdef _MSC_VER
#pragma endregion
#endif

#ifdef __cplusplus
}
#endif
