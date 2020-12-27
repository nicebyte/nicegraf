/**
 * Copyright (c) 2019 nicegraf contributors
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
#define _CRT_SECURE_NO_WARNINGS
#include "nicegraf.h"
#include "nicegraf_internal.h"
#include "dynamic_array.h"
#include "gl_43_core.h"
#define EGLAPI // prevent __declspec(dllimport) issue on Windows
#include "EGL/egl.h"
#include "EGL/eglext.h"
#if defined(WIN32)
#include <malloc.h>
#else
#include <alloca.h>
#endif
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#if defined(WIN32)
#if defined(alloca)
#undef alloca
#endif
#define alloca _alloca
#endif

#if !defined(NGF_EMULATED_SPEC_CONST_PREFIX)
#define NGF_EMULATED_SPEC_CONST_PREFIX "SPIRV_CROSS_CONSTANT_ID_"
#endif

#pragma region ngf_impl_type_definitions

struct ngf_graphics_pipeline_t {
  uint32_t id;
  GLuint program_pipeline;
  GLuint vao;
  ngf_irect2d viewport;
  ngf_irect2d scissor;
  ngf_rasterization_info rasterization;
  ngf_multisample_info multisample;
  ngf_depth_stencil_info depth_stencil;
  ngf_blend_info blend;
  uint32_t dynamic_state_mask;
  const ngf_vertex_buf_binding_desc *vert_buf_bindings;
  uint32_t nvert_buf_bindings;
  GLenum primitive_type;
  uint32_t ndescriptors_layouts;
  ngfi_native_binding_map binding_map;
  GLuint owned_stages[NGF_STAGE_COUNT];
  uint32_t nowned_stages;
};

#define NGFGL_MAX_DRAW_BUFFERS 5

struct ngf_render_target_t {
  bool   is_srgb;
  GLuint framebuffer;
  size_t ndraw_buffers;
  GLenum draw_buffers[NGFGL_MAX_DRAW_BUFFERS];
  uint32_t nattachments;
  ngf_attachment attachment_infos[];
};

typedef struct ngfgl_vbuf_binding_info_t {
  uint32_t binding;
  GLuint buffer;
  size_t offset;
} ngfgl_vbuf_binding_info;

struct ngf_context_t {
  EGLDisplay dpy;
  EGLContext ctx;
  EGLConfig cfg;
  EGLSurface surface;
  struct {
    struct ngf_graphics_pipeline_t pipeline;
    NGFI_DARRAY_OF(ngfgl_vbuf_binding_info) vbuf_table;
     GLuint bound_index_buffer;
  } cached_state;
  bool has_bound_pipeline;
  bool has_swapchain;
  bool has_depth;
  bool srgb_surface;
  ngf_present_mode present_mode;
  ngf_type bound_index_buffer_type;
};

struct ngf_shader_stage_t {
  GLuint glprogram;
  GLenum gltype;
  GLenum glstagebit;

  char *source_code;
  GLint source_code_size;
};

struct ngf_attrib_buffer_t {
  GLuint glbuffer;
};

struct ngf_index_buffer_t {
  GLuint glbuffer;
};

struct ngf_uniform_buffer_t {
  GLuint glbuffer;
  size_t size;
};

struct ngf_pixel_buffer_t {
  GLuint glbuffer;
  size_t size;
};

struct ngf_image_t {
  GLuint glimage;
  GLenum bind_point;
  bool is_renderbuffer;
  bool is_multisample;
  bool is_srgb;
  GLenum glformat;
  GLenum gltype;
};

struct ngf_sampler_t {
  GLuint glsampler;
};

struct ngf_pass {
  ngf_clear *clears;
  ngf_attachment_load_op *loadops;
  uint32_t nloadops;
};

typedef enum {
  NGFGL_CMD_BIND_PIPELINE,
  NGFGL_CMD_BEGIN_PASS,
  NGFGL_CMD_END_PASS,
  NGFGL_CMD_VIEWPORT,
  NGFGL_CMD_SCISSOR,
  NGFGL_CMD_LINE_WIDTH,
  NGFGL_CMD_BLEND_CONSTANTS,
  NGFGL_CMD_STENCIL_REFERENCE,
  NGFGL_CMD_STENCIL_WRITE_MASK,
  NGFGL_CMD_STENCIL_COMPARE_MASK,
  NGFGL_CMD_BIND_UNIFORM_BUFFER,
  NGFGL_CMD_BIND_TEXTURE,
  NGFGL_CMD_BIND_SAMPLER,
  NGFGL_CMD_BIND_ATTRIB_BUFFER,
  NGFGL_CMD_BIND_INDEX_BUFFER,
  NGFGL_CMD_DRAW,
  NGFGL_CMD_DRAW_INDEXED,
  NGFGL_CMD_COPY,
  NGFGL_CMD_WRITE_IMAGE,
  NGFGL_CMD_NONE
} ngfgl_emulated_cmd_type;

typedef struct ngfgl_emulated_cmd {
  ngfgl_emulated_cmd_type type;
  union {
    ngf_graphics_pipeline pipeline;
    ngf_irect2d viewport;
    ngf_irect2d scissor;
    float line_width;
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
    struct {
      GLuint buffer;
      GLuint index;
      GLsizei offset;
      GLsizei range;
    } uniform_buffer_bind_op;
    struct {
      ngf_image texture;
      GLuint unit;
    } texture_bind_op;
    struct {
      ngf_sampler sampler;
      GLuint unit;
    } sampler_bind_op;
    struct {
      ngf_attrib_buffer buf;
      uint32_t binding;
      uint32_t offset;
    } attrib_buffer_bind_op;
    struct {
      ngf_render_target target;
    } begin_pass;
    struct {
      ngf_index_buffer index_buffer;
      ngf_type type;
    } index_buffer_bind;
    struct {
      uint32_t nelements;
      uint32_t ninstances;
      uint32_t first_element;
      bool indexed;
    } draw;
    struct {
      GLuint src;
      GLuint dst;
      size_t size;
      size_t src_offset;
      size_t dst_offset;
    } copy;
    struct {
      GLuint src_pbuffer;
      size_t src_data_offset;
      ngf_image_ref dst_image_ref;
      ngf_offset3d offset;
      ngf_extent3d dimensions;
    } write_image;
  };
} ngfgl_emulated_cmd;

#define NGFGL_CMDS_PER_CMD_BLOCK  16u

typedef struct ngfgl_cmd_block {
  struct ngfgl_cmd_block *next;
  ngfgl_emulated_cmd      cmds[NGFGL_CMDS_PER_CMD_BLOCK];
  uint32_t                next_cmd_idx;
} ngfgl_cmd_block;

struct ngf_cmd_buffer_t {
  ngf_graphics_pipeline bound_pipeline;
  ngfgl_cmd_block *first_cmd_block;
  ngfgl_cmd_block *last_cmd_block;
  bool renderpass_active;
  ngfi_cmd_buffer_state state;
};
#pragma endregion

#pragma region ngf_impl_enum_maps
static GLenum gl_shader_stage(ngf_stage_type stage) {
  static const GLenum stages[NGF_STAGE_COUNT] = {
    GL_VERTEX_SHADER,
    GL_FRAGMENT_SHADER
  };
  return stages[stage];
}

static GLenum get_gl_shader_stage_bit(ngf_stage_type stage) {
  static const GLenum stages[NGF_STAGE_COUNT] = {
    GL_VERTEX_SHADER_BIT,
    GL_FRAGMENT_SHADER_BIT
  };
  return stages[stage];
}

static GLenum get_gl_type(ngf_type t) {
  static const GLenum types[NGF_TYPE_COUNT] = {
    GL_BYTE,
    GL_UNSIGNED_BYTE,
    GL_SHORT,
    GL_UNSIGNED_SHORT,
    GL_INT,
    GL_UNSIGNED_INT,
    GL_FLOAT,
    GL_HALF_FLOAT,
    GL_DOUBLE
  };
  
  return types[t];
}

static GLenum get_gl_poly_mode(ngf_polygon_mode m) {
  static const GLenum poly_mode[NGF_POLYGON_MODE_COUNT] = {
    GL_FILL,
    GL_LINE,
    GL_POINT
  };
  return poly_mode[(size_t)(m)];
}

static GLenum get_gl_cull_mode(ngf_cull_mode m) {
  static const GLenum cull_mode[NGF_CULL_MODE_COUNT] = {
    GL_BACK,
    GL_FRONT,
    GL_FRONT_AND_BACK,
    GL_NONE // not really a valid culling mode but GL has no
            // separate enum entry for "no culling", it's turned
            // on and off with a function call.
  };
  return cull_mode[(size_t)(m)];
}

static GLenum get_gl_face(ngf_front_face_mode m) {
  static const GLenum face[NGF_FRONT_FACE_COUNT] = {
    GL_CCW,
    GL_CW
  };
  return face[(size_t)(m)];
}

static GLenum get_gl_compare(ngf_compare_op op) {
  static const GLenum compare[NGF_COMPARE_OP_COUNT] = {
    GL_NEVER,
    GL_LESS,
    GL_LEQUAL,
    GL_EQUAL,
    GL_GEQUAL,
    GL_GREATER,
    GL_NOTEQUAL,
    GL_ALWAYS,
  };

  return compare[(size_t)(op)];
}

static GLenum get_gl_stencil_op(ngf_stencil_op op) {
  static const GLenum o[NGF_STENCIL_OP_COUNT] = {
    GL_KEEP,
    GL_ZERO,
    GL_REPLACE,
    GL_INCR,
    GL_INCR_WRAP,
    GL_DECR,
    GL_DECR_WRAP,
    GL_INVERT
  };

  return o[(size_t)(op)];
}

static GLenum get_gl_blend_factor(ngf_blend_factor f) {
  static const GLenum factor[NGF_BLEND_FACTOR_COUNT] = {
    GL_ZERO,
    GL_ONE,
    GL_SRC_COLOR,
    GL_ONE_MINUS_SRC_COLOR,
    GL_DST_COLOR,
    GL_ONE_MINUS_DST_COLOR,
    GL_SRC_ALPHA,
    GL_ONE_MINUS_SRC_ALPHA,
    GL_DST_ALPHA,
    GL_ONE_MINUS_DST_ALPHA,
    GL_CONSTANT_COLOR,
    GL_ONE_MINUS_CONSTANT_COLOR,
    GL_CONSTANT_ALPHA,
    GL_ONE_MINUS_CONSTANT_ALPHA
  };
  return factor[(size_t)(f)];
}

static GLenum get_gl_blend_op(ngf_blend_op op) {
  static const GLenum ops[NGF_BLEND_OP_COUNT] = {
    GL_FUNC_ADD,
    GL_FUNC_SUBTRACT,
    GL_FUNC_REVERSE_SUBTRACT,
    GL_MIN,
    GL_MAX
  };
  return ops[op];
}

static GLenum get_gl_primitive_type(ngf_primitive_type p) {
  static const GLenum primitives[NGF_PRIMITIVE_TYPE_COUNT] = {
    GL_TRIANGLES,
    GL_TRIANGLE_STRIP,
    GL_TRIANGLE_FAN,
    GL_LINES,
    GL_LINE_STRIP
  };
  return primitives[p];
}

typedef struct glformat {
  GLenum internal_format;
  GLenum format;
  GLenum type;
  uint8_t rbits;
  uint8_t gbits;
  uint8_t bbits;
  uint8_t abits;
  uint8_t dbits;
  uint8_t sbits;
  bool    srgb;
} glformat;

static glformat get_gl_format(ngf_image_format f) {
  static const glformat formats[NGF_IMAGE_FORMAT_COUNT] = {
    {GL_R8, GL_RED, GL_UNSIGNED_BYTE, 8, 0, 0, 0, 0, 0, false},
    {GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 8, 8, 0, 0, 0, 0, false},
    {GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 8, 8, 8, 0, 0, 0, false},
    {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 8, 8, 8, 8, 0, 0, false},
    {GL_SRGB8, GL_RGB, GL_UNSIGNED_BYTE, 8, 8, 8, 0, 0, 0, true},
    {GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE, 8, 8, 8, 8, 0, 0, true},
    {GL_RGB8, GL_BGR, GL_UNSIGNED_BYTE, 8, 8, 8, 0, 0, 0, false},
    {GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE, 8, 8, 8, 8, 0, 0, false},
    {GL_SRGB8, GL_BGR, GL_UNSIGNED_BYTE, 8, 8, 8, 0, 0, 0, true},
    {GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE, 8, 8, 8, 8, 0, 0, true},
    {GL_R32F, GL_RED, GL_FLOAT, 32, 0, 0, 0, 0, 0, false},
    {GL_RG32F, GL_RG, GL_FLOAT, 32, 32, 0, 0, 0, 0, false},
    {GL_RGB32F, GL_RGB, GL_FLOAT, 32, 32, 32, 0, 0, 0,  false},
    {GL_RGBA32F, GL_RGB, GL_FLOAT, 32, 32, 32, 32, 0, 0,  false},
    {GL_R16F, GL_RED, GL_HALF_FLOAT, 16, 0, 0, 0, 0, 0, false},
    {GL_RG16F, GL_RG, GL_HALF_FLOAT, 16, 16, 0, 0, 0, 0, false},
    {GL_RGB16F, GL_RGB, GL_HALF_FLOAT, 16, 16, 16, 0, 0, 0, false},
    {GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 16, 16, 16, 16, 0, 0, false},
    {GL_R11F_G11F_B10F, GL_RGB, GL_HALF_FLOAT, 11, 11, 10, 0, 0, 0, false},
    {GL_R16, GL_RED, GL_UNSIGNED_SHORT, 16, 0, 0, 0, 0, 0, false},
    {GL_R16_SNORM, GL_RED, GL_SHORT, 16, 0, 0, 0, 0, 0, false},
    {GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, 16, 0, 0, 0, 0, 0, false},
    {GL_R16I, GL_RED_INTEGER, GL_SHORT, 16, 0, 0, 0, 0, 0, false},
    {GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT, 16, 16, 0, 0, 0, 0, false},
    {GL_RGB16UI, GL_RGB_INTEGER, GL_UNSIGNED_SHORT, 16, 16, 16, 0, 0, 0, false},
    {GL_RGBA16UI, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, 16, 16, 16, 16, 0, 0, false},
    {GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, 32, 0, 0, 0, 0, 0, false},
    {GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT, 32, 32, 0, 0, 0, 0, false},
    {GL_RGB32UI, GL_RGB_INTEGER, GL_UNSIGNED_INT, 32, 32, 32, 0, 0, 0, false},
    {GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, 32, 32, 32, 32, 0, 0, false},
    {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 0, 0, 0, 0, 32, 0,
     false},
    {GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_FLOAT, 0, 0, 0, 0, 16, 0,
     false},
    {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_FLOAT, 0, 0, 0, 0, 24, 8,
     false},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, false}
  };
  return formats[f];
}

static GLenum get_gl_filter(ngf_sampler_filter f) {
  static const GLenum filters[NGF_FILTER_COUNT] = {
    GL_NEAREST,
    GL_LINEAR,
  };

  return filters[f];
}

static GLenum get_min_mip_filter(GLenum min_filter,
                                 GLenum mip_filter) {
  if (min_filter == GL_LINEAR && mip_filter == GL_NEAREST) {
    return GL_LINEAR_MIPMAP_NEAREST;
  } else if (min_filter == GL_LINEAR && mip_filter == GL_LINEAR) {
    return GL_LINEAR_MIPMAP_LINEAR;
  } else if (min_filter == GL_NEAREST && mip_filter == GL_LINEAR) {
    return GL_NEAREST_MIPMAP_LINEAR;
  } else {
    return GL_NEAREST_MIPMAP_NEAREST;
  }
}

static GLenum get_gl_wrap(ngf_sampler_wrap_mode e) {
  static const GLenum modes[NGF_WRAP_MODE_COUNT] = {
    GL_CLAMP_TO_EDGE,
    GL_CLAMP_TO_BORDER,
    GL_REPEAT,
    GL_MIRRORED_REPEAT
  };
  return modes[e];
}

static GLenum get_gl_cubemap_face(ngf_cubemap_face face) {
  static const GLenum faces[NGF_CUBEMAP_FACE_COUNT] = {
    GL_TEXTURE_CUBE_MAP_POSITIVE_X,
    GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
    GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
    GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
    GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
  };
  return faces[face];
}

#pragma endregion

static ngf_diagnostic_info ngfi_diag_info = {
  .verbosity = NGF_DIAGNOSTICS_VERBOSITY_DEFAULT,
  .userdata  = NULL,
  .callback  = NULL
};

static void GL_APIENTRY ngfgl_debug_message_callback(
  GLenum source,
  GLenum type,
  GLuint id,
  GLenum severity,
  GLsizei length,
  const GLchar* message,
  const void* userdata) {
  NGFI_FAKE_USE(length, severity, source, type, id, userdata);
  if (ngfi_diag_info.callback) {
    ngf_diagnostic_message_type ngf_msg_type;
    switch (severity) {
    case GL_DEBUG_SEVERITY_NOTIFICATION:
      ngf_msg_type = NGF_DIAGNOSTIC_INFO;
      break;
    case GL_DEBUG_SEVERITY_LOW:
      ngf_msg_type = NGF_DIAGNOSTIC_WARNING;
      break;
    default:
      ngf_msg_type = NGF_DIAGNOSTIC_ERROR;
    }
    ngfi_diag_info.callback(ngf_msg_type, ngfi_diag_info.userdata, message);
  }
}

ngf_error ngf_initialize(const ngf_init_info *init_info) {
  ngfi_diag_info = init_info->diag_info;
  ngfi_device_caps_create();
  return NGF_ERROR_OK;
}

ngf_error ngf_create_context(const ngf_context_info *info,
                             ngf_context *result) {
  assert(info);
  assert(result);
  
  ngf_error err_code = NGF_ERROR_OK;
  const ngf_swapchain_info *swapchain_info = info->swapchain_info;
  const ngf_context shared = info->shared_context;

  *result = NGFI_ALLOC(struct ngf_context_t);
  ngf_context ctx = *result;
  if (ctx == NULL) {
    err_code = NGF_ERROR_OUT_OF_MEM;
    goto ngf_create_context_cleanup;
  }

  // Connect to a display.
  eglBindAPI(EGL_OPENGL_API);
  ctx->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(ctx->dpy != EGL_NO_DISPLAY);
  int egl_maj, egl_min;
  if (eglInitialize(ctx->dpy, &egl_maj, &egl_min) == EGL_FALSE) {
    NGFI_DIAG_ERROR("Failed to initialize EGL, EGL error: %d", eglGetError());
    err_code = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngf_create_context_cleanup;
  }

  // Set present mode.
  if (swapchain_info != NULL) {
    ctx->has_swapchain = true;
    ctx->present_mode = swapchain_info->present_mode;
  } else {
    ctx->has_swapchain = false;
  }

  // Choose EGL config.
  EGLint config_attribs[28];
  size_t a = 0;
  config_attribs[a++] = EGL_CONFORMANT; config_attribs[a++] = EGL_OPENGL_BIT;
  if (swapchain_info != NULL) {
    const glformat color_format = get_gl_format(swapchain_info->cfmt);
    const glformat depth_stencil_format = get_gl_format(swapchain_info->dfmt);
    config_attribs[a++] = EGL_COLOR_BUFFER_TYPE;
    config_attribs[a++] = EGL_RGB_BUFFER;
    config_attribs[a++] = EGL_RED_SIZE;
    config_attribs[a++] = color_format.rbits;
    config_attribs[a++] = EGL_GREEN_SIZE;
    config_attribs[a++] = color_format.gbits;
    config_attribs[a++] = EGL_BLUE_SIZE;
    config_attribs[a++] = color_format.bbits;
    config_attribs[a++] = EGL_ALPHA_SIZE;
    config_attribs[a++] = color_format.abits;
    config_attribs[a++] = EGL_DEPTH_SIZE;
    config_attribs[a++] = depth_stencil_format.dbits;
    config_attribs[a++] = EGL_STENCIL_SIZE;
    config_attribs[a++] = depth_stencil_format.sbits;
    config_attribs[a++] = EGL_SAMPLE_BUFFERS;
    config_attribs[a++] = swapchain_info->nsamples > 0 ? 1 : 0;
    config_attribs[a++] = EGL_SAMPLES;
    config_attribs[a++] = swapchain_info->nsamples;
    config_attribs[a++] = EGL_SURFACE_TYPE;
    config_attribs[a++] = EGL_WINDOW_BIT;
  } 
  config_attribs[a++] = EGL_NONE;
  EGLint num = 0;
  if(!eglChooseConfig(ctx->dpy, config_attribs, &ctx->cfg, 1, &num)) {
    NGFI_DIAG_ERROR("Failed to choose EGL config, EGL error: %d", eglGetError());
    err_code = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngf_create_context_cleanup;
  }
  ctx->has_depth = (swapchain_info->dfmt != NGF_IMAGE_FORMAT_UNDEFINED);

  // Create context with chosen config.
  EGLint is_debug = (ngfi_diag_info.verbosity == NGF_DIAGNOSTICS_VERBOSITY_DETAILED);
  EGLint context_attribs[] = {
    EGL_CONTEXT_MAJOR_VERSION, 4,
    EGL_CONTEXT_MINOR_VERSION, 3,
    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
    EGL_CONTEXT_OPENGL_DEBUG, is_debug,
    EGL_NONE
  };
  EGLContext egl_shared = shared ? shared->ctx : EGL_NO_CONTEXT;
  ctx->ctx =
    eglCreateContext(ctx->dpy, ctx->cfg, egl_shared, context_attribs);
  if (ctx->ctx == EGL_NO_CONTEXT) {
    NGFI_DIAG_ERROR("Failed create EGL context, EGL error: %d", eglGetError());
    err_code = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngf_create_context_cleanup;
  }

  // Create surface if necessary.
  ctx->srgb_surface = false;
  if (swapchain_info) {
    const glformat color_format = get_gl_format(swapchain_info->cfmt);
    EGLint egl_surface_attribs[] = {
      EGL_RENDER_BUFFER, swapchain_info->capacity_hint <= 1
                             ? EGL_SINGLE_BUFFER
                             : EGL_BACK_BUFFER,
      EGL_GL_COLORSPACE_KHR, color_format.srgb ? EGL_GL_COLORSPACE_SRGB_KHR
                                               : EGL_GL_COLORSPACE_LINEAR_KHR,
      EGL_NONE
    };
    ctx->srgb_surface = color_format.srgb;
    ctx->surface = eglCreateWindowSurface(ctx->dpy,
                                          ctx->cfg,
                                          (EGLNativeWindowType)swapchain_info->native_handle,
                                          egl_surface_attribs);
    if (ctx->surface == EGL_NO_SURFACE) {
      NGFI_DIAG_ERROR("Failed to create EGL window surface, EGL error: %d", eglGetError());
      err_code = NGF_ERROR_OBJECT_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }
  } else {
    ctx->surface = EGL_NO_SURFACE;
  }

  ctx->has_bound_pipeline = false;
  NGFI_DARRAY_RESET(ctx->cached_state.vbuf_table, 10);
  ctx->cached_state.bound_index_buffer = GL_NONE;

ngf_create_context_cleanup:
  if (err_code != NGF_ERROR_OK) {
    ngf_destroy_context(ctx);
  }
  *result = ctx;
  return err_code;
}

ngf_error ngf_resize_context(ngf_context ctx,
                             uint32_t new_width,
                             uint32_t new_height) {
  NGFI_FAKE_USE(ctx, new_width, new_height);
  return NGF_ERROR_OK;
}

NGFI_THREADLOCAL ngf_context CURRENT_CONTEXT = NULL;
NGFI_THREADLOCAL ngfi_block_allocator *COMMAND_POOL = NULL;

ngf_error ngf_set_context(ngf_context ctx) {
  assert(ctx);
  if (CURRENT_CONTEXT == ctx) {
    NGFI_DIAG_WARNING("Attempt to set a context that is already current on the calling thread.");
    return NGF_ERROR_OK;
  }
  if (CURRENT_CONTEXT && (CURRENT_CONTEXT != ctx)) {
    NGFI_DIAG_ERROR("Attempt to set a context when the calling thread already has a current context.");
    return NGF_ERROR_INVALID_OPERATION;
  }

  bool result = eglMakeCurrent(ctx->dpy, ctx->surface, ctx->surface, ctx->ctx);
  if (result) {
    CURRENT_CONTEXT = ctx;
    if (ctx->has_swapchain) {
      if (ctx->present_mode == NGF_PRESENTATION_MODE_FIFO) {
        eglSwapInterval(ctx->dpy, 1);
      } else {
        eglSwapInterval(ctx->dpy, 0);
      }
    }
    glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
    if (ngfi_diag_info.verbosity == NGF_DIAGNOSTICS_VERBOSITY_DETAILED) {
      glEnable(GL_DEBUG_OUTPUT);
      glDebugMessageControl(GL_DONT_CARE,
                            GL_DONT_CARE,
                            GL_DONT_CARE,
                            0,
                            NULL,
                            GL_TRUE);
      glDebugMessageCallback(ngfgl_debug_message_callback, NULL);
    }

    /* Check if ARB|EXT_clip_control is present, set clipspace z range to [0; 1] if yes. */
    void (*glClipControl)(GLenum, GLenum) =
      (void (*)(GLenum, GLenum))eglGetProcAddress("glClipControl");
    if (glClipControl) {
      glClipControl(0x8CA1/*GL_LOWER_LEFT*/, 0x935F/*GL_ZERO_TO_ONE*/);
    }

    /* Initialize the device capabilities structure if necessary. */
    ngf_device_capabilities* caps_ptr = ngfi_device_caps_lock();
    if (caps_ptr) {
      caps_ptr->clipspace_z_zero_to_one = (glClipControl != NULL);
      ngfi_device_caps_unlock(caps_ptr);
    }

    return NGF_ERROR_OK;
  } else {
    NGFI_DIAG_ERROR("Failed to make EGL context current, EGL error: %d", eglGetError());
    return NGF_ERROR_INVALID_OPERATION;
  }
}

