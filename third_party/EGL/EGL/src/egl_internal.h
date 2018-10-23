/**
 * EGL windows desktop implementation.
 *
 * The MIT License (MIT)
 *
 * Copyright (c) since 2014 Norbert Nopper
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef EGL_INTERNAL_H_
#define EGL_INTERNAL_H_

#define _EGL_VENDOR "Norbert Nopper"

#define _EGL_VERSION "1.5 Version 0.3.3"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(__VC32__) && !defined(__CYGWIN__) && !defined(__SCITECH_SNAP__) /* Win32 and WinCE */

#include <windows.h>

#if !defined(EGL_NO_GLEW)
#include <GL/glew.h>
#include <GL/wglew.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include "wglext.h"
#endif  // EGL_NO_GLEW

#define CONTEXT_ATTRIB_LIST_SIZE 13

typedef struct _NativeSurfaceContainer {

	HDC hdc;

} NativeSurfaceContainer;

typedef struct _NativeContextContainer {

	HGLRC ctx;

} NativeContextContainer;

typedef struct _NativeLocalStorageContainer {

	HWND hwnd;

	HDC hdc;

	HGLRC ctx;

	void* placeholder;

} NativeLocalStorageContainer;


#elif defined(__unix__)

#include <X11/X.h>

#if !defined(EGL_NO_GLEW)
#include <GL/glew.h>
#include <GL/glxew.h>
#else
#include <GL/glx.h>
#endif  // EGL_NO_GLEW
#define CONTEXT_ATTRIB_LIST_SIZE 10

typedef struct _NativeSurfaceContainer {

	GLXDrawable drawable;

	GLXFBConfig config;

} NativeSurfaceContainer;

typedef struct _NativeContextContainer {

	GLXContext ctx;

} NativeContextContainer;

typedef struct _NativeLocalStorageContainer {

	Display* display;

	Window window;

	GLXContext ctx;

} NativeLocalStorageContainer;

#else
#error "Platform not recognized"
#endif

#include <EGL/egl.h>

//

typedef struct _EGLConfigImpl
{

	// Returns the number of bits in the alpha mask buffer.
	EGLint alphaMaskSize;

	// Returns the number of bits of alpha stored in the color buffer.
	EGLint alphaSize;

	// Returns EGL_TRUE if color buffers can be bound to an RGB texture, EGL_FALSE otherwise.
	EGLint bindToTextureRGB;

	// Returns EGL_TRUE if color buffers can be bound to an RGBA texture, EGL_FALSE otherwise.
	EGLint bindToTextureRGBA;

	// Returns the number of bits of blue stored in the color buffer.
	EGLint blueSize;

	// Returns the depth of the color buffer. It is the sum of EGL_RED_SIZE, EGL_GREEN_SIZE, EGL_BLUE_SIZE, and EGL_ALPHA_SIZE.
	EGLint bufferSize;

	// Returns the color buffer type. Possible types are EGL_RGB_BUFFER and EGL_LUMINANCE_BUFFER.
	EGLint colorBufferType;

	// Returns the caveats for the frame buffer configuration. Possible caveat values are EGL_NONE, EGL_SLOW_CONFIG, and EGL_NON_CONFORMANT.
	EGLint configCaveat;

	// Returns the ID of the frame buffer configuration.
	EGLint configId;

	// Returns a bitmask indicating which client API contexts created with respect to this config are conformant.
	EGLint conformant;

	// Returns the number of bits in the depth buffer.
	EGLint depthSize;

	// Returns the number of bits of green stored in the color buffer.
	EGLint greenSize;

	// Returns the frame buffer level. Level zero is the default frame buffer. Positive levels correspond to frame buffers that overlay the default buffer and negative levels correspond to frame buffers that underlay the default buffer.
	EGLint level;

	// Returns the number of bits of luminance stored in the luminance buffer.
	EGLint luminanceSize;

	// Input only: Must be followed by the handle of a valid native pixmap, cast to EGLint, or EGL_NONE.
	EGLint matchNativePixmap;

	// Returns the maximum height of a pixel buffer surface in pixels.
	EGLint maxPBufferHeight;

	// Returns the maximum size of a pixel buffer surface in pixels.
	EGLint maxPBufferPixels;

	// Returns the maximum width of a pixel buffer surface in pixels.
	EGLint maxPBufferWidth;

	// Returns the maximum value that can be passed to eglSwapInterval.
	EGLint maxSwapInterval;

	// Returns the minimum value that can be passed to eglSwapInterval.
	EGLint minSwapInterval;

	// Returns EGL_TRUE if native rendering APIs can render into the surface, EGL_FALSE otherwise.
	EGLint nativeRenderable;

	// Returns the ID of the associated native visual.
	EGLint nativeVisualId;

	// Returns the type of the associated native visual.
	EGLint nativeVisualType;

	// Returns the number of bits of red stored in the color buffer.
	EGLint redSize;

	// Returns a bitmask indicating the types of supported client API contexts.
	EGLint renderableType;

	// Returns the number of multisample buffers.
	EGLint sampleBuffers;

	// Returns the number of samples per pixel.
	EGLint samples;

	// Returns the number of bits in the stencil buffer.
	EGLint stencilSize;

	// Returns a bitmask indicating the types of supported EGL surfaces.
	EGLint surfaceType;

	// Returns the transparent blue value.
	EGLint transparentBlueValue;

	// Returns the transparent green value.
	EGLint transparentGreenValue;

	// Returns the transparent red value.
	EGLint transparentRedValue;

	// Returns the type of supported transparency. Possible transparency values are: EGL_NONE, and EGL_TRANSPARENT_RGB.
	EGLint transparentType;

	// Own data.

	EGLint drawToWindow;
	EGLint drawToPixmap;
	EGLint drawToPBuffer;
	EGLint doubleBuffer;

	struct _EGLConfigImpl* next;

} EGLConfigImpl;

