/**
Copyright (c) 2018 nicegraf contributors

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights to/Users/nicebyte/Code/nicegraf-samples/02-spec-consts/main.cpp
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

#include "nicegraf.h"
#include "nicegraf_wrappers.h"
#include "nicegraf_internal.h"
#include <new>
#include <memory>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#if TARGET_OS_OSX
#import <AppKit/AppKit.h>
#define _NGF_VIEW_TYPE NSView
#else
#import <UIKit/UIKit.h>
#define _NGF_VIEW_TYPE UIView
#endif

id<MTLDevice> MTL_DEVICE = nil;

#pragma mark ngf_struct_definitions

struct ngf_context {
  id<MTLDevice> device = nil;
  CAMetalLayer *layer = nil;
  id<MTLCommandQueue> queue = nil;
  id<CAMetalDrawable> next_drawable = nil;
  bool is_current = false;
  ngf_swapchain_info swapchain_info;
  id<MTLCommandBuffer> pending_cmd_buffer = nil;
};

NGF_THREADLOCAL ngf_context *CURRENT_CONTEXT = nullptr;

struct ngf_render_target {
  mutable MTLRenderPassDescriptor *pass_descriptor = nil;
  bool is_default = false;
};

struct ngf_cmd_buffer {
  id<MTLCommandBuffer> mtl_cmd_buffer = nil;
  id<MTLRenderCommandEncoder> active_rce = nil;
  const ngf_graphics_pipeline *active_pipe = nullptr;
  id<MTLBuffer> bound_index_buffer = nil;
  MTLIndexType bound_index_buffer_type;
};

struct ngf_shader_stage {
  id<MTLLibrary> func_lib = nil;
  ngf_stage_type type;
};

struct ngf_graphics_pipeline {
  id<MTLRenderPipelineState> pipeline = nil;
  MTLPrimitiveType primitive_type = MTLPrimitiveTypeTriangle;
};

struct ngf_buffer {
  id<MTLBuffer> mtl_buffer = nil;
};

struct ngf_attrib_buffer {
  id<MTLBuffer> mtl_buffer = nil;
};

struct ngf_index_buffer {
  id<MTLBuffer> mtl_buffer = nil;
};

struct ngf_uniform_buffer {
  id<MTLBuffer> mtl_buffer = nil;
};

#pragma mark ngf_enum_maps

static MTLPixelFormat get_mtl_pixel_format(ngf_image_format fmt) {
  static const MTLPixelFormat pixel_format[NGF_IMAGE_FORMAT_COUNT] = {
    MTLPixelFormatR8Unorm,
    MTLPixelFormatRG8Unorm,
    MTLPixelFormatInvalid, // RGB8, Metal does not support.
    MTLPixelFormatRGBA8Unorm,
    MTLPixelFormatInvalid, // RGB8, Metal does not support.
    MTLPixelFormatRGBA8Unorm_sRGB,
    MTLPixelFormatInvalid, // RGB8, Metal does not support.
    MTLPixelFormatBGRA8Unorm,
    MTLPixelFormatInvalid, // RGB8, Metal does not support.
    MTLPixelFormatBGRA8Unorm_sRGB,
    MTLPixelFormatR32Float,
    MTLPixelFormatRG32Float,
    MTLPixelFormatInvalid, // RGB32F, Metal does not support.
    MTLPixelFormatRGBA32Float,
    MTLPixelFormatR16Float,
    MTLPixelFormatRG16Float,
    MTLPixelFormatInvalid, // RGB16F, Metal does not support.
    MTLPixelFormatRGBA16Float,
    MTLPixelFormatDepth32Float,
#if TARGET_OS_OSX
    MTLPixelFormatDepth16Unorm,
    MTLPixelFormatDepth24Unorm_Stencil8,
#else
    MTLPixelFormatInvalid, // DEPTH16, iOS does not support.
    MTLPixelFormatInvalid, // DEPTH24_STENCIL8, iOS does not support.
#endif
  };
  return fmt >= NGF_IMAGE_FORMAT_UNDEFINED
             ? MTLPixelFormatInvalid
             : pixel_format[fmt];
}

static MTLLoadAction get_mtl_load_action(ngf_attachment_load_op op) {
  static const MTLLoadAction action[] = {
    MTLLoadActionDontCare,
    MTLLoadActionLoad,
    MTLLoadActionClear
  };
  return action[op];
}

static MTLDataType get_mtl_type(ngf_type type) {
  static const MTLDataType types[NGF_TYPE_COUNT] = {
    MTLDataTypeNone, /* Int8, Metal does not support.*/
    MTLDataTypeNone, /*UInt8, Metal does not support*/
    MTLDataTypeShort,
    MTLDataTypeUShort,
    MTLDataTypeInt,
    MTLDataTypeUInt,
    MTLDataTypeFloat,
    MTLDataTypeHalf,
    MTLDataTypeNone /* Double,Metal does not support.*/
  };
  return types[type];
}

