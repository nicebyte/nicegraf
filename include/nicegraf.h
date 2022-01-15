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
 * 
 * This file contains the core nicegraf API declarations.
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
 * - The library is currently not intended to be linked dynamically.
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
 * may vary from platform to platform; nicegraf reports it by invoking a
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
 * memory allocation callbacks (see \ref ngf_allocation_callbacks).
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
 * This is a special value used within the \ref ngf_device_capabilities structure
 * to indicate that a limit value (i.e. max texture size) is not known or not
 * relevant for the current backend.
 */
#define NGF_DEVICE_LIMIT_UNKNOWN (~0u)

/**
 * @struct ngf_device_capabilities
 * \ingroup ngf
 * Contains information about various device features, limits, etc. Clients
 * shouldn't instantiate this structure. See \ref ngf_get_device_capabilities.
 */
typedef struct ngf_device_capabilities {
  /**
   * When binding uniform buffers, the specified offset must be
   * a multiple of this number.
   */
  size_t uniform_buffer_offset_alignment;

  /**
   * When binding a uniform buffer, the specified range must not exceed
   * this value.
   */
  size_t max_uniform_buffer_range;

  /**
   * When binding texel buffers, the specified offset must be
   * a multiple of this number.
   */
  size_t texel_buffer_offset_alignment;

  /**
   * The maximum allowed number of vertex attributes per pipeline.
   */
  size_t max_vertex_input_attributes_per_pipeline;

  /**
   * The maximum allowed number of sampled images (textures) per single
   * shader stage. Descriptors with type \ref NGF_DESCRIPTOR_IMAGE_AND_SAMPLER
   * and \ref NGF_DESCRIPTOR_TEXEL_BUFFER do count against this limit.
   */
  size_t max_sampled_images_per_stage;

  /**
   * The maximum allowed number of sampler objects per single shader stage.
   * Descriptors with type \ref NGF_DESCRIPTOR_IMAGE_AND_SAMPLER do count against
   * this limit.
   */
  size_t max_samplers_per_stage;

  /**
   * The maximum allowed number of uniform buffers per single shader stage.
   */
  size_t max_uniform_buffers_per_stage;

  /**
   * This is the maximum number of _components_, across all inputs, for the fragment
   * stage. "Input component" refers to the individual components of an input vector.
   * For example, if the fragment stage has a single float4 input (vector of 4 floats),
   * then it has 4 input components.
   */
  size_t max_fragment_input_components;

  /**
   * This is the maximum number of inputs for the fragment stage.
   */
  size_t max_fragment_inputs;

  /**
   * Maximum allowed width of a 1D image.
   */
  size_t max_1d_image_dimension;

  /**
   * Maximum allowed width, or height of a 2D image.
   */
  size_t max_2d_image_dimension;

  /**
   * Maximum allowed width, height, or depth of a 3D image.
   */

  size_t max_3d_image_dimension;

  /**
   * Maximum allowed width, or height of a cubemap.
   */
  size_t max_cube_image_dimension;

  /**
   * Maximum allowed number of layers in an image.
   */
  size_t max_image_layers;

  /**
   * Maximum number of color attachments that can be written to
   * during a render pass.
   */
  size_t max_color_attachments_per_pass;

  /**
   * The maximum degree of sampler anisotropy.
   */
  float max_sampler_anisotropy;

  /**
   * This flag is set to `true` if the platform supports [0; 1]
   * range for the clip-space z coordinate. nicegraf enforces clip-space
   * z to be in this range on all backends that support it. This ensures
   * better precision for near-field objects.
   * See the following for an in-depth explanation:
   * http://web.archive.org/web/20210829130722/https://developer.nvidia.com/content/depth-precision-visualized
   */
  bool clipspace_z_zero_to_one;

  /**
   * This flag is set to true if the device supports cubemap arrays.
   */
  bool cubemap_arrays_supported;
} ngf_device_capabilities;

/**
 * @enum ngf_diagnostic_log_verbosity
 * \ingroup ngf
 * Verbosity levels for the diagnostic message log.
 */
typedef enum ngf_diagnostic_log_verbosity {
  /**
   * \ingroup ngf 
   * Normal level, reports only severe errors. */
  NGF_DIAGNOSTICS_VERBOSITY_DEFAULT, 

  /**
   * \ingroup ngf 
   * Recommended for debug builds, may induce performance overhead. */
  NGF_DIAGNOSTICS_VERBOSITY_DETAILED 
} ngf_diagnostic_log_verbosity;

/**
 * @enum ngf_diagnostic_message_type
 * \ingroup ngf
 * Type of a diagnostic log entry.
 */