void ngf_destroy_context(ngf_context ctx) {
  if (ctx) {
    if (ctx->ctx != EGL_NO_CONTEXT) {
      eglDestroyContext(ctx->dpy, ctx->ctx);
    }
    if (ctx->surface != EGL_NO_SURFACE) {
      eglDestroySurface(ctx->dpy, ctx->surface);
    }
    eglTerminate(ctx->dpy);
    NGFI_DARRAY_DESTROY(ctx->cached_state.vbuf_table);
    NGFI_FREE(ctx);
  }
}

const ngf_device_capabilities* ngf_get_device_capabilities() {
  return ngfi_device_caps_read();
}

ngf_error ngfgl_check_link_status(GLuint program, const char *debug_name) {
  GLint link_status;
  glGetProgramiv(program, GL_LINK_STATUS, &link_status);
  if (link_status != GL_TRUE) {
    // See previous comment about debug callback.
    GLint info_log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_length);
    char *info_log = malloc((size_t)info_log_length + 1u);
    info_log[info_log_length] = '\0';
    glGetProgramInfoLog(program, info_log_length, &info_log_length,
                        info_log);
    if (debug_name) {
      NGFI_DIAG_MSG(NGF_DIAGNOSTIC_ERROR, "Error linking %s", debug_name);
    }
    NGFI_DIAG_MSG(NGF_DIAGNOSTIC_ERROR, info_log);
    free(info_log);
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }
  return NGF_ERROR_OK;
}

