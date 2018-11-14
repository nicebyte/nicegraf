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

#define _CRT_SECURE_NO_WARNINGS
#include "nicegraf.h"
#include "nicegraf_internal.h"
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

typedef struct {
  uint32_t ngf_binding_id;
  uint32_t native_binding_id;
} _ngf_native_binding;

struct ngf_graphics_pipeline {
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
  ngf_tessellation_info tessellation;
  const ngf_vertex_buf_binding_desc *vert_buf_bindings;
  uint32_t nvert_buf_bindings;
  GLenum primitive_type;
  uint32_t ndescriptors_layouts;
  const _ngf_native_binding **binding_map;
  GLuint owned_stages[NGF_STAGE_COUNT];
  uint32_t nowned_stages;
};

struct ngf_context {
  EGLDisplay dpy;
  EGLContext ctx;
  EGLConfig cfg;
  EGLSurface surface;
  ngf_graphics_pipeline cached_state;
  bool force_pipeline_update;
  bool has_swapchain;
  ngf_present_mode present_mode;
  ngf_type bound_index_buffer_type;
};

struct ngf_shader_stage {
  GLuint glprogram;
  GLenum gltype;
  GLenum glstagebit;

  char *source_code;
  uint32_t source_code_size;
};

struct ngf_buffer {
  GLuint glbuffer;
  GLint bind_point;
  GLint bind_point_read;
  GLenum access_type;
};

struct ngf_descriptor_set_layout {
  ngf_descriptor_set_layout_info info;
};

struct ngf_descriptor_set {
  ngf_descriptor_write *bind_ops;
  uint32_t *bindings;
  uint32_t nslots;
};

struct ngf_image {
  GLuint glimage;
  GLenum bind_point;
  bool is_renderbuffer;
  bool is_multisample;
  GLenum glformat;
  GLenum gltype;
};

struct ngf_sampler {
  GLuint glsampler;
};

struct ngf_render_target {
  GLuint framebuffer;
  uint32_t nattachments;
  ngf_attachment_type attachment_types[];
};

struct ngf_pass {
  ngf_clear_info *clears;
  ngf_attachment_load_op *loadops;
  uint32_t nloadops;
};

typedef enum {
  _NGF_CMD_BIND_PIPELINE,
  _NGF_CMD_BEGIN_PASS,
  _NGF_CMD_VIEWPORT,
  _NGF_CMD_SCISSOR,
  _NGF_CMD_LINE_WIDTH,
  _NGF_CMD_BLEND_CONSTANTS,
  _NGF_CMD_STENCIL_REFERENCE,
  _NGF_CMD_STENCIL_WRITE_MASK,
  _NGF_CMD_STENCIL_COMPARE_MASK,
  _NGF_CMD_BIND_DESCRIPTOR_SET,
  _NGF_CMD_BIND_VERTEX_BUFFER,
  _NGF_CMD_BIND_INDEX_BUFFER,
  _NGF_CMD_DRAW,
  _NGF_CMD_DRAW_INDEXED,
  _NGF_CMD_NONE
} _ngf_emulated_cmd_type;

typedef struct _ngf_emulated_cmd {
  struct _ngf_emulated_cmd *next;
  _ngf_emulated_cmd_type type;
  union {
    const ngf_graphics_pipeline *pipeline;
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
    struct {
      const ngf_descriptor_set *set;
      uint32_t slot;
    } descriptor_set_bind_op;
    struct {
      const ngf_buffer *buf;
      uint32_t binding;
      uint32_t offset;
    } vertex_buffer_bind_op;
    struct {
      const ngf_render_target *target;
      const ngf_pass *pass;
    } begin_pass;
    struct {
      const ngf_buffer *index_buffer;
      ngf_type type;
    } index_buffer_bind;
    struct {
      uint32_t nelements;
      uint32_t ninstances;
      uint32_t first_element;
      bool indexed;
    } draw;
  };
} _ngf_emulated_cmd;

struct ngf_cmd_buffer {
  _ngf_emulated_cmd *first_cmd;
  _ngf_emulated_cmd *last_cmd;
  bool recording;
  bool renderpass_active;
};

static GLenum gl_shader_stage(ngf_stage_type stage) {
  static const GLenum stages[] = {
    GL_VERTEX_SHADER,
    GL_TESS_CONTROL_SHADER,
    GL_TESS_EVALUATION_SHADER,
    GL_GEOMETRY_SHADER,
    GL_FRAGMENT_SHADER
  };
  return stages[stage];
}

static GLenum gl_shader_stage_bit(ngf_stage_type stage) {
  static const GLenum stages[] = {
    GL_VERTEX_SHADER_BIT,
    GL_TESS_CONTROL_SHADER_BIT,
    GL_TESS_EVALUATION_SHADER_BIT,
    GL_GEOMETRY_SHADER_BIT,
    GL_FRAGMENT_SHADER_BIT
  };
  return stages[stage];
}