typedef struct _EGLSurfaceImpl
{

	EGLBoolean initialized;
	EGLBoolean destroy;

	EGLBoolean drawToWindow;
	EGLBoolean drawToPixmap;
	EGLBoolean drawToPBuffer;
	EGLBoolean doubleBuffer;
	EGLint configId;

	EGLNativeWindowType win;

	NativeSurfaceContainer nativeSurfaceContainer;

	struct _EGLSurfaceImpl* next;

} EGLSurfaceImpl;

typedef struct _EGLContextListImpl
{

	EGLSurfaceImpl* surface;

	NativeContextContainer nativeContextContainer;

	struct _EGLContextListImpl* next;

} EGLContextListImpl;

typedef struct _EGLContextImpl
{

	EGLBoolean initialized;
	EGLBoolean destroy;

	EGLint configId;

	struct _EGLContextImpl* sharedCtx;

	EGLContextListImpl* rootCtxList;

	EGLint attribList[CONTEXT_ATTRIB_LIST_SIZE];

	struct _EGLContextImpl* next;

} EGLContextImpl;

typedef struct _EGLDisplayImpl
{

	EGLBoolean initialized;
	EGLBoolean destroy;

	EGLNativeDisplayType display_id;

	EGLSurfaceImpl* rootSurface;
	EGLContextImpl* rootCtx;
	EGLConfigImpl* rootConfig;

	EGLSurfaceImpl* currentDraw;
	EGLSurfaceImpl* currentRead;
	EGLContextImpl* currentCtx;

	struct _EGLDisplayImpl* next;

} EGLDisplayImpl;

typedef struct _LocalStorage
{

	NativeLocalStorageContainer dummy;

	EGLint error;

	EGLenum api;

	EGLDisplayImpl* rootDpy;

	EGLContextImpl* currentCtx;

} LocalStorage;

//

void _eglInternalSetDefaultConfig(EGLConfigImpl* config);

//

EGLBoolean __internalInit(NativeLocalStorageContainer* nativeLocalStorageContainer);

EGLBoolean __internalTerminate(NativeLocalStorageContainer* nativeLocalStorageContainer);

EGLBoolean __deleteContext(const EGLDisplayImpl* walkerDpy, const NativeContextContainer* nativeContextContainer);

EGLBoolean __processAttribList(EGLint* target_attrib_list, const EGLint* attrib_list, EGLint* error);

EGLBoolean __createWindowSurface(EGLSurfaceImpl* newSurface, EGLNativeWindowType win, const EGLint *attrib_list, const EGLDisplayImpl* walkerDpy, const EGLConfigImpl* walkerConfig, EGLint* error);

EGLBoolean __destroySurface(EGLNativeWindowType win, const NativeSurfaceContainer* nativeSurfaceContainer);

__eglMustCastToProperFunctionPointerType __getProcAddress(const char *procname);

EGLBoolean __initialize(EGLDisplayImpl* walkerDpy, const NativeLocalStorageContainer* nativeLocalStorageContainer, EGLint* error);

EGLBoolean __createContext(NativeContextContainer* nativeContextContainer, const EGLDisplayImpl* walkerDpy, const NativeSurfaceContainer* nativeSurfaceContainer, const NativeContextContainer* sharedNativeContextContainer, const EGLint* attribList);

EGLBoolean __makeCurrent(const EGLDisplayImpl* walkerDpy, const NativeSurfaceContainer* nativeSurfaceContainer, const NativeContextContainer* nativeContextContainer);

EGLBoolean __swapBuffers(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* walkerSurface);

EGLBoolean __swapInterval(const EGLDisplayImpl* walkerDpy, EGLint interval);

#endif /* EGL_INTERNAL_H_ */