ngf_error ngfgl_compile_shader(const char *source, GLint source_len,
                              const char *debug_name,
                              GLenum stage,
                              const ngf_specialization_info *spec_info,
                              GLuint *result) {
  ngf_error err = NGF_ERROR_OK;
  *result = GL_NONE;
  GLuint shader = GL_NONE;

  // Obtain separate pointers to the first line of input (#version directive)
  // and the rest of input. We will later insert additional defines between
  // them.
  const char *first_line = source;
  while (*first_line != '#' && first_line - source < source_len) first_line++;
  if (strncmp(first_line, "#version", 8u)) {
    NGFI_DIAG_ERROR("First line in GL shader must specify GLSL version");
    return NGF_ERROR_OBJECT_CREATION_FAILED;
  }
  const char *rest_of_source = first_line;
  while(*(rest_of_source++) != '\n' &&
        (rest_of_source - source < source_len));
  const GLint first_line_len = (GLint)(rest_of_source - first_line);

  // We will split the shader source into up to 3 chunks that will be fed to
  // glShaderSource.
  // The first chunk will always be the "#version..." line.
  // If constant specialization is being done (spec_info != NULL), the second
  // chunk will contain the corresponding #defines and the third chunk will
  // contain the rest of the shader code.
  // If no specialization is being done, the second chunk will contain the rest
  // of the shader code, and there will be no third chunk.
  const GLsizei nsource_chunks = 2 + (spec_info == NULL ? 0 : 1);
  const char *source_chunks[3];
  GLint source_chunk_lengths[3];
  
  // First chunk - always the "#version..." line. 
  source_chunks[0] = first_line;
  source_chunk_lengths[0] = first_line_len;

  char *defines_buffer = NULL;
  if (spec_info != NULL) {
    // Specialization is being done - generate appropriate #defines for the
    // second source chunk.
    static char spec_define_template[] =
        "#define "
        NGF_EMULATED_SPEC_CONST_PREFIX
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

    // Allocate enough space to hold all the #defines.
    const uint32_t spec_define_max_size = NGFI_ARRAYSIZE(spec_define_template);
    const uint32_t defines_buffer_size =
        spec_define_max_size * spec_info->nspecializations;
    defines_buffer = NGFI_ALLOCN(char, defines_buffer_size);
    if (defines_buffer == NULL) {
      err = NGF_ERROR_OUT_OF_MEM;
      goto ngfgl_compile_shader_cleanup;
    }

    // Generate a #define for each specialization entry.
    char *defines_buffer_write_ptr = defines_buffer;
    for (uint32_t i= 0u; i < spec_info->nspecializations; ++i) {
      // Specialization entry.
      const ngf_constant_specialization *spec = &spec_info->specializations[i];

      // Ptr to spec constant value.
      const uint8_t *data = (uint8_t*)spec_info->value_buffer + spec->offset;

      uint32_t bytes_written = 0;  // bytes written during this iteration.

      // Write the #define to the buffer
      #define WRITE_SPEC_DEFINE(format, type) \
          bytes_written = snprintf(defines_buffer_write_ptr,\
                                   spec_define_max_size, \
                                   "#define %s%d " format "\n",\
                                   NGF_EMULATED_SPEC_CONST_PREFIX, \
                                   spec->constant_id, \
                                   *(const type*)data);
      switch (spec->type) {
      case NGF_TYPE_DOUBLE: WRITE_SPEC_DEFINE("%f", double); break;
      case NGF_TYPE_FLOAT:
      case NGF_TYPE_HALF_FLOAT: WRITE_SPEC_DEFINE("%f", float); break;
      case NGF_TYPE_INT8: WRITE_SPEC_DEFINE("%d", int8_t); break;
      case NGF_TYPE_INT16: WRITE_SPEC_DEFINE("%d", int16_t); break;
      case NGF_TYPE_INT32: WRITE_SPEC_DEFINE("%d", int32_t); break;
      case NGF_TYPE_UINT8: WRITE_SPEC_DEFINE("%d", uint8_t); break;
      case NGF_TYPE_UINT16: WRITE_SPEC_DEFINE("%d", uint16_t); break;
      case NGF_TYPE_UINT32: WRITE_SPEC_DEFINE("%d", uint32_t); break;
      default: assert(false);
      }
      #undef WRITE_SPEC_DEFINE

      // Verify that snprintf did not return an error and did not discard any
      // characters (if it did, that means the written string was too long).
      if (bytes_written > 0 && bytes_written < spec_define_max_size) {
        defines_buffer_write_ptr += bytes_written;
      } else {
        err = NGF_ERROR_OBJECT_CREATION_FAILED;
        goto ngfgl_compile_shader_cleanup;
      }
    }
    source_chunks[1] = defines_buffer;
    source_chunk_lengths[1] =
        (GLint)(defines_buffer_write_ptr - defines_buffer);
  }

  // Final source chunk - rest of the shader code.
  source_chunks[nsource_chunks - 1] = rest_of_source;
  source_chunk_lengths[nsource_chunks - 1] = source_len -
                                              (GLint)(rest_of_source - source);

  // Compile the shader.
  shader = glCreateShader(stage);
  glShaderSource(shader, nsource_chunks, source_chunks, source_chunk_lengths);
  glCompileShader(shader);
  GLint compile_status;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
  if (compile_status != GL_TRUE) {
    // Note: theoretically, the OpenGL debug callback extension should
    // invoke the debug callback on shader compilation failure.
    // In practice, it varies between vendors, so we just force-call the
    // debug callback here to make sure it is always invoked. Sadness...
    // You should probably be validating your shaders through glslang as
    // one of the build steps anyways...
    GLint info_log_length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);
    char *info_log = malloc((size_t)info_log_length + 1u);
    info_log[info_log_length] = '\0';
    glGetShaderInfoLog(shader, info_log_length, &info_log_length, info_log);
    if (debug_name) {
      NGFI_DIAG_MSG(NGF_DIAGNOSTIC_ERROR, "Error compiling %s", debug_name);
    }
    NGFI_DIAG_MSG(NGF_DIAGNOSTIC_ERROR, info_log);
    free(info_log);
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngfgl_compile_shader_cleanup;
  }
  *result = glCreateProgram();
  glProgramParameteri(*result, GL_PROGRAM_SEPARABLE, GL_TRUE);
  glAttachShader(*result, shader);
  glLinkProgram(*result);
  glDetachShader(*result, shader);
  err = ngfgl_check_link_status(*result, debug_name);
  if (err != NGF_ERROR_OK) {
    goto ngfgl_compile_shader_cleanup;
  }

ngfgl_compile_shader_cleanup:
  if (shader != GL_NONE) {
    glDeleteShader(shader);
  }
  if (defines_buffer != NULL) {
    NGFI_FREEN(defines_buffer, spec_info->nspecializations);
  }
  if (err != NGF_ERROR_OK && *result != GL_NONE) {
    glDeleteProgram(*result);
  }
  return err;
}

ngf_error ngf_create_shader_stage(const ngf_shader_stage_info *info,
                                  ngf_shader_stage *result) {
  assert(info);
  assert(result);
  ngf_error err = NGF_ERROR_OK;
  ngf_shader_stage stage = NULL;
  *result = NGFI_ALLOC(struct ngf_shader_stage_t);
  stage = *result;
  if (stage == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_create_shader_stage_cleanup;
  }
  stage->gltype = gl_shader_stage(info->type);
  stage->glstagebit = get_gl_shader_stage_bit(info->type);
  stage->source_code = NULL;
  stage->source_code_size = 0u;
  // Save off the source code in case we need to recompile for pipelines
  // doing specialization.
  stage->source_code = NGFI_ALLOCN(char, info->content_length);
  if (stage->source_code == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_create_shader_stage_cleanup;
  }
  stage->source_code_size = (GLint)info->content_length;
  strncpy(stage->source_code, info->content, info->content_length);
  err = ngfgl_compile_shader(stage->source_code, stage->source_code_size,
                            info->debug_name, stage->gltype, NULL,
                            &stage->glprogram);
ngf_create_shader_stage_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_shader_stage(stage);
  }
  return err;
}

void ngf_destroy_shader_stage(ngf_shader_stage stage) {
  if (stage != NULL) {
    glDeleteProgram(stage->glprogram);
    if (stage->source_code != NULL) {
      NGFI_FREEN(stage->source_code, (size_t)stage->source_code_size);
    }
    NGFI_FREE(stage);
  }
}

const ngf_plmd_cis_map_entry * _lookup_cis_map(uint32_t set, uint32_t binding,
                                               const ngf_plmd_cis_map *map) {
  for (uint32_t i = 0u; i < map->nentries; ++i) {
    if (set == map->entries[i]->separate_set_id &&
        binding == map->entries[i]->separate_binding_id) {
      return (map->entries[i]);
    }
  }
  return NULL;
}

ngf_error ngf_create_graphics_pipeline(const ngf_graphics_pipeline_info *info,
                                       ngf_graphics_pipeline *result) {
  static uint32_t global_id = 0;
  ngf_error err = NGF_ERROR_OK;

  *result = NGFI_ALLOC(struct ngf_graphics_pipeline_t);
  ngf_graphics_pipeline pipeline = *result;
  if (pipeline == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_create_pipeline_cleanup;
  }

  // Copy over some state.
  pipeline->viewport = *(info->viewport);
  pipeline->scissor = *(info->scissor);
  pipeline->rasterization = *(info->rasterization);
  pipeline->multisample = *(info->multisample);
  pipeline->depth_stencil = *(info->depth_stencil);
  pipeline->blend = *(info->blend);
  
  const ngf_vertex_input_info *input = info->input_info;

  // Copy over vertex buffer binding information.
  pipeline->nvert_buf_bindings = input->nvert_buf_bindings;
  if (input->nvert_buf_bindings > 0) {
    ngf_vertex_buf_binding_desc *vert_buf_bindings =
        NGFI_ALLOCN(ngf_vertex_buf_binding_desc,
                   input->nvert_buf_bindings);
    pipeline->vert_buf_bindings = vert_buf_bindings;
    if (pipeline->vert_buf_bindings == NULL) {
      err = NGF_ERROR_OUT_OF_MEM;
      goto ngf_create_pipeline_cleanup;
    }
    memcpy(vert_buf_bindings,
           input->vert_buf_bindings,
           sizeof(ngf_vertex_buf_binding_desc) * input->nvert_buf_bindings);
  } else {
    pipeline->vert_buf_bindings = NULL;
  }

  // Create a map of NGF -> OpenGL resource bindings.
  const ngf_pipeline_layout_info *pipeline_layout = info->layout;
  pipeline->ndescriptors_layouts = pipeline_layout->ndescriptor_set_layouts;
  err = ngfi_create_native_binding_map(pipeline_layout,
                                       info->image_to_combined_map,
                                       info->sampler_to_combined_map,
                                       &pipeline->binding_map);
  if (err != NGF_ERROR_OK) {
    goto ngf_create_pipeline_cleanup;
  }

  // Store attribute format in VAO.
  glGenVertexArrays(1, &pipeline->vao);
  glBindVertexArray(pipeline->vao);
  for (size_t a = 0; a < input->nattribs; ++a) {
    const ngf_vertex_attrib_desc *attrib = &(input->attribs[a]);
    glEnableVertexAttribArray(attrib->location);
    if (attrib->normalized ||
        attrib->type == NGF_TYPE_FLOAT ||
        attrib->type == NGF_TYPE_DOUBLE ||
        attrib->type == NGF_TYPE_HALF_FLOAT) {
      glVertexAttribFormat(attrib->location,
                          (GLint)attrib->size,
                           get_gl_type(attrib->type),
                           attrib->normalized,
                           attrib->offset);
    } else {
      glVertexAttribIFormat(attrib->location,
                           (GLint)attrib->size,
                            get_gl_type(attrib->type),
                            attrib->offset);
    }
    glVertexAttribBinding(attrib->location, attrib->binding);
  }
  for (uint32_t b = 0; b < pipeline->nvert_buf_bindings; ++b) {
    glVertexBindingDivisor(pipeline->vert_buf_bindings[b].binding,
                           pipeline->vert_buf_bindings[b].input_rate);
  }
  glBindVertexArray(0);
  pipeline->primitive_type = get_gl_primitive_type(info->primitive_type);

  // Create and configure the program pipeline object with the provided
  // shader stages.
  if (info->nshader_stages >= NGFI_ARRAYSIZE(info->shader_stages)) {
    err = NGF_ERROR_OUT_OF_BOUNDS;
    goto ngf_create_pipeline_cleanup;
  }
  pipeline->nowned_stages = 0u;
  glGenProgramPipelines(1, &pipeline->program_pipeline);
  if (info->spec_info->nspecializations == 0u) {
    for (size_t s = 0; s < info->nshader_stages; ++s) {
      glUseProgramStages(pipeline->program_pipeline,
                         info->shader_stages[s]->glstagebit,
                         info->shader_stages[s]->glprogram);
    }
  } else {
    for (size_t s = 0; err == NGF_ERROR_OK && s < info->nshader_stages; ++s) {
      if (info->shader_stages[s]->source_code != NULL) {
        err =
            ngfgl_compile_shader(info->shader_stages[s]->source_code,
                                info->shader_stages[s]->source_code_size,
                                "",
                                info->shader_stages[s]->gltype,
                                info->spec_info,
                                &pipeline->
                                    owned_stages[pipeline->nowned_stages++]);

        glUseProgramStages(pipeline->program_pipeline,
                           info->shader_stages[s]->glstagebit,
                           pipeline->owned_stages[s]);
      } else {
        err = NGF_ERROR_OBJECT_CREATION_FAILED;
      }
    }   
  }

  // Set dynamic state mask.
  pipeline->dynamic_state_mask = info->dynamic_state_mask;

  // Assign a unique id to the pipeline.
  pipeline->id = ++global_id;

ngf_create_pipeline_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_graphics_pipeline(pipeline);
  } 
  return err;
}

