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
 * @file 
 * @brief nicegraf declarations.
 */

/**
 * \mainpage Reference Documentation
 * 
 * These pages contain documentation automatically generated from nicegraf's
 * code comments. The text's purpose is to concisely describe the intended
 * behavior and failure modes of the API.
 * 
 * If viewing this document in a web browser or a PDF viewer, click one of the
 * following links to proceed to the documentation for the corresponding module.
 *
 *  - \ref ngf
 *  - \ref ngf_util
 */

/**
 * \defgroup ngf Core C API
 * This section contains the documentation for the core nicegraf routines, 
 * structures and enumerations.
 * 
 * \subsection core-remarks General Remarks
 * 
 * - nicegraf does not guarantee the order of structure fields to be preserved
 *   from version to version, even across minor versions. Clients should
 *   therefore never assume a specific memory layout for any of nicegraf
 *   structures.
 * 
 * - When nicegraf's C headers are included from C++, all global functions
 *   within them are automatically declared to have C linkage. Additionally,
 *   they are declared to be noexcept.
 * 
 * \subsection object-model Object Model
 * 
 * nicegraf objects, such as images, buffers, render targets, etc., are
 * represented using opaque handles. The objects are created and destroyed
 * explicitly by the application, and it is the responsibility of the
 * application to ensure a correct order of destruction.
 * 
 * \subsection error-reporting Error Reporting
 * 
 * Most nicegraf routines report their completion status by returning an
 * \ref ngf_error, and write their results to out-parameters. The returned value
 * is a generic error code. Detailed, human-readable information about errors
 * may vary from platform to platform and is reported by invoking a
 * user-provided callback function (see \ref ngf_diagnostic_info). The callback
 * function must accept the diagnostic message type (see
 * \ref ngf_diagnostic_message_type), an arbitrary void pointer (the value of
 * which the user may specify when providing the callback), a printf-style
 * format string, and an arbitrary number of arguments specifying the data for
 * the format-string.
 *  
 * \subsection host-memory-management Host Memory Management
 * 
 * By default, nicegraf uses the standard malloc/free to manage host memory for
 * internal purposes. The client may override this behavior by supplying custom
 * memory allocation callbacks (see \ref ngf_set_allocation_callbacks).
 * 
 * \subsection gpu-memory-management GPU Memory Management
 * 
 * nicegraf internally manages GPU memory for all backends. It is currently not
 * possible for clients to override this behavior and do their own GPU memory
 * management.
 *  
 */
 


#pragma once

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
 * @struct ngf_device_capabilities
 * \ingroup ngf
 * Contains information about various device features, limits, etc. Clients
 * shouldn't instantiate this structure. See \ref ngf_get_device_capabilities.
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
 * @enum ngf_device_preference
 * \ingroup ngf
 * Enumerates the possible device hints for \ref ngf_initialize.
 * \ingroup enums
 */
typedef enum ngf_device_preference {
  NGF_DEVICE_PREFERENCE_DISCRETE,   /**< Prefer discrete (high-power) GPU. */
  NGF_DEVICE_PREFERENCE_INTEGRATED, /**< Prefer integrated GPU. */
  NGF_DEVICE_PREFERENCE_DONTCARE    /**< No preference. */
} ngf_device_preference;
/* TODO: API for choosing device explicitly.*/


/**
 * @enum ngf_diagnostic_log_verbosity
 * \ingroup ngf
 * Verbosity levels for the diagnostic message log.
 */
typedef enum ngf_diagnostic_log_verbosity {
  NGF_DIAGNOSTICS_VERBOSITY_DEFAULT, /**< Normal level, reports only severe
                                        errors. */
  NGF_DIAGNOSTICS_VERBOSITY_DETAILED /**< Recommended for debug builds, may
                                        induce performance overhead. */
} ngf_diagnostic_log_verbosity;

/**
 * @enum ngf_diagnostic_message_type
 * \ingroup ngf
 * Type of a diagnostic log entry.
 */
typedef enum ngf_diagnostic_message_type {
  NGF_DIAGNOSTIC_INFO,    /**< Informational message, not actionable. */
  NGF_DIAGNOSTIC_WARNING, /**< Message warns of a potential issue with an API
                             call. */
  NGF_DIAGNOSTIC_ERROR    /**< Message provides details of an API call failure or a
                             severe performance issue. */
} ngf_diagnostic_message_type;