typedef enum ngf_diagnostic_message_type {
  /**
   * \ingroup ngf
   * Informational message, not actionable. */
  NGF_DIAGNOSTIC_INFO,

  /**
   * \ingroup ngf
   * Message warns of a potential issue with an API call.*/
  NGF_DIAGNOSTIC_WARNING, 

  /**
   * \ingroup ngf
   * Message provides details of an API call failure or a severe performance issue. */
  NGF_DIAGNOSTIC_ERROR
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
 * @struct ngf_allocation_callbacks
 * \ingroup ngf
 * Specifies host memory allocation callbacks for the library's internal needs.
 */
typedef struct ngf_allocation_callbacks {
  /**
   * This callback shall allocate a region of memory that is able to fit `nobjs` objects
   * of size `obj_size`, and return a pointer to the allocated region.
   * The starting address of the allocated region shall have the largest alignment for the
   * target platform.
   */
  void* (*allocate)(size_t obj_size, size_t nobjs);

  /**
   * This callback shall free a region allocated by the custom allocator. The count
   * and size of objects in the region are supplied as additional parameters.
   */
  void (*free)(void* ptr, size_t obj_size, size_t nobjs);
} ngf_allocation_callbacks;

/**
 * @typedef ngf_device_handle
 * \ingroup ngf
 * 
 * A handle that uniquely identifies a rendering device.
 *
 * Note that the value of the handle corresponding to the same exact physical device may be
 * different across different instances of the same client. In other words, if the client
 * application shuts down, then starts up again, it may get different values for device handles than
 * it did before. Therefore, device handles should not be persisted. \ingroup ngf
 */
typedef uint32_t ngf_device_handle;

/**
 * @enum ngf_device_performance_tier
 * Enumerates different types of rendering devices.
 * \ingroup ngf
 */
typedef enum ngf_device_performance_tier {
  /** \ingroup ngf
   * For high-performance devices, such as discrete GPU. */
  NGF_DEVICE_PERFORMANCE_TIER_HIGH = 0,

  /** \ingroup ngf
   * For low-power integrated GPUs, software rendering, etc.  */
  NGF_DEVICE_PERFORMANCE_TIER_LOW,

  /** \ingroup ngf
   * The specific performance profile is unknown. */
  NGF_DEVICE_PERFORMANCE_TIER_UNKNOWN,

  NGF_DEVICE_PERFORMANCE_TIER_COUNT
} ngf_device_performance_tier;

/**
 * Maximum length of a device's name.
 * \ingroup ngf
 */
#define NGF_DEVICE_NAME_MAX_LENGTH (256u)

/**
 * @struct ngf_device
 * Information about a rendering device.
 * See also: \ref ngf_get_device_list
 * \ingroup ngf
 */
typedef struct ngf_device {
  ngf_device_performance_tier performance_tier; /**< Device's performance tier. */
  ngf_device_handle           handle; /**< A handle to be passed to \ref ngf_initialize. */

  /**
   * A string associated with the device. This is _not_ guaranteed to be unique per device.
   */
  char name[NGF_DEVICE_NAME_MAX_LENGTH];

  ngf_device_capabilities capabilities; /**< Device capabilities and limits. */
} ngf_device;

/**
 * @struct ngf_init_info
 * nicegraf initialization parameters.
 * See also: \ref ngf_initialize.
 */
typedef struct ngf_init_info {
  /**
   * Pointer to a structure containing a diagnostic log configuration.
   * If this pointer is set to `NULL`, no diagnostic callback shall be invoked.
   */
  const ngf_diagnostic_info* diag_info;

  /**
   * Pointer to a structure specifying custom allocation callbacks, which the library
   * shall use to manage CPU memory for internal use.
   * If this pointer is set to `NULL`, standard malloc and free are used.
   */
  const ngf_allocation_callbacks* allocation_callbacks;

  /**
   * Handle for the rendering device that nicegraf shall execute rendering commands on.
   * A list of available device and their handles can be obtained with \ref ngf_enumerate_devices.
   */
  ngf_device_handle device;
} ngf_init_info;

/**
 * @enum ngf_error
 * \ingroup ngf
 * Enumerates the error codes that nicegraf routines may return.
 * See also \ref error-reporting.
 */
typedef enum ngf_error {
  /** \ingroup ngf
   * No error, operation finished successfully. */
  NGF_ERROR_OK = 0,

  /** \ingroup ngf
   * Host memory allocation failed. */
  NGF_ERROR_OUT_OF_MEM,

  /** \ingroup ngf
   * A call to the backend API that was
   * supposed to create an object failed.*/
  NGF_ERROR_OBJECT_CREATION_FAILED,

  /** \ingroup ngf
   * The operation would have resulted in an out of
   * bounds access. */
  NGF_ERROR_OUT_OF_BOUNDS,

  /** \ingroup ngf
   * A format enumerator provided as part of an argument to the call is not valid in that context.
   */
  NGF_ERROR_INVALID_FORMAT,

  /** \ingroup ngf
   * A size passed as part of an argument to the call is either too large or too small.*/
  NGF_ERROR_INVALID_SIZE,

  /** \ingroup ngf
   * An enumerator passed as part of an argument to the call is not valid in that context.*/
  NGF_ERROR_INVALID_ENUM,

  /** \ingroup ngf
   * The routine did not complete successfully. */
  NGF_ERROR_INVALID_OPERATION
  /*..add new errors above this line */
} ngf_error;

/**
 * @struct ngf_irect2d
 * \ingroup ngf
 * Represents a rectangular, axis-aligned 2D region with integer coordinates.
 */
typedef struct ngf_irect2d {
  int32_t  x;      /**< X coord of lower-left corner. */
  int32_t  y;      /**< Y coord of lower-left corner. */
  uint32_t width;  /**< The size of the rectangle along the x-axis. */
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
  int32_t x; /**< Offset along the x-axis. */
  int32_t y; /**< Offset along the y-axis. */
  int32_t z; /**< Offset along the z-axis. */
} ngf_offset3d;

/**
 * @enum ngf_stage_type
 * \ingroup ngf
 * Shader stage types.
 * Note that some back-ends might not support all of these.
 */
typedef enum ngf_stage_type {
  /** \ingroup ngf
   * Indicates the vertex processing stage. */
  NGF_STAGE_VERTEX = 0, 

  /** \ingroup ngf
   * Indicates the fragment processing stage. */
  NGF_STAGE_FRAGMENT,
  // TODO: compute pipelines
  NGF_STAGE_COUNT
} ngf_stage_type;

/**
 * @struct ngf_shader_stage_info
 * \ingroup ngf
 *
 * Describes a programmable shader stage.
 */
typedef struct ngf_shader_stage_info {
  ngf_stage_type type; /**< Stage type (vert/frag/etc.) */

  /**
   * This shall be a pointer to a memory buffer containing the code for
   * the shader stage.
   *
   * The specific contents of the buffer depend on which backend nicegraf
   * is being used with:
   *  - for the Vulkan backend, nicegraf expects the SPIR-V bytecode for the shader stage.
   *  - for the Metal backend, nicegraf expects the source code for the shader stage in the Metal
   * Shading Language.
   *
   * Additionally, the Metal backend expects the code to contain a special comment, mapping all
   * <descriptor set, binding> pairs to the native Metal argument table slots. The comment shall
   * be a C-style block comment - beginning with a forward slash, followed by an asterisk - 
   * containing the following word:
   * 
   * ```
   * NGF_NATIVE_BINDING_MAP
   * ```
   * 
   * followed by a newline character. 
   * 
   * Each of the following lines until the end of the comment shall have the following format:
   * 
   * ```
   * (s b) : m
   * ```
   * 
   * where `s` is the set number, `b` is the binding number within the set, and `m` is the index
   * of the corresponding resource in Metal's argument table.
   * 
   * For example, let's say the Metal shader refers to index 3 in the texture argument table. 
   * Adding the following line to the binding map comment
   * 
   * ```
   * (0 1) : 3
   * ```
   * 
   * would tell the nicegraf metal backend to use the third slot of the texture argument table when
   * an image is bound to set 0, binding 1 using \ref ngf_cmd_bind_resources.
   * 
   * When compiling HLSL shaders using nicegraf-shaderc, the comment with the binding map is generated
   * automatically.
   */
  const void* content;

  /** The number of bytes in the \ref ngf_shader_stage_info::content buffer. */
  uint32_t    content_length;
  const char* debug_name;       /**< Optional name, will appear in debug logs, may be NULL.*/
  const char* entry_point_name; /**< Entry point name for this shader stage. */
} ngf_shader_stage_info;

/**
 * @struct ngf_shader_stage
 * \ingroup ngf
 * 
 * An opaque handle to a programmable stage of the rendering pipeline.
 *
 * Programmable stages are specified using backend-specific blobs of
 * data, as described in the documentation for \ref ngf_shader_stage_info::content.
 *
 * On platforms that require a compilation step at runtime, details about
 * compile errors are reported via the debug callback mechanism.
 * 
 * Shader stage objects are necessary for creating \ref ngf_graphics_pipeline objects, but once
 * the pipelines have been created, the shader stages that had been used to create
 * them can safely be disposed of.
 * 
 * See also: \ref ngf_shader_stage_info, \ref ngf_create_shader_stage, \ref ngf_destroy_shader_stage.
 */
typedef struct ngf_shader_stage_t* ngf_shader_stage;

/**
 * @enum ngf_polygon_mode
 * \ingroup ngf
 * 
 * Enumerates ways to draw polygons.
 * See also \ref ngf_rasterization_info.
 */
typedef enum ngf_polygon_mode {
  /** \ingroup ngf
   * Fill the entire polyoon.*/
  NGF_POLYGON_MODE_FILL = 0, 

  /** \ingroup ngf
   * Outline only.*/
  NGF_POLYGON_MODE_LINE,
  
  /** \ingroup ngf
   * Vertices only.*/
  NGF_POLYGON_MODE_POINT,   
  NGF_POLYGON_MODE_COUNT
} ngf_polygon_mode;

/**
 * @enum ngf_cull_mode
 * \ingroup ngf
 * 
 * Enumerates polygon culling strategies.
 * See also \ref ngf_rasterization_info.
 */
typedef enum ngf_cull_mode {
  /** \ingroup ngf
   * Cull back-facing polygons. */
  NGF_CULL_MODE_BACK = 0,       

  /** \ingroup ngf
   * Cull front-facing polygons. */
  NGF_CULL_MODE_FRONT,   
  
  /** \ingroup ngf
   * Cull all polygons. */
  NGF_CULL_MODE_FRONT_AND_BACK, 

  /** \ingroup ngf
   * Do not cull anything. */
  NGF_CULL_MODE_NONE,           
  NGF_CULL_MODE_COUNT
} ngf_cull_mode;

/**
 * @enum ngf_front_face_mode
 * \ingroup ngf
 * Enumerates possible vertex winding orders, which are used to decide which
 * polygons are front- or back-facing.
 * See also \ref ngf_rasterization_info.
 */
typedef enum ngf_front_face_mode {
  /** \ingroup ngf
   * Polygons with vertices in counter-clockwise order are considered front-facing. */
  NGF_FRONT_FACE_COUNTER_CLOCKWISE = 0,

  /** \ingroup ngf
   * Polygons with vertices in clockwise order are considered front-facing. */
  NGF_FRONT_FACE_CLOCKWISE,

  NGF_FRONT_FACE_COUNT
} ngf_front_face_mode;

/**
 * @struct ngf_rasterization_info
 * \ingroup ngf
 * Rasterization stage parameters.
 */
typedef struct ngf_rasterization_info {
  bool discard; /**< Enable/disable rasterizer discard. Use this in pipelines that
                     don't write any fragment data.*/
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
  /** \ingroup ngf
   * Comparison test never succeeds. */
  NGF_COMPARE_OP_NEVER = 0,

  /** \ingroup ngf
   * Comparison test succeeds if A < B. */
  NGF_COMPARE_OP_LESS,

  /** \ingroup ngf
   * Comparison test succeeds if A <= B. */
  NGF_COMPARE_OP_LEQUAL,

  /** \ingroup ngf
   * Comparison test succeeds if A == B. */
  NGF_COMPARE_OP_EQUAL,

  /** \ingroup ngf
   * Comparison test succeeds if A >= B. */
  NGF_COMPARE_OP_GEQUAL,

  /** \ingroup ngf
   * Comparison test succeeds if A > B. */
  NGF_COMPARE_OP_GREATER,

  /** \ingroup ngf
   * Comparison test succeeds if A != B. */
  NGF_COMPARE_OP_NEQUAL,

  /** \ingroup ngf
   * Comparison test always succeeds. */
  NGF_COMPARE_OP_ALWAYS,

  NGF_COMPARE_OP_COUNT
} ngf_compare_op;

/**
 * @enum ngf_stencil_op
 * \ingroup ngf
 * Operations that can be performed on stencil buffer.
 */
typedef enum ngf_stencil_op {
  /** \ingroup ngf
   * Don't touch. */
  NGF_STENCIL_OP_KEEP = 0,

  /** \ingroup ngf
   * Set to 0. */
  NGF_STENCIL_OP_ZERO,

  /** \ingroup ngf
   * Replace with reference value. */
  NGF_STENCIL_OP_REPLACE,

  /** \ingroup ngf
   * Increment, clamping to max value. */
  NGF_STENCIL_OP_INCR_CLAMP,

  /** \ingroup ngf
   * Increment, wrapping to 0. */
  NGF_STENCIL_OP_INCR_WRAP,

  /** \ingroup ngf
   * Decrement, clamping to 0. */
  NGF_STENCIL_OP_DECR_CLAMP,

  /** \ingroup ngf
   * Decrement, wrapping to max value. */
  NGF_STENCIL_OP_DECR_WRAP,

  /** \ingroup ngf
   * Bitwise invert. */
  NGF_STENCIL_OP_INVERT,

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
  ngf_stencil_op depth_fail_op; /**< What to do when depth test fails but stencil test passes.*/
  ngf_compare_op compare_op;    /**< Stencil comparison function.*/
  uint32_t       compare_mask;  /**< Compare mask.*/
  uint32_t       write_mask;    /**< Write mask.*/
  uint32_t       reference;     /**< Reference value (used for \ref NGF_STENCIL_OP_REPLACE).*/
} ngf_stencil_info;

/**
 * @struct ngf_depth_stencil_info
 * \ingroup ngf
 * A graphics pipeline's depth/stencil state description.
 */
typedef struct ngf_depth_stencil_info {
  /**
   * Stencil test and actions for front-facing polys.
   * This is ignored when stencil testing is disabled.
   */
  ngf_stencil_info front_stencil;

  /**
   * Stencil test and actions for back-facing polys.
   * This is ignored when stencil testing is disabled.
   */
  ngf_stencil_info back_stencil;

  /**
   * The comparison function to use when performing the depth test.
   * This is ignored when depth testing is disabled.
   */
  ngf_compare_op depth_compare;

  /**
   * Whether to enable stencil testing.
   * The exact procedure for the stencil test, and the actions to
   * perform on success or failure can be specified separately
   * for front- and back-facing polygons (see \ref ngf_depth_stencil_info::front_stencil and
   * \ref ngf_depth_stencil_info::back_stencil).
   */
  bool stencil_test;

  /**
   * Whether to enable depth test.
   * When this is enabled, fragments that fail the test specified in
   * \ref ngf_depth_stencil_info::depth_compare, get discarded.
   */
  bool depth_test;

  /**
   * Whether to enable writing to the depth buffer.
   * When this is enabled, fragments that pass the depth test have their
   * depth written into the depth buffer.
   */
  bool depth_write;

} ngf_depth_stencil_info;

/**
 * @enum ngf_blend_factor
 * \ingroup ngf
 * Factors that can be used for source and destination values during the blend operation.
 * The factor can be thought
 * See \ref ngf_blend_info for details.
 */
typedef enum ngf_blend_factor {
  /**
   * \ingroup ngf
   * - If used as a blend factor for color: sets each color component to 0;
   * - if used as a blend factor for alpha: sets alpha to 0.
   */
  NGF_BLEND_FACTOR_ZERO = 0,

  /**
   * \ingroup ngf
   * - If used as a blend factor for color: leaves the color unchanged;
   * - if used as a blend factor for alpha: leaves the alpha value unchanged.
   */
  NGF_BLEND_FACTOR_ONE,

  /**
   * \ingroup ngf
   * - If used as a blend factor for color: multiplies each color component by the corresponding
   * component of the "source" color value;
   * - if used as a blend factor for alpha: multiples the alpha value by the "source" alpha value.
   */
  NGF_BLEND_FACTOR_SRC_COLOR,

  /**
   * \ingroup ngf
   * - If used as a blend factor for color: multiplies each color component by one minus the
   * corresponding component of the "source" color value;
   * - if used as a blend factor for alpha: multiples the alpha value by one minus the "source"
   * alpha value.
   */
  NGF_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,

  /** 
   * \ingroup ngf
   * - If used as a blend factor for color: multiplies each color component by the corresponding
   * component of the "destination" color value;
   * - if used as a blend factor for alpha: multiples the alpha value by the "destination" alpha
   * value.
   */
  NGF_BLEND_FACTOR_DST_COLOR,

  /**
   * \ingroup ngf
   * - If used as a blend factor for color: multiplies each color component by one minus the
   * corresponding component of the "destination" color value;
   * - if used as a blend factor for alpha: multiples the alpha value by one minus the "destination"
   * alpha value.
   */
  NGF_BLEND_FACTOR_ONE_MINUS_DST_COLOR,

  /**
   * \ingroup ngf
   * - If used as a blend factor for color: multiplies each color component by the "source" alpha
   * value;
   * - if used as a blend factor for alpha: multiples the alpha value by the "source" alpha value.
   */
  NGF_BLEND_FACTOR_SRC_ALPHA,

  /**
   * \ingroup ngf
   * - If used as a blend factor for color: multiplies each color component by one minus the
   * "source" alpha value;
   * - if used as a blend factor for alpha: multiples the alpha value by one minus the "source"
   * alpha value.
   */
  NGF_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,

  /**
   * \ingroup ngf
   * - If used as a blend factor for color: multiplies each color component by the "destination"
   * alpha value;
   * - if used as a blend factor for alpha: multiples the alpha value by the "destination" alpha
   * value.
   */
  NGF_BLEND_FACTOR_DST_ALPHA,

  /**
   * \ingroup ngf
   * - If used as a blend factor for color: multiplies each color component by one minus the
   * "destination" alpha value;
   * - if used as a blend factor for alpha: multiples the alpha value by one minus the "destination"
   * alpha value.
   */
  NGF_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,

  /**
   * \ingroup ngf
   * - If used as a blend factor for color: multiplies the red, green and blue components of the
   * color by the 1st, 2nd and 3rd elements of \ref ngf_blend_info::blend_consts respectively;
   * - if used as a blend factor for alpha: multiplies the alpha value by the 4th component of \ref
   * ngf_blend_info::blend_consts.
   */
  NGF_BLEND_FACTOR_CONSTANT_COLOR,

  /**
   * \ingroup ngf
   * - If used as a blend factor for color: multiplies the red, green and blue components of the
   * color by one minus the 1st, 2nd and 3rd elements of \ref ngf_blend_info::blend_consts
   * respectively;
   * - if used as a blend factor for alpha: multiplies the alpha value by one minus the 4th
   * component of \ref ngf_blend_info::blend_consts.
   */
  NGF_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR,

  /**
   * \ingroup ngf
   * - If used as a blend factor for color: multiplies the components of the color by the 4th
   * element of \ref ngf_blend_info::blend_consts;
   * - if used as a blend factor for alpha: multiplies the alpha value by the 4th component of \ref
   * ngf_blend_info::blend_consts.
   */
  NGF_BLEND_FACTOR_CONSTANT_ALPHA,

  /**
   * \ingroup ngf
   * - If used as a blend factor for color: multiplies the components of the color by one minus the
   * 4th element of \ref ngf_blend_info::blend_consts;
   * - if used as a blend factor for alpha: multiplies the alpha value by one minus the 4th
   * component of \ref ngf_blend_info::blend_consts.
   */
  NGF_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA,

  NGF_BLEND_FACTOR_COUNT
} ngf_blend_factor;

/**
 * @enum ngf_blend_op
 * \ingroup ngf
 * Operations that can be performed to blend the values computed by the fragment stage
 * (source values, denoted `S` in the member documentation) with values already present
 * in the target color attachment of the framebuffer (destination values, denoted `D` in
 * the member documentation).
 *
 * The factors (\ref ngf_blend_factor) for the source and destination values are denoted
 * as `Fs` and `Fd` respectively in the member documentation below.
 *
 */
typedef enum ngf_blend_op {
  /** \ingroup ngf
   * The result of the blend operation shall be `S*Fs + D*Fd` */
  NGF_BLEND_OP_ADD,     

  /** \ingroup ngf
   * The result of the blend operation shall be `S*Fs - D*Fd` */
  NGF_BLEND_OP_SUB,     

  /** \ingroup ngf
   * The result of the blend operation shall be `D*Fd - S*Fs` */
  NGF_BLEND_OP_REV_SUB, 

  /** \ingroup ngf
   * The result of the blend operation shall be `min(S, D)`   */
  NGF_BLEND_OP_MIN,     

  /** \ingroup ngf
   * The result of the blend operation shall be `max(S, D)`   */
  NGF_BLEND_OP_MAX,     

  NGF_BLEND_OP_COUNT
} ngf_blend_op;

/**
 * @struct ngf_blend_info
 * \ingroup ngf
 * Describes how blending should be handled by the pipeline.
 * If blending is disabled, the resulting color and alpha values are directly assigned
 * the color and alpha values computed at the fragment stage.
 *
 * When blending is enabled, the resulting color and alpha values are computed using the
 * corresponding blend operations and factors (specified separately for color and alpha).
 * Note that if the render target attachment from which the destination values are read
 * uses an sRGB format, the destination color values are linearized prior to being used
 * in a blend operation.
 *
 * If the render target attachment uses an sRGB format, the resulting color value
 * is converted to an sRGB representation prior to being finally written to the attachment.
 */
typedef struct ngf_blend_info {
  ngf_blend_op     blend_op_color;         /**< The blend operation to perform for color. */
  ngf_blend_op     blend_op_alpha;         /**< The blend operation to perform for alpha. */
  ngf_blend_factor src_color_blend_factor; /**< The source blend factor for color. */
  ngf_blend_factor dst_color_blend_factor; /**< The destination blend factor for color. */
  ngf_blend_factor src_alpha_blend_factor; /**< The source blend factor for alpha. */
  ngf_blend_factor dst_alpha_blend_factor; /**< The destination blend factor for alpha. */
  float            blend_consts[4];        /**< Blend constants used by
                                                \ref NGF_BLEND_FACTOR_CONSTANT_COLOR, \ref
                                              NGF_BLEND_FACTOR_CONSTANT_ALPHA, \ref
                                              NGF_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR and \ref
                                              NGF_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA . */
  bool enable;                             /**< Specifies whether blending is enabled.*/
} ngf_blend_info;

/**
 * @enum ngf_type
 * \ingroup ngf
 * Enumerates the available vertex attribute component types.
 */
typedef enum ngf_type {
  /** \ingroup ngf
   * Signed 8-bit integer. */
  NGF_TYPE_INT8 = 0,

  /** \ingroup ngf
   * Unsigned 8-bit integer. */
  NGF_TYPE_UINT8,

  /** \ingroup ngf
   * Signed 16-bit integer. */
  NGF_TYPE_INT16,

  /** \ingroup ngf
   * Unsigned 16-bit integer. */
  NGF_TYPE_UINT16,

  /** \ingroup ngf
   * Signed 32-bit integer. */
  NGF_TYPE_INT32,

  /** \ingroup ngf
   * Unsigned 32-bit integer. */
  NGF_TYPE_UINT32,

  /** \ingroup ngf
   * 32-bit floating point number. */
  NGF_TYPE_FLOAT,

  /** \ingroup ngf
   * 16-bit floating point number. */
  NGF_TYPE_HALF_FLOAT,

  /** \ingroup ngf
   * Double-precision floating point number. */
  NGF_TYPE_DOUBLE,

  NGF_TYPE_COUNT
} ngf_type;

/**
 * @enum ngf_input_rate
 * \ingroup ngf
 * The vertex input rate specifies whether a new set of attributes is read from a buffer per each
 * vertex or per each instance.
 */
typedef enum ngf_vertex_input_rate {
  /**
   * \ingroup ngf
   * 
   * Attributes are read per-vertex.
   * With this vertex input rate, each vertex receives its own set of attributes.
   */
  NGF_INPUT_RATE_VERTEX = 0,

  /**
   * \ingroup ngf
   *
   * Attributes are read per-instance.
   * With this vertex input rate, all vertices within the same instance share the same
   * attribute values.
   */
  NGF_INPUT_RATE_INSTANCE,
  NGF_VERTEX_INPUT_RATE_COUNT
} ngf_vertex_input_rate;

/**
 * @struct ngf_vertex_buf_binding_desc
 * \ingroup ngf
 * Specifies a vertex buffer binding.
 * A _vertex buffer binding_ may be thought of as a slot to which a vertex attribute buffer can be
 * bound. An \ref ngf_graphics_pipeline may have several such slots, which are addressed by their
 * indices. Vertex attribute buffers can be bound to these slots with \ref
 * ngf_cmd_bind_attrib_buffer. The binding also partly defines how the contents of the bound buffer
 * is interpreted - via \ref ngf_vertex_buf_binding_desc::stride and \ref
 * ngf_vertex_buf_binding_desc::input_rate
 */
typedef struct ngf_vertex_buf_binding_desc {
  uint32_t binding; /**< Index of the binding that this structure describes.*/

  /**
   * Specifies the distance (in bytes) between the starting bytes of two consecutive attribute
   * values. When set to 0, the attribute values are assumed to be tightly packed (i.e. the next
   * value of the attribute immediately follows the previous with no gaps).
   *
   * As an example, assume the buffer contains data for a single attribute, such as the position of
   * a vertex in three-dimensional space. Each component of the position is a 32-bit floating point
   * number. The values are laid out in memory one after another:
   *
   * ```
   *  ________ ________ ________ ________ ________ ________ ____
   * |        |        |        |        |        |        |
   * | pos0.x | pos0.y | pos0.z | pos1.x | pos1.y | pos1.z | ...
   * |________|________|________|________|________|________|____
   *
   * ```
   * In this case, the stride is 3*4 = 12 bytes - the distance from the beginning of the first
   * attribute to the beginning of the next attribute is equal to the size of one attribute value.
   * However, it can be set to 0, because in this case the attribute values are tightly packed - the
   * next immediately follows the previous.
   *
   * Now consider a different case, where we have two attributes: a three-dimensional position and
   * an RGB color, and the buffer first lists all the attribute values for the first vertex,
   * then all attribute values for the second vertex and so on:
   *
   * ```
   *  ________ ________ ________ ________ ________ ________ ________ _____
   * |        |        |        |        |        |        |        |
   * | pos0.x | pos0.y | pos0.z | col0.x | col0.y | col0.z | pos1.x | ...
   * |________|________|________|________|________|________|________|_____
   *
   * ```
   *
   * In this case, the stride has to be nonzero because the position of the next vertex does
   * not immediately follow the position previous one - there is the value of the color attribute in
   * between. In this case, assuming the attribute components use a 32-bit floating point, the
   * stride would have to be `3 * 4 + 3 * 4 = 24` bytes.
   */
  uint32_t stride;

  /**
   * Specifies whether attributes are read from the bound buffer
   * per-vetex or per-instance.
   */
  ngf_vertex_input_rate input_rate;
} ngf_vertex_buf_binding_desc;

/**
 * @struct ngf_vertex_attrib_desc
 * \ingroup ngf
 * Specifies information about a vertex attribute.
 */
typedef struct ngf_vertex_attrib_desc {
  uint32_t location; /**< Attribute index. */
  uint32_t binding;  /**< The index of the vertex attribute buffer binding to use.*/
  uint32_t offset;   /**< Offset in the buffer at which attribute data starts.*/
  ngf_type type;     /**< Type of attribute component.*/
  uint32_t size;     /**< Number of attribute components. This value has to be between 1 and 4
                        (inclusive). */

  /**
   * Whether the vertex stage sees the raw or normalized values for the attribute components.
   * Only attribute components of types \ref NGF_TYPE_INT8, \ref NGF_TYPE_UINT8, \ref
   * NGF_TYPE_INT16 and \ref NGF_TYPE_UINT16 can be normalized. For signed types, the values are
   * scaled to the [-1; 1] floating point range, for unsigned types they are scaled to [0; 1].
   */
  bool normalized;
} ngf_vertex_attrib_desc;

/**
 * @struct ngf_vertex_input_info
 * \ingroup ngf
 * Specifies information about the pipeline's vertex input.
 */
typedef struct ngf_vertex_input_info {
  uint32_t nattribs;           /**< Number of attribute descriptions.*/
  uint32_t nvert_buf_bindings; /**< Number of vertex buffer binding descriptions.*/

  /**
   * Pointer to an array of structures describing vertex attribute buffer
   * bindings.
   */
  const ngf_vertex_buf_binding_desc* vert_buf_bindings;

  /**
   * Pointer to an array of structures describing the vertex attributes.
   */
  const ngf_vertex_attrib_desc* attribs;
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
 * 
 * Specifies the state of multisampling.
 */
typedef struct ngf_multisample_info {
  ngf_sample_count sample_count;      /**< MSAA sample count. */
  bool             alpha_to_coverage; /**< Whether alpha-to-coverage is enabled.*/
} ngf_multisample_info;

/**
 * @enum ngf_image_format
 * \ingroup ngf
 * 
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
  NGF_IMAGE_FORMAT_BC7,
  NGF_IMAGE_FORMAT_BC7_SRGB,
  NGF_IMAGE_FORMAT_ASTC_4x4,
  NGF_IMAGE_FORMAT_ASTC_4x4_SRGB,
  NGF_IMAGE_FORMAT_ASTC_5x4,
  NGF_IMAGE_FORMAT_ASTC_5x4_SRGB,
  NGF_IMAGE_FORMAT_ASTC_5x5,
  NGF_IMAGE_FORMAT_ASTC_5x5_SRGB,
  NGF_IMAGE_FORMAT_ASTC_6x5,
  NGF_IMAGE_FORMAT_ASTC_6x5_SRGB,
  NGF_IMAGE_FORMAT_ASTC_6x6,
  NGF_IMAGE_FORMAT_ASTC_6x6_SRGB,
  NGF_IMAGE_FORMAT_ASTC_8x5,
  NGF_IMAGE_FORMAT_ASTC_8x5_SRGB,
  NGF_IMAGE_FORMAT_ASTC_8x6,
  NGF_IMAGE_FORMAT_ASTC_8x6_SRGB,
  NGF_IMAGE_FORMAT_ASTC_8x8,
  NGF_IMAGE_FORMAT_ASTC_8x8_SRGB,
  NGF_IMAGE_FORMAT_ASTC_10x5,
  NGF_IMAGE_FORMAT_ASTC_10x5_SRGB,
  NGF_IMAGE_FORMAT_ASTC_10x6,
  NGF_IMAGE_FORMAT_ASTC_10x6_SRGB,
  NGF_IMAGE_FORMAT_ASTC_10x8,
  NGF_IMAGE_FORMAT_ASTC_10x8_SRGB,
  NGF_IMAGE_FORMAT_ASTC_10x10,
  NGF_IMAGE_FORMAT_ASTC_10x10_SRGB,
  NGF_IMAGE_FORMAT_ASTC_12x10,
  NGF_IMAGE_FORMAT_ASTC_12x10_SRGB,
  NGF_IMAGE_FORMAT_ASTC_12x12,
  NGF_IMAGE_FORMAT_ASTC_12x12_SRGB,
  NGF_IMAGE_FORMAT_DEPTH32,
  NGF_IMAGE_FORMAT_DEPTH16,
  NGF_IMAGE_FORMAT_DEPTH24_STENCIL8,
  NGF_IMAGE_FORMAT_UNDEFINED,
  NGF_IMAGE_FORMAT_COUNT
} ngf_image_format;

/**
 * @enum ngf_attachment_type
 * \ingroup ngf
 * Enumerates render target attachment types.
 */
typedef enum ngf_attachment_type {
  /** \ingroup ngf
   * For attachments containing color data. */
  NGF_ATTACHMENT_COLOR = 0,    

  /** \ingroup ngf
   * For attachments containing depth data. */
  NGF_ATTACHMENT_DEPTH,        

  /** \ingroup ngf
   * For attachments containing combined depth and stencil data. */
  NGF_ATTACHMENT_DEPTH_STENCIL 
} ngf_attachment_type;

/**
 * @struct ngf_attachment_description
 * \ingroup ngf
 * Describes the type and format of a render target attachment.
 */
typedef struct ngf_attachment_description {
  ngf_attachment_type type; /**< What the attachment shall be used for. */
  ngf_image_format format;  /**< Format of the associated image. Note that it must be valid for the
                               given attachment type. */
  ngf_sample_count sample_count; /**< Number of samples per pixel in the associated image. */
  bool is_sampled; /**< Whether the image associated with this attachment is sampled from a shader
                      at any point. */
} ngf_attachment_description;

/**
 * @struct ngf_attachment_descriptions
 * \ingroup ngf
 * A list of attachment descriptions.
 */
typedef struct ngf_attachment_descriptions {
  /** Pointer to a continuous array of \ref ngf_attachment_descriptions::ndescs \ref
   * ngf_attachment_description objects.
   */
  const ngf_attachment_description* descs;

  uint32_t ndescs; /**< The number of attachment descriptions in the list. */
} ngf_attachment_descriptions;

/**
 * @enum ngf_primitive_topology
 * \ingroup ngf
 * 
 * Enumerates the available primitive topologies (ways to group vertices into primitives).
 */
typedef enum ngf_primitive_topology {
  /**
   * \ingroup ngf
   * A list of separate triangles - each three vertices define a separate triangle.
   */
  NGF_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST = 0,

  /**
   * \ingroup ngf
   * A list of connected triangles, with consecutive triangles sharing an edge like so:
   * ```
   *  o---------o-----------o
   *   \       /  \       /
   *     \   /      \   / ...
   *       o----------o
   *
   * ```
   */
  NGF_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,

  /**
   * \ingroup ngf
   * A list of separate lines. Each two vertices define a separate line.
   */
  NGF_PRIMITIVE_TOPOLOGY_LINE_LIST,

  /**
   * \ingroup ngf
   * A list of connected lines. The end of a line is the beginning of the next line in the list.
   */
  NGF_PRIMITIVE_TOPOLOGY_LINE_STRIP,

  NGF_PRIMITIVE_TOPOLOGY_COUNT
} ngf_primitive_topology;

/**
 * @struct ngf_constant_specialization
 * \ingroup ngf
 * 
 * A constant specialization entry, sets the value for a single specialization constant.
 */
typedef struct ngf_constant_specialization {
  uint32_t constant_id; /**< ID of the specialization constant used in the shader stage */
  uint32_t offset;      /**< Offset at which the user-provided value is stored in the specialization
                           buffer. */
  ngf_type type;        /**< Type of the specialization constant. */
} ngf_constant_specialization;

/**
 * @struct ngf_specialization_info
 * \ingroup ngf
 * Sets specialization constant values for a pipeline.
 * Specialization constants are a kind of shader constant whose values can be set at pipeline
 * creation time. The shaders that run as part of said pipeline will then see the provided values
 * during execution.
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
 * 
 * Contains all information necessary for creating a graphics pipeline object.
 */
typedef struct ngf_graphics_pipeline_info {
  ngf_shader_stage              shader_stages[5]; /**< The programmable stages for this pipeline. */
  uint32_t                      nshader_stages; /**< The number of programmable stages involved. */
  const ngf_rasterization_info* rasterization;  /**< Specifies the parameters for the rasterizer. */
  const ngf_multisample_info*   multisample;    /**< Specifies the parameters for multisampling. */

  /**
   * Specifies the parameters for depth and stencil testing.
   */
  const ngf_depth_stencil_info* depth_stencil;

  /**
   * Specifies the parameters for blending.
   */
  const ngf_blend_info* blend;

  /**
   * Specifies vertex attributes and vertex attribute buffer bindings.
   */
  const ngf_vertex_input_info* input_info;

  ngf_primitive_topology primitive_topology; /**< The primitive topology used by this pipeline. */
  const ngf_specialization_info* spec_info;  /**< Specifies the values for specialization constants
                                                (if any) used by the programmable stages. */

  /**
   * Describes which render targets compatible with this pipeline.
   * A compatible render target must have the same number of attachments as specified in the list,
   * with matching type, format and sample count.
   */
  const ngf_attachment_descriptions* compatible_rt_attachment_descs;
} ngf_graphics_pipeline_info;

/**
 * @struct ngf_graphics_pipeline
 * \ingroup ngf
 * 
 * An opaque handle to a graphics pipeline object.
 * 
 * See also: \ref ngf_graphics_pipeline_info, \ref ngf_create_graphics_pipeline and \ref ngf_destroy_graphics_pipeline.
 */
typedef struct ngf_graphics_pipeline_t* ngf_graphics_pipeline;

/**
 * @enum ngf_descriptor_type
 * \ingroup ngf
 * 
 * Available descriptor types.
 * Not that some back-ends may not support all of the listed descriptor types.
 */
typedef enum ngf_descriptor_type {
  /**
   * \ingroup ngf
   * 
   * A uniform buffer, also known as a constant buffer, can be used to pass
   * a small to medium sized chunk of data to the shader in a structured way.
   * See also \ref ngf_buffer.
   */
  NGF_DESCRIPTOR_UNIFORM_BUFFER = 0,

  /**
   * \ingroup ngf
   * 
   * An \ref ngf_image.
   */
  NGF_DESCRIPTOR_IMAGE,

  /**
   * \ingroup ngf
   * 
   * An \ref ngf_sampler.
   */
  NGF_DESCRIPTOR_SAMPLER,

  /**
   * \ingroup ngf
   * 
   * A combination of an image and sampler in a single object.
   */
  NGF_DESCRIPTOR_IMAGE_AND_SAMPLER,

  /**
   * \ingroup ngf
   * 
   * A texel buffer can be used to pass a large amount of unstructured data
   * (i.e. a big array of `float4`s) to the shader. See also \ref ngf_buffer.
   */
  NGF_DESCRIPTOR_TEXEL_BUFFER,

  NGF_DESCRIPTOR_TYPE_COUNT
} ngf_descriptor_type;

/**
 * @enum ngf_sampler_filter
 * \ingroup ngf
 * 
 *  Enumerates filters for texture lookups.
 */
typedef enum ngf_sampler_filter {
  /** 
   * \ingroup ngf
   * 
   * When used as the minification (\ref ngf_sampler_info::min_filter) or  magnification (\ref
   * ngf_sampler_info::mag_filter) filter, the result of the filtering operation shall be the
   * value of the texel whose center is nearest to the sample.
   * 
   * When used as \ref ngf_sampler_info::mip_filter, makes the selected mip level snap to the one
   * that is closest to the requested mip level value.
   */
  NGF_FILTER_NEAREST = 0, 

  /** 
   * \ingroup ngf
   * 
   * When used as the minification (\ref ngf_sampler_info::min_filter) or  magnification (\ref
   * ngf_sampler_info::mag_filter) filter, the result of the filtering operation shall be linearly
   * interpolated from the values of 4 (in case of 2D images) or 8 (in case of 3D images) texels
   * whose centers are nearest to the sample.
   * 
   * When used as \ref ngf_sampler_info::mip_filter, linearly blends the values from two mip levels
   * closest to the requested mip level value.
   */
  NGF_FILTER_LINEAR,      
  NGF_FILTER_COUNT
} ngf_sampler_filter;

/**
 * @enum ngf_sampler_wrap_mode
 * \ingroup ngf
 * 
 * Enumerates strategies for dealing with sampling an image out-of-bounds.
 */
typedef enum ngf_sampler_wrap_mode {
  /** \ingroup ngf
   * Clamp the texel value to what's at the edge of the image. */
  NGF_WRAP_MODE_CLAMP_TO_EDGE = 0, 

  /** \ingroup ngf
   * Repeat the image contents. */
  NGF_WRAP_MODE_REPEAT,            

  /** \ingroup ngf
   * Repeat the image contents, mirrored. */
  NGF_WRAP_MODE_MIRRORED_REPEAT,   

  NGF_WRAP_MODE_COUNT
} ngf_sampler_wrap_mode;

/**
 * @struct ngf_sampler_info
 * \ingroup ngf
 * 
 * Information for creating an \ref ngf_sampler object.
 */
typedef struct ngf_sampler_info {
  ngf_sampler_filter    min_filter; /**< The filter to apply when the sampled image is minified .*/
  ngf_sampler_filter    mag_filter; /**< The filter to apply when the sampled image is magnified. */
  ngf_sampler_filter    mip_filter; /**< The filter to use when transitioning between mip levels. */
  ngf_sampler_wrap_mode wrap_u;     /**< Wrap mode for the U coordinate. */
  ngf_sampler_wrap_mode wrap_v;     /**< Wrap mode for the V coordinate. */
  ngf_sampler_wrap_mode wrap_w;     /**< Wrap mode for the W coordinate. */
  float lod_max;          /**< Maximum mip level that shall be used during the filtering operation.
                           *  Note that this refers to the _level itself_ and not the dimensions of data
                           *  residing in that level, e.g. level 0 (the smallest possible level) has
                           *  the largest dimensions.
                           */
  float lod_min;          /**< Minimum mip level that shall be used during the filtering operation.
                           *  Note that this refers to the _level itself_ and not the dimensions of data
                           *  residing in that level, e.g. level 0 (the smallest possible level) has
                           *  the largest dimensions.
                           */
  float lod_bias;         /**< A bias to add to the mip level calculated during the sample operation. */
  float max_anisotropy;   /**< Max allowed degree of anisotropy. Ignored if \ref
                            * ngf_sampler_info::enable_anisotropy is false.
                            */
  bool enable_anisotropy; /**< Whether to allow anisotropic filtering. */
} ngf_sampler_info;

/**
 * @struct ngf_sampler
 * \ingroup ngf
 * 
 * An opaque handle for a sampler object.  
 * 
 * Samplers encapsulate how to filter an image - what happens when an image is minified or
 * magnified, whether anisotropic filtering is enabled, etc. See \ref ngf_sampler_info for more
 * details.
 * 
 * Samplers can be bound separately from images - in which case the shader code sees them as two
 * distinct objects, and the same sampler can be ussed to sample two different images. They can also
 * be combined into a single descriptor (see \ref NGF_DESCRIPTOR_IMAGE_AND_SAMPLER), in which case
 * the shader code sees only a single image object, which can be sampled only one certain way.
 */
typedef struct ngf_sampler_t* ngf_sampler;

/**
 * @enum ngf_image_usage
 * \ingroup ngf
 * 
 * Image usage flags.
 *
 * A valid image usage mask may be formed by combining one or more of these
 * values with a bitwise OR operator.
 */
typedef enum ngf_image_usage {
  /** \ingroup ngf
   * The image may be read from in a shader.*/
  NGF_IMAGE_USAGE_SAMPLE_FROM = 0x01, 

  /** \ingroup ngf
   * The image may be used as an attachment for a render target.*/
  NGF_IMAGE_USAGE_ATTACHMENT = 0x02,

  /** \ingroup ngf
   * The image may be used as a destination for a transfer operation. **/
  NGF_IMAGE_USAGE_XFER_DST = 0x04,

  /** \ingroup ngf
   * Mipmaps may be generated for the image with \ref ngf_cmd_generate_mipmaps. */
  NGF_IMAGE_USAGE_MIPMAP_GENERATION = 0x08
} ngf_image_usage;

/**
 * @enum ngf_image_type
 * \ingroup ngf
 * 
 * Enumerates the possible image types.
 */
typedef enum ngf_image_type {
  /** \ingroup ngf
   * Two-dimensional image. */
  NGF_IMAGE_TYPE_IMAGE_2D = 0,

  /** \ingroup ngf
   * Three-dimensional image. */
  NGF_IMAGE_TYPE_IMAGE_3D,

  /** \ingroup ngf 
   * Cubemap. */
  NGF_IMAGE_TYPE_CUBE,

  NGF_IMAGE_TYPE_COUNT
} ngf_image_type;

/**
 * @struct ngf_image_info
 * \ingroup ngf
 * 
 * Information required to create an \ref ngf_image object.
 */
typedef struct ngf_image_info {
  ngf_image_type type;    /**< The image type. */
  ngf_extent3d   extent;  /**< The width, height and depth. Note that dimensions irrelevant for the
                             specified image type are ignored.*/
  uint32_t         nmips; /**< The number of mip levels in the image.*/
  uint32_t         nlayers;      /**< Number of layers within the image. */
  ngf_image_format format;       /**< Internal format.*/
  ngf_sample_count sample_count; /**< The number of samples per pixel in the image. **/
  uint32_t         usage_hint;   /**< Specifies how the client intends to use the image. Must be a
                                      combination of \ref ngf_image_usage flags.*/
} ngf_image_info;

/**
 * @struct ngf_image
 * \ingroup ngf
 * 
 * An opaque handle to an image object.
 * 
 * Images are multidimensional arrays of data that can be sampled from in shaders, or rendered into.
 * The individual elements of such arrays shall be referred to as "texels". An \ref ngf_image_format
 * describes the specific type and layout of data elements within a single texel. Note that
 * compressed image formats typically don't store values of texels directly, rather they store enough
 * information that the texel values can be reconstructed (perhaps lossily) by the rendering device.
 * 
 * Images can be one of the following types (see \ref ngf_image_type):
 *  - a two-dimensional image, identified by \ref NGF_IMAGE_TYPE_IMAGE_2D and representing a
 *    two-dimensional array of texels;
 *  - a three-dimensional image, identified by \ref NGF_IMAGE_TYPE_IMAGE_3D and representing a
 *    three-dimensional array of texels;
 *  - a cubemap, identified by \ref NGF_IMAGE_TYPE_CUBE and representing a collection of six
 *    two-dimensional texel arrays, each corresponding to a face of a cube.
 * 
 * An image object may actually contain several images of the same type, format and dimensions.
 * Those are referred to as "layers" and images containing more than a single layer are called
 * "layered", or "image arrays". Note that a multi-layered 2D image is different from a
 * single-layered 3D image, because filtering is not performed across levels when sampling it. Also
 * note that layered cubemaps are not supported by all hardware - see \ref
 * ngf_device_capabilities::cubemap_arrays_supported.
 * 
 * Each image layer may contain mip levels. Mip level 0 is the layer itself, and each subsequent
 * level (1, 2 and so on) is 2x smaller in dimensions, and usually contains the downscaled version
 * of the preceding level for the purposes of filtering, although the application is free to upload
 * arbitrary data into any mip level, as long as dimension requirements are respected.
 */
typedef struct ngf_image_t* ngf_image;

/**
 * @enum ngf_cubemap_face
 * \ingroup ngf
 * 
 * Members of this enumeration are used to refer to the different faces of a cubemap.
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
 * 
 * A reference to a part of an image.
 */
typedef struct ngf_image_ref {
  ngf_image        image;        /**< The image being referred to.*/
  uint32_t         mip_level;    /**< The mip level within the image.*/
  uint32_t         layer;        /**< The layer within the image.*/
  ngf_cubemap_face cubemap_face; /**< The face of the cubemap for cubemaps, ignored for
                                      non-cubemap images.*/
} ngf_image_ref;

/**
 * @struct ngf_render_target_info
 * \ingroup ngf
 * Information required to create a render target object.
 */
typedef struct ngf_render_target_info {
  /** List of attachment descriptions. */
  const ngf_attachment_descriptions* attachment_descriptions;
  /** Image references, describing what is bound to each attachment. */
  const ngf_image_ref* attachment_image_refs;
} ngf_render_target_info;

/**
 * @struct ngf_render_target
 * \ingroup ngf
 * An opaque handle to a render target object.
 */
typedef struct ngf_render_target_t* ngf_render_target;

/**
 * @struct ngf_clear_info
 * \ingroup ngf
 * Specifies a render target clear operation.
 */
typedef union ngf_clear_info {
  /**
   * The color to clear to. Each element corresponds to the red, green, blue and alpha channel
   * respectively, and is a floating point value within the [0; 1] range, with 0.0 corresponding to
   * none an 1.0 corresponding to full intensity. If the format of the render target image does not
   * have a corresponding channel, the value is ignored.
   * This field is used for color attachments only.
   */
  float clear_color[4];

  /**
   * The depth and stencil values to clear to. This field is used for depth or combined
   * depth/stencil attachments only.
   */
  struct {
    float    clear_depth;   /**< The depth value to clear to. */
    uint32_t clear_stencil; /**< The stencil value to clear to. */
  } clear_depth_stencil;
} ngf_clear;

/**
 * @enum ngf_attachment_load_op
 * \ingroup ngf
 * Enumerates actions that can be performed on attachment "load" (at the start of a render pass).
 */
typedef enum ngf_attachment_load_op {
  /** \ingroup ngf
   * Don't care what happens. */
  NGF_LOAD_OP_DONTCARE = 0, 

  /** \ingroup ngf
   * Preserve the prior contents of the attachment. */
  NGF_LOAD_OP_KEEP,         
  
  /** \ingroup ngf
   * Clear the attachment. */
  NGF_LOAD_OP_CLEAR,        
  NGF_LOAD_OP_COUNT
} ngf_attachment_load_op;

/**
 * @enum ngf_attachment_store_op
 * \ingroup ngf
 * Enumerates actions that can be performed on attachment "store" (at the end of a render pass).
 */
typedef enum ngf_attachment_store_op {
  /**
   * \ingroup ngf
   * 
   * Don't care what happens. Use this if you don't plan on reading back the
   * contents of the attachment in any shaders, or presenting it to screen.
   */
  NGF_STORE_OP_DONTCARE = 0,

  /**
   * \ingroup ngf
   * 
   * Use this if you plan on reading the contents of the attachment in any shaders or
   * presenting it to screen. The contents of the attachment shall be written out to system memory.
   */
  NGF_STORE_OP_STORE,

  NGF_STORE_OP_COUNT
} ngf_attachment_store_op;

/**
 * @struct ngf_pass_info
 * \ingroup ngf
 * Information required to begin a render pass.
 */
typedef struct ngf_pass_info {
  /**
   * A render target that shall be rendered to during this pass.
   */
  ngf_render_target render_target;

  /**
   * A pointer to a buffer of \ref ngf_load_op enumerators specifying the operation to perform at
   * the start of the render pass for each attachment of \ref ngf_pass_info::render_target. The
   * buffer must have at least the same number of elements as there are attachments in the render
   * target. The `i`th element of the buffer corresponds to the `i`th attachment.
   */
  const ngf_attachment_load_op* load_ops;

  /**
   * A pointer to a buffer of \ref ngf_store_op enumerators specifying the operation to perform at
   * the end of the render pass for each attachment of \ref ngf_pass_info::render_target. The
   * buffer must have at least the same number of elements as there are attachments in the render
   * target. The `i`th element of the buffer corresponds to the `i`th attachment.
   */
  const ngf_attachment_store_op* store_ops;

  /**
   * If no attachment has a clear as its load op, this field may be NULL.
   * Otherwise, it shall be a pointer to a buffer of \ref ngf_clear objects. The buffer must contain
   * at least as many elements as there are attachments in the render target. The `i`th element of
   * the buffer corresponds to the `i`th attachment. For attachments that are to be cleared at the
   * beginning of the pass, the clear values from the corresponding element of the buffer are used.
   * The rest of the buffer's elements are ignored.
   */
  const ngf_clear* clears;
} ngf_pass_info;

/**
 * @enum ngf_buffer_storage_type
 * \ingroup ngf
 * Enumerates types of memory backing a buffer object.
 */
typedef enum ngf_buffer_storage_type {
  /**
   * \ingroup ngf
   * Memory that can be read by the host.
   */
  NGF_BUFFER_STORAGE_HOST_READABLE,

  /**
   * \ingroup ngf
   * Memory that can be written to by the host.
   */
  NGF_BUFFER_STORAGE_HOST_WRITEABLE,

  /**
   * \ingroup ngf
   * Memory that can be both read from and written to by the
   * host.
   */
  NGF_BUFFER_STORAGE_HOST_READABLE_WRITEABLE,

  /**
   * \ingroup ngf
   * 
   * Private memory that cannot be accessed by the host directly. The contents of a
   * buffer backed by this type of memory can only be modified by executing a
   * \ref ngf_cmd_copy_buffer.
   */
  NGF_BUFFER_STORAGE_PRIVATE
} ngf_buffer_storage_type;

/**
 * @enum ngf_buffer_usage
 * \ingroup ngf
 * Enumerates the buffer usage flags. A valid buffer usage mask may be formed by combining a subset
 * of these values with a bitwise OR operator.
 */
typedef enum ngf_buffer_usage {
  /** \ingroup ngf
   * The buffer may be used as a source for transfer operations. */
  NGF_BUFFER_USAGE_XFER_SRC = 0x01,       
  
  /** \ingroup ngf
   * The buffer may be used as a destination for transfer operations. */
  NGF_BUFFER_USAGE_XFER_DST = 0x02,       

  /** \ingroup ngf
   * The buffer may be bound as a uniform buffer. */
  NGF_BUFFER_USAGE_UNIFORM_BUFFER = 0x04, 

  /** \ingroup ngf
  * The buffer may be used as the source of index data for indexed draws. */
  NGF_BUFFER_USAGE_INDEX_BUFFER   = 0x08, 
  
  /** \ingroup ngf
   * The buffer may be used as a source of vertex attribute data. */
  NGF_BUFFER_USAGE_VERTEX_BUFFER = 0x10,  

  /** \ingroup ngf
   * The buffer may be bound as a uniform texel buffer. */
  NGF_BUFFER_USAGE_TEXEL_BUFFER = 0x20    
} ngf_buffer_usage;

/**
 * @struct ngf_buffer_info
 * \ingroup ngf
 * Information required to create a buffer object.
 */
typedef struct ngf_buffer_info {
  size_t                  size;         /**< The size of the buffer in bytes. */
  ngf_buffer_storage_type storage_type; /**< Flags specifying the preferred storage type.*/
  uint32_t                buffer_usage; /**< Flags specifying the intended usage.*/
} ngf_buffer_info;

/**
 * @struct ngf_buffer
 * \ingroup ngf
 * An opaque handle to a buffer object.
 */
typedef struct ngf_buffer_t* ngf_buffer;

/**
 * @struct ngf_buffer_bind_info
 * \ingroup ngf
 * Specifies a buffer resource bind operation.
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
 * Specifies an image and/or sampler resource bind operation. To bind a combined image sampler, both
 * fields have to be set.
 */
typedef struct ngf_image_sampler_bind_info {
  ngf_image   image;   /**< The image to bind. Can be NULL if binding just a sampler. */
  ngf_sampler sampler; /**< The sampler to bind. Can be NULL if binding just an image. */
} ngf_image_sampler_bind_info;

/**
 * @struct ngf_resource_bind_op
 * \ingroup ngf
 * Specifies a resource (image, buffer, etc.) bind operation, together with
 * the target set and binding IDs. See \ref ngf_cmd_bind_resources for details.
 */
typedef struct ngf_resource_bind_op {
  uint32_t            target_set;     /**< Target set ID. */
  uint32_t            target_binding; /**< Target binding ID. */
  ngf_descriptor_type type;           /**< The type of the resource being bound. */
  union {
    ngf_buffer_bind_info        buffer;
    ngf_image_sampler_bind_info image_sampler;
  } info; /**< The details about the resource being bound, depending on type. */
} ngf_resource_bind_op;

/**
 * @enum ngf_present_mode
 * \ingroup ngf
 * Enumerates possible presentation modes.
 * "Presentation mode" refers to the particular way the CPU,
 * GPU and the presentation engine interact. Some of the listed presentation modes
 * may not be supported on various backend, hardware or OS combinations. If an
 * unsupported mode is requested, nicegraf silently falls back onto \ref NGF_PRESENTATION_MODE_FIFO.
 */
typedef enum ngf_present_mode {
  /**
   * \ingroup ngf
   * 
   * This is the only presentation mode that is guaranteed to be supported.
   * In this mode, the presentation requests are queued internally, and the
   * presentation engine waits for the vertical blanking signal to present
   * the image at the front of the queue. This mode guarantees no
   * frame tearing.
   */
  NGF_PRESENTATION_MODE_FIFO,

  /**
   * \ingroup ngf
   * 
   * In this mode, the presentation engine does not wait for the vertical blanking signal, instead
   * presenting an image immediately. This mode results in lower latency but may induce frame
   * tearing. It is not recommended to use this mode on mobile targets.
   */
  NGF_PRESENTATION_MODE_IMMEDIATE
} ngf_present_mode;

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
 * @struct ngf_context
 * \ingroup ngf
 * An opaque handle to a nicegraf rendering context.
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
 * 
 * See also: \ref ngf_context_info and \ref ngf_create_context.
 */
typedef struct ngf_context_t* ngf_context;

/**
 * @struct ngf_context_info
 * \ingroup ngf
 * Configures a nicegraf rendering context.
 */
typedef struct ngf_context_info {
  /**
   * Configures the swapchain that the context will be presenting to. This
   * can be NULL if all rendering is done off-screen and the context never
   * presents to a window.
   */
  const ngf_swapchain_info* swapchain_info;

  /**
   * A reference to another context; the newly created context shall be able to use the resources
   * (such as buffers and images) created within the given context, and vice versa Can be NULL.
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
 * of commands by calling \ref ngf_start_cmd_buffer which implicitly
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
 */
typedef struct ngf_cmd_buffer_t* ngf_cmd_buffer;

/**
 * @struct ngf_render_encoder
 * \ingroup ngf
 * 
 * A render encoder records rendering commands (such as draw calls) into its
 * corresponding command buffer.
 */
typedef struct {
  uintptr_t __handle;
} ngf_render_encoder;

/**
 * @struct ngf_xfer_encoder
 * \ingroup ngf
 * 
 * A transfer encoder records transfer commands (i.e. copying buffer contents)
 * into its corresponding command buffer.
 */
typedef struct {
  uintptr_t __handle;
} ngf_xfer_encoder;

/**
 * @typedef ngf_frame_token
 * \ingroup ngf
 * A token identifying a frame of rendering. See \ref ngf_begin_frame and \ref ngf_end_frame for
 * details.
 */
typedef uint32_t ngf_frame_token;

#ifdef _MSC_VER
#pragma endregion

#pragma region ngf_function_declarations
#endif

/**
 * \ingroup ngf
 * 
 * Obtains a list of rendering devices available to nicegraf.
 *
 * This function is not thread-safe.
 * The devices are not returned in any particular order, and the order is not guaranteed to be the
 * same every time the function is called.
 * @param devices pointer to a pointer to `const` \ref ngf_device. If not `NULL`, this will be
 * populated with a pointer to an array of \ref ngf_device instances, each containing data about a
 * rendering device available to the system. Callers should not attempt to free the returned
 * pointer.
 * @param ndevices pointer to a `uint32_t`. If not NULL, the number of available rendering devices
 * shall be written to the memory pointed to by this parameter.
 */
ngf_error ngf_get_device_list(const ngf_device** devices, uint32_t* ndevices);

/**
 * \ingroup ngf
 * 
 * Initializes nicegraf.
 *
 * The client should call this function only once during the
 * entire lifetime of the application. This function is not thread safe.
 * @param init_info Initialization parameters.
 */
ngf_error ngf_initialize(const ngf_init_info* init_info) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Creates a new \ref ngf_context.
 * 
 * @param info The context configuration.
 */
ngf_error ngf_create_context(const ngf_context_info* info, ngf_context* result) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Destroys the given \ref ngf_context.
 * 
 * @param ctx The context to destroy.
 */
void ngf_destroy_context(ngf_context ctx) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Adjust the images associated with the given context's swapchain.
 
 * This function must be called every time that the window the context's presenting to is resized.
 * It is up to the client application to detect the resize events and call this function.
 * Not calling this function on resize results in undefined behavior.
 * 
 * @param ctx The context to operate on
 * @param new_width New window client area width in pixels
 * @param new_height New window client area height in pixels
 */
ngf_error ngf_resize_context(ngf_context ctx, uint32_t new_width, uint32_t new_height) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Set the given nicegraf context as current for the calling thread.
 * 
 * All subsequent rendering operations invoked from the calling thread shall affect
 * the given context.
 *
 * Once a context has been set as current on a thread, it cannot be migrated to
 * another thread.
 *
 * @param ctx The context to set as current.
 */
ngf_error ngf_set_context(ngf_context ctx) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Begin a frame of rendering.
 * 
 * This function starts a frame of rendering in the calling thread's current context.
 * It generates a special token associated with the frame, which is required for recording
 * command buffers (see \ref ngf_start_cmd_buffer).
 * @param token A pointer to a \ref ngf_frame_token. The generated frame token shall be returned here.
 */
ngf_error ngf_begin_frame(ngf_frame_token* token) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * End the current frame of rendering on the calling thread's context.
 * 
 * @param token The frame token generated by the corresponding preceding call to \ref ngf_begin_frame.
 */
ngf_error ngf_end_frame(ngf_frame_token token) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * @return A pointer to an \ref ngf_device_capabilities instance, or NULL, if no context is present
 *         on the calling thread.
 */
const ngf_device_capabilities* ngf_get_device_capabilities(void) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Creates a new shader stage object.
 *
 * @param stages Information required to construct the shader stage object.
 * @param result Pointer to where the handle to the newly created object will be returned.
 */
ngf_error
ngf_create_shader_stage(const ngf_shader_stage_info* info, ngf_shader_stage* result) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Destroys the given shader stage.
 * 
 * @param stage The handle to the shader stage object to be destroyed.
 */
void ngf_destroy_shader_stage(ngf_shader_stage stage) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Creates a new graphics pipeline object.
 * 
 * @param info Information required to construct the graphics pipeline object.
 * @param result Pointer to where the handle to the newly created object will be returned.
 */
ngf_error ngf_create_graphics_pipeline(
    const ngf_graphics_pipeline_info* info,
    ngf_graphics_pipeline*            result) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Destroys the given graphics pipeline object.
 * 
 * @param pipeline The handle to the pipeline object to be destroyed.
 */
void ngf_destroy_graphics_pipeline(ngf_graphics_pipeline pipeline) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Creates a new image object.
 * 
 * @param info Information required to construct the image object.
 * @param result Pointer to where the handle to the newly created object will be returned.
 */
ngf_error ngf_create_image(const ngf_image_info* info, ngf_image* result) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Destroys the given image object.
 * 
 * @param image The handle to the image object to be destroyed.
 */
void ngf_destroy_image(ngf_image image) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Creates a new sampler object.
 * 
 * @param info Information required to construct the sampler object.
 * @param result Pointer ti where the handle to the newly created object will be returned.
 */
ngf_error ngf_create_sampler(const ngf_sampler_info* info, ngf_sampler* result) NGF_NOEXCEPT;

/**
 * \ingroup ngf
 * 
 * Destroys the given sampler object.
 * 
 * @param ssampler The handle to the sampler object to be destroyed.
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
void ngf_cmd_bind_resources(
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