void ngf_destroy_graphics_pipeline(ngf_graphics_pipeline pipeline) {
  if (pipeline) {
    if (pipeline->nvert_buf_bindings > 0 &&
        pipeline->vert_buf_bindings) {
      NGFI_FREEN(pipeline->vert_buf_bindings, pipeline->nvert_buf_bindings);
    }
    ngfi_destroy_binding_map(pipeline->binding_map);
    glDeleteProgramPipelines(1, &pipeline->program_pipeline);
    glDeleteVertexArrays(1, &pipeline->vao);
    for (uint32_t s = 0u; s < pipeline->nowned_stages; ++s) {
      glDeleteProgram(pipeline->owned_stages[s]);
    }
    NGFI_FREEN(pipeline, 1);
  }
}

ngf_error ngf_create_image(const ngf_image_info *info, ngf_image *result) {
  assert(info);
  assert(result);

  *result = NGFI_ALLOC(struct ngf_image_t);
  ngf_image image = *result;
  if (image == NULL) {
    return NGF_ERROR_OUT_OF_MEM;
  }

  const glformat glf = get_gl_format(info->format);
  image->glformat = glf.format;
  image->is_srgb = glf.srgb;
  image->gltype = glf.type;
  image->is_multisample = info->nsamples > 1;

  const bool cant_use_renderbuffer =
      info->usage_hint & NGF_IMAGE_USAGE_SAMPLE_FROM ||
      info->nmips > 1 ||
      info->extent.depth > 1 ||
      info->type != NGF_IMAGE_TYPE_IMAGE_2D;
  if (cant_use_renderbuffer) {
    image->is_renderbuffer = false;
    if (info->type == NGF_IMAGE_TYPE_IMAGE_2D &&
        info->extent.depth <= 1) {
      image->bind_point =
        info->nsamples > 1 ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;
    } else if (info->type == NGF_IMAGE_TYPE_IMAGE_2D &&
               info->extent.depth > 1) {
      image->bind_point =
          info->nsamples > 1
            ? GL_TEXTURE_2D_MULTISAMPLE_ARRAY
            : GL_TEXTURE_2D_ARRAY;
    } else if (info->type == NGF_IMAGE_TYPE_IMAGE_3D &&
               info->nsamples == 0u) {
      image->bind_point = GL_TEXTURE_3D;
    } else if (info->type == NGF_IMAGE_TYPE_CUBE &&
               info->nsamples == 0u) {
      image->bind_point = info->extent.depth > 1
          ? GL_TEXTURE_CUBE_MAP_ARRAY
          : GL_TEXTURE_CUBE_MAP;
    } else {
      ngf_destroy_image(image);
      NGFI_DIAG_ERROR("Can't create an image with specified properties.");
      return NGF_ERROR_OBJECT_CREATION_FAILED;
    }
 
    glGenTextures(1, &(image->glimage));
    glBindTexture(image->bind_point, image->glimage);
    if (image->bind_point == GL_TEXTURE_2D ||
        image->bind_point == GL_TEXTURE_CUBE_MAP) {
      glTexStorage2D(image->bind_point,
                     (GLsizei)info->nmips,
                     glf.internal_format,
                     (GLsizei)info->extent.width,
                     (GLsizei)info->extent.height);
    } else if (image->bind_point == GL_TEXTURE_2D_ARRAY ||
               image->bind_point == GL_TEXTURE_CUBE_MAP_ARRAY ||
               image->bind_point == GL_TEXTURE_3D) {
      // NOTE: for cube map array textures the "depth" is counted in
      // layer-faces, not layers.
      const GLsizei depth = (GLsizei)info->extent.depth *
          (image->bind_point == GL_TEXTURE_CUBE_MAP_ARRAY ? 6 : 1);
      glTexStorage3D(image->bind_point,
                     (GLsizei)info->nmips,
                     glf.internal_format,
                     (GLsizei)info->extent.width,
                     (GLsizei)info->extent.height,
                     depth);
    } else if (image->bind_point == GL_TEXTURE_2D_MULTISAMPLE) {
      glTexStorage2DMultisample(image->bind_point,
                                (GLsizei)info->nsamples,
                                glf.internal_format,
                                (GLsizei)info->extent.width,
                                (GLsizei)info->extent.height,
                                GL_TRUE);
    } else if (image->bind_point == GL_TEXTURE_2D_MULTISAMPLE_ARRAY) {
      glTexStorage3DMultisample(image->bind_point,
                                (GLsizei)info->nsamples,
                                glf.internal_format,
                                (GLsizei)info->extent.width,
                                (GLsizei)info->extent.height,
                                (GLsizei)info->extent.depth,
                                GL_TRUE);
    }
  } else {
    image->is_renderbuffer = true;
    image->bind_point = GL_RENDERBUFFER;
    (glGenRenderbuffers(1, &(image->glimage)));
    (glBindRenderbuffer(GL_RENDERBUFFER, image->glimage));
    if (info->nsamples <= 1) {
      glRenderbufferStorage(image->bind_point,
                            glf.internal_format,
                            (GLsizei)info->extent.width,
                            (GLsizei)info->extent.height);
    } else {
      glRenderbufferStorageMultisample(image->bind_point,
                                       (GLsizei)info->nsamples,
                                       glf.internal_format,
                                       (GLsizei)info->extent.width,
                                       (GLsizei)info->extent.height);
    }
  }
  return NGF_ERROR_OK;
}

void ngf_destroy_image(ngf_image image) {
  if (image != NULL) {
    if (!image->is_renderbuffer) {
      glDeleteTextures(1, &(image->glimage));
    } else {
      glDeleteRenderbuffers(1, &(image->glimage));
    }
    NGFI_FREE(image);
  }
}

ngf_error ngf_create_sampler(const ngf_sampler_info *info,
                             ngf_sampler *result) {
  assert(info);
  assert(result);

  *result = NGFI_ALLOC(struct ngf_sampler_t);
  ngf_sampler sampler = *result;
  if (sampler == NULL) {
    return NGF_ERROR_OUT_OF_MEM;
  }
  
  glGenSamplers(1, &(sampler->glsampler));
  const GLenum min_filter = get_gl_filter(info->min_filter);
  const GLenum mip_filter = get_gl_filter(info->mip_filter);
  const GLenum min_mip_filter = get_min_mip_filter(min_filter, mip_filter);
  glSamplerParameteri(sampler->glsampler, GL_TEXTURE_MIN_FILTER,
                      (GLint)min_mip_filter);
  glSamplerParameteri(sampler->glsampler, GL_TEXTURE_MAG_FILTER,
                      (GLint)get_gl_filter(info->mag_filter));
  glSamplerParameteri(sampler->glsampler, GL_TEXTURE_WRAP_S,
                      (GLint)get_gl_wrap(info->wrap_s));
  glSamplerParameteri(sampler->glsampler, GL_TEXTURE_WRAP_T,
                      (GLint)get_gl_wrap(info->wrap_t));
  glSamplerParameteri(sampler->glsampler, GL_TEXTURE_WRAP_R,
                      (GLint)get_gl_wrap(info->wrap_r));
  glSamplerParameterf(sampler->glsampler, GL_TEXTURE_MIN_LOD, info->lod_min);
  glSamplerParameterf(sampler->glsampler, GL_TEXTURE_MAX_LOD, info->lod_max);
  glSamplerParameterf(sampler->glsampler, GL_TEXTURE_LOD_BIAS, info->lod_bias);
  glSamplerParameterfv(sampler->glsampler, GL_TEXTURE_BORDER_COLOR,
                       info->border_color);
  if (info->enable_anisotropy) {
    glSamplerParameterf(sampler->glsampler, GL_TEXTURE_MAX_ANISOTROPY,
                        info->max_anisotropy);
  }
  return NGF_ERROR_OK;
}

void ngf_destroy_sampler(ngf_sampler sampler) {
  assert(sampler);
  glDeleteSamplers(1, &(sampler->glsampler));
  NGFI_FREE(sampler);
}

ngf_error ngf_default_render_target(
    ngf_attachment_load_op color_load_op,
    ngf_attachment_load_op depth_load_op,
    ngf_attachment_store_op color_store_op,
    ngf_attachment_store_op depth_store_op,
    const ngf_clear *clear_color,
    const ngf_clear *clear_depth,
    ngf_render_target *result) {
  assert(result);
  if (CURRENT_CONTEXT->has_swapchain) {
    ngf_render_target default_render_target =
      (ngf_render_target)NGFI_ALLOCN(uint8_t,
                                    offsetof(struct ngf_render_target_t,
                                             attachment_infos) +
                                     3u * sizeof(ngf_attachment));
    if (default_render_target == NULL) {
      return NGF_ERROR_OUT_OF_MEM;
    }
    default_render_target->is_srgb = CURRENT_CONTEXT->srgb_surface;
    ngf_attachment *default_color_attachment =
        &default_render_target->attachment_infos[0];
    default_color_attachment->type = NGF_ATTACHMENT_COLOR;
    default_color_attachment->load_op = color_load_op;
    default_color_attachment->store_op = color_store_op;
    if (color_load_op == NGF_LOAD_OP_CLEAR) {
      assert(clear_color);
      default_color_attachment->clear = *clear_color;
    }
    default_render_target->nattachments = 1u;
    default_render_target->ndraw_buffers = 1u;
    if (CURRENT_CONTEXT->has_depth) {
      ngf_attachment *default_depth_attachment =
          &default_render_target->attachment_infos[1];
      default_depth_attachment->type = NGF_ATTACHMENT_DEPTH;
      default_depth_attachment->load_op = depth_load_op;
      default_depth_attachment->store_op = depth_store_op;
      if (depth_load_op == NGF_LOAD_OP_CLEAR) {
        assert(clear_depth);
        default_depth_attachment->clear = *clear_depth;
       }
      default_render_target->nattachments += 1u;
    }
    default_render_target->framebuffer = 0;
    *result = default_render_target;
  } else {
    NGFI_DIAG_ERROR("Current context cannot provide a default render target");
    return NGF_ERROR_INVALID_OPERATION;
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_create_render_target(const ngf_render_target_info *info,
                                   ngf_render_target *result) {

  assert(info);
  assert(result);
  assert(info->nattachments < 7);

  ngf_error err = NGF_ERROR_OK;
  *result = (ngf_render_target) NGFI_ALLOCN(uint8_t,
                                           offsetof(struct ngf_render_target_t,
                                                    attachment_infos) +
                                              sizeof(ngf_attachment) *
                                              info->nattachments);
  ngf_render_target render_target = *result;
  if (render_target == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    goto ngf_create_render_target_cleanup;
  }

  glGenFramebuffers(1, &(render_target->framebuffer));
  GLint old_framebuffer;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, render_target->framebuffer);
  size_t ncolor_attachment = 0;
  render_target->nattachments = info->nattachments;
  render_target->is_srgb = false;
  for (size_t i = 0; i < info->nattachments; ++i) {
    const ngf_attachment *a = &(info->attachments[i]);
    render_target->attachment_infos[i] = *a;
    GLenum gl_attachment;
    switch (a->type) {
    case NGF_ATTACHMENT_COLOR: {
      size_t draw_buffer = ncolor_attachment;
      gl_attachment = (GLenum)(GL_COLOR_ATTACHMENT0 + (ncolor_attachment++));
      assert(draw_buffer < NGFGL_MAX_DRAW_BUFFERS);
      render_target->draw_buffers[draw_buffer] = gl_attachment;
      render_target->is_srgb |= a->image_ref.image->is_srgb;
      break;
    }
    case NGF_ATTACHMENT_DEPTH:
      gl_attachment = GL_DEPTH_ATTACHMENT;
      break;
    case NGF_ATTACHMENT_DEPTH_STENCIL:
      gl_attachment = GL_DEPTH_STENCIL_ATTACHMENT;
      break;
    case NGF_ATTACHMENT_STENCIL:
      gl_attachment = GL_STENCIL_ATTACHMENT;
      break;
    default:
      gl_attachment = GL_COLOR_ATTACHMENT0;
      assert(0);
    }
    if (!a->image_ref.image->is_renderbuffer &&
        (a->image_ref.image->bind_point == GL_TEXTURE_2D_ARRAY)) {
      glFramebufferTextureLayer(GL_FRAMEBUFFER,
                                gl_attachment,
                                a->image_ref.image->glimage,
                                (GLint)a->image_ref.mip_level,
                                (GLint)a->image_ref.layer);
    } else if (!a->image_ref.image->is_renderbuffer) {
      glFramebufferTexture(GL_FRAMEBUFFER,
                           gl_attachment,
                           a->image_ref.image->glimage,
                           (GLint)a->image_ref.mip_level);
    } else {
      glBindRenderbuffer(GL_RENDERBUFFER, 0);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                gl_attachment,
                                GL_RENDERBUFFER,
                                a->image_ref.image->glimage);
    }
  }
  render_target->ndraw_buffers = ncolor_attachment;

  GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  bool fb_ok = fb_status == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)old_framebuffer);
  if (!fb_ok) {
    err = NGF_ERROR_OBJECT_CREATION_FAILED;
    goto ngf_create_render_target_cleanup;
  }