/**
 * The diagnostic callback function type.
 */
typedef void (*ngf_diagnostic_callback)(ngf_diagnostic_message_type, void*, const char*, ...);

/**
 * @struct ngf_diagnostic_info
 * \ingroup ngf
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
 * @struct ngf_init_info
 * nicegraf initialization parameters.
 */
typedef struct ngf_init_info {
  ngf_device_preference device_pref; /**< Which type of device to prefer.
                                          May be ignored, depending on the backend.
                                      */
  ngf_diagnostic_info diag_info;     /**< Diagnostic log configuration. */
} ngf_init_info;

/**
 * @enum ngf_error
 * \ingroup ngf
 * Error codes.
 *
 * nicegraf functions report errors via return values. Results are stored in
 * output arguments.
 */
typedef enum ngf_error {
  NGF_ERROR_OK = 0,     /**< No error, operation finished successfully. */
  NGF_ERROR_OUT_OF_MEM, /**< Host memory allocation failed. */
  NGF_ERROR_OBJECT_CREATION_FAILED, /**< A call to the backend API that was
                                       supposed to create an object failed. */
  NGF_ERROR_OUT_OF_BOUNDS, /**< The operation would have resulted in an out of
                              bounds access. */
  NGF_ERROR_INVALID_FORMAT, /**< A format enumerator provided as part of an
                               argument to the call is not valid in that
                               context.*/
  NGF_ERROR_INVALID_SIZE, /**< A size passed as part of an argument to the
                            call is either too large or too small.*/
  NGF_ERROR_INVALID_ENUM, /**< An enumerator passed as part of an argument to
                             the call is not valid in that context.*/
  NGF_ERROR_INVALID_OPERATION /**< The routine did not complete successfully. */
  /*..add new errors above this line */
} ngf_error;

/**
 * @enum ngf_present_mode
 * \ingroup ngf
 * Possible present modes.
 */
typedef enum ngf_present_mode {
  NGF_PRESENTATION_MODE_FIFO,     /**< Frames get queued ("wait for vsync") */
  NGF_PRESENTATION_MODE_IMMEDIATE /**< Doesn't wait for vsync */
} ngf_present_mode;

/**
 * @struct ngf_irect2d
 * \ingroup ngf
 * Represents a rectangular, axis-aligned 2D region with integer coordinates.
 */
typedef struct ngf_irect2d {
  int32_t  x; /**< X coord of lower-left corner. */
  int32_t  y; /**< Y coord of lower-left corner. */
  uint32_t width; /**< The size of the rectangle along the x-axis. */
  uint32_t height; /**< The size of the rectangle along the y-axis. */
} ngf_irect2d;

/**
 * @struct ngf_extent3d
 * \ingroup ngf
 * Represents a rectangular, axis-aligned 3D volume.
 */
typedef struct ngf_extent3d {
  uint32_t width;  /**< The size of the volume along the x-axis. */
  uint32_t height; /**< The size of the volume along the y-axis. */
  uint32_t depth;  /**< The size of the volume along he z-axis. */ 
} ngf_extent3d;

/**
 * @struct ngf_offset3d
 * \ingroup ngf
 * Three-dimensional offset.
 */
typedef struct ngf_offset3d {
  int32_t x;
  int32_t y;
  int32_t z;
} ngf_offset3d;

/**
 * @enum ngf_stage_type
 * \ingroup ngf
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
 * @struct ngf_shader_stage_info
 * \ingroup ngf
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
 * @struct ngf_shader_stage
 * \ingroup ngf
 * A programmable stage of the rendering pipeline.
 *
 * Programmable stages are specified using backend-specific blobs of
 * data.
 *
 * On platforms that require a compilation step at runtime, details about
 * compile errors are reported via the debug callback mechanism.
 *
 * On some back-ends, the full compile/link step may be repeated during
 * pipeline creation (if using constant specialization). This does not apply
 * to back-ends that support specialization natively with no extensions (i.e.
 * Vulkan and Metal).
 */
typedef struct ngf_shader_stage_t* ngf_shader_stage;