static MTLVertexFormat get_mtl_attrib_format(ngf_type type, uint32_t size) {
  static const MTLVertexFormat formats[NGF_TYPE_COUNT][4] = {
    {MTLVertexFormatChar, MTLVertexFormatChar2,
     MTLVertexFormatChar3, MTLVertexFormatChar4},
    {MTLVertexFormatUChar, MTLVertexFormatUChar2,
     MTLVertexFormatUChar3, MTLVertexFormatUChar4},
    {MTLVertexFormatShort, MTLVertexFormatShort2,
     MTLVertexFormatShort3, MTLVertexFormatShort4},
    {MTLVertexFormatUShort, MTLVertexFormatUShort2,
     MTLVertexFormatUShort3, MTLVertexFormatUShort4},
    {MTLVertexFormatInt, MTLVertexFormatInt2,
     MTLVertexFormatInt3, MTLVertexFormatInt4},
    {MTLVertexFormatUInt, MTLVertexFormatUInt2,
     MTLVertexFormatUInt3, MTLVertexFormatUInt4},
    {MTLVertexFormatFloat, MTLVertexFormatFloat2,
     MTLVertexFormatFloat3, MTLVertexFormatFloat4},
    {MTLVertexFormatHalf, MTLVertexFormatHalf2,
     MTLVertexFormatHalf3, MTLVertexFormatHalf4},
    {MTLVertexFormatInvalid, MTLVertexFormatInvalid,
     MTLVertexFormatInvalid, MTLVertexFormatInvalid}, // Double, Metal does not
                                                      // support.
  };
  assert(size <= 4u && size > 0u);
  return formats[type][size - 1u];
}

static MTLVertexStepFunction get_mtl_step_function(ngf_input_rate rate) {
  static const MTLVertexStepFunction funcs[NGF_INPUT_RATE_COUNT] = {
    MTLVertexStepFunctionPerVertex,
    MTLVertexStepFunctionPerInstance
  };
  return funcs[rate];
}

static MTLPrimitiveTopologyClass
get_mtl_primitive_topology_class(ngf_primitive_type type) {
  static const MTLPrimitiveTopologyClass
  topo_class[NGF_PRIMITIVE_TYPE_COUNT] = {
    MTLPrimitiveTopologyClassTriangle,
    MTLPrimitiveTopologyClassTriangle,
    MTLPrimitiveTopologyClassUnspecified, // Triangle Fan, Metal does not
                                          // support.
    MTLPrimitiveTopologyClassLine,
    MTLPrimitiveTopologyClassLine,
    MTLPrimitiveTopologyClassUnspecified, // Patch list, tessellation not
                                          // implemented yet.
  };
  return topo_class[type];
}

static MTLPrimitiveType get_mtl_primitive_type(ngf_primitive_type type) {
  static const MTLPrimitiveType types[NGF_PRIMITIVE_TYPE_COUNT] = {
    MTLPrimitiveTypeTriangle,
    MTLPrimitiveTypeTriangleStrip,
    MTLPrimitiveTypePoint, // Incorrect
    MTLPrimitiveTypeLine,
    MTLPrimitiveTypeLineStrip,
    MTLPrimitiveTypePoint // Incorrect;
  };
  return types[type];
}