ngf_create_render_target_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_render_target(render_target);
  }
  return err;
}

void ngf_destroy_render_target(ngf_render_target render_target) {
  if (render_target != NULL) {
    if (render_target->framebuffer != 0) {
      glDeleteFramebuffers(1, &(render_target->framebuffer));
    }
    NGFI_FREE(render_target);
  }
}

ngf_error ngf_resolve_render_target(const ngf_render_target src,
                                    ngf_render_target dst,
                                    const ngf_irect2d *src_rect) {
  GLint prev_fbo;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, src->framebuffer);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst->framebuffer);
  glBlitFramebuffer(src_rect->x, src_rect->y,
                    src_rect->x + (GLsizei)src_rect->width,
                    src_rect->y + (GLsizei)src_rect->height,
                    src_rect->x, src_rect->y,
                    src_rect->x + (GLsizei)src_rect->width,
                    src_rect->y + (GLsizei)src_rect->height,
                    GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
  return NGF_ERROR_OK;
}

GLuint ngfgl_create_buffer(GLenum type,
                          const ngf_buffer_info *info) {
  assert(type == GL_ARRAY_BUFFER ||
         type == GL_ELEMENT_ARRAY_BUFFER ||
         type == GL_UNIFORM_BUFFER ||
         type == GL_PIXEL_UNPACK_BUFFER);
  GLuint buf;
  glGenBuffers(1u, &buf);
  glBindBuffer(type, buf);
  GLenum gl_buffer_usage = GL_NONE;
  switch (info->storage_type) {
  case NGF_BUFFER_STORAGE_HOST_READABLE:
  case NGF_BUFFER_STORAGE_HOST_WRITEABLE:
  case NGF_BUFFER_STORAGE_HOST_READABLE_WRITEABLE:
    gl_buffer_usage = GL_STREAM_DRAW;
    break;
  case NGF_BUFFER_STORAGE_PRIVATE:
    gl_buffer_usage = GL_STATIC_DRAW;
    break;
  }

  glBufferData(type, (GLsizeiptr)info->size, NULL, gl_buffer_usage);

  return buf;
}

void* ngfgl_buffer_map_range(GLenum bind_target,
                            GLuint gl_buffer,
                            size_t offset,
                            size_t size,
                            uint32_t flags) {
  // TODO: return NULL if buffer uses private storage.
  glBindBuffer(bind_target, gl_buffer);
  GLbitfield map_access = GL_MAP_UNSYNCHRONIZED_BIT;
  if (flags & NGF_BUFFER_MAP_READ_BIT) map_access |= GL_MAP_READ_BIT;
  if (flags & NGF_BUFFER_MAP_WRITE_BIT) map_access |= GL_MAP_WRITE_BIT |
                                                      GL_MAP_FLUSH_EXPLICIT_BIT;
  return glMapBufferRange(bind_target, (GLintptr)offset, (GLsizeiptr)size,
                          map_access);
}

void ngfgl_buffer_flush_range(GLenum bind_target,
                              GLuint gl_buffer,
                              size_t offset,
                              size_t size) {
  // TODO: assert that buffer is mapped.
  glBindBuffer(bind_target, gl_buffer);
  glFlushMappedBufferRange(bind_target, (GLintptr)offset,
                           (GLsizeiptr)size);
}

void ngfgl_buffer_unmap(GLenum bind_target,
                       GLuint gl_buffer) {
  // TODO: assert that buffer is mapped.
  glBindBuffer(bind_target, gl_buffer);
  glUnmapBuffer(bind_target);
}

ngf_error ngf_create_attrib_buffer(const ngf_attrib_buffer_info *info,
                                   ngf_attrib_buffer *result) {
  assert(info);
  assert(result);
  *result = NGFI_ALLOC(struct ngf_attrib_buffer_t);
  ngf_attrib_buffer buf = *result;
  if (buf == NULL) {
    return NGF_ERROR_OUT_OF_MEM;
  }
  buf->glbuffer = ngfgl_create_buffer(GL_ARRAY_BUFFER, info);
  return NGF_ERROR_OK;
}

void ngf_destroy_attrib_buffer(ngf_attrib_buffer buf) {
  if (buf != NULL) {
    glDeleteBuffers(1u, &buf->glbuffer);
    NGFI_FREE(buf);
  }
}

void* ngf_attrib_buffer_map_range(ngf_attrib_buffer buf,
                                  size_t offset,
                                  size_t size,
                                  uint32_t flags) {
  return ngfgl_buffer_map_range(GL_ARRAY_BUFFER,
                               buf->glbuffer,
                               offset,
                               size,
                               flags);
}

void ngf_attrib_buffer_flush_range(ngf_attrib_buffer buf,
                                   size_t offset,
                                   size_t size) {
  ngfgl_buffer_flush_range(GL_ARRAY_BUFFER,
                          buf->glbuffer,
                          offset, size);
}

void ngf_attrib_buffer_unmap(ngf_attrib_buffer buf) {
  ngfgl_buffer_unmap(GL_ARRAY_BUFFER, buf->glbuffer);
}

ngf_error ngf_create_index_buffer(const ngf_index_buffer_info *info,
                                  ngf_index_buffer *result) {
  assert(info);
  assert(result);
  *result = NGFI_ALLOC(struct ngf_index_buffer_t);
  ngf_index_buffer buf = *result;
  if (buf == NULL) {
    return NGF_ERROR_OUT_OF_MEM;
  }
  buf->glbuffer = ngfgl_create_buffer(GL_ELEMENT_ARRAY_BUFFER, info);
  return NGF_ERROR_OK;
}

void ngf_destroy_index_buffer(ngf_index_buffer buf) {
  if (buf != NULL) {
    glDeleteBuffers(1u, &buf->glbuffer);
    NGFI_FREE(buf);
  }
}

void* ngf_index_buffer_map_range(ngf_index_buffer buf,
                                 size_t offset,
                                 size_t size,
                                 uint32_t flags) {
  return ngfgl_buffer_map_range(GL_ELEMENT_ARRAY_BUFFER,
                               buf->glbuffer,
                               offset,
                               size,
                               flags);
}

void ngf_index_buffer_flush_range(ngf_index_buffer buf,
                                   size_t offset,
                                   size_t size) {
  ngfgl_buffer_flush_range(GL_ELEMENT_ARRAY_BUFFER,
                          buf->glbuffer,
                          offset, size);
}

void ngf_index_buffer_unmap(ngf_index_buffer buf) {
  ngfgl_buffer_unmap(GL_ELEMENT_ARRAY_BUFFER, buf->glbuffer);
}

ngf_error ngf_create_uniform_buffer(const ngf_uniform_buffer_info *info,
                                    ngf_uniform_buffer *result) {
  assert(info);
  assert(result);
  *result = NGFI_ALLOC(struct ngf_uniform_buffer_t);
  ngf_uniform_buffer buf = *result;
  if (buf == NULL) {
    return NGF_ERROR_OUT_OF_MEM;
  }
  buf->glbuffer = ngfgl_create_buffer(GL_UNIFORM_BUFFER, info);
  return NGF_ERROR_OK;
}

void ngf_destroy_uniform_buffer(ngf_uniform_buffer buf) {
  if (buf != NULL) {
    glDeleteBuffers(1u, &buf->glbuffer);
    NGFI_FREE(buf);
  }
}

void* ngf_uniform_buffer_map_range(ngf_uniform_buffer buf,
                                   size_t offset,
                                   size_t size,
                                   uint32_t flags) {
  return ngfgl_buffer_map_range(GL_UNIFORM_BUFFER,
                               buf->glbuffer,
                               offset,
                               size,
                               flags);
}

void ngf_uniform_buffer_flush_range(ngf_uniform_buffer buf,
                                    size_t offset,
                                    size_t size) {
  ngfgl_buffer_flush_range(GL_UNIFORM_BUFFER,
                          buf->glbuffer,
                          offset,
                          size);
}

void ngf_uniform_buffer_unmap(ngf_uniform_buffer buf) {
  ngfgl_buffer_unmap(GL_UNIFORM_BUFFER,
                    buf->glbuffer);
}

ngf_error ngf_create_pixel_buffer(const ngf_pixel_buffer_info *info,
                                  ngf_pixel_buffer *result) {
  assert(info);
  assert(result);
  *result = NGFI_ALLOC(struct ngf_pixel_buffer_t);
  ngf_pixel_buffer buf = *result;
  if (buf == NULL) {
    return NGF_ERROR_OUT_OF_MEM;
  }
  ngf_buffer_info buf_info;
  buf_info.size = info->size;
  switch (info->usage) {
  case NGF_PIXEL_BUFFER_USAGE_WRITE:
    buf_info.storage_type = NGF_BUFFER_STORAGE_HOST_WRITEABLE;
    break;
  default: assert(false);
  }
  buf->glbuffer = ngfgl_create_buffer(GL_PIXEL_UNPACK_BUFFER, &buf_info);
  return NGF_ERROR_OK;
}

void* ngf_pixel_buffer_map_range(ngf_pixel_buffer buf,
                                 size_t offset,
                                 size_t size,
                                 uint32_t flags) {
  return ngfgl_buffer_map_range(GL_PIXEL_UNPACK_BUFFER,
                               buf->glbuffer,
                               offset,
                               size,
                               flags);
}

void ngf_pixel_buffer_flush_range(ngf_pixel_buffer buf,
                                  size_t offset,
                                  size_t size) {
  ngfgl_buffer_flush_range(GL_PIXEL_UNPACK_BUFFER,
                          buf->glbuffer,
                          offset,
                          size);
}

void ngf_pixel_buffer_unmap(ngf_pixel_buffer buf) {
  ngfgl_buffer_unmap(GL_PIXEL_UNPACK_BUFFER, buf->glbuffer);
}

void ngf_destroy_pixel_buffer(ngf_pixel_buffer buf) {
  if (buf != NULL) {
    glDeleteBuffers(1, &buf->glbuffer);
    NGFI_FREE(buf);
  }
}

ngf_error ngf_create_cmd_buffer(const ngf_cmd_buffer_info *info,
                                ngf_cmd_buffer *result) {
  NGFI_FAKE_USE(info);
  assert(result);
  if (COMMAND_POOL == NULL) {
    COMMAND_POOL = ngfi_blkalloc_create(sizeof(ngfgl_cmd_block), 100);
  }
  ngf_error err = NGF_ERROR_OK;
  *result = NGFI_ALLOC(struct ngf_cmd_buffer_t);
  ngf_cmd_buffer buf = *result;
  if (buf == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
  }
  buf->first_cmd_block = buf->last_cmd_block = NULL;
  buf->state = NGFI_CMD_BUFFER_READY;
  buf->renderpass_active = false;
  return err;
}

void ngfgl_cmd_buffer_free_cmds(ngf_cmd_buffer buf) {
  const bool has_first_cmd = buf->first_cmd_block != NULL;
  const bool has_last_cmd = buf->last_cmd_block != NULL;
  assert(!(has_first_cmd ^ has_last_cmd));
  if (has_first_cmd && has_last_cmd && COMMAND_POOL != NULL) {
    for (ngfgl_cmd_block *c = buf->first_cmd_block; c != NULL; c = c->next) {
      ngfi_blkalloc_free(COMMAND_POOL, c);
    }
  }
  buf->first_cmd_block = buf->last_cmd_block = NULL;
}

void ngf_destroy_cmd_buffer(ngf_cmd_buffer buf) {
  if (buf != NULL) {
    ngfgl_cmd_buffer_free_cmds(buf);
    NGFI_FREE(buf);
  }
}