/**
 * @enum ngf_polygon_mode
 * \ingroup ngf
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
 * @enum ngf_cull_mode
 * \ingroup ngf
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
 * @enum ngf_front_face_mode
 * \ingroup ngf
 * Ways to determine front-facing polygons.
 */
typedef enum ngf_front_face_mode {
  NGF_FRONT_FACE_COUNTER_CLOCKWISE = 0, /**< CCW winding is front-facing.*/
  NGF_FRONT_FACE_CLOCKWISE,             /**< CW winding is front-facing. */
  NGF_FRONT_FACE_COUNT
} ngf_front_face_mode;

/**
 * @struct ngf_rasterization_info
 * \ingroup ngf
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
 * @enum ngf_compare_op
 * \ingroup ngf
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
 * @enum ngf_stencil_op
 * \ingroup ngf
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
 * @struct ngf_stencil_info
 * \ingroup ngf
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
 * @struct ngf_depth_stencil_info
 * \ingroup ngf
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
 * @enum ngf_blend_factor
 * \ingroup ngf
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
 * @enum ngf_blend_op
 * \ingroup ngf
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
 * @struct ngf_blend_info
 * \ingroup ngf
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
 * @enum ngf_input_rate
 * \ingroup ngf
 * Vertex attribute's input rate.
 */
typedef enum ngf_input_rate {
  NGF_INPUT_RATE_VERTEX = 0, /**< attribute changes per-vertex*/
  NGF_INPUT_RATE_INSTANCE,   /**< attribute changes per-instance*/
  NGF_INPUT_RATE_COUNT
} ngf_input_rate;

/**
 * @enum ngf_type
 * \ingroup ngf
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
 * @struct ngf_vertex_buf_binding_desc
 * \ingroup ngf
 * Specifies an attribute binding.
 */
typedef struct ngf_vertex_buf_binding_desc {
  uint32_t       binding;    /**< Index of the binding that this structure describes.*/
  uint32_t       stride;     /**< Number of bytes between consecutive attribute values.*/
  ngf_input_rate input_rate; /**< Whether attributes read from this binding
                                  change per-vetex or per-instance.*/
} ngf_vertex_buf_binding_desc;

/**
 * @struct ngf_vertex_attrib_desc
 * \ingroup ngf
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
 * @struct ngf_vertex_input_info
 * \ingroup ngf
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

/**
 * @enum ngf_sample_count
 * \ingroup ngf
 * Specifies the number of MSAA samples.
 */
typedef enum ngf_sample_count {
  NGF_SAMPLE_COUNT_1  = 1,
  NGF_SAMPLE_COUNT_2  = 2,
  NGF_SAMPLE_COUNT_4  = 4,
  NGF_SAMPLE_COUNT_8  = 8,
  NGF_SAMPLE_COUNT_16 = 16,
  NGF_SAMPLE_COUNT_32 = 32,
  NGF_SAMPLE_COUNT_64 = 64,
} ngf_sample_count;

/**
 * @struct ngf_multisample_info
 * \ingroup ngf
 * Specifies state of multisampling.
 */
typedef struct ngf_multisample_info {
  ngf_sample_count sample_count;      /**< MSAA sample count. */
  bool             alpha_to_coverage; /**< Whether alpha-to-coverage is enabled.*/
} ngf_multisample_info;

/**
 * @enum ngf_buffer_storage_type
 * \ingroup ngf
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
 * @enum ngf_buffer_usage
 * \ingroup ngf
 * Flags for specifying how the buffer is intended to be used.
 */
typedef enum ngf_buffer_usage {
  NGF_BUFFER_USAGE_XFER_SRC = 0x01, /**< Buffer may be used as a source for
                                       transfer operations. */
  NGF_BUFFER_USAGE_XFER_DST = 0x02,  /**< Buffer may be used as a
                                        destination for transfer operations.  */
  NGF_BUFFER_USAGE_UNIFORM_BUFFER = 0x04, /**< Buffer may be bound as a uniform
                                             buffer. */
  NGF_BUFFER_USAGE_INDEX_BUFFER =
      0x08, /**< Buffer may be used as the source of index data for indexed
               drawcalls. */
  NGF_BUFFER_USAGE_VERTEX_BUFFER =
      0x10, /**< Buffer may be used as the source of vertex attribute data. */

  NGF_BUFFER_USAGE_TEXEL_BUFFER = 0x20 /**< Buffer may be bound as a uniform
                                          texel buffer. */
} ngf_buffer_usage;