static MTLIndexType get_mtl_index_type(ngf_type type) {
  assert(type == NGF_TYPE_UINT16 || type == NGF_TYPE_UINT32);
  return type == NGF_TYPE_UINT16 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32;
}

template <class NgfObjType, void(*Dtor)(NgfObjType*)>
class _ngf_object_nursery {
public:
  explicit _ngf_object_nursery(NgfObjType *memory) : ptr_(memory) {
    new(memory) NgfObjType();
  }
  ~_ngf_object_nursery() { if(ptr_ != nullptr) { Dtor(ptr_); } }
  NgfObjType* operator->() { return ptr_; }
  NgfObjType* release() {
    NgfObjType *tmp = ptr_;
    ptr_ = nullptr;
    return tmp;
  }
  operator bool() { return ptr_ != nullptr; }
private:
  NgfObjType *ptr_;
};

#define _NGF_NURSERY(type, name) \
  _ngf_object_nursery<ngf_##type, ngf_destroy_##type> \
      name(NGF_ALLOC(ngf_##type));

#pragma mark ngf_function_implementations

ngf_error ngf_initialize(ngf_device_preference dev_pref) {
#if TARGET_OS_OSX
  id<NSObject> dev_observer = nil;
  const NSArray<id<MTLDevice>> *devices =
      MTLCopyAllDevicesWithObserver(&dev_observer,
                                    ^(id<MTLDevice> d,
                                      MTLDeviceNotificationName n){});
  bool found_device = false;
  for (uint32_t d = 0u; !found_device && d < devices.count; ++d) {
    MTL_DEVICE = devices[d++];
    found_device = (dev_pref != NGF_DEVICE_PREFERENCE_DISCRETE) ||
                   !MTL_DEVICE.lowPower;
  }
#else
  MTL_DEVICE = MTLCreateSystemDefaultDevice();
  bool found_device = (MTL_DEVICE != nil);
#endif

  return found_device ? NGF_ERROR_OK : NGF_ERROR_INITIALIZATION_FAILED;
}

ngf_error ngf_begin_frame(ngf_context *ctx) {
  assert(ctx && ctx == CURRENT_CONTEXT);
  CURRENT_CONTEXT->next_drawable = [ctx->layer nextDrawable];
  return (!CURRENT_CONTEXT->next_drawable) ?  NGF_ERROR_NO_FRAME : NGF_ERROR_OK;
}

ngf_error ngf_end_frame(ngf_context*) {
  if(CURRENT_CONTEXT->next_drawable && CURRENT_CONTEXT->pending_cmd_buffer) {
    [CURRENT_CONTEXT->pending_cmd_buffer
       presentDrawable:CURRENT_CONTEXT->next_drawable];
    [CURRENT_CONTEXT->pending_cmd_buffer commit];
    CURRENT_CONTEXT->next_drawable = nil;
    CURRENT_CONTEXT->pending_cmd_buffer = nil;
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_default_render_target(
  ngf_attachment_load_op color_load_op,
  ngf_attachment_load_op depth_load_op,
  const ngf_clear *clear_color,
  const ngf_clear *clear_depth,
  ngf_render_target **result) {
  assert(result);
  if (CURRENT_CONTEXT->layer) {
    _NGF_NURSERY(render_target, rt);
    rt->is_default = true;
    rt->pass_descriptor = [MTLRenderPassDescriptor new];
    rt->pass_descriptor.colorAttachments[0].texture = nil;
    rt->pass_descriptor.colorAttachments[0].loadAction =
        get_mtl_load_action(color_load_op);
    rt->pass_descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    if (color_load_op == NGF_LOAD_OP_CLEAR) {
      assert(clear_color);
      rt->pass_descriptor.colorAttachments[0].clearColor =
          MTLClearColorMake(clear_color->clear_color[0],
                            clear_color->clear_color[1],
                            clear_color->clear_color[2],
                            clear_color->clear_color[3]);
    }
    // TODO: depth
    *result = rt.release();
    return NGF_ERROR_OK;
  } else {
    return NGF_ERROR_NO_DEFAULT_RENDER_TARGET;;
  }
}

CAMetalLayer* _ngf_create_swapchain(ngf_swapchain_info &swapchain_info,
                                   id<MTLDevice> device) {
  CAMetalLayer *layer = [CAMetalLayer layer];
  layer.device = device;
  CGSize size;
  size.width = swapchain_info.width;
  size.height = swapchain_info.height;
  layer.drawableSize = size; 
  layer.pixelFormat = get_mtl_pixel_format(swapchain_info.cfmt);
#if TARGET_OS_OSX
  if (@available(macOS 10.13.2, *)) {
    layer.maximumDrawableCount = swapchain_info.capacity_hint;
  }
  if (@available(macOS 10.13, *)) {
    layer.displaySyncEnabled =
          (swapchain_info.present_mode == NGF_PRESENTATION_MODE_IMMEDIATE);
  }
#endif
  _NGF_VIEW_TYPE *view=
        CFBridgingRelease((void*)swapchain_info.native_handle);
#if TARGET_OS_OSX
  [view setLayer:layer];
#else
  [view.layer addSublayer:layer];
  [layer setContentsScale:view.layer.contentsScale];
  [layer setPosition:view.center];
#endif
  swapchain_info.native_handle = (uintptr_t)(CFBridgingRetain(view));

  return layer;
}

ngf_error ngf_create_context(const ngf_context_info *info,
                             ngf_context **result) {
  assert(info);
  assert(result);
  _NGF_NURSERY(context, ctx);
  if (!ctx) {
    return NGF_ERROR_OUTOFMEM;
  }

  ctx->device = MTL_DEVICE;
  if (info->shared_context != nullptr) {
    ctx->queue = info->shared_context->queue;
  } else {
    ctx->queue = [ctx->device newCommandQueue];
  }

  if (info->swapchain_info) {
    ctx->swapchain_info = *(info->swapchain_info);
    ctx->layer = _ngf_create_swapchain(ctx->swapchain_info, ctx->device);
    // TODO: depth
  }
 
  *result = ctx.release(); 
  return NGF_ERROR_OK;
}

void ngf_destroy_context(ngf_context *ctx) {
  // TODO: unset current context
  assert(ctx);
  ctx->~ngf_context();
  NGF_FREE(ctx);
}

ngf_error ngf_resize_context(ngf_context *ctx,
                             uint32_t new_width,
                             uint32_t new_height) {
  assert(ctx);
  ctx->swapchain_info.width = new_width;
  ctx->swapchain_info.height = new_height;
  ctx->layer = _ngf_create_swapchain(ctx->swapchain_info, ctx->device);
  return NGF_ERROR_OK;
}

ngf_error ngf_set_context(ngf_context *ctx) {
  if(CURRENT_CONTEXT != NULL) {
    return NGF_ERROR_CALLER_HAS_CURRENT_CONTEXT;
  } else if (ctx->is_current) {
    return NGF_ERROR_CONTEXT_ALREADY_CURRENT;
  }
  CURRENT_CONTEXT = ctx;
  ctx->is_current = true;
  return NGF_ERROR_OK;
}

ngf_error ngf_create_shader_stage(const ngf_shader_stage_info *info,
                                  ngf_shader_stage **result) {
  assert(info);
  assert(result);
 
  _NGF_NURSERY(shader_stage, stage);
  if (!stage) {
    return NGF_ERROR_OUTOFMEM;
  }
  
  stage->type = info->type;

  // Create a MTLLibrary for this stage.
  if (!info->is_binary) { // Either compile from source...
    NSString *source = [[NSString alloc] initWithBytes:info->content
                                 length:info->content_length
                                 encoding:NSUTF8StringEncoding];
    MTLCompileOptions *opts = [MTLCompileOptions new];
    NSError *err = nil;
    stage->func_lib = [CURRENT_CONTEXT->device newLibraryWithSource:source
                                  options:opts
                                  error:&err];
    if (!stage->func_lib) {
      // TODO: call debug callback with error message here.
      NSLog(@"%@\n", err);
      return NGF_ERROR_CREATE_SHADER_STAGE_FAILED;    
    }
  } else { // ...or set binary.
#if TARGET_OS_OSX
    uint32_t required_format = 0u;
#else
    uint32_t required_format = 1u;
#endif
    if (info->binary_format != required_format) {
      return NGF_ERROR_SHADER_STAGE_INVALID_BINARY_FORMAT;
    }
    dispatch_data_t libdata = dispatch_data_create(info->content,
                                                   info->content_length,
                                                   dispatch_get_main_queue(),
                                                   ^{});
    NSError *err;
    stage->func_lib = [CURRENT_CONTEXT->device newLibraryWithData:libdata
                                               error:&err];
    if (!stage->func_lib) {
      return NGF_ERROR_CREATE_SHADER_STAGE_FAILED;
    }
  }

  // Set debug name.
  if (info->debug_name != nullptr) {
    stage->func_lib.label = [[NSString alloc]
                               initWithUTF8String:info->debug_name];
  }

  *result = stage.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_shader_stage(ngf_shader_stage *stage) {
  if (stage != nullptr) {
    stage->~ngf_shader_stage();
    NGF_FREE(stage);
  }
}

ngf_error ngf_get_binary_shader_stage(const ngf_shader_stage_info *info,
                                      ngf_shader_stage **result) {
  return NGF_ERROR_CANNOT_READ_BACK_SHADER_STAGE_BINARY;
}

ngf_error ngf_get_binary_shader_stage_size(const ngf_shader_stage *stage,
                                           size_t *size) {
  return NGF_ERROR_CANNOT_READ_BACK_SHADER_STAGE_BINARY;
}

ngf_error ngf_create_render_target(const ngf_render_target_info *info,
                                   ngf_render_target **result) {
  return NGF_ERROR_OK;
}

void ngf_destroy_render_target(ngf_render_target *rt) {
  if (rt != nullptr) {
    rt->~ngf_render_target();
    NGF_FREE(rt);
  }
}

id<MTLFunction> _ngf_get_shader_main(id<MTLLibrary> func_lib,
                                     MTLFunctionConstantValues *spec_consts) {
  NSError *err = nil;
  return spec_consts == nil
           ? [func_lib newFunctionWithName:@"main0"]
           : [func_lib newFunctionWithName:@"main0"
                            constantValues:spec_consts
                                     error:&err];
}

ngf_error ngf_create_graphics_pipeline(const ngf_graphics_pipeline_info *info,
                                       ngf_graphics_pipeline **result) {
  assert(info);
  assert(result);
  auto *mtl_pipe_desc = [MTLRenderPipelineDescriptor new];
  mtl_pipe_desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm; // TODO: fix
  
  // Populate specialization constant values.
  MTLFunctionConstantValues *spec_consts = nil;
  if (info->spec_info != nullptr) {
    spec_consts = [MTLFunctionConstantValues new];
    for (uint32_t s = 0u; s < info->spec_info->nspecializations; ++s) {
      const ngf_constant_specialization *spec =
          &info->spec_info->specializations[s];
      MTLDataType type = get_mtl_type(spec->type);
      if (type == MTLDataTypeNone) {
        return NGF_ERROR_FAILED_TO_CREATE_PIPELINE;
      }
      void *write_ptr =
          ((uint8_t*)info->spec_info->value_buffer + spec->offset);
      [spec_consts setConstantValue:write_ptr
                               type:get_mtl_type(spec->type)
                            atIndex:spec->constant_id];
    }
  }
  
  // Set stage functions.
  for (uint32_t s = 0u; s < info->nshader_stages; ++s) {
    const ngf_shader_stage *stage = info->shader_stages[s];
    if (stage->type == NGF_STAGE_VERTEX) {
      assert(!mtl_pipe_desc.vertexFunction);
      mtl_pipe_desc.vertexFunction =
          _ngf_get_shader_main(stage->func_lib, spec_consts);
    } else if (stage->type == NGF_STAGE_FRAGMENT) {
      assert(!mtl_pipe_desc.fragmentFunction);
      mtl_pipe_desc.fragmentFunction =
          _ngf_get_shader_main(stage->func_lib, spec_consts);
    }
  }
  
  // Configure vertex input.
  const ngf_vertex_input_info &vertex_input_info = *info->input_info;
  MTLVertexDescriptor *vert_desc = mtl_pipe_desc.vertexDescriptor;
  for (uint32_t a = 0u; a < vertex_input_info.nattribs; ++a) {
    MTLVertexAttributeDescriptor *attr_desc = vert_desc.attributes[a];
    const ngf_vertex_attrib_desc &attr_info = vertex_input_info.attribs[a];
    attr_desc.offset = vertex_input_info.attribs[a].offset;
    attr_desc.bufferIndex = vertex_input_info.attribs[a].binding;
    attr_desc.format = get_mtl_attrib_format(attr_info.type, attr_info.size);
  }
  for (uint32_t b = 0u; b < vertex_input_info.nvert_buf_bindings; ++b) {
    MTLVertexBufferLayoutDescriptor *binding_desc = vert_desc.layouts[b];
    const ngf_vertex_buf_binding_desc &binding_info =
        vertex_input_info.vert_buf_bindings[b];
    binding_desc.stride = binding_info.stride;
    binding_desc.stepFunction = get_mtl_step_function(binding_info.input_rate);
  }
  
  // Set primitive topology.
  mtl_pipe_desc.inputPrimitiveTopology =
      get_mtl_primitive_topology_class(info->primitive_type);
  
  _NGF_NURSERY(graphics_pipeline, pipeline);
  NSError *err = nil;
  pipeline->pipeline = [CURRENT_CONTEXT->device
      newRenderPipelineStateWithDescriptor:mtl_pipe_desc
      error:&err];
  pipeline->primitive_type = get_mtl_primitive_type(info->primitive_type);
  if (err) {
    NSLog(@"... [%@]\n", err);
    // TODO: invoke debug callback
    return NGF_ERROR_FAILED_TO_CREATE_PIPELINE;
  } else {
    *result = pipeline.release();
    return NGF_ERROR_OK;
  }
}

void ngf_destroy_graphics_pipeline(ngf_graphics_pipeline *pipe) {
  if (pipe != nullptr) {
    pipe->~ngf_graphics_pipeline();
    NGF_FREE(pipe);
  }
}

id<MTLBuffer> _ngf_create_buffer(const ngf_vertex_data_info *info) {
  // TODO: take usage hint into account.
  id<MTLBuffer> mtl_buffer = [CURRENT_CONTEXT->device newBufferWithLength:info->buffer_size
                           options:MTLResourceOptionCPUCacheModeWriteCombined];
  if (info->buffer_ptr) {
    memcpy((uint8_t*)[mtl_buffer contents],
                      info->buffer_ptr, info->buffer_size);
  } else {
    assert(info->fill_callback);
    info->fill_callback([mtl_buffer contents],
                        info->buffer_size,
                        info->fill_callback_userdata);
  }
  return mtl_buffer;
}

ngf_error ngf_create_attrib_buffer(const ngf_attrib_buffer_info *info,
                                      ngf_attrib_buffer **result) {
  _NGF_NURSERY(attrib_buffer, buf);
  buf->mtl_buffer = _ngf_create_buffer(info);
  *result = buf.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_attrib_buffer(ngf_attrib_buffer *buf) {
  if (buf != nullptr) {
    buf->~ngf_attrib_buffer();
    NGF_FREE(buf);
  }
}

ngf_error ngf_create_index_buffer(const ngf_index_buffer_info *info,
                                   ngf_index_buffer **result) {
  _NGF_NURSERY(index_buffer, buf);
  buf->mtl_buffer = _ngf_create_buffer(info);
  *result = buf.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_index_buffer(ngf_index_buffer *buf) {
  if (buf != nullptr) {
    buf->~ngf_index_buffer();
    NGF_FREE(buf);
  }
}

ngf_error ngf_create_uniform_buffer(const ngf_uniform_buffer_info *info,
                                    ngf_uniform_buffer **result) {
  return NGF_ERROR_OK;
}

void ngf_destroy_uniform_buffer(ngf_uniform_buffer *buf) {}

ngf_error ngf_write_uniform_buffer(ngf_uniform_buffer*, void*, size_t) {
  return NGF_ERROR_OK;
}

ngf_error ngf_create_cmd_buffer(const ngf_cmd_buffer_info*,
                                ngf_cmd_buffer **result) {
  _NGF_NURSERY(cmd_buffer, cmd_buffer);
  *result = cmd_buffer.release();
  return NGF_ERROR_OK;
}

void ngf_destroy_cmd_buffer(ngf_cmd_buffer *cmd_buffer) {
  if (cmd_buffer != nullptr) {
    cmd_buffer->~ngf_cmd_buffer();
    NGF_FREE(cmd_buffer);
  }
}

ngf_error ngf_start_cmd_buffer(ngf_cmd_buffer *cmd_buffer) {
  assert(cmd_buffer);
  cmd_buffer->mtl_cmd_buffer = nil;
  cmd_buffer->mtl_cmd_buffer = [CURRENT_CONTEXT->queue commandBuffer];
  cmd_buffer->active_rce = nil;
  return NGF_ERROR_OK;
}

ngf_error ngf_end_cmd_buffer(ngf_cmd_buffer *cmd_buffer) {
  if (cmd_buffer->active_rce) {
    [cmd_buffer->active_rce endEncoding];
  }
  return NGF_ERROR_OK;
}

ngf_error ngf_submit_cmd_buffer(uint32_t n, ngf_cmd_buffer **cmd_buffers) {
  if (CURRENT_CONTEXT->pending_cmd_buffer) {
    [CURRENT_CONTEXT->pending_cmd_buffer commit];
    CURRENT_CONTEXT->pending_cmd_buffer = nil;
  }
  for (uint32_t b = 0u; b < n; ++b) {
    if (b < n - 1u) {
      [cmd_buffers[b]->mtl_cmd_buffer commit];
    } else {
      CURRENT_CONTEXT->pending_cmd_buffer = cmd_buffers[b]->mtl_cmd_buffer;
    }
    cmd_buffers[b]->mtl_cmd_buffer = nil;
  }
  return NGF_ERROR_OK;
}

void ngf_cmd_begin_pass(ngf_cmd_buffer *cmd_buffer,
                        const ngf_render_target *rt) {
  if (cmd_buffer->active_rce) {
    [cmd_buffer->active_rce endEncoding];
  }
  if (rt->is_default) {
    assert(CURRENT_CONTEXT->next_drawable);
    rt->pass_descriptor.colorAttachments[0].texture =
      CURRENT_CONTEXT->next_drawable.texture;
    // TODO: depth
  }
  cmd_buffer->active_rce =
      [cmd_buffer->mtl_cmd_buffer
       renderCommandEncoderWithDescriptor:rt->pass_descriptor];
}

void ngf_cmd_end_pass(ngf_cmd_buffer *cmd_buffer) {
  [cmd_buffer->active_rce endEncoding];
  cmd_buffer->active_rce = nil;
  cmd_buffer->active_pipe = nullptr;
}

void ngf_cmd_bind_pipeline(ngf_cmd_buffer *buf,
                           const ngf_graphics_pipeline *pipeline) {
  [buf->active_rce setRenderPipelineState:pipeline->pipeline];
  buf->active_pipe = pipeline;
}

void ngf_cmd_viewport(ngf_cmd_buffer *buf, const ngf_irect2d *r) {
  MTLViewport viewport;
  viewport.originX = r->x;
  viewport.originY = r->y;
  viewport.width = r->width;
  viewport.height = r->height;

  // TODO: fix
  viewport.znear = 0.0f;
  viewport.zfar = 1.0f;

  [buf->active_rce setViewport:viewport];
}

void ngf_cmd_scissor(ngf_cmd_buffer *buf, const ngf_irect2d *r) {
  MTLScissorRect scissor;
  scissor.x = (NSUInteger)r->x;
  scissor.y = (NSUInteger)r->y;
  scissor.width = r->width;
  scissor.height = r->height;
  [buf->active_rce setScissorRect:scissor];
}

void ngf_cmd_draw(ngf_cmd_buffer *buf, bool indexed,
                  uint32_t first_element, uint32_t nelements,
                  uint32_t ninstances) {
  MTLPrimitiveType prim_type = buf->active_pipe->primitive_type;
  if (!indexed) {
    [buf->active_rce drawPrimitives:prim_type
                      vertexStart:first_element
                      vertexCount:nelements
                      instanceCount:ninstances
                      baseInstance:0];
  } else {
    MTLIndexType index_type = buf->bound_index_buffer_type;
    size_t index_size =
        index_type == MTLIndexTypeUInt16 ? sizeof(uint16_t) : sizeof(uint32_t);
    [buf->active_rce drawIndexedPrimitives:prim_type
     indexCount:nelements indexType:buf->bound_index_buffer_type
     indexBuffer:buf->bound_index_buffer
     indexBufferOffset:first_element * index_size];
  }
}

void ngf_cmd_bind_attrib_buffer(ngf_cmd_buffer *cmd_buf,
                                const ngf_attrib_buffer *buf,
                                uint32_t binding,
                                uint32_t offset) {
  [cmd_buf->active_rce setVertexBuffer:buf->mtl_buffer
                                offset:offset
                               atIndex:binding];
}

void ngf_cmd_bind_index_buffer(ngf_cmd_buffer *cmd_buf,
                               const ngf_index_buffer *buf,
                               ngf_type type) {
  cmd_buf->bound_index_buffer = buf->mtl_buffer;
  cmd_buf->bound_index_buffer_type = get_mtl_index_type(type);
}

#define PLACEHOLDER_CREATE_DESTROY(name) \
ngf_error ngf_create_##name(const ngf_##name##_info*, \
                              ngf_##name **result) { \
  *result = nullptr; return NGF_ERROR_OK; } \
void ngf_destroy_##name(ngf_##name *) {}

PLACEHOLDER_CREATE_DESTROY(image)
PLACEHOLDER_CREATE_DESTROY(sampler)
PLACEHOLDER_CREATE_DESTROY(descriptor_set_layout)

ngf_error ngf_create_descriptor_set(const ngf_descriptor_set_layout*,
                                    ngf_descriptor_set **result) {
  *result = nullptr;
  return NGF_ERROR_OK;
}

void ngf_destroy_descriptor_set(ngf_descriptor_set*) {}

#define PLACEHOLDER_CMD(name, ...) \
void ngf_cmd_##name(ngf_cmd_buffer*, __VA_ARGS__) {}

PLACEHOLDER_CMD(stencil_reference, uint32_t uint32_t)
PLACEHOLDER_CMD(stencil_compare_mask, uint32_t uint32_t)
PLACEHOLDER_CMD(stencil_write_mask, uint32_t uint32_t)
PLACEHOLDER_CMD(line_width, float)
PLACEHOLDER_CMD(blend_factors, ngf_blend_factor, ngf_blend_factor)
PLACEHOLDER_CMD(bind_descriptor_set, const ngf_descriptor_set*, uint32_t)

ngf_error ngf_apply_descriptor_writes(const ngf_descriptor_write *writes,
                                      const uint32_t nwrites,
                                      ngf_descriptor_set *set) {
  return NGF_ERROR_OK;
}

void ngf_debug_message_callback(void *userdata,
                                void (*callback)(const char*, const void*)) {
}

ngf_error ngf_populate_image(ngf_image *image,
                             uint32_t level,
                             ngf_offset3d offset,
                             ngf_extent3d dimensions,
                             const void *data) {
  return NGF_ERROR_OK;
}

ngf_error ngf_populate_buffer(ngf_buffer *buf,
                              size_t offset,
                              size_t size,
                              const void *data) {
  memcpy((uint8_t*)[buf->mtl_buffer contents] + offset, data, size);
  return NGF_ERROR_OK;
}