ngf_error ngf_start_cmd_buffer(ngf_cmd_buffer buf) {
  assert(buf);
  if (buf->state != NGFI_CMD_BUFFER_READY) {
    NGFI_DIAG_ERROR("Command buffer is not in READY state, and cannot be started.");
    return NGF_ERROR_INVALID_OPERATION;
  }
  ngf_error err = NGF_ERROR_OK;
  if (buf->first_cmd_block != NULL) {
    ngfgl_cmd_buffer_free_cmds(buf);
  }
  buf->first_cmd_block = ngfi_blkalloc_alloc(COMMAND_POOL);
  if (buf->first_cmd_block == NULL) {
    err = NGF_ERROR_OUT_OF_MEM;
    NGFI_FREE(buf);
  } else {
    buf->first_cmd_block->next_cmd_idx = 0u;
    buf->first_cmd_block->next = NULL;
    buf->last_cmd_block = buf->first_cmd_block;
  }
  buf->bound_pipeline = NULL;
  return err;
}
ngf_error ngf_cmd_buffer_start_render(ngf_cmd_buffer buf,
                                      ngf_render_encoder *enc) {
  if (buf->state != NGFI_CMD_BUFFER_READY) {
    enc->__handle = 0u;
    NGFI_DIAG_ERROR("Command buffer is not in READY state, can't start a new encoder.");
    return NGF_ERROR_INVALID_OPERATION;
  }
  enc->__handle = (uintptr_t)buf;
  return NGF_ERROR_OK;
}

ngf_error ngf_cmd_buffer_start_xfer(ngf_cmd_buffer buf,
                                    ngf_xfer_encoder *enc) {
  if (buf->state != NGFI_CMD_BUFFER_READY) {
    enc->__handle = 0u;
    NGFI_DIAG_ERROR("Command buffer is not in READY state, can't start a new encoder.");
    return NGF_ERROR_INVALID_OPERATION;
  }
  enc->__handle = (uintptr_t)buf;
  return NGF_ERROR_OK;
}

ngf_error ngf_render_encoder_end(ngf_render_encoder enc) {
  if (((ngf_cmd_buffer)enc.__handle)->renderpass_active) {
    return NGF_ERROR_INVALID_OPERATION;
  }
  enc.__handle = 0u;
  return NGF_ERROR_OK;
}

ngf_error ngf_xfer_encoder_end(ngf_xfer_encoder enc) {
  enc.__handle = 0u;
  return NGF_ERROR_OK;
}

#define NGFGL_NEWCMD(enc, cmd) {\
  ngf_cmd_buffer buf = (ngf_cmd_buffer)(void*)enc.__handle; \
  if (buf->last_cmd_block->next_cmd_idx == NGFGL_CMDS_PER_CMD_BLOCK) { \
    buf->last_cmd_block->next = ngfi_blkalloc_alloc(COMMAND_POOL); \
    buf->last_cmd_block = buf->last_cmd_block->next; \
    buf->last_cmd_block->next = NULL; \
    buf->last_cmd_block->next_cmd_idx = 0u; \
  } \
  cmd = &buf->last_cmd_block->cmds[buf->last_cmd_block->next_cmd_idx++]; \
}


void ngf_cmd_bind_gfx_pipeline(ngf_render_encoder enc,
                               const ngf_graphics_pipeline pipeline) {

  ngfgl_emulated_cmd *cmd = NULL;
  NGFGL_NEWCMD(enc, cmd);
  cmd->type = NGFGL_CMD_BIND_PIPELINE;
  cmd->pipeline = pipeline;
  ((ngf_cmd_buffer)enc.__handle)->bound_pipeline = pipeline;
}

void ngf_cmd_viewport(ngf_render_encoder enc, const ngf_irect2d *viewport) {
  ngfgl_emulated_cmd *cmd = NULL;
  NGFGL_NEWCMD(enc, cmd);
  cmd->type = NGFGL_CMD_VIEWPORT;
  cmd->viewport = *viewport;
}

void ngf_cmd_scissor(ngf_render_encoder enc, const ngf_irect2d *scissor) {
  ngfgl_emulated_cmd *cmd = NULL;
  NGFGL_NEWCMD(enc, cmd);
  cmd->type = NGFGL_CMD_SCISSOR;
  cmd->scissor = *scissor;
}

void ngf_cmd_stencil_reference(ngf_render_encoder enc, uint32_t front,
                               uint32_t back) {
  ngfgl_emulated_cmd *cmd = NULL;
  NGFGL_NEWCMD(enc, cmd);
  cmd->type = NGFGL_CMD_STENCIL_REFERENCE;
  cmd->stencil_reference.front = front;
  cmd->stencil_reference.back = back;
}

void ngf_cmd_stencil_compare_mask(ngf_render_encoder enc, uint32_t front,
                                  uint32_t back) {
  ngfgl_emulated_cmd *cmd = NULL;
  NGFGL_NEWCMD(enc, cmd);
  cmd->type = NGFGL_CMD_STENCIL_COMPARE_MASK;
  cmd->stencil_compare_mask.front = front;
  cmd->stencil_compare_mask.back = back;
}

void ngf_cmd_stencil_write_mask(ngf_render_encoder enc, uint32_t front,
                                uint32_t back) {
  ngfgl_emulated_cmd *cmd = NULL;
  NGFGL_NEWCMD(enc, cmd);
  cmd->type = NGFGL_CMD_STENCIL_WRITE_MASK;
  cmd->stencil_write_mask.front = front;
  cmd->stencil_write_mask.back = back;
}

void ngf_cmd_line_width(ngf_render_encoder enc, float line_width) {
  ngfgl_emulated_cmd *cmd = NULL;
  NGFGL_NEWCMD(enc, cmd);
  cmd->type = NGFGL_CMD_LINE_WIDTH;
  cmd->line_width = line_width;
}

void ngf_cmd_bind_gfx_resources(ngf_render_encoder enc,
                                const ngf_resource_bind_op *bind_ops,
                                uint32_t nbind_ops) {
  ngf_cmd_buffer cmdbuf = (ngf_cmd_buffer)enc.__handle;
  for (uint32_t o = 0u; o < nbind_ops; ++o) {
    const ngf_resource_bind_op *bind_op = &bind_ops[o];
    const ngfi_native_binding *native_binding =
        ngfi_binding_map_lookup(cmdbuf->bound_pipeline->binding_map,
                                bind_op->target_set,
                                bind_op->target_binding);
    if (native_binding == NULL) {
      static const char *err_msg = "invalid binding id";
      NGFI_DIAG_MSG(NGF_DIAGNOSTIC_ERROR, "Invalid binding id");
      continue;
    }
    switch (bind_op->type) {
    case NGF_DESCRIPTOR_UNIFORM_BUFFER: {
      ngfgl_emulated_cmd *uniform_buffer_bind_cmd = NULL;
      NGFGL_NEWCMD(enc, uniform_buffer_bind_cmd);
      uniform_buffer_bind_cmd->type = NGFGL_CMD_BIND_UNIFORM_BUFFER;
      uniform_buffer_bind_cmd->uniform_buffer_bind_op.buffer =
          bind_op->info.uniform_buffer.buffer->glbuffer;
      uniform_buffer_bind_cmd->uniform_buffer_bind_op.index =
          native_binding->native_binding_id;
      uniform_buffer_bind_cmd->uniform_buffer_bind_op.offset =
          (GLsizei)bind_op->info.uniform_buffer.offset;
      uniform_buffer_bind_cmd->uniform_buffer_bind_op.range =
          (GLsizei)bind_op->info.uniform_buffer.range;
      break;
    }
    case NGF_DESCRIPTOR_TEXTURE: {
      for (uint32_t c = 0u; c < native_binding->ncis_bindings; ++c) {
        ngfgl_emulated_cmd *texture_bind_cmd = NULL;
        NGFGL_NEWCMD(enc, texture_bind_cmd);
        texture_bind_cmd->type = NGFGL_CMD_BIND_TEXTURE;
        texture_bind_cmd->texture_bind_op.texture =
            bind_op->info.image_sampler.image_subresource.image;
        texture_bind_cmd->texture_bind_op.unit =
            native_binding->cis_bindings[c];
      }
      break;
    }
    case NGF_DESCRIPTOR_SAMPLER: {
      for (uint32_t c = 0u; c < native_binding->ncis_bindings; ++c) {
        ngfgl_emulated_cmd *sampler_bind_cmd = NULL;
        NGFGL_NEWCMD(enc, sampler_bind_cmd);
        sampler_bind_cmd->type = NGFGL_CMD_BIND_SAMPLER;
        sampler_bind_cmd->sampler_bind_op.sampler =
            bind_op->info.image_sampler.sampler;
        sampler_bind_cmd->sampler_bind_op.unit =
            native_binding->cis_bindings[c];
      }
      break;
    }
    case NGF_DESCRIPTOR_TEXTURE_AND_SAMPLER: {
      ngfgl_emulated_cmd *texture_bind_cmd = NULL;
      NGFGL_NEWCMD(enc, texture_bind_cmd);
      texture_bind_cmd->type = NGFGL_CMD_BIND_TEXTURE;
      texture_bind_cmd->texture_bind_op.texture =
        bind_op->info.image_sampler.image_subresource.image;
      texture_bind_cmd->texture_bind_op.unit =
        native_binding->native_binding_id;
      ngfgl_emulated_cmd *sampler_bind_cmd = NULL;
      NGFGL_NEWCMD(enc, sampler_bind_cmd);
      sampler_bind_cmd->type = NGFGL_CMD_BIND_SAMPLER;
      sampler_bind_cmd->sampler_bind_op.sampler =
          bind_op->info.image_sampler.sampler;
      sampler_bind_cmd->sampler_bind_op.unit =
          native_binding->native_binding_id;
      break;
    default:
      break;
    }
    }
  }
}

void ngf_cmd_bind_attrib_buffer(ngf_render_encoder enc,
                                const ngf_attrib_buffer vbuf,
                                uint32_t binding, uint32_t offset) {
  ngfgl_emulated_cmd *cmd = NULL;
  NGFGL_NEWCMD(enc, cmd);
  cmd->type = NGFGL_CMD_BIND_ATTRIB_BUFFER;
  cmd->attrib_buffer_bind_op.binding = binding;
  cmd->attrib_buffer_bind_op.buf = vbuf;
  cmd->attrib_buffer_bind_op.offset = offset;
}

void ngf_cmd_bind_index_buffer(ngf_render_encoder enc,
                               const ngf_index_buffer idxbuf,
                               ngf_type index_type) {
  ngfgl_emulated_cmd *cmd = NULL;
  NGFGL_NEWCMD(enc, cmd);
  cmd->type = NGFGL_CMD_BIND_INDEX_BUFFER;
  cmd->index_buffer_bind.index_buffer = idxbuf;
  cmd->index_buffer_bind.type = index_type;
}

void ngf_cmd_begin_pass(ngf_render_encoder enc, const ngf_render_target target) {
  ngfgl_emulated_cmd *cmd = NULL;
  NGFGL_NEWCMD(enc, cmd);
  cmd->type = NGFGL_CMD_BEGIN_PASS;
  cmd->begin_pass.target = target;
  ((ngf_cmd_buffer)enc.__handle)->renderpass_active = true;
}

void ngf_cmd_end_pass(ngf_render_encoder enc) {
  ((ngf_cmd_buffer)enc.__handle)->renderpass_active = false;
  ngfgl_emulated_cmd *cmd = NULL;
  NGFGL_NEWCMD(enc, cmd);
  cmd->type = NGFGL_CMD_END_PASS;
}

void ngf_cmd_draw(ngf_render_encoder enc, bool indexed,
                  uint32_t first_element, uint32_t nelements,
                  uint32_t ninstances) {
  ngfgl_emulated_cmd *cmd = NULL;
  NGFGL_NEWCMD(enc, cmd);
  cmd->type = NGFGL_CMD_DRAW;
  cmd->draw.first_element = first_element;
  cmd->draw.nelements = nelements;
  cmd->draw.ninstances = ninstances;
  cmd->draw.indexed = indexed;
}

void ngfgl_cmd_copy_buffer(ngf_xfer_encoder enc,
                          GLuint src,
                          GLuint dst,
                          size_t size,
                          size_t src_offset,
                          size_t dst_offset) {
  ngfgl_emulated_cmd *cmd = NULL;
  NGFGL_NEWCMD(enc, cmd);
  cmd->type = NGFGL_CMD_COPY;
  cmd->copy.src = src;
  cmd->copy.dst = dst;
  cmd->copy.size = size;
  cmd->copy.src_offset = src_offset;
  cmd->copy.dst_offset = dst_offset;

}

void ngf_cmd_write_image(ngf_xfer_encoder enc,
                         const ngf_pixel_buffer src,
                         size_t src_offset,
                         ngf_image_ref dst,
                         const ngf_offset3d *offset,
                         const ngf_extent3d *extent) {
  ngfgl_emulated_cmd *cmd = NULL;
  NGFGL_NEWCMD(enc, cmd);
  cmd->type = NGFGL_CMD_WRITE_IMAGE;
  cmd->write_image.src_pbuffer = src->glbuffer;
  cmd->write_image.src_data_offset = src_offset;
  cmd->write_image.dst_image_ref = dst;
  cmd->write_image.offset = *offset;
  cmd->write_image.dimensions = *extent;
}

// TODO: assert that buffer is not mapped below.

void ngf_cmd_copy_attrib_buffer(ngf_xfer_encoder enc,
                                const ngf_attrib_buffer src,
                                ngf_attrib_buffer dst,
                                size_t size,
                                size_t src_offset,
                                size_t dst_offset) {
  ngfgl_cmd_copy_buffer(enc, src->glbuffer, dst->glbuffer,
                       size, src_offset, dst_offset);
}