/**
 * @struct ngf_buffer_info
 * \ingroup ngf
 * Information required for buffer creation.
 */
typedef struct ngf_buffer_info {
  size_t                  size; /**< Size of the buffer in bytes. */
  ngf_buffer_storage_type storage_type; /**< Flags specifying preferred storage
                                           type.*/
  uint32_t                buffer_usage; /**< Flags specifying intended usage.*/
} ngf_buffer_info;

/**
 * @struct ngf_buffer
 * \ingroup ngf
 * A memory buffer.
 */
typedef struct ngf_buffer_t* ngf_buffer;

/**
 * @enum ngf_image_type
 * \ingroup ngf
 * Possible image types.
 */
typedef enum ngf_image_type {
  NGF_IMAGE_TYPE_IMAGE_2D = 0,
  NGF_IMAGE_TYPE_IMAGE_3D,
  NGF_IMAGE_TYPE_CUBE,
  NGF_IMAGE_TYPE_COUNT
} ngf_image_type;

/**
 * @enum ngf_image_format
 * \ingroup ngf
 * Image formats.
 *
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
  NGF_IMAGE_FORMAT_RGB9E5,
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
 * @enum ngf_image_usage
 * \ingroup ngf
 * Image usage flags.
 */
typedef enum ngf_image_usage {
  NGF_IMAGE_USAGE_SAMPLE_FROM = 0x01, /**< Can be read from in a shader.*/
  NGF_IMAGE_USAGE_ATTACHMENT  = 0x02, /**< Can be used as an attachment for a
                                           render target.*/
  NGF_IMAGE_USAGE_XFER_DST = 0x04, /**< Can be used as a destination for a transfer operation. **/
  NGF_IMAGE_USAGE_MIPMAP_GENERATION = 0x08 /**< Use this flag to enable auto mipmap generation. */
} ngf_image_usage;

/**
 * @struct ngf_image_info
 * \ingroup ngf
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

/**
 * @struct ngf_image
 * \ingroup ngf
 * An image object.
 */
typedef struct ngf_image_t* ngf_image;

/**
 * @enum ngf_cubemap_face
 * \ingroup ngf
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
 * @struct ngf_image_ref
 * \ingroup ngf
 * Reference to a part of an image.
 */
typedef struct ngf_image_ref {
  ngf_image        image;        /**< Image being referred to.*/
  uint32_t         mip_level;    /**< Mip level within the image.*/
  uint32_t         layer;        /**< Layer within the image.*/
  ngf_cubemap_face cubemap_face; /**< Face of the cubemap, ignored for
                                      non-cubemap images.*/
} ngf_image_ref;

/**
 * @enum ngf_attachment_type
 * \ingroup ngf
 * Rendertarget attachment types.
 */
typedef enum ngf_attachment_type {
  NGF_ATTACHMENT_COLOR = 0,
  NGF_ATTACHMENT_DEPTH,
  NGF_ATTACHMENT_DEPTH_STENCIL
} ngf_attachment_type;

/**
 * @enum ngf_attachment_load_op
 * \ingroup ngf
 * What to do on attachment load.
 */
typedef enum ngf_attachment_load_op {
  NGF_LOAD_OP_DONTCARE = 0, /**< Don't care what happens. */
  NGF_LOAD_OP_KEEP,         /**< Preserve the prior contents of the attachment. */
  NGF_LOAD_OP_CLEAR,        /**< Clear attachment. */
  NGF_LOAD_OP_COUNT
} ngf_attachment_load_op;

/**
 * @enum ngf_attachment_store_op
 * \ingroup ngf
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
 * @struct ngf_clear_info
 * \ingroup ngf
 * Specifies a rendertarget clear operation.
 */
typedef union ngf_clear_info {
  float clear_color[4];
  struct {
    float    clear_depth;
    uint32_t clear_stencil;
  } clear_depth_stencil;
} ngf_clear;

/**
 * @struct ngf_attachment_description
 * \ingroup ngf
 * Describes the type and format of a render target attachment.
 */