static GLenum gl_type(ngf_type t) {
  static const GLenum types[] = {
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

static GLenum gl_poly_mode(ngf_polygon_mode m) {
  static const GLenum poly_mode[] = {
    GL_FILL,
    GL_LINE,
    GL_POINT
  };
  return poly_mode[(size_t)(m)];
}

static GLenum gl_cull_mode(ngf_cull_mode m) {
  static const GLenum cull_mode[] = {
    GL_BACK,
    GL_FRONT,
    GL_FRONT_AND_BACK,
    GL_NONE // not really a valid culling mode but GL has no
            // separate enum entry for "no culling", it's turned
            // on and off with a function call.
  };
  return cull_mode[(size_t)(m)];
}

static GLenum gl_face(ngf_front_face_mode m) {
  static const GLenum face[] = {
    GL_CCW,
    GL_CW
  };
  return face[(size_t)(m)];
}

static GLenum gl_compare(ngf_compare_op op) {
  static const GLenum compare[] = {
    GL_NEVER,
    GL_LESS,
    GL_LEQUAL,
    GL_EQUAL,
    GL_GEQUAL,
    GL_GREATER,
    GL_NOTEQUAL,
    GL_ALWAYS
  };

  return compare[(size_t)(op)];
}

static GLenum gl_stencil_op(ngf_stencil_op op) {
  static const GLenum o[] = {
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

static GLenum gl_blendfactor(ngf_blend_factor f) {
  static const GLenum factor[] = {
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

static GLenum get_gl_primitive_type(ngf_primitive_type p) {
  static const GLenum primitives[] = {
    GL_TRIANGLES,
    GL_TRIANGLE_STRIP,
    GL_TRIANGLE_FAN,
    GL_LINES,
    GL_LINE_STRIP,
    GL_PATCHES
  };
  return primitives[p];
}

typedef struct {
  GLenum internal_format;
  GLenum format;
  GLenum type;
  uint8_t rbits;
  uint8_t bbits;
  uint8_t gbits;
  uint8_t abits;
  uint8_t dbits;
  uint8_t sbits;
  bool    srgb;
} glformat;

static glformat get_glformat(ngf_image_format f) {
  static const glformat formats[] = {
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

static GLenum gl_filter(ngf_sampler_filter f) {
  static const GLenum filters[] = {
    GL_NEAREST,
    GL_LINEAR,
    GL_LINEAR_MIPMAP_NEAREST,
    GL_LINEAR_MIPMAP_LINEAR
  };

  return filters[f];
}

static GLenum gl_wrap(ngf_sampler_wrap_mode e) {
  static const GLenum modes[] = {
    GL_CLAMP_TO_EDGE,
    GL_CLAMP_TO_BORDER,
    GL_REPEAT,
    GL_MIRRORED_REPEAT
  };
  return modes[e];
}

void (*NGF_DEBUG_CALLBACK)(const char *message, const void *userdata) = NULL;
void *NGF_DEBUG_USERDATA = NULL;

void GL_APIENTRY ngf_gl_debug_callback(GLenum source,
                                       GLenum type,
                                       GLuint id,
                                       GLenum severity,
                                       GLsizei length,
                                       const GLchar* message,
                                       const void* userdata) {
  if (NGF_DEBUG_CALLBACK) {
    NGF_DEBUG_CALLBACK(message, userdata);
  }
}

ngf_error ngf_initialize(ngf_device_preference dev_pref) {return NGF_ERROR_OK;}

ngf_error ngf_create_context(const ngf_context_info *info,
                             ngf_context **result) {
  assert(info);
  assert(result);
  
  ngf_error err_code = NGF_ERROR_OK;
  const ngf_swapchain_info *swapchain_info = info->swapchain_info;
  const ngf_context *shared = info->shared_context;

  *result = NGF_ALLOC(ngf_context);
  ngf_context *ctx = *result;
  if (ctx == NULL) {
    err_code = NGF_ERROR_OUTOFMEM;
    goto ngf_create_context_cleanup;
  }

  // Connect to a display.
  eglBindAPI(EGL_OPENGL_API);
  ctx->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(ctx->dpy != EGL_NO_DISPLAY);
  int egl_maj, egl_min;
  if (eglInitialize(ctx->dpy, &egl_maj, &egl_min) == EGL_FALSE) {
    err_code = NGF_ERROR_CONTEXT_CREATION_FAILED;
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
    const glformat color_format = get_glformat(swapchain_info->cfmt);
    const glformat depth_stencil_format = get_glformat(swapchain_info->dfmt);
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
    err_code = NGF_ERROR_CONTEXT_CREATION_FAILED;
    goto ngf_create_context_cleanup;
  }

  // Create context with chosen config.
  EGLint is_debug = info->debug;
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
    err_code = NGF_ERROR_CONTEXT_CREATION_FAILED;
    goto ngf_create_context_cleanup;
  }

  // Create surface if necessary.
  if (swapchain_info) {
    const glformat color_format = get_glformat(swapchain_info->cfmt);
    EGLint egl_surface_attribs[] = {
      EGL_RENDER_BUFFER, swapchain_info->capacity_hint <= 1
                             ? EGL_SINGLE_BUFFER
                             : EGL_BACK_BUFFER,
      EGL_GL_COLORSPACE_KHR, color_format.srgb ? EGL_GL_COLORSPACE_SRGB_KHR
                                               : EGL_GL_COLORSPACE_LINEAR_KHR,
      EGL_NONE
    };
    ctx->surface = eglCreateWindowSurface(ctx->dpy,
                                          ctx->cfg,
                                          (EGLNativeWindowType)swapchain_info->native_handle,
                                          egl_surface_attribs);
    if (ctx->surface == EGL_NO_SURFACE) {
      err_code = NGF_ERROR_SWAPCHAIN_CREATION_FAILED;
      goto ngf_create_context_cleanup;
    }
  }

  ctx->force_pipeline_update = true;

ngf_create_context_cleanup:
  if (err_code != NGF_ERROR_OK) {
    ngf_destroy_context(ctx);
  }
  *result = ctx;
  return err_code;
}

ngf_error ngf_resize_context(ngf_context *ctx,
                             uint32_t new_width,
                             uint32_t new_height) {
  return NGF_ERROR_OK;
}

NGF_THREADLOCAL ngf_context *CURRENT_CONTEXT = NULL;
NGF_THREADLOCAL _ngf_block_allocator *COMMAND_POOL = NULL;

ngf_error ngf_set_context(ngf_context *ctx) {
  assert(ctx);
  bool result = eglMakeCurrent(ctx->dpy, ctx->surface, ctx->surface, ctx->ctx);
  if (result) {
    CURRENT_CONTEXT = ctx;
  }
  if (ctx->has_swapchain) {
    if (ctx->present_mode == NGF_PRESENTATION_MODE_FIFO) {
      eglSwapInterval(ctx->dpy, 1);
    } else {
      eglSwapInterval(ctx->dpy, 0);
    }
  }
  return result ? NGF_ERROR_OK : NGF_ERROR_INVALID_CONTEXT;
}

void ngf_destroy_context(ngf_context *ctx) {
  if (ctx) {
    if (ctx->ctx != EGL_NO_CONTEXT) {
      eglDestroyContext(ctx->dpy, ctx->ctx);
    }
    if (ctx->surface != EGL_NO_SURFACE) {
      eglDestroySurface(ctx->dpy, ctx->surface);
    }
    eglTerminate(ctx->dpy);
    NGF_FREE(ctx);
  }
}

ngf_error _ngf_compile_shader(const char *source, GLint source_len,
                              const char *debug_name,
                              GLenum stage,
                              const ngf_specialization_info *spec_info,
                              GLuint *result) {
  ngf_error err = NGF_ERROR_OK;
  *result = GL_NONE;

  // Obtain separate pointers to the first line of input (#version directive)
  // and the rest of input. We will later insert additional defines between them.
  const char *first_line = source;
  const char *rest_of_source = source;
  while(*(rest_of_source++) != '\n' &&
        (rest_of_source - source < source_len));
  const GLint first_line_len = (uint32_t)(rest_of_source - source);

  // Space for chunk pointers and chunk lengths.
  const uint32_t nsource_chunks =
      2u + (spec_info == NULL ? 0u : 1u);
  const char *source_chunks[3];
  GLint source_chunk_lengths[3];

  // Fill out chunk and chunk length data, generating additional defines from
  // the specialization constant data and inserting them between the first line
  // and the rest of the code.
  source_chunks[0] = first_line;
  source_chunk_lengths[0] = first_line_len;

  char *spec_defines = NULL;
  if (spec_info != NULL) {
    static char spec_define_template[] =
        "#define " NGF_EMULATED_SPEC_CONST_PREFIX "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    const uint32_t spec_define_max_size = NGF_ARRAYSIZE(spec_define_template);
    const uint32_t defines_buffer_size =
        spec_define_max_size * spec_info->nspecializations;
    spec_defines = NGF_ALLOCN(char, defines_buffer_size);
    uint32_t defines_length = 0u;
    for (uint32_t i= 0u; i < nsource_chunks - 1u; ++i) {
      const ngf_constant_specialization *spec = &spec_info->specializations[i];
      char *buf = spec_defines + defines_length;
      uint32_t max_write_len = defines_buffer_size - defines_length;
      uint32_t bytes_written = snprintf(buf, max_write_len, "#define %s%d ",
                                        NGF_EMULATED_SPEC_CONST_PREFIX,
                                        spec->constant_id);
      if (bytes_written < max_write_len) {
        defines_length += bytes_written;
        max_write_len -= bytes_written;
        buf += bytes_written;
      } else {

      }

      const uint8_t *data = (uint8_t*)spec_info->value_buffer + spec->offset;
      #define STRINGIFY_CONSTANT_VALUE(format, type) \
          bytes_written = snprintf(buf, max_write_len, format "\n", *(const type*)data); \
          break;
      switch (spec->type) {
      case NGF_TYPE_DOUBLE: STRINGIFY_CONSTANT_VALUE("%f", double);
      case NGF_TYPE_FLOAT:
      case NGF_TYPE_HALF_FLOAT: STRINGIFY_CONSTANT_VALUE("%f", float);
      case NGF_TYPE_INT8: STRINGIFY_CONSTANT_VALUE("%d", int8_t);
      case NGF_TYPE_INT16: STRINGIFY_CONSTANT_VALUE("%d", int16_t);
      case NGF_TYPE_INT32: STRINGIFY_CONSTANT_VALUE("%d", int32_t);
      case NGF_TYPE_UINT8: STRINGIFY_CONSTANT_VALUE("%d", uint8_t);
      case NGF_TYPE_UINT16: STRINGIFY_CONSTANT_VALUE("%d", uint16_t);
      case NGF_TYPE_UINT32: STRINGIFY_CONSTANT_VALUE("%d", uint32_t);
      default: assert(false);
      }
      #undef STRINGIZE_VALUE
      if (bytes_written < max_write_len) {
        defines_length += bytes_written;
      } else {
        assert(false);
      }
    }
    source_chunks[1] = spec_defines;
    source_chunk_lengths[1] = defines_length;
   }
  source_chunks[nsource_chunks - 1u] = rest_of_source;
  source_chunk_lengths[nsource_chunks - 1u] = source_len - first_line_len;

  // Compile the shader.
  GLuint shader = glCreateShader(stage);
  glShaderSource(shader, nsource_chunks, source_chunks, source_chunk_lengths);
  glCompileShader(shader);
  GLint compile_status;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
  if (compile_status != GL_TRUE) {
    if (NGF_DEBUG_CALLBACK) {
      // Note: theoretically, the OpenGL debug callback extension should
      // invoke the debug callback on shader compilation failure.
      // In practice, it varies between vendors, so we just force-call the
      // debug callback here to make sure it is always invoked. Sadness...
      // You should probably be validating your shaders through glslang as
      // one of the build steps anyways...
      GLint info_log_length = 0;
      glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);
      char *info_log = malloc(info_log_length + 1);
      info_log[info_log_length] = '\0';
      glGetShaderInfoLog(shader, info_log_length, &info_log_length, info_log);
      if (debug_name) {
        char msg[100];
        snprintf(msg, NGF_ARRAYSIZE(msg) - 1, "Error compiling %s",
                 debug_name);
        NGF_DEBUG_CALLBACK(msg, NGF_DEBUG_USERDATA);
      }
      NGF_DEBUG_CALLBACK(info_log, NGF_DEBUG_USERDATA);
      free(info_log);
    }
    err = NGF_ERROR_CREATE_SHADER_STAGE_FAILED;
    goto _ngf_compile_shader_cleanup;
  }
  *result = glCreateProgram();
  glProgramParameteri(*result, GL_PROGRAM_SEPARABLE, GL_TRUE);
  glAttachShader(*result, shader);
  glLinkProgram(*result);
  GLint link_status;
  glGetProgramiv(*result, GL_LINK_STATUS, &link_status);
  glDetachShader(*result, shader);
  if (link_status != GL_TRUE) {
    if (NGF_DEBUG_CALLBACK) {
      // See previous comment about debug callback.
      GLint info_log_length = 0;
      glGetProgramiv(*result, GL_INFO_LOG_LENGTH, &info_log_length);
      char *info_log = malloc(info_log_length + 1);
      info_log[info_log_length] = '\0';
      glGetProgramInfoLog(*result, info_log_length, &info_log_length,
                          info_log);
      if (debug_name) {
        char msg[100];
        snprintf(msg, NGF_ARRAYSIZE(msg) - 1, "Error linking %s",
                 debug_name);
        NGF_DEBUG_CALLBACK(msg, NGF_DEBUG_USERDATA);
      }
      NGF_DEBUG_CALLBACK(info_log, NGF_DEBUG_USERDATA);
      free(info_log);
    }
    err = NGF_ERROR_CREATE_SHADER_STAGE_FAILED;
    goto _ngf_compile_shader_cleanup;
  }

_ngf_compile_shader_cleanup:
  if (shader != GL_NONE) {
    glDeleteShader(shader);
  }
  if (spec_defines != NULL) {
    NGF_FREEN(spec_defines, spec_info->nspecializations);
   }
  if (err != NGF_ERROR_OK && *result != GL_NONE) {
    glDeleteProgram(*result);
  }
  return err;
}

ngf_error ngf_create_shader_stage(const ngf_shader_stage_info *info,
                                  ngf_shader_stage **result) {
  assert(info);
  assert(result);
  ngf_error err = NGF_ERROR_OK;
  ngf_shader_stage *stage = NULL;
  *result = NGF_ALLOC(ngf_shader_stage);
  stage = *result;
  if (stage == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_shader_stage_cleanup;
  }
  stage->gltype = gl_shader_stage(info->type);
  stage->glstagebit = gl_shader_stage_bit(info->type);
  stage->source_code_size = (GLint)info->content_length;
  err = _ngf_compile_shader(info->content, stage->source_code_size,
                            info->debug_name, stage->gltype, NULL,
                            &stage->glprogram);

  // Save off the source code in case we need to recompile for pipelines
  // doing specialization.
  stage->source_code = NGF_ALLOCN(char, stage->source_code_size);
  if (stage->source_code == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_shader_stage_cleanup;
  }
  strncpy(stage->source_code, info->content, info->content_length);

ngf_create_shader_stage_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_shader_stage(stage);
  }
  return err;
}

void ngf_destroy_shader_stage(ngf_shader_stage *stage) {
  if (stage != NULL) {
    glDeleteProgram(stage->glprogram);
    if (stage->source_code != NULL) {
      NGF_FREEN(stage->source_code, stage->source_code_size);
    }
    NGF_FREE(stage);
  }
}

ngf_error ngf_create_descriptor_set_layout(const ngf_descriptor_set_layout_info *info,
                                           ngf_descriptor_set_layout **result) {
  assert(info);
  assert(result);
  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_descriptor_set_layout);
  ngf_descriptor_set_layout *layout = *result;
  if (layout == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_descriptor_set_layout_cleanup;
  }

  layout->info.ndescriptors = info->ndescriptors;
  layout->info.descriptors = NGF_ALLOCN(ngf_descriptor_info,
                                        info->ndescriptors);
  if (layout->info.descriptors == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_descriptor_set_layout_cleanup;
  }
  memcpy(layout->info.descriptors,
         info->descriptors,
         sizeof(ngf_descriptor_info) * info->ndescriptors);

ngf_create_descriptor_set_layout_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_descriptor_set_layout(layout);
  }
  return err;
}

void ngf_destroy_descriptor_set_layout(ngf_descriptor_set_layout *layout) {
  if (layout != NULL) {
    if (layout->info.ndescriptors > 0 &&
        layout->info.descriptors) {
        NGF_FREEN(layout->info.descriptors, layout->info.ndescriptors);
    }
    NGF_FREE(layout);
  }
}

ngf_error ngf_create_descriptor_set(const ngf_descriptor_set_layout *layout,
                                    ngf_descriptor_set **result) {
  assert(layout);
  assert(result);

  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_descriptor_set);
  ngf_descriptor_set *set = *result;
  if (set == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_descriptor_set_cleanup;
  }

  set->nslots = layout->info.ndescriptors;
  set->bind_ops = NGF_ALLOCN(ngf_descriptor_write, layout->info.ndescriptors);
  if (set->bind_ops == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_descriptor_set_cleanup;
  }

  set->bindings = NGF_ALLOCN(GLuint, set->nslots);
  if (set->bindings == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_descriptor_set_cleanup;
  }

  for (size_t s = 0; s < set->nslots; ++s) {
    set->bind_ops[s].type = layout->info.descriptors[s].type;
    set->bindings[s] = layout->info.descriptors[s].id;
  }

ngf_create_descriptor_set_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_descriptor_set(set);
  }

  return err;
}

void ngf_destroy_descriptor_set(ngf_descriptor_set *set) {
  if (set != NULL) {
    if (set->nslots > 0 && set->bind_ops) {
      NGF_FREEN(set->bind_ops, set->nslots);
    }
    if (set->nslots > 0 && set->bindings) {
      NGF_FREEN(set->bindings, set->nslots);
    }
    NGF_FREE(set);
  }
}

ngf_error ngf_apply_descriptor_writes(const ngf_descriptor_write *writes,
                                      const uint32_t nwrites,
                                      ngf_descriptor_set *set) {
  for (size_t s = 0; s < nwrites; ++s) {
    const ngf_descriptor_write *write = &(writes[s]);
    bool found_binding = false;
    for (uint32_t s = 0u; s < set->nslots; ++s) {
      if (set->bind_ops[s].type == write->type &&
          set->bindings[s] == write->binding) {
        set->bind_ops[s] = *write;
        found_binding = true;
        break;
      }
    }
    if (!found_binding) return NGF_ERROR_INVALID_BINDING;
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_create_graphics_pipeline(const ngf_graphics_pipeline_info *info,
                                       ngf_graphics_pipeline **result) {
  static uint32_t global_id = 0;
  ngf_error err = NGF_ERROR_OK;

  *result = NGF_ALLOC(ngf_graphics_pipeline);
  ngf_graphics_pipeline *pipeline = *result;
  if (pipeline == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_pipeline_cleanup;
  }

  // Copy over some state.
  pipeline->viewport = *(info->viewport);
  pipeline->scissor = *(info->scissor);
  pipeline->rasterization = *(info->rasterization);
  pipeline->multisample = *(info->multisample);
  pipeline->depth_stencil = *(info->depth_stencil);
  pipeline->blend = *(info->blend);
  pipeline->tessellation = *(info->tessellation);
  
  const ngf_vertex_input_info *input = info->input_info;

  // Copy over vertex buffer binding information.
  pipeline->nvert_buf_bindings = input->nvert_buf_bindings;
  if (input->nvert_buf_bindings > 0) {
    ngf_vertex_buf_binding_desc *vert_buf_bindings =
        NGF_ALLOCN(ngf_vertex_buf_binding_desc,
                   input->nvert_buf_bindings);
    pipeline->vert_buf_bindings = vert_buf_bindings;
    if (pipeline->vert_buf_bindings == NULL) {
      err = NGF_ERROR_OUTOFMEM;
      goto ngf_create_pipeline_cleanup;
    }
    memcpy(vert_buf_bindings,
           input->vert_buf_bindings,
           sizeof(ngf_vertex_buf_binding_desc) * input->nvert_buf_bindings);
  } else {
    pipeline->vert_buf_bindings = NULL;
  }

  // Create a map of NGF -> OpenGL bindings.
  const ngf_pipeline_layout_info *pipeline_layout = info->layout;
  pipeline->ndescriptors_layouts = pipeline_layout->ndescriptors_layouts;
  _ngf_native_binding **binding_map =
      NGF_ALLOCN(_ngf_native_binding*, pipeline_layout->ndescriptors_layouts);
  pipeline->binding_map = binding_map;
  if (pipeline->binding_map == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_pipeline_cleanup;
  }
  memset(binding_map, 0,
         sizeof(_ngf_native_binding*) * pipeline_layout->ndescriptors_layouts);
  uint32_t total_c[NGF_DESCRIPTOR_TYPE_COUNT] = {0u};
  for (uint32_t set = 0u; set < pipeline_layout->ndescriptors_layouts; ++set) {
    uint32_t set_c[NGF_DESCRIPTOR_TYPE_COUNT] = {0u};
    const ngf_descriptor_set_layout_info *set_layout =
        &pipeline_layout->descriptors_layouts[set]->info;
    binding_map[set] = NGF_ALLOCN(_ngf_native_binding,
                                  set_layout->ndescriptors + 1);
    if (binding_map[set] == NULL) {
      err = NGF_ERROR_OUTOFMEM;
      goto ngf_create_pipeline_cleanup;
    }
    binding_map[set][set_layout->ndescriptors].ngf_binding_id = (uint32_t)(-1);
    for (uint32_t b = 0u; b < set_layout->ndescriptors; ++b) {
      _ngf_native_binding *mapping = &binding_map[set][b];
      const ngf_descriptor_info *desc_info = &set_layout->descriptors[b];
      const ngf_descriptor_type desc_type = desc_info->type;
      mapping->ngf_binding_id = desc_info->id;
      mapping->native_binding_id = total_c[desc_type] + (set_c[desc_type]++);
    }
    for (uint32_t i = 0u; i < NGF_DESCRIPTOR_TYPE_COUNT; ++i) {
      total_c[i] += set_c[i];
    }
  }

  // Store attribute format in VAO.
  glGenVertexArrays(1, &pipeline->vao);
  glBindVertexArray(pipeline->vao);
  for (size_t a = 0; a < input->nattribs; ++a) {
    const ngf_vertex_attrib_desc *attrib = &(input->attribs[a]);
    glEnableVertexAttribArray(attrib->location);
    glVertexAttribFormat(attrib->location,
                         attrib->size,
                         gl_type(attrib->type),
                         attrib->normalized,
                         attrib->offset);
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
  if (info->nshader_stages >= NGF_ARRAYSIZE(info->shader_stages)) {
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
    for (size_t s = 0; s < info->nshader_stages; ++s) {
      err =
          _ngf_compile_shader(info->shader_stages[s]->source_code,
                              info->shader_stages[s]->source_code_size,
                              "",
                              info->shader_stages[s]->gltype,
                              info->spec_info,
                              &pipeline->
                                  owned_stages[pipeline->nowned_stages++]);

      glUseProgramStages(pipeline->program_pipeline,
                         info->shader_stages[s]->glstagebit,
                         pipeline->owned_stages[s]);
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

void ngf_destroy_graphics_pipeline(ngf_graphics_pipeline *pipeline) {
  if (pipeline) {
    if (pipeline->nvert_buf_bindings > 0 &&
        pipeline->vert_buf_bindings) {
      NGF_FREEN(pipeline->vert_buf_bindings, pipeline->nvert_buf_bindings);
    }
    if (pipeline->binding_map) {
      for (uint32_t set = 0u; set < pipeline->ndescriptors_layouts; ++set) {
        if (pipeline->binding_map[set]) {
          NGF_FREE(pipeline->binding_map[set]);
        }
      }
      NGF_FREE(pipeline->binding_map);
    }
    glDeleteProgramPipelines(1, &pipeline->program_pipeline);
    glDeleteVertexArrays(1, &pipeline->vao);
    for (uint32_t s = 0u; s < pipeline->nowned_stages; ++s) {
      glDeleteProgram(pipeline->owned_stages[s]);
    }
    NGF_FREEN(pipeline, 1);
  }
}

ngf_error ngf_create_image(const ngf_image_info *info, ngf_image **result) {
  assert(info);
  assert(result);

  *result = NGF_ALLOC(ngf_image);
  ngf_image *image = *result;
  if (image == NULL) {
    return NGF_ERROR_OUTOFMEM;
  }

  glformat glf = get_glformat(info->format);
  image->glformat = glf.format;
  image->gltype = glf.type;
  image->is_multisample = info->nsamples > 1;
  if (info->usage_hint & NGF_IMAGE_USAGE_SAMPLE_FROM ||
      info->nmips > 1 ||
      info->extent.depth > 1 ||
      info->type != NGF_IMAGE_TYPE_IMAGE_2D) {
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
    } else if (info->type == NGF_IMAGE_TYPE_IMAGE_3D) {
      image->bind_point = GL_TEXTURE_3D;
    }
    glGenTextures(1, &(image->glimage));
    glBindTexture(image->bind_point, image->glimage);
    if (image->bind_point == GL_TEXTURE_2D) {
      glTexStorage2D(image->bind_point,
                     info->nmips,
                     glf.internal_format,
                     info->extent.width,
                     info->extent.height);
    } else if (image->bind_point == GL_TEXTURE_2D_ARRAY ||
               image->bind_point == GL_TEXTURE_3D) { 
      glTexStorage3D(image->bind_point,
                     info->nmips,
                     glf.internal_format,
                     info->extent.width,
                     info->extent.height,
                     info->extent.depth);
    } else if (image->bind_point == GL_TEXTURE_2D_MULTISAMPLE) {
      glTexStorage2DMultisample(image->bind_point,
                                info->nsamples,
                                glf.internal_format,
                                info->extent.width,
                                info->extent.height,
                                GL_TRUE);
    } else if (image->bind_point == GL_TEXTURE_2D_MULTISAMPLE_ARRAY) {
      glTexStorage3DMultisample(image->bind_point,
                                info->nsamples,
                                glf.internal_format,
                                info->extent.width,
                                info->extent.height,
                                info->extent.depth,
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
                            info->extent.width,
                            info->extent.height);
    } else {
      glRenderbufferStorageMultisample(image->bind_point,
                                       info->nsamples,
                                       glf.internal_format,
                                       info->extent.width,
                                       info->extent.height);
    }
  }
  return NGF_ERROR_OK;
}

void ngf_destroy_image(ngf_image *image) {
  if (image != NULL) {
    if (!image->is_renderbuffer) {
      glDeleteTextures(1, &(image->glimage));
    } else {
      glDeleteRenderbuffers(1, &(image->glimage));
    }
    NGF_FREE(image);
  }
}

ngf_error ngf_populate_image(ngf_image *image,
                             uint32_t level,
                             ngf_offset3d offset,
                             ngf_extent3d dimensions,
                             const void *data) {
  assert(image);
  assert(data);
  if (image->is_multisample || image->is_renderbuffer) {
    return NGF_ERROR_CANT_POPULATE_IMAGE;
  } else {
    glBindTexture(image->bind_point, image->glimage);
    if (image->bind_point == GL_TEXTURE_2D) {
      glTexSubImage2D(image->bind_point,
                      level,
                      offset.x,
                      offset.y,
                      dimensions.width,
                      dimensions.height,
                      image->glformat,
                      image->gltype,
                      data);
    } else {
      glTexSubImage3D(image->bind_point,
                      level,
                      offset.x,
                      offset.y,
                      offset.z,
                      dimensions.width,
                      dimensions.height,
                      dimensions.depth,
                      image->glformat,
                      image->gltype,
                      data);
    }
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_create_sampler(const ngf_sampler_info *info,
                             ngf_sampler **result) {
  assert(info);
  assert(result);

  *result = NGF_ALLOC(ngf_sampler);
  ngf_sampler *sampler = *result;
  if (sampler == NULL) {
    return NGF_ERROR_OUTOFMEM;
  }
  
  glGenSamplers(1, &(sampler->glsampler));
  glSamplerParameteri(sampler->glsampler, GL_TEXTURE_MIN_FILTER, gl_filter(info->min_filter));
  glSamplerParameteri(sampler->glsampler, GL_TEXTURE_MAG_FILTER, gl_filter(info->mag_filter));
  glSamplerParameteri(sampler->glsampler, GL_TEXTURE_WRAP_S, gl_wrap(info->wrap_s));
  glSamplerParameteri(sampler->glsampler, GL_TEXTURE_WRAP_T, gl_wrap(info->wrap_t));
  glSamplerParameteri(sampler->glsampler, GL_TEXTURE_WRAP_R, gl_wrap(info->wrap_r));
  glSamplerParameterf(sampler->glsampler, GL_TEXTURE_MIN_LOD, info->lod_min);
  glSamplerParameterf(sampler->glsampler, GL_TEXTURE_MAX_LOD, info->lod_max);
  glSamplerParameterf(sampler->glsampler, GL_TEXTURE_LOD_BIAS, info->lod_bias);
  glSamplerParameterfv(sampler->glsampler, GL_TEXTURE_BORDER_COLOR, info->border_color);
  // TODO: anisotropic filtering

  return NGF_ERROR_OK;
}

void ngf_destroy_sampler(ngf_sampler *sampler) {
  assert(sampler);
  glDeleteSamplers(1, &(sampler->glsampler));
  NGF_FREE(sampler);
}

ngf_error ngf_default_render_target(ngf_render_target **result) {
  static NGF_THREADLOCAL ngf_render_target *default_target = NULL;
  if (default_target == NULL) {
    default_target =
        (ngf_render_target*) NGF_ALLOCN(uint8_t,
                                        offsetof(ngf_render_target,
                                                 attachment_types) +
                                        3u * sizeof(ngf_attachment_type));
  }
  default_target->framebuffer = 0;
  default_target->attachment_types[0] = NGF_ATTACHMENT_COLOR;
  default_target->attachment_types[1] = NGF_ATTACHMENT_DEPTH;
  default_target->attachment_types[2] = NGF_ATTACHMENT_STENCIL;
  default_target->nattachments = 3;
  *result = default_target;
  return NGF_ERROR_OK;
}

ngf_error ngf_create_render_target(const ngf_render_target_info *info,
                                   ngf_render_target **result) {

  assert(info);
  assert(result);
  assert(info->nattachments < 7);

  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_render_target);
  ngf_render_target *render_target = *result;
  if (render_target == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_render_target_cleanup;
  }

  glGenFramebuffers(1, &(render_target->framebuffer));
  GLint old_framebuffer;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, render_target->framebuffer);
  size_t ncolor_attachment = 0;
  render_target->nattachments = info->nattachments;
  for (size_t i = 0; i < info->nattachments; ++i) {
    const ngf_attachment *a = &(info->attachments[i]);
    render_target->attachment_types[i] = a->type;
    GLenum attachment;
    switch (a->type) {
    case NGF_ATTACHMENT_COLOR:
      attachment = (GLenum)(GL_COLOR_ATTACHMENT0 + (ncolor_attachment++));
      break;
    case NGF_ATTACHMENT_DEPTH:
      attachment = GL_DEPTH_ATTACHMENT;
      break;
    case NGF_ATTACHMENT_DEPTH_STENCIL:
      attachment = GL_DEPTH_STENCIL_ATTACHMENT;
      break;
    case NGF_ATTACHMENT_STENCIL:
      attachment = GL_STENCIL_ATTACHMENT;
      break;
    default:
      assert(0);
    }
    if (!a->image_ref.image->is_renderbuffer &&
        (a->image_ref.layered ||
         a->image_ref.image->bind_point != GL_TEXTURE_2D_ARRAY)) {
      glFramebufferTexture(GL_FRAMEBUFFER,
                           attachment,
                           a->image_ref.image->glimage,
                           a->image_ref.mip_level);
    } else if (!a->image_ref.image->is_renderbuffer) {
      glFramebufferTextureLayer(GL_FRAMEBUFFER,
                                attachment,
                                a->image_ref.image->glimage,
                                a->image_ref.mip_level,
                                a->image_ref.layer);
    } else {
      glBindRenderbuffer(GL_RENDERBUFFER, 0);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER,
                                attachment,
                                GL_RENDERBUFFER,
                                a->image_ref.image->glimage);
    }
  }

  GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  bool fb_ok = fb_status == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, old_framebuffer);
  if (!fb_ok) {
    err = NGF_ERROR_INCOMPLETE_RENDER_TARGET;
    goto ngf_create_render_target_cleanup;
  }

ngf_create_render_target_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_render_target(render_target);
  }
  return err;
}

void ngf_destroy_render_target(ngf_render_target *render_target) {
  if (render_target != NULL) {
    glDeleteFramebuffers(1, &(render_target->framebuffer));
    NGF_FREE(render_target);
  }
}

ngf_error ngf_resolve_render_target(const ngf_render_target *src,
                                    ngf_render_target *dst,
                                    const ngf_irect2d *src_rect) {
  GLint prev_fbo;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, src->framebuffer);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst->framebuffer);
  glBlitFramebuffer(src_rect->x, src_rect->y,
                    src_rect->x + src_rect->width,
                    src_rect->y + src_rect->height,
                    src_rect->x, src_rect->y,
                    src_rect->x + src_rect->width,
                    src_rect->y + src_rect->height,
                    GL_COLOR_BUFFER_BIT,
                    GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
  return NGF_ERROR_OK;
}

ngf_error ngf_create_buffer(const ngf_buffer_info *info, ngf_buffer **result) {
  assert(info);
  assert(result);

  *result = NGF_ALLOC(ngf_buffer);
  ngf_buffer *buffer = *result;
  if (buffer == NULL) {
    return NGF_ERROR_OUTOFMEM;
  }

  glGenBuffers(1, &(buffer->glbuffer));
  switch (info->type) {
  case NGF_BUFFER_TYPE_VERTEX:
    buffer->bind_point = GL_ARRAY_BUFFER;
    buffer->bind_point_read = GL_ARRAY_BUFFER_BINDING;
    break;
  case NGF_BUFFER_TYPE_INDEX:
    buffer->bind_point = GL_ELEMENT_ARRAY_BUFFER;
    buffer->bind_point_read = GL_ELEMENT_ARRAY_BUFFER_BINDING;
    break;
  case NGF_BUFFER_TYPE_UNIFORM:
    buffer->bind_point = GL_UNIFORM_BUFFER;
    buffer->bind_point_read = GL_UNIFORM_BUFFER_BINDING;
    break;
  default:
    assert(0);
  }
  static GLenum access_types[3][3] = {
    {GL_STATIC_DRAW, GL_STATIC_READ, GL_STATIC_COPY},
    {GL_DYNAMIC_DRAW, GL_DYNAMIC_READ, GL_DYNAMIC_COPY},
    {GL_STREAM_DRAW, GL_DYNAMIC_READ, GL_DYNAMIC_COPY},
  };
  assert(info->access_freq< 3 && info->access_type < 3);
  buffer->access_type = access_types[(size_t)(info->access_freq)]
                                    [(size_t)(info->access_type)]; 

  if (info->access_freq != NGF_BUFFER_USAGE_STATIC) {
    glBindBuffer(buffer->bind_point, buffer->glbuffer);
    glBufferData(buffer->bind_point,
                 info->size,
                 NULL,
                 buffer->access_type);
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_populate_buffer(ngf_buffer *buf,
                              size_t offset,
                              size_t size,
                              const void *data) {
  assert(buf);
  assert(data);
  glBindBuffer(buf->bind_point, buf->glbuffer);
  if (buf->access_type == GL_STATIC_DRAW ||
      buf->access_type == GL_STATIC_COPY ||
      buf->access_type == GL_STATIC_READ) {
    glBufferData(buf->bind_point, size, data, buf->access_type);
  } else {
    glBufferSubData(buf->bind_point, offset, size, data);
  }
  return NGF_ERROR_OK;
}

void ngf_destroy_buffer(ngf_buffer *buffer) {
  if (buffer != NULL) {
    glDeleteBuffers(1, &(buffer->glbuffer));
    NGF_FREE(buffer);
  }
}

ngf_error ngf_create_pass(const ngf_pass_info *info, ngf_pass **result) {
  assert(info);
  assert(result);

  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_pass);
  ngf_pass *pass = *result;
  if (pass == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_pass_cleanup;
  }
  pass->nloadops = info->nloadops;
  pass->clears = NGF_ALLOCN(ngf_clear_info, info->nloadops);
  if (pass->clears == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_pass_cleanup;
  }
  memcpy(pass->clears, info->clears, sizeof(ngf_clear_info) * pass->nloadops);
  pass->loadops = NGF_ALLOCN(ngf_attachment_load_op, info->nloadops);
  if (pass->loadops == NULL) {
    err = NGF_ERROR_OUTOFMEM;
    goto ngf_create_pass_cleanup;
  }
  memcpy(pass->loadops,
         info->loadops,
         sizeof(ngf_attachment_load_op) * pass->nloadops);

ngf_create_pass_cleanup:
  if (err != NGF_ERROR_OK) {
    ngf_destroy_pass(pass);
  }
  return err;
}

void ngf_destroy_pass(ngf_pass *pass) {
  if (pass != NULL) {
    if (pass->nloadops > 0 && pass->clears) {
      NGF_FREEN(pass->clears, pass->nloadops);
    }
    if (pass->nloadops > 0 && pass->loadops) {
      NGF_FREEN(pass->loadops, pass->nloadops);
    }
    NGF_FREE(pass);
  }
}

ngf_error ngf_cmd_buffer_create(ngf_cmd_buffer **result) {
  assert(result);
  if (COMMAND_POOL == NULL) {
    COMMAND_POOL = _ngf_blkalloc_create(sizeof(_ngf_emulated_cmd), 65000u);
  }
  ngf_error err = NGF_ERROR_OK;
  *result = NGF_ALLOC(ngf_cmd_buffer);
  ngf_cmd_buffer *buf = *result;
  if (buf == NULL) {
    err = NGF_ERROR_OUTOFMEM;
  }
  buf->first_cmd = buf->last_cmd = NULL;
  buf->recording = buf->renderpass_active = false;
  return err;
}

void _ngf_cmd_buffer_free_cmds(ngf_cmd_buffer *buf) {
  bool has_first_cmd = buf->first_cmd != NULL;
  bool has_last_cmd = buf->last_cmd != NULL;
  assert(!(has_first_cmd ^ has_last_cmd) &&
          buf->last_cmd->next == NULL);
  if (has_first_cmd && has_last_cmd && COMMAND_POOL != NULL) {
    for (_ngf_emulated_cmd *c = buf->first_cmd; c != NULL; c = c->next) {
      _ngf_blkalloc_free(COMMAND_POOL, c);
    }
  }
  buf->first_cmd = buf->last_cmd = NULL;
}

void ngf_cmd_buffer_destroy(ngf_cmd_buffer *buf) {
  if (buf != NULL) {
    _ngf_cmd_buffer_free_cmds(buf);
    NGF_FREE(buf);
  }
}

ngf_error ngf_cmd_buffer_start(ngf_cmd_buffer *buf) {
  assert(buf);
  ngf_error err = NGF_ERROR_OK;
  if (buf->recording) {
    err = NGF_ERROR_CMD_BUFFER_ALREADY_RECORDING;
  } else {
    buf->recording = true;
    assert(buf->first_cmd == NULL && buf->last_cmd == NULL);
    buf->first_cmd = _ngf_blkalloc_alloc(COMMAND_POOL);
    if (buf->first_cmd == NULL) {
      err = NGF_ERROR_OUTOFMEM;
      NGF_FREE(buf);
    } else {
      buf->first_cmd->type = _NGF_CMD_NONE;
      buf->last_cmd = buf->first_cmd;
    }
  }
  return err;
}

ngf_error ngf_cmd_buffer_end(ngf_cmd_buffer *buf) {
  assert(buf);
  ngf_error err = NGF_ERROR_OK;
  if (!buf->recording) {
    err = NGF_ERROR_CMD_BUFFER_WAS_NOT_RECORDING;
  }
  buf->recording = false;
  return err;
}

#define _NGF_APPENDCMD(buf, cmd) { \
    cmd->next = NULL; \
    buf->last_cmd->next = cmd; \
    buf->last_cmd = cmd; }


void ngf_cmd_bind_pipeline(ngf_cmd_buffer *buf,
                           const ngf_graphics_pipeline *pipeline) {
  _ngf_emulated_cmd *cmd = _ngf_blkalloc_alloc(COMMAND_POOL);
  cmd->type = _NGF_CMD_BIND_PIPELINE;
  cmd->pipeline = pipeline;
  _NGF_APPENDCMD(buf, cmd);
}

void ngf_cmd_viewport(ngf_cmd_buffer *buf, const ngf_irect2d *viewport) {
  _ngf_emulated_cmd *cmd = _ngf_blkalloc_alloc(COMMAND_POOL);
  cmd->type = _NGF_CMD_VIEWPORT;
  cmd->viewport = *viewport;
  _NGF_APPENDCMD(buf, cmd);
}

void ngf_cmd_scissor(ngf_cmd_buffer *buf, const ngf_irect2d *scissor) {
  _ngf_emulated_cmd *cmd = _ngf_blkalloc_alloc(COMMAND_POOL);
  cmd->type = _NGF_CMD_SCISSOR;
  cmd->viewport = *scissor;
  _NGF_APPENDCMD(buf, cmd);
}

void ngf_cmd_stencil_reference(ngf_cmd_buffer *buf, uint32_t front,
                               uint32_t back) {
  _ngf_emulated_cmd *cmd = _ngf_blkalloc_alloc(COMMAND_POOL);
  cmd->type = _NGF_CMD_STENCIL_REFERENCE;
  cmd->stencil_reference.front = front;
  cmd->stencil_reference.back = back;
  _NGF_APPENDCMD(buf, cmd);
}

void ngf_cmd_stencil_compare_mask(ngf_cmd_buffer *buf, uint32_t front,
                                  uint32_t back) {
  _ngf_emulated_cmd *cmd = _ngf_blkalloc_alloc(COMMAND_POOL);
  cmd->type = _NGF_CMD_STENCIL_COMPARE_MASK;
  cmd->stencil_compare_mask.front = front;
  cmd->stencil_compare_mask.back = back;
  _NGF_APPENDCMD(buf, cmd);
}

void ngf_cmd_stencil_write_mask(ngf_cmd_buffer *buf, uint32_t front,
                                uint32_t back) {
  _ngf_emulated_cmd *cmd = _ngf_blkalloc_alloc(COMMAND_POOL);
  cmd->type = _NGF_CMD_STENCIL_WRITE_MASK;
  cmd->stencil_write_mask.front = front;
  cmd->stencil_write_mask.back = back;
  _NGF_APPENDCMD(buf, cmd);
}

void ngf_cmd_line_width(ngf_cmd_buffer *buf, float line_width) {
  _ngf_emulated_cmd *cmd = _ngf_blkalloc_alloc(COMMAND_POOL);
  cmd->type = _NGF_CMD_LINE_WIDTH;
  cmd->line_width = line_width;
  _NGF_APPENDCMD(buf, cmd);
}

void ngf_cmd_blend_factors(ngf_cmd_buffer *buf,
                           ngf_blend_factor sfactor,
                           ngf_blend_factor dfactor) {
  _ngf_emulated_cmd *cmd = _ngf_blkalloc_alloc(COMMAND_POOL);
  cmd->type = _NGF_CMD_BLEND_CONSTANTS;
  cmd->blend_factors.sfactor = sfactor;
  cmd->blend_factors.dfactor = dfactor;
  _NGF_APPENDCMD(buf, cmd);
}

void ngf_cmd_bind_descriptor_set(ngf_cmd_buffer *buf,
                                 const ngf_descriptor_set *set,
                                 uint32_t slot) {
  _ngf_emulated_cmd *cmd = _ngf_blkalloc_alloc(COMMAND_POOL);
  cmd->type = _NGF_CMD_BIND_DESCRIPTOR_SET;
  cmd->descriptor_set_bind_op.slot = slot;
  cmd->descriptor_set_bind_op.set = set;
  _NGF_APPENDCMD(buf, cmd);
}

void ngf_cmd_bind_vertex_buffer(ngf_cmd_buffer *buf,
                                const ngf_buffer *vbuf,
                                uint32_t binding, uint32_t offset) {
  _ngf_emulated_cmd *cmd = _ngf_blkalloc_alloc(COMMAND_POOL);
  cmd->type = _NGF_CMD_BIND_VERTEX_BUFFER;
  cmd->vertex_buffer_bind_op.binding = binding;
  cmd->vertex_buffer_bind_op.buf = vbuf;
  cmd->vertex_buffer_bind_op.offset = offset;
  _NGF_APPENDCMD(buf, cmd);
}

void ngf_cmd_bind_index_buffer(ngf_cmd_buffer *buf, const ngf_buffer *idxbuf,
                               ngf_type index_type) {
  _ngf_emulated_cmd *cmd = _ngf_blkalloc_alloc(COMMAND_POOL);
  cmd->type = _NGF_CMD_BIND_INDEX_BUFFER;
  cmd->index_buffer_bind.index_buffer = idxbuf;
  cmd->index_buffer_bind.type = index_type;
  _NGF_APPENDCMD(buf, cmd);
}

void ngf_cmd_begin_pass(ngf_cmd_buffer *buf, const ngf_pass *pass,
                        const ngf_render_target *target) {
  _ngf_emulated_cmd *cmd = _ngf_blkalloc_alloc(COMMAND_POOL);
  cmd->type = _NGF_CMD_BEGIN_PASS;
  cmd->begin_pass.target = target;
  cmd->begin_pass.pass = pass;
  _NGF_APPENDCMD(buf, cmd);
  buf->renderpass_active = true;
}

void ngf_cmd_end_pass(ngf_cmd_buffer *buf) {
  buf->renderpass_active = false;
}

void ngf_cmd_draw(ngf_cmd_buffer *buf, bool indexed,
                  uint32_t first_element, uint32_t nelements,
                  uint32_t ninstances) {
  _ngf_emulated_cmd *cmd = _ngf_blkalloc_alloc(COMMAND_POOL);
  cmd->type = _NGF_CMD_DRAW;
  cmd->draw.first_element = first_element;
  cmd->draw.nelements = nelements;
  cmd->draw.ninstances = ninstances;
  cmd->draw.indexed = indexed;
  _NGF_APPENDCMD(buf, cmd);
}

ngf_error ngf_cmd_buffer_submit(uint32_t nbuffers, ngf_cmd_buffer **bufs) {
  assert(bufs);
  const ngf_graphics_pipeline *bound_pipeline = &(CURRENT_CONTEXT->cached_state);
  for (uint32_t b = 0u; b < nbuffers; ++b) {
    const ngf_cmd_buffer *buf = bufs[b];
    for (const _ngf_emulated_cmd *cmd = buf->first_cmd->next; cmd != NULL; cmd = cmd->next) {
      switch (cmd->type) {
      case _NGF_CMD_BIND_PIPELINE: {
        const ngf_graphics_pipeline *cached_state = &CURRENT_CONTEXT->cached_state;
        const bool force_pipeline_update = CURRENT_CONTEXT->force_pipeline_update;
        const ngf_graphics_pipeline *pipeline = cmd->pipeline;
        if (cached_state->id != pipeline->id || force_pipeline_update) {
          CURRENT_CONTEXT->cached_state.id = pipeline->id;
          glBindProgramPipeline(pipeline->program_pipeline);
          if (!NGF_STRUCT_EQ(pipeline->viewport, cached_state->viewport) ||
              force_pipeline_update) {
            glViewport(pipeline->viewport.x,
                       pipeline->viewport.y,
                       pipeline->viewport.width,
                       pipeline->viewport.height);
          }

          if (!NGF_STRUCT_EQ(pipeline->scissor, cached_state->scissor) ||
              force_pipeline_update) {
            glScissor(pipeline->scissor.x,
                      pipeline->scissor.y,
                      pipeline->scissor.width,
                      pipeline->scissor.height);
          }

          const ngf_rasterization_info *rast = &(pipeline->rasterization);
          const ngf_rasterization_info *cached_rast = &(cached_state->rasterization);
          if (cached_rast->discard != rast->discard || force_pipeline_update) {
            if (rast->discard) {
              glEnable(GL_RASTERIZER_DISCARD);
            } else {
              glDisable(GL_RASTERIZER_DISCARD);
            }
          }
          if (cached_rast->polygon_mode != rast->polygon_mode ||
              force_pipeline_update) {
            glPolygonMode(GL_FRONT_AND_BACK, gl_poly_mode(rast->polygon_mode));
          }
          if (cached_rast->cull_mode != rast->cull_mode || force_pipeline_update) {
            if (rast->cull_mode != NGF_CULL_MODE_NONE) {
              glEnable(GL_CULL_FACE);
              glCullFace(gl_cull_mode(rast->cull_mode));
            } else {
              glDisable(GL_CULL_FACE);
            }
          }
          if (cached_rast->front_face != rast->front_face ||
              force_pipeline_update) {
            glFrontFace(gl_face(rast->front_face));
          }
          if (cached_rast->line_width != rast->line_width ||
              force_pipeline_update) {
            glLineWidth(rast->line_width);
          }

          if (cached_state->multisample.multisample !=
              pipeline->multisample.multisample ||
              force_pipeline_update) {
            if (pipeline->multisample.multisample) {
              glEnable(GL_MULTISAMPLE);
            } else {
              glDisable(GL_MULTISAMPLE);
            }
          }

          if (cached_state->multisample.alpha_to_coverage !=
              pipeline->multisample.alpha_to_coverage || force_pipeline_update) {
            if (pipeline->multisample.alpha_to_coverage) {
              glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
            } else {
              glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
            }
          }

          const ngf_depth_stencil_info *depth_stencil = &(pipeline->depth_stencil);
          const ngf_depth_stencil_info *cached_depth_stencil =
              &(cached_state->depth_stencil);
          if (cached_depth_stencil->depth_test != depth_stencil->depth_test ||
              force_pipeline_update) {
            if (depth_stencil->depth_test) {
              glEnable(GL_DEPTH_TEST);
              glDepthFunc(gl_compare(depth_stencil->depth_compare));
            } else {
              glDisable(GL_DEPTH_TEST);
            }
          }
          if (cached_depth_stencil->depth_write != depth_stencil->depth_write ||
              force_pipeline_update) {
            if (depth_stencil->depth_write) {
              glDepthMask(GL_TRUE);
            } else {
              glDepthMask(GL_FALSE);
            }
          }
          if (cached_depth_stencil->stencil_test != depth_stencil->stencil_test ||
              !NGF_STRUCT_EQ(cached_depth_stencil->back_stencil,
                             depth_stencil->back_stencil) ||
              !NGF_STRUCT_EQ(cached_depth_stencil->front_stencil,
                             depth_stencil->front_stencil) ||
              force_pipeline_update) {
            if (depth_stencil->stencil_test) {
              glEnable(GL_STENCIL_TEST);
              glStencilFuncSeparate(
                GL_FRONT,
                gl_compare(depth_stencil->front_stencil.compare_op),
                depth_stencil->front_stencil.reference,
                depth_stencil->front_stencil.compare_mask);
              glStencilOpSeparate(
                GL_FRONT,
                gl_stencil_op(depth_stencil->front_stencil.fail_op),
                gl_stencil_op(depth_stencil->front_stencil.depth_fail_op),
                gl_stencil_op(depth_stencil->front_stencil.pass_op));
              glStencilMaskSeparate(GL_FRONT,
                                    depth_stencil->front_stencil.write_mask);
              glStencilFuncSeparate(
                GL_BACK,
                gl_compare(depth_stencil->back_stencil.compare_op),
                depth_stencil->back_stencil.reference,
                depth_stencil->back_stencil.compare_mask);
              glStencilOpSeparate(
                GL_BACK,
                gl_stencil_op(depth_stencil->back_stencil.fail_op),
                gl_stencil_op(depth_stencil->back_stencil.depth_fail_op),
                gl_stencil_op(depth_stencil->back_stencil.pass_op));
              glStencilMaskSeparate(GL_BACK,
                                    depth_stencil->back_stencil.write_mask);
            } else { 
              glDisable(GL_STENCIL_TEST);
            }
          }
          if (cached_depth_stencil->min_depth != depth_stencil->min_depth ||
              cached_depth_stencil->max_depth != depth_stencil->max_depth ||
              force_pipeline_update) {
            glDepthRangef(depth_stencil->min_depth, depth_stencil->max_depth);
          }

          const ngf_blend_info *blend = &(pipeline->blend);
          const ngf_blend_info *cached_blend = &(cached_state->blend);
          if (cached_blend->enable != blend->enable ||
              cached_blend->sfactor != blend->sfactor ||
              cached_blend->dfactor != blend->dfactor || force_pipeline_update) {
            if (blend->enable) {
              glEnable(GL_BLEND);
              glBlendFunc(gl_blendfactor(blend->sfactor),
                          gl_blendfactor(blend->dfactor));
            } else {
              glDisable(GL_BLEND);
            }
          }

          if (cached_state->tessellation.patch_vertices !=
              pipeline->tessellation.patch_vertices || force_pipeline_update) {
            glPatchParameteri(GL_PATCH_VERTICES,
                              pipeline->tessellation.patch_vertices);
          }
          glBindVertexArray(pipeline->vao);
          CURRENT_CONTEXT->cached_state = *pipeline;
        }
        CURRENT_CONTEXT->force_pipeline_update = false;
        bound_pipeline = pipeline;
        break; }

      case _NGF_CMD_VIEWPORT:
        glViewport(cmd->viewport.x,
                   cmd->viewport.y,
                   cmd->viewport.width,
                   cmd->viewport.height);
          break;

      case _NGF_CMD_SCISSOR:
        glScissor(cmd->scissor.x,
                  cmd->scissor.y,
                  cmd->scissor.width,
                  cmd->scissor.height);
        break;

      case _NGF_CMD_LINE_WIDTH:
        glLineWidth(cmd->line_width);
        break;

      case _NGF_CMD_BLEND_CONSTANTS:
        glBlendFunc(cmd->blend_factors.sfactor,
                    cmd->blend_factors.dfactor);
        break;

      case _NGF_CMD_STENCIL_WRITE_MASK:
        glStencilMaskSeparate(GL_FRONT, cmd->stencil_write_mask.front);
        glStencilMaskSeparate(GL_BACK, cmd->stencil_write_mask.back);
        break;

      case _NGF_CMD_STENCIL_COMPARE_MASK: {
        GLint back_func, front_func, front_ref, back_ref;
        glGetIntegerv(GL_STENCIL_BACK_FUNC, &back_func);
        glGetIntegerv(GL_STENCIL_FUNC, &front_func);
        glGetIntegerv(GL_STENCIL_BACK_REF, &back_ref);
        glGetIntegerv(GL_STENCIL_REF, &front_ref);
        glStencilFuncSeparate(GL_FRONT,
                              front_func,
                              front_ref,
                              cmd->stencil_compare_mask.front);
        glStencilFuncSeparate(GL_BACK,
                              back_func,
                              back_ref,
                              cmd->stencil_compare_mask.back);
        break;
      }

      case _NGF_CMD_STENCIL_REFERENCE: {
        GLint back_func, front_func, front_mask, back_mask;
        glGetIntegerv(GL_STENCIL_BACK_FUNC, &back_func);
        glGetIntegerv(GL_STENCIL_FUNC, &front_func);
        glGetIntegerv(GL_STENCIL_BACK_VALUE_MASK, &back_mask);
        glGetIntegerv(GL_STENCIL_VALUE_MASK, &front_mask);
        glStencilFuncSeparate(GL_FRONT,
                              front_func,
                              cmd->stencil_reference.front,
                              front_mask);
        glStencilFuncSeparate(GL_BACK,
                              back_func,
                              cmd->stencil_reference.back,
                              back_mask);         
        break;
      }

      case _NGF_CMD_BIND_DESCRIPTOR_SET: {
        if (cmd->descriptor_set_bind_op.slot >
            bound_pipeline->ndescriptors_layouts) {
          return NGF_ERROR_INVALID_RESOURCE_SET_BINDING;
        }
        const uint32_t ngf_set = cmd->descriptor_set_bind_op.slot;
        const ngf_descriptor_set *set = cmd->descriptor_set_bind_op.set;
        for (size_t j = 0; j < set->nslots; ++j) {
          const ngf_descriptor_write *rbop = &(set->bind_ops[j]);
          const uint32_t ngf_binding = set->bindings[j];
          const _ngf_native_binding *binding_map = bound_pipeline->binding_map[ngf_set];
          uint32_t b_idx = 0u;
          while (binding_map[b_idx].ngf_binding_id != ngf_binding && 
                 binding_map[b_idx].ngf_binding_id != (uint32_t)(-1)) ++b_idx;
          if (binding_map[b_idx].native_binding_id == (uint32_t)(-1)) {
            return NGF_ERROR_INVALID_BINDING;
          }
          uint32_t native_binding = binding_map[b_idx].ngf_binding_id;

          // TODO: assert compatibility w/ descriptor set layout?
          switch (rbop->type) {
          case NGF_DESCRIPTOR_UNIFORM_BUFFER: {
            const ngf_descriptor_write_buffer *buf_bind_op =
                &(rbop->op.buffer_bind);
            glBindBufferRange(
              GL_UNIFORM_BUFFER,
              native_binding,
              buf_bind_op->buffer->glbuffer,
              buf_bind_op->offset,
              buf_bind_op->range);
            break;
            }
          case NGF_DESCRIPTOR_LOADSTORE_IMAGE: {
            const ngf_descriptor_write_image_sampler *img_bind_op =
                &(rbop->op.image_sampler_bind);
            glBindImageTexture(native_binding,
                               img_bind_op->image_subresource.image->glimage,
                               img_bind_op->image_subresource.mip_level,
                               img_bind_op->image_subresource.layered,
                               img_bind_op->image_subresource.layer,
                               GL_READ_ONLY, // TODO: fix
                               GL_RGB8); // TODO: fix
            break;
            }
          case NGF_DESCRIPTOR_TEXTURE: {
            const ngf_descriptor_write_image_sampler *img_bind_op =
                &(rbop->op.image_sampler_bind);
            glActiveTexture(native_binding);
            glBindTexture(img_bind_op->image_subresource.image->bind_point,
                          img_bind_op->image_subresource.image->glimage);
            break;
            }
          case NGF_DESCRIPTOR_SAMPLER: {
            const ngf_descriptor_write_image_sampler *img_bind_op =
                &(rbop->op.image_sampler_bind);
            glBindSampler(native_binding, img_bind_op->sampler->glsampler);
            break;
            }
          case NGF_DESCRIPTOR_TEXTURE_AND_SAMPLER: {
            const ngf_descriptor_write_image_sampler *img_bind_op =
                &(rbop->op.image_sampler_bind);
            glActiveTexture(GL_TEXTURE0 + native_binding);
            glBindTexture(img_bind_op->image_subresource.image->bind_point,
                          img_bind_op->image_subresource.image->glimage);
            glBindSampler(native_binding, img_bind_op->sampler->glsampler); 
            break;
            }
          default:
            assert(0);
          }
        }
        break;
      }

      case _NGF_CMD_BIND_VERTEX_BUFFER: {
        GLsizei stride = 0;
        bool found_binding = false;
        for (uint32_t b = 0;
             !found_binding && b < bound_pipeline->nvert_buf_bindings;
             ++b) {
          if (bound_pipeline->vert_buf_bindings[b].binding ==
              cmd->vertex_buffer_bind_op.binding) {
            stride = bound_pipeline->vert_buf_bindings[b].stride;
            found_binding = true;
          }
        }
        assert(found_binding);
        glBindVertexBuffer(cmd->vertex_buffer_bind_op.binding,
                           cmd->vertex_buffer_bind_op.buf->glbuffer,
                           cmd->vertex_buffer_bind_op.offset,
                           stride); 
        break;
      }

      case _NGF_CMD_BIND_INDEX_BUFFER:
        glBindBuffer(cmd->index_buffer_bind.index_buffer->bind_point,
                     cmd->index_buffer_bind.index_buffer->glbuffer);
        CURRENT_CONTEXT->bound_index_buffer_type = cmd->index_buffer_bind.type;
        break;

      case _NGF_CMD_BEGIN_PASS: {
        const ngf_pass *pass = cmd->begin_pass.pass;
        const ngf_render_target *target = cmd->begin_pass.target;
        glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);
        uint32_t c = 0u;
        uint32_t color_clear = 0u;
        for (uint32_t l = 0u; l < pass->nloadops; ++l) {
          if (pass->loadops[l] == NGF_LOAD_OP_CLEAR) {
            const ngf_clear_info *clear = &pass->clears[c++];
            switch (target->attachment_types[l]) {
            case NGF_ATTACHMENT_COLOR:
              glClearBufferfv(GL_COLOR, color_clear++, clear->clear_color);
              break;
            case NGF_ATTACHMENT_DEPTH:
              glClearBufferfv(GL_DEPTH, 0, &clear->clear_depth);
              break;
            case NGF_ATTACHMENT_STENCIL:
              glClearBufferiv(GL_STENCIL, 0, &clear->clear_stencil);
              break;
            }
          }
        }
        break;
      }

      case _NGF_CMD_DRAW:
        if (!cmd->draw.indexed && cmd->draw.ninstances == 1u) {
          glDrawArrays(bound_pipeline->primitive_type, cmd->draw.first_element,
                       cmd->draw.nelements);
        } else if (!cmd->draw.indexed && cmd->draw.ninstances > 1u) {
          glDrawArraysInstanced(bound_pipeline->primitive_type,
                                cmd->draw.first_element,
                                cmd->draw.nelements,
                                cmd->draw.ninstances);
        } else if (cmd->draw.indexed && cmd->draw.ninstances == 1u) {
          glDrawElements(bound_pipeline->primitive_type,
                         cmd->draw.nelements,
                         gl_type(CURRENT_CONTEXT->bound_index_buffer_type),
                         (void*)(uintptr_t)cmd->draw.first_element);
        } else if (cmd->draw.indexed && cmd->draw.ninstances > 1u) {
          glDrawElementsInstanced(bound_pipeline->primitive_type,
                                  cmd->draw.nelements,
                                  gl_type(CURRENT_CONTEXT->bound_index_buffer_type),
                                  (void*)(uintptr_t)cmd->draw.first_element,
                                  cmd->draw.ninstances);
        }
        break;
      default:
        assert(false);
      }
    }
    _ngf_cmd_buffer_free_cmds(bufs[b]);
  }
  return NGF_ERROR_OK;
}

void ngf_finish() {
  glFlush();
  glFinish();
}

void ngf_debug_message_callback(void *userdata,
                                void(*callback)(const char*, const void*)) {

  glDebugMessageControl(GL_DONT_CARE,
                        GL_DONT_CARE,
                        GL_DONT_CARE,
                        0,
                        NULL,
                        GL_TRUE);
  glEnable(GL_DEBUG_OUTPUT);
  NGF_DEBUG_CALLBACK = callback;
  NGF_DEBUG_USERDATA = userdata;
  glDebugMessageCallback(ngf_gl_debug_callback, userdata);
}

void ngf_insert_log_message(const char *message) {
  glDebugMessageInsert(
    GL_DEBUG_SOURCE_APPLICATION,
    GL_DEBUG_TYPE_MARKER,
    0,
    GL_DEBUG_SEVERITY_NOTIFICATION,
    (uint32_t)strlen(message),
    message);
}

void ngf_begin_debug_group(const char *title) {
  glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, (uint32_t)strlen(title), title);
}

void ngf_end_debug_group() {
  glPopDebugGroup();
}

ngf_error ngf_begin_frame(ngf_context *ctx) { return NGF_ERROR_OK; }

ngf_error ngf_end_frame(ngf_context *ctx) {
  return eglSwapBuffers(ctx->dpy, ctx->surface)
      ? NGF_ERROR_OK
      : NGF_ERROR_END_FRAME_FAILED;
}