void ngf_cmd_copy_index_buffer(ngf_xfer_encoder enc,
                               const ngf_index_buffer src,
                               ngf_index_buffer dst,
                               size_t size,
                               size_t src_offset,
                               size_t dst_offset) {
  ngfgl_cmd_copy_buffer(enc, src->glbuffer, dst->glbuffer,
                       size, src_offset, dst_offset);
 }

void ngf_cmd_copy_uniform_buffer(ngf_xfer_encoder enc,
                                 const ngf_uniform_buffer src,
                                 ngf_uniform_buffer dst,
                                 size_t size,
                                 size_t src_offset,
                                 size_t dst_offset) {
  ngfgl_cmd_copy_buffer(enc, src->glbuffer, dst->glbuffer,
                       size, src_offset, dst_offset);
 }

ngf_error ngf_submit_cmd_buffers(uint32_t nbuffers, ngf_cmd_buffer *bufs) {
  assert(bufs);
  ngf_render_target active_rt = NULL;
  for (uint32_t buf_i = 0u; buf_i < nbuffers; ++buf_i) {
    const ngf_cmd_buffer buf = bufs[buf_i];
    for (const ngfgl_cmd_block *block = buf->first_cmd_block; block!= NULL;
         block= block->next) {
      for (uint32_t i = 0; i < block->next_cmd_idx; ++i) {
        const ngfgl_emulated_cmd *cmd = &block->cmds[i];
        switch (cmd->type) {
        case NGFGL_CMD_BIND_PIPELINE: {
          const ngf_graphics_pipeline bound_pipe = 
            CURRENT_CONTEXT->has_bound_pipeline
              ? &(CURRENT_CONTEXT->cached_state.pipeline)
              : NULL;

          const ngf_graphics_pipeline pipeline = cmd->pipeline;
          if (!bound_pipe || bound_pipe->id != pipeline->id) {
            // Bind graphics program.
            if (!bound_pipe ||
                 bound_pipe->program_pipeline != pipeline->program_pipeline) {
              glBindProgramPipeline(pipeline->program_pipeline);
            }

            // Set viewport state.
            const bool viewport_dynamic = pipeline->dynamic_state_mask &
                                          NGF_DYNAMIC_STATE_VIEWPORT;
            if (!viewport_dynamic &&
                (!bound_pipe ||
                 !NGFI_STRUCT_EQ(pipeline->viewport, bound_pipe->viewport))) {
              glViewport((GLsizei)pipeline->viewport.x,
                         (GLsizei)pipeline->viewport.y,
                         (GLsizei)pipeline->viewport.width,
                         (GLsizei)pipeline->viewport.height);
            }

            // Set scissor state.
            const bool scissor_dynamic = pipeline->dynamic_state_mask &
                                         NGF_DYNAMIC_STATE_SCISSOR;
            if (!scissor_dynamic &&
                (!bound_pipe ||
                 !NGFI_STRUCT_EQ(pipeline->scissor, bound_pipe->scissor))) {
              glScissor((GLsizei)pipeline->scissor.x,
                        (GLsizei)pipeline->scissor.y,
                        (GLsizei)pipeline->scissor.width,
                        (GLsizei)pipeline->scissor.height);
            }

            // Set rasterizer state.
            const ngf_rasterization_info *rast = &(pipeline->rasterization);
            const ngf_rasterization_info *prev_rast =
                bound_pipe ? &(bound_pipe->rasterization) : NULL;
            if (!prev_rast ||
                 prev_rast->discard != rast->discard) {
              if (rast->discard) {
                glEnable(GL_RASTERIZER_DISCARD);
              } else {
                glDisable(GL_RASTERIZER_DISCARD);
              }
            }
            if (!prev_rast ||
                 prev_rast->polygon_mode != rast->polygon_mode) {
              glPolygonMode(GL_FRONT_AND_BACK,
                            get_gl_poly_mode(rast->polygon_mode));
            }
            if (!prev_rast ||
                prev_rast->cull_mode != rast->cull_mode) {
              if (rast->cull_mode != NGF_CULL_MODE_NONE) {
                glEnable(GL_CULL_FACE);
                glCullFace(get_gl_cull_mode(rast->cull_mode));
              } else {
                glDisable(GL_CULL_FACE);
              }
            }
            if (!prev_rast ||
                prev_rast->front_face != rast->front_face) {
              glFrontFace(get_gl_face(rast->front_face));
            }
            if (!prev_rast ||
                prev_rast->line_width != rast->line_width) {
              glLineWidth(rast->line_width);
            }

            // Enable/disable multisampling.
            if (!bound_pipe ||
                bound_pipe->multisample.multisample !=
                pipeline->multisample.multisample) {
              if (pipeline->multisample.multisample) {
                glEnable(GL_MULTISAMPLE);
              } else {
                glDisable(GL_MULTISAMPLE);
              }
            }

            // Enable/disable alpha-to-coverage.
            if (!bound_pipe ||
                bound_pipe->multisample.alpha_to_coverage !=
                    pipeline->multisample.alpha_to_coverage) {
              if (pipeline->multisample.alpha_to_coverage) {
                glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
              } else {
                glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
              }
            }

            // Set depth/stencil state.
            const ngf_depth_stencil_info *depth_stencil =
                &(pipeline->depth_stencil);
            const ngf_depth_stencil_info *prev_depth_stencil =
                bound_pipe ? &(bound_pipe->depth_stencil) : NULL;

            if (!prev_depth_stencil ||
                prev_depth_stencil->depth_test != depth_stencil->depth_test) {
              if (depth_stencil->depth_test) {
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(get_gl_compare(depth_stencil->depth_compare));
              } else {
                glDisable(GL_DEPTH_TEST);
              }
            }
            if (!prev_depth_stencil ||
                 prev_depth_stencil->depth_write !=
                 depth_stencil->depth_write) {
              if (depth_stencil->depth_write) {
                glDepthMask(GL_TRUE);
              } else {
                glDepthMask(GL_FALSE);
              }
            }
            if (!prev_depth_stencil ||
                 prev_depth_stencil->stencil_test !=
                   depth_stencil->stencil_test ||
                !NGFI_STRUCT_EQ(prev_depth_stencil->back_stencil,
                               depth_stencil->back_stencil) ||
                !NGFI_STRUCT_EQ(prev_depth_stencil->front_stencil,
                               depth_stencil->front_stencil)) {
              if (depth_stencil->stencil_test) {
                glEnable(GL_STENCIL_TEST);
                glStencilFuncSeparate(
                  GL_FRONT,
                  get_gl_compare(depth_stencil->front_stencil.compare_op),
                  (GLint)depth_stencil->front_stencil.reference,
                  depth_stencil->front_stencil.compare_mask);
                glStencilOpSeparate(
                  GL_FRONT,
                  get_gl_stencil_op(depth_stencil->front_stencil.fail_op),
                  get_gl_stencil_op(depth_stencil->front_stencil.depth_fail_op),
                  get_gl_stencil_op(depth_stencil->front_stencil.pass_op));
                glStencilMaskSeparate(GL_FRONT,
                                      depth_stencil->front_stencil.write_mask);
                glStencilFuncSeparate(
                  GL_BACK,
                  get_gl_compare(depth_stencil->back_stencil.compare_op),
                  (GLint)depth_stencil->back_stencil.reference,
                  depth_stencil->back_stencil.compare_mask);
                glStencilOpSeparate(
                  GL_BACK,
                  get_gl_stencil_op(depth_stencil->back_stencil.fail_op),
                  get_gl_stencil_op(depth_stencil->back_stencil.depth_fail_op),
                  get_gl_stencil_op(depth_stencil->back_stencil.pass_op));
                glStencilMaskSeparate(GL_BACK,
                                      depth_stencil->back_stencil.write_mask);
              } else { 
                glDisable(GL_STENCIL_TEST);
              }
            }
            if (!prev_depth_stencil ||
                prev_depth_stencil->min_depth != depth_stencil->min_depth ||
                prev_depth_stencil->max_depth != depth_stencil->max_depth) {
              glDepthRangef(depth_stencil->min_depth, depth_stencil->max_depth);
            }

            // Set blend state.
            const ngf_blend_info *blend = &(pipeline->blend);
            const ngf_blend_info *prev_blend =
                bound_pipe ? &(bound_pipe->blend) : NULL;
            if (!prev_blend || memcmp(blend, prev_blend, sizeof(ngf_blend_info))) {
              if (blend->enable) {
                glEnable(GL_BLEND);
                glBlendEquationSeparate(get_gl_blend_op(blend->blend_op_color),
                                        get_gl_blend_op(blend->blend_op_alpha));
                glBlendFuncSeparate(get_gl_blend_factor(blend->src_color_blend_factor),
                                    get_gl_blend_factor(blend->dst_color_blend_factor),
                                    get_gl_blend_factor(blend->src_alpha_blend_factor),
                                    get_gl_blend_factor(blend->dst_alpha_blend_factor));
                glBlendColor(blend->blend_color[0],
                             blend->blend_color[1],
                             blend->blend_color[2],
                             blend->blend_color[3]);
              } else {
                glDisable(GL_BLEND);
              }
            }

            // Set vertex input state.
            if ((!bound_pipe ||
                  bound_pipe->vao != pipeline->vao) &&
                  glIsVertexArray(pipeline->vao)) {
              glBindVertexArray(pipeline->vao);
              // Keep the same set of attribute buffers bound despite VAO change.
              const size_t nvbuf_table_entries =
                  NGFI_DARRAY_SIZE(CURRENT_CONTEXT->cached_state.vbuf_table);
              for (size_t e = 0; e < nvbuf_table_entries; ++e) {
                bool found_binding = false;
                const ngfgl_vbuf_binding_info *vbuf_table_entry =
                    &NGFI_DARRAY_AT(CURRENT_CONTEXT->cached_state.vbuf_table,
                                    e);
                if (!glIsBuffer(vbuf_table_entry->buffer)) {
                  continue;
                }
                // Update only bindings relevant to this pipeline.
                for (uint32_t b = 0;
                     !found_binding && b < pipeline->nvert_buf_bindings;
                     ++b) {
                  if (pipeline->vert_buf_bindings[b].binding ==
                      vbuf_table_entry->binding) {
                    const GLsizei stride =
                      (GLsizei)pipeline->vert_buf_bindings[b].stride;
                    glBindVertexBuffer(vbuf_table_entry->binding,
                                       vbuf_table_entry->buffer,
                                       vbuf_table_entry->offset,
                                       stride);
                    found_binding = true;
                  }
                }
              }

              // Rebind index buffer.
              if (glIsBuffer(CURRENT_CONTEXT->cached_state.bound_index_buffer)) {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                  CURRENT_CONTEXT->cached_state.bound_index_buffer);
              }
            }
            CURRENT_CONTEXT->cached_state.pipeline = *pipeline;
          }
          CURRENT_CONTEXT->has_bound_pipeline = true;
          glEnable(GL_SCISSOR_TEST);
          break; }

        case NGFGL_CMD_VIEWPORT:
            glViewport(cmd->viewport.x,
                       cmd->viewport.y,
                       (GLsizei)cmd->viewport.width,
                       (GLsizei)cmd->viewport.height);
          break;

        case NGFGL_CMD_SCISSOR:
            glScissor(cmd->scissor.x,
                      cmd->scissor.y,
                      (GLsizei)cmd->scissor.width,
                      (GLsizei)cmd->scissor.height);
          break;

        case NGFGL_CMD_LINE_WIDTH:
          glLineWidth(cmd->line_width);
          break;

        case NGFGL_CMD_STENCIL_WRITE_MASK:
          glStencilMaskSeparate(GL_FRONT, cmd->stencil_write_mask.front);
          glStencilMaskSeparate(GL_BACK, cmd->stencil_write_mask.back);
          break;

        case NGFGL_CMD_STENCIL_COMPARE_MASK: {
          GLint back_func, front_func, front_ref, back_ref;
          glGetIntegerv(GL_STENCIL_BACK_FUNC, &back_func);
          glGetIntegerv(GL_STENCIL_FUNC, &front_func);
          glGetIntegerv(GL_STENCIL_BACK_REF, &back_ref);
          glGetIntegerv(GL_STENCIL_REF, &front_ref);
          glStencilFuncSeparate(GL_FRONT,
                                (GLenum)front_func,
                                front_ref,
                                cmd->stencil_compare_mask.front);
          glStencilFuncSeparate(GL_BACK,
                                (GLenum)back_func,
                                back_ref,
                                cmd->stencil_compare_mask.back);
          break;
        }

        case NGFGL_CMD_STENCIL_REFERENCE: {
          GLint back_func, front_func, front_mask, back_mask;
          glGetIntegerv(GL_STENCIL_BACK_FUNC, &back_func);
          glGetIntegerv(GL_STENCIL_FUNC, &front_func);
          glGetIntegerv(GL_STENCIL_BACK_VALUE_MASK, &back_mask);
          glGetIntegerv(GL_STENCIL_VALUE_MASK, &front_mask);
          glStencilFuncSeparate(GL_FRONT,
                                (GLenum)front_func,
                                (GLint)cmd->stencil_reference.front,
                                (GLuint)front_mask);
          glStencilFuncSeparate(GL_BACK,
                                (GLenum)back_func,
                                (GLint)cmd->stencil_reference.back,
                                (GLuint)back_mask);         
          break;
        }

        case NGFGL_CMD_BIND_UNIFORM_BUFFER:
          glBindBufferRange(GL_UNIFORM_BUFFER,
                            cmd->uniform_buffer_bind_op.index,
                            cmd->uniform_buffer_bind_op.buffer,
                            cmd->uniform_buffer_bind_op.offset,
                            cmd->uniform_buffer_bind_op.range);
          break;

        case NGFGL_CMD_BIND_TEXTURE:
          glActiveTexture(GL_TEXTURE0 + cmd->texture_bind_op.unit);
          glBindTexture(cmd->texture_bind_op.texture->bind_point,
                        cmd->texture_bind_op.texture->glimage);
          break;

        case NGFGL_CMD_BIND_SAMPLER:
          glBindSampler(cmd->sampler_bind_op.unit,
                        cmd->sampler_bind_op.sampler->glsampler);
          break;

        case NGFGL_CMD_BIND_ATTRIB_BUFFER: {
          const ngf_graphics_pipeline bound_pipeline =
            CURRENT_CONTEXT->has_bound_pipeline
              ? &CURRENT_CONTEXT->cached_state.pipeline
              : NULL;

          // Update the table of bound attrib buffers.
          const size_t nvbuf_table_entries = NGFI_DARRAY_SIZE(
              CURRENT_CONTEXT->cached_state.vbuf_table);
          bool vbuf_table_entry_found = false;
          for (size_t e = 0;
               !vbuf_table_entry_found && e < nvbuf_table_entries;
               ++e) { // Try to find and update vertex buffer table entry.
            ngfgl_vbuf_binding_info *vbuf_table_entry = &NGFI_DARRAY_AT(
                CURRENT_CONTEXT->cached_state.vbuf_table,
                e);
            if (vbuf_table_entry->binding == cmd->attrib_buffer_bind_op.binding) {
              vbuf_table_entry->buffer =
                cmd->attrib_buffer_bind_op.buf->glbuffer;
              vbuf_table_entry->offset =
                cmd->attrib_buffer_bind_op.offset;
              vbuf_table_entry_found = true;
            }
          }
          if (!vbuf_table_entry_found) { // Must add new entry.
            const ngfgl_vbuf_binding_info new_entry = {
              .binding = cmd->attrib_buffer_bind_op.binding,
              .buffer = cmd->attrib_buffer_bind_op.buf->glbuffer,
              .offset = cmd->attrib_buffer_bind_op.offset
            };
            NGFI_DARRAY_APPEND(CURRENT_CONTEXT->cached_state.vbuf_table,
                               new_entry);
          }
          
          // Bind the attribute buffer.
          if (bound_pipeline) {
            GLsizei stride = 0;
            bool found_binding = false;
            for (uint32_t binding = 0;
                 !found_binding &&
                 binding < bound_pipeline->nvert_buf_bindings;
                 ++binding) {
              if (bound_pipeline->vert_buf_bindings[binding].binding ==
                  cmd->attrib_buffer_bind_op.binding) {
                stride =
                    (GLsizei)bound_pipeline->vert_buf_bindings[binding].stride;
                found_binding = true;
              }
            }
            assert(found_binding);
            glBindVertexBuffer(cmd->attrib_buffer_bind_op.binding,
                               cmd->attrib_buffer_bind_op.buf->glbuffer,
                               cmd->attrib_buffer_bind_op.offset,
                               stride);
          }
          break;
        }

        case NGFGL_CMD_BIND_INDEX_BUFFER:
          glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,
                       cmd->index_buffer_bind.index_buffer->glbuffer);
          CURRENT_CONTEXT->cached_state.bound_index_buffer =
              cmd->index_buffer_bind.index_buffer->glbuffer;
          CURRENT_CONTEXT->bound_index_buffer_type = cmd->index_buffer_bind.type;
          break;

        case NGFGL_CMD_BEGIN_PASS: {
          active_rt = cmd->begin_pass.target;
          glBindFramebuffer(GL_FRAMEBUFFER, active_rt->framebuffer);
          if (active_rt->is_srgb) glEnable(GL_FRAMEBUFFER_SRGB);
          else glDisable(GL_FRAMEBUFFER_SRGB);
          uint32_t color_clear = 0u;
          glDisable(GL_SCISSOR_TEST);
          glDepthMask(GL_TRUE);
          if (active_rt->ndraw_buffers > 1u) {
            glDrawBuffers((GLsizei)active_rt->ndraw_buffers,
                           active_rt->draw_buffers);
          }
          for (uint32_t a = 0u; a < active_rt->nattachments; ++a) {
            const ngf_attachment *attachment = &active_rt->attachment_infos[a];
            if (attachment->load_op == NGF_LOAD_OP_CLEAR) {
              const ngf_clear *clear = &attachment->clear;
              switch (attachment->type) {
              case NGF_ATTACHMENT_COLOR:
                glClearBufferfv(GL_COLOR, (GLint)(color_clear++),
                                clear->clear_color);
                break;
              case NGF_ATTACHMENT_DEPTH:
                glClearBufferfv(GL_DEPTH, 0, &clear->clear_depth);
                break;
              case NGF_ATTACHMENT_STENCIL: {
                GLint v = (GLint)clear->clear_stencil;
                glClearBufferiv(GL_STENCIL, 0, &v);
                break;
              case NGF_ATTACHMENT_DEPTH_STENCIL:
                break;
              }
              }
            }
          }
          glEnable(GL_SCISSOR_TEST);
          break;
        }

        case NGFGL_CMD_END_PASS: {
          assert(active_rt);
          const uint32_t max_discarded_attachments =
              active_rt->nattachments + 1u; // +1 needed in case we need to
                                            // handle depth/stencil separately.
          GLenum *gl_attachments =
              alloca(sizeof(GLenum) * max_discarded_attachments);
          uint32_t ndiscarded_attachments = 0u;
          uint32_t ndiscarded_color_attachments = 0u;
          // Annoyingly, desktop GL specification of glInvalidateFramebuffer
          // differs strongly between which enum values to use for invalidating
          // attachments of a "normal" vs default framebuffer. This distinction
          // is not present on GLES 3 though (TODO: turn off this path for
          // GLES 3).
          const bool is_default_framebuffer = active_rt->framebuffer == 0;
          for (uint32_t a = 0u; a < active_rt->nattachments; ++a) {
            const ngf_attachment *attachment = &active_rt->attachment_infos[a];
            if (attachment->store_op == NGF_STORE_OP_DONTCARE) {
              switch (attachment->type) {
              case NGF_ATTACHMENT_COLOR:
                gl_attachments[ndiscarded_attachments++] =
                  is_default_framebuffer 
                    ? GL_COLOR
                    : GL_COLOR_ATTACHMENT0 + (++ndiscarded_color_attachments);
                break;
              case NGF_ATTACHMENT_DEPTH:
                gl_attachments[ndiscarded_attachments++] = 
                  is_default_framebuffer ? GL_DEPTH : GL_DEPTH_ATTACHMENT;
                break;
              case NGF_ATTACHMENT_STENCIL:
                gl_attachments[ndiscarded_attachments++] =
                  is_default_framebuffer
                    ? GL_STENCIL
                    : GL_STENCIL_ATTACHMENT;
                break;
              case NGF_ATTACHMENT_DEPTH_STENCIL:
                if (!is_default_framebuffer) {
                  gl_attachments[ndiscarded_attachments++] =
                      GL_DEPTH_STENCIL_ATTACHMENT;
                } else {
                  gl_attachments[ndiscarded_attachments++] = GL_DEPTH;
                  gl_attachments[ndiscarded_attachments++] = GL_STENCIL;
                }
                break;
              }
            }
          }
          if (ndiscarded_attachments > 0u) {
            glInvalidateFramebuffer(GL_FRAMEBUFFER,
                                    (GLsizei)ndiscarded_attachments,
                                    gl_attachments);
          }
          break;
        }
        case NGFGL_CMD_DRAW: {
          const ngf_graphics_pipeline bound_pipeline =
            CURRENT_CONTEXT->has_bound_pipeline
              ? &CURRENT_CONTEXT->cached_state.pipeline
              : NULL;
          assert(bound_pipeline);
          if (!cmd->draw.indexed && cmd->draw.ninstances == 1u) {
            glDrawArrays(bound_pipeline->primitive_type,
                         (GLint)cmd->draw.first_element,
                         (GLsizei)cmd->draw.nelements);
          } else if (!cmd->draw.indexed && cmd->draw.ninstances > 1u) {
            glDrawArraysInstanced(bound_pipeline->primitive_type,
                                  (GLint)cmd->draw.first_element,
                                  (GLsizei)cmd->draw.nelements,
                                  (GLsizei)cmd->draw.ninstances);
          } else if (cmd->draw.indexed && cmd->draw.ninstances == 1u) {
            assert(CURRENT_CONTEXT->bound_index_buffer_type == NGF_TYPE_UINT16 ||
                   CURRENT_CONTEXT->bound_index_buffer_type == NGF_TYPE_UINT32);
            size_t elem_size =
                CURRENT_CONTEXT->bound_index_buffer_type == NGF_TYPE_UINT16 ? 2
                                                                            : 4;
            glDrawElements(bound_pipeline->primitive_type,
                           (GLsizei)cmd->draw.nelements,
                           get_gl_type(CURRENT_CONTEXT->bound_index_buffer_type),
                           (void*)(uintptr_t)(cmd->draw.first_element *
                                              elem_size));
          } else if (cmd->draw.indexed && cmd->draw.ninstances > 1u) {
            assert(CURRENT_CONTEXT->bound_index_buffer_type == NGF_TYPE_UINT16 ||
                   CURRENT_CONTEXT->bound_index_buffer_type == NGF_TYPE_UINT32);
            size_t elem_size =
                CURRENT_CONTEXT->bound_index_buffer_type == NGF_TYPE_UINT16 ? 2
                                                                            : 4;
            glDrawElementsInstanced(bound_pipeline->primitive_type,
                                    (GLsizei)cmd->draw.nelements,
                                    get_gl_type(CURRENT_CONTEXT->bound_index_buffer_type),
                                    (void*)((uintptr_t)(cmd->draw.first_element *
                                                        elem_size)),  
                                    (GLsizei)cmd->draw.ninstances);
          }
          break; }
        case NGFGL_CMD_COPY:
          glBindBuffer(GL_COPY_READ_BUFFER, cmd->copy.src);
          glBindBuffer(GL_COPY_WRITE_BUFFER, cmd->copy.dst);
          glCopyBufferSubData(GL_COPY_READ_BUFFER,
                              GL_COPY_WRITE_BUFFER,
                              (GLintptr)cmd->copy.src_offset,
                              (GLintptr)cmd->copy.dst_offset,
                              (GLsizei)cmd->copy.size);
          break;
        case NGFGL_CMD_WRITE_IMAGE: {
          const ngf_image_ref *img_ref = &cmd->write_image.dst_image_ref;
          const ngf_offset3d *offset = &cmd->write_image.offset;
          const ngf_extent3d *extent = &cmd->write_image.dimensions;
          const GLenum bind_point = img_ref->image->bind_point;
          glGetError();
          glBindBuffer(GL_PIXEL_UNPACK_BUFFER, cmd->write_image.src_pbuffer);

          glBindTexture(bind_point, img_ref->image->glimage);
          if (bind_point != GL_TEXTURE_3D &&
              bind_point != GL_TEXTURE_2D_ARRAY &&
              bind_point != GL_TEXTURE_CUBE_MAP_ARRAY) {
            const GLenum real_bind_point =
                bind_point != GL_TEXTURE_CUBE_MAP
                    ? bind_point
                    : get_gl_cubemap_face(img_ref->cubemap_face);
            glTexSubImage2D(real_bind_point,
                            (GLsizei)img_ref->mip_level,
                            offset->x,
                            offset->y,
                            (GLsizei)extent->width,
                            (GLsizei)extent->height,
                            img_ref->image->glformat,
                            img_ref->image->gltype,
                            (void*)cmd->write_image.src_data_offset);
          } else {
            const GLsizei z = (GLsizei)
                (bind_point != GL_TEXTURE_CUBE_MAP_ARRAY
                     ? (uint32_t)offset->z
                     : (uint32_t)offset->z * 6u + img_ref->cubemap_face);
            glTexSubImage3D(bind_point,
                            (GLint)img_ref->mip_level,
                            offset->x,
                            offset->y,
                            z,
                            (GLsizei)extent->width,
                            (GLsizei)extent->height,
                            (GLsizei)extent->depth,
                            img_ref->image->glformat,
                            img_ref->image->gltype,
                            (void*)cmd->write_image.src_data_offset);
          }
          glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
          break;
        }
        default:
          assert(false);
        }
      }
    }
    ngfgl_cmd_buffer_free_cmds(bufs[buf_i]);
  }
  return NGF_ERROR_OK;
}

void ngf_finish() {
  glFlush();
  glFinish();
}

ngf_error ngf_begin_frame() {
  glGenBuffers(0, NULL);
  return NGF_ERROR_OK;
}

ngf_error ngf_end_frame() {
  return eglSwapBuffers(CURRENT_CONTEXT->dpy, CURRENT_CONTEXT->surface)
      ? NGF_ERROR_OK
      : NGF_ERROR_INVALID_OPERATION;
}