typedef struct ngf_attachment_description {
  ngf_attachment_type type;         /**< What the attachment shall be used for. */
  ngf_image_format    format;       /**< Format of the associated image. */
  ngf_sample_count    sample_count; /**< Number of samples per pixel in the associated image. */
  bool                is_sampled; /**< Whether this attachment's associated image is sampled from a
                                       shader at any point. */
} ngf_attachment_description;

/**
 * @struct ngf_attachment_descriptions
 * \ingroup ngf
 * A list of attachment descriptions.
 */
typedef struct ngf_attachment_descriptions {
  uint32_t                          ndescs;
  const ngf_attachment_description* descs;
} ngf_attachment_descriptions;

/**
 * @struct ngf_render_target_info
 * \ingroup ngf
 * Specifies information about a rendertarget.
 */
typedef struct ngf_render_target_info {
  const ngf_attachment_descriptions* attachment_descriptions; /**< List of attachment descriptions
                                                                   for this render target. */
  const ngf_image_ref* attachment_image_refs;                 /**< Image references corresponding
                                                                   to each attachment in the list. */
} ngf_render_target_info;

/**
 * @struct ngf_render_target
 * \ingroup ngf
 * Render target. 
 */
typedef struct ngf_render_target_t* ngf_render_target;

/**
 * @struct ngf_pass_info
 * \ingroup ngf
 * Information about a render pass.
 */
typedef struct ngf_pass_info {
  ngf_render_target              render_target;
  const ngf_attachment_load_op*  load_ops;
  const ngf_attachment_store_op* store_ops;
  const ngf_clear*               clears;
} ngf_pass_info;

/**
 * @struct ngf_swapchain_info
 * \ingroup ngf
 * Swapchain configuration.
 */
typedef struct ngf_swapchain_info {
  ngf_image_format color_format;  /**< Swapchain image format. */
  ngf_image_format depth_format;  /**< Format to use for the depth buffer, if set to
                                     NGF_IMAGE_FORMAT_UNDEFINED, no depth buffer will be created. */
  ngf_sample_count sample_count;  /**< Number of samples per pixel (0 for non-multisampled) */
  uint32_t         capacity_hint; /**< Number of images in swapchain (may be ignored)*/
  uint32_t         width;         /**< Width of swapchain images in pixels. */
  uint32_t         height;        /**< Height of swapchain images in pixels. */
  uintptr_t        native_handle; /**< HWND, ANativeWindow, NSWindow, etc. */
  ngf_present_mode present_mode;  /**< Desired present mode. */
} ngf_swapchain_info;

/**
 * @enum ngf_sampler_filter
 * \ingroup ngf
 *  Min/mag filters.
 */
typedef enum ngf_sampler_filter {
  NGF_FILTER_NEAREST = 0,
  NGF_FILTER_LINEAR,
  NGF_FILTER_COUNT
} ngf_sampler_filter;

/**
 * @enum ngf_sampler_wrap_mode
 * \ingroup ngf
 * What to do when sampling out-of-bounds.
 */
typedef enum ngf_sampler_wrap_mode {
  NGF_WRAP_MODE_CLAMP_TO_EDGE = 0,
  NGF_WRAP_MODE_CLAMP_TO_BORDER,
  NGF_WRAP_MODE_REPEAT,
  NGF_WRAP_MODE_MIRRORED_REPEAT,
  NGF_WRAP_MODE_COUNT
} ngf_sampler_wrap_mode;

/**
 * @struct ngf_sampler_info
 * \ingroup ngf
 * Describes a sampler object.
 */
typedef struct ngf_sampler_info {
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
 * @struct ngf_sampler
 * \ingroup ngf
 * Sampler object.
 */
typedef struct ngf_sampler_t* ngf_sampler;

/**
 * @enum ngf_descriptor_type
 * \ingroup ngf
 * Available descriptor types.
 * Not that some back-ends may not support all of the listed descriptor types.
 */
typedef enum ngf_descriptor_type {
  NGF_DESCRIPTOR_UNIFORM_BUFFER = 0,
  NGF_DESCRIPTOR_IMAGE,
  NGF_DESCRIPTOR_SAMPLER,
  NGF_DESCRIPTOR_IMAGE_AND_SAMPLER,
  NGF_DESCRIPTOR_TEXEL_BUFFER,
  NGF_DESCRIPTOR_TYPE_COUNT
} ngf_descriptor_type;

/**
 * @struct ngf_buffer_bind_info
 * \ingroup ngf
 * Specifies a buffer bind operation.
 */
typedef struct ngf_buffer_bind_info {
  ngf_buffer       buffer; /**< Which buffer to bind.*/
  size_t           offset; /**< Offset at which to bind the buffer.*/
  size_t           range;  /**< Bound range.*/
  ngf_image_format format; /**< Texel format (texel buffers only). */
} ngf_buffer_bind_info;

/**
 * @struct ngf_image_sampler_bind_info 
 * \ingroup ngf
 * Specifies an image bind operation.
 */
typedef struct ngf_image_sampler_bind_info {
  ngf_image_ref image_subresource; /**< Image portion to bind.*/
  ngf_sampler   sampler;           /**< Sampler to use.*/
} ngf_image_sampler_bind_info;

/**
 * @struct ngf_resource_bind_op
 * \ingroup ngf
 * Specifies a resource (image, buffer, etc.) bind operation, together with
 * the target set and binding IDs.
 */
typedef struct ngf_resource_bind_op {
  uint32_t            target_set; /**< Target set ID. */
  uint32_t            target_binding; /**< Target binding ID. */
  ngf_descriptor_type type; /**< Type of the resource being bound. */
  union {
    ngf_buffer_bind_info        buffer;
    ngf_image_sampler_bind_info image_sampler;
  } info; /**< Details of the resource being bound, depending on type. */
} ngf_resource_bind_op;

/**
 * @struct ngf_graphics_pipeline
 * \ingroup ngf
 * Graphics pipeline object.
 */
typedef struct ngf_graphics_pipeline_t* ngf_graphics_pipeline;

/**
 * @enum ngf_primitive_type
 * \ingroup ngf
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
 * @struct ngf_constant_specialization
 * \ingroup ngf
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
 * @struct ngf_specialization_info
 * \ingroup ngf
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
 * @struct ngf_graphics_pipeline_info
 * \ingroup ngf
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
  const ngf_specialization_info*     spec_info;
  const ngf_attachment_descriptions* compatible_rt_attachment_descs;
} ngf_graphics_pipeline_info;

/**
 * @struct ngf_allocation_callbacks
 * \ingroup ngf
 * Specifies host memory allocation callbacks for the library's internal needs.
 */
typedef struct ngf_allocation_callbacks {
  // TODO: specify alignments?
  void* (*allocate)(size_t obj_size, size_t nobjs);
  void (*free)(void* ptr, size_t obj_size, size_t nobjs);
} ngf_allocation_callbacks;

/**
 * @struct ngf_context
 * \ingroup ngf
 * Nicegraf rendering context.
 *
 * A context represents the internal state of the library that is required for
 * performing most of the library's functionality. This includes, but is not
 * limited to: presenting rendered content in a window; creating and managing
 * resources, such as images, buffers and command buffers; recording and
 * submitting command buffers.
 *
 * Most operations, with the exception of `ngf_init` and context management
 * functions themelves, require a context to be "current" on the calling
 * thread.
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
 * @struct ngf_context_info
 * \ingroup ngf
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
   * to the other one's resources (such as buffers and images) and vice versa
   * Can be NULL.
   */
  const ngf_context shared_context;
} ngf_context_info;

/**
 * @struct ngf_cmd_buffer_info
 * \ingroup ngf
 * Information about a command buffer.
 */
typedef struct ngf_cmd_buffer_info {
  uint32_t flags; /**< Reserved for future use. */
} ngf_cmd_buffer_info;

/**
 * @struct ngf_cmd_buffer
 * \ingroup ngf
 * Encodes a series of rendering commands.
 *
 * Internally, a command buffer may be in any of the following five states:
 *   - new;
 *   - ready;
 *   - recording;
 *   - awaiting submission;
 *   - submitted.
 *
 * Every newly created command buffer is in the "new" state. It can be
 * transitioned to the "ready" state by calling `ngf_start_cmd_buffer` on it.
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
 * Once all of the desired commands have been recorded, and the command buffer
 * is in the "awaiting submission" state, the command buffer may be submitted
 * for execution via a call to \ref ngf_cmd_buffer_submit, which transitions it
 * into the "submitted" state.
 *
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
 * @struct ngf_render_encoder
 * \ingroup ngf
 * A render encoder records rendering commands (such as draw calls) into its
 * corresponding command buffer.
 */
typedef struct {
  uintptr_t __handle;
} ngf_render_encoder;

/**
 * @struct ngf_xfer_encoder
 * \ingroup ngf
 * A transfer encoder records transfer commands (i.e. copying buffer contents)
 * into its corresponding command buffer.
 */
typedef struct {
  uintptr_t __handle;
} ngf_xfer_encoder;

/**
 * @typedef ngf_frame_token
 * \ingroup ngf
 * A token identifying a frame of rendering.
 */
typedef uint32_t ngf_frame_token;

#ifdef _MSC_VER
#pragma endregion

#pragma region ngf_function_declarations
#endif

/**
 * \ingroup ngf
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
 * @return A pointer to an \ref ngf_device_capabilities instance, or NULL,
 *         if no context is present on the calling thread.
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
 * The caller should not attempt to destroy the returned render target. It shall
 * be destroyed automatically, together with the parent context.
 */
ngf_render_target ngf_default_render_target() NGF_NOEXCEPT;

/**
 * Obtain the attachment descriptions for the default render target.
 * The caller should not attempt to free the returned pointer or modify the contents of the memory
 * it points to.
 */
const ngf_attachment_descriptions* ngf_default_render_target_attachment_descs() NGF_NOEXCEPT;

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
 * Creates a new buffer.
 * @param info see \ref ngf_buffer_info
 * @param result the new buffer handle will be stored here.
 */
ngf_error ngf_create_buffer(const ngf_buffer_info* info, ngf_buffer* result) NGF_NOEXCEPT;

/**
 * Discards the given buffer.
 */
void ngf_destroy_buffer(ngf_buffer buffer) NGF_NOEXCEPT;

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
void* ngf_buffer_map_range(ngf_buffer buf, size_t offset, size_t size) NGF_NOEXCEPT;

/**
 * Ensure that any new data in the mapped range will be visible to subsequently
 * submitted commands.
 * @param ptr A \ref ngf_buffer_ptr object wrapping a handle to the buffer that
 *            needs to be flushed.
 * @param offset Offset, relative to the start of the mapped range, at which
 *               the flushed region starts, in bytes.
 * @param size  Size of the flushed region in bytes.
 */
void ngf_buffer_flush_range(ngf_buffer buf, size_t offset, size_t size) NGF_NOEXCEPT;

/**
 * Unmaps a previously mapped buffer. The pointer returned for that buffer
 * by the corresponding call to \ref ngf_buffer_map_range becomes invalid.
 * @param buf The buffer that needs to be unmapped.
 */
void ngf_buffer_unmap(ngf_buffer buf) NGF_NOEXCEPT;

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
 * Starts a new render pass and returns an encoder to record the associated commands
 * to.
 * @param buf The buffer to create the encoder for. Must be in the "ready"
 *            state, will be transitioned to the "recording" state.
 * @param pass_info Specifies renderpass parameters.
 */
ngf_error ngf_cmd_begin_render_pass(
    ngf_cmd_buffer       buf,
    const ngf_pass_info* pass_info,
    ngf_render_encoder*  enc) NGF_NOEXCEPT;

/**
 * Similar to the above but with some choices pre-made:
 *   - All color attachments of the render target are cleared to the specified color.
 *   - Depth and stencil attachments are cleared to the specified respective values.
 *   - The store action for any attachment that is not marked as sampled_from, is set to
 *     NGF_STORE_OP_DONTCARE.
 *   - The store action for attachments marked as sampled_from, is set to NGF_STORE_OP_STORE.
 */
ngf_error ngf_cmd_begin_render_pass_simple(
    ngf_cmd_buffer      buf,
    ngf_render_target   rt,
    float               clear_color_r,
    float               clear_color_g,
    float               clear_color_b,
    float               clear_color_a,
    float               clear_depth,
    uint32_t            clear_stencil,
    ngf_render_encoder* enc) NGF_NOEXCEPT;

/**
 * Starts a new encoder for transfer commands associated with the given
 * command buffer.
 * @param buf The buffer to create the encoder for. Must be in the "ready"
 *            state, will be transitioned to the "recording" state.
 */
ngf_error ngf_cmd_begin_xfer_pass(ngf_cmd_buffer buf, ngf_xfer_encoder* enc) NGF_NOEXCEPT;

/**
 * Disposes of the given render cmd encoder, transitioning its corresponding
 * command buffer to the "ready" state.
 */
ngf_error ngf_cmd_end_render_pass(ngf_render_encoder enc) NGF_NOEXCEPT;

/**
 * Disposes of the given transfer cmd encoder, transitioning its corresponding
 * command buffer to the "ready" state.
 */
ngf_error ngf_cmd_end_xfer_pass(ngf_xfer_encoder enc) NGF_NOEXCEPT;

void ngf_cmd_bind_gfx_pipeline(ngf_render_encoder buf, const ngf_graphics_pipeline pipeline)
    NGF_NOEXCEPT;

/**
 * Sets the viewport to be used in subsequent rendering commands.
 * The viewport defines a region of the destination framebuffer that the resulting rendering
 * is scaled to fit into.
 */
void ngf_cmd_viewport(ngf_render_encoder buf, const ngf_irect2d* r) NGF_NOEXCEPT;

/**
 * Sets the scissor region to be used in the subsequent rendering commands.
 * The scissor defines a region of the framebuffer that can be affected by the rendering commands.
 * Any pixels outside of that region are not written to.
 */
void ngf_cmd_scissor(ngf_render_encoder buf, const ngf_irect2d* r) NGF_NOEXCEPT;

void ngf_cmd_stencil_reference(ngf_render_encoder buf, uint32_t front, uint32_t back) NGF_NOEXCEPT;
void ngf_cmd_stencil_compare_mask(ngf_render_encoder buf, uint32_t front, uint32_t back)
    NGF_NOEXCEPT;
void ngf_cmd_stencil_write_mask(ngf_render_encoder buf, uint32_t front, uint32_t back) NGF_NOEXCEPT;
void ngf_cmd_line_width(ngf_render_encoder buf, float line_width) NGF_NOEXCEPT;

/**
 * Bind resources for the shader to read. See ngf_resource_bind_op for more information.
 */
void ngf_cmd_bind_gfx_resources(
    ngf_render_encoder          buf,
    const ngf_resource_bind_op* bind_operations,
    uint32_t                    nbind_operations) NGF_NOEXCEPT;

void ngf_cmd_bind_attrib_buffer(
    ngf_render_encoder buf,
    const ngf_buffer   vbuf,
    uint32_t           binding,
    uint32_t           offset) NGF_NOEXCEPT;
void ngf_cmd_bind_index_buffer(ngf_render_encoder buf, const ngf_buffer idxbuf, ngf_type index_type)
    NGF_NOEXCEPT;

/**
 * Execute a draw call.
 */
void ngf_cmd_draw(
    ngf_render_encoder buf,
    bool               indexed,
    uint32_t           first_element,
    uint32_t           nelements,
    uint32_t           ninstances) NGF_NOEXCEPT;

void ngf_cmd_copy_buffer(
    ngf_xfer_encoder enc,
    const ngf_buffer src,
    ngf_buffer       dst,
    size_t           size,
    size_t           src_offset,
    size_t           dst_offset) NGF_NOEXCEPT;

void ngf_cmd_write_image(
    ngf_xfer_encoder buf,
    const ngf_buffer src,
    size_t           src_offset,
    ngf_image_ref    dst,
    ngf_offset3d     offset,
    ngf_extent3d     extent) NGF_NOEXCEPT;

/**
 * Generate mipmaps from level 0 of the given image and write the results to the remaining levels of
 * the given image. Requires the image to have been created with NGF_IMAGE_USAGE_MIPMAP_GENERATION.
 */
ngf_error ngf_cmd_generate_mipmaps(ngf_xfer_encoder xfenc, ngf_image img) NGF_NOEXCEPT;

#ifdef _MSC_VER
#pragma endregion
#endif

#ifdef __cplusplus
}
#endif
