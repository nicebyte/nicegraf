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

#include "egl_internal.h"

#if defined(EGL_NO_GLEW)
typedef void(*__PFN_glFinish)();

__PFN_glFinish glFinish_PTR = NULL;
PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB = NULL;
PFNWGLGETPIXELFORMATATTRIBIVARBPROC wglGetPixelFormatAttribivARB = NULL;
PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = NULL;
PFNWGLGETEXTENSIONSSTRINGARBPROC wglGetExtensionsStringARB = NULL;
PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = NULL;
#endif


static LRESULT CALLBACK __DummyWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
     return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

EGLBoolean __internalInit(NativeLocalStorageContainer* nativeLocalStorageContainer)
{
	if (!nativeLocalStorageContainer)
	{
		return EGL_FALSE;
	}

	if (nativeLocalStorageContainer->hdc && nativeLocalStorageContainer->ctx)
	{
		return EGL_TRUE;
	}

	if (nativeLocalStorageContainer->hdc)
	{
		return EGL_FALSE;
	}

	if (nativeLocalStorageContainer->ctx)
	{
		return EGL_FALSE;
	}

	//

    WNDCLASS wc;
    memset(&wc, 0, sizeof(WNDCLASS));

    wc.lpfnWndProc   = __DummyWndProc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.lpszClassName = "DummyWindow";

    RegisterClass(&wc);

    nativeLocalStorageContainer->hwnd = CreateWindowEx(
        0,
        "DummyWindow",
        "",
        0,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL,
        NULL,
		wc.hInstance,
        NULL
    );

    //

	if (!nativeLocalStorageContainer->hwnd)
	{
		return EGL_FALSE;
	}

    nativeLocalStorageContainer->hdc = GetDC(nativeLocalStorageContainer->hwnd);

	if (!nativeLocalStorageContainer->hdc)
	{
		DestroyWindow(nativeLocalStorageContainer->hwnd);
		nativeLocalStorageContainer->hwnd = 0;

		return EGL_FALSE;
	}

	PIXELFORMATDESCRIPTOR dummyPfd = {
			sizeof(PIXELFORMATDESCRIPTOR),
			1,
			PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,    	//Flags
			PFD_TYPE_RGBA,            									   	//The kind of framebuffer. RGBA or palette.
			32,                        									   	//Colordepth of the framebuffer.
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 24,                     	//Number of bits for the depthbuffer
			8,                        										//Number of bits for the stencilbuffer
			0,                        										//Number of Aux buffers in the framebuffer.
			PFD_MAIN_PLANE,
			0, 0, 0, 0
	};

	EGLint dummyPixelFormat = ChoosePixelFormat(nativeLocalStorageContainer->hdc, &dummyPfd);

	if (dummyPixelFormat == 0)
	{
		ReleaseDC(0, nativeLocalStorageContainer->hdc);
		nativeLocalStorageContainer->hdc = 0;

		DestroyWindow(nativeLocalStorageContainer->hwnd);
		nativeLocalStorageContainer->hwnd = 0;

		return EGL_FALSE;
	}

	if (!SetPixelFormat(nativeLocalStorageContainer->hdc, dummyPixelFormat, &dummyPfd))
	{
		ReleaseDC(0, nativeLocalStorageContainer->hdc);
		nativeLocalStorageContainer->hdc = 0;

		DestroyWindow(nativeLocalStorageContainer->hwnd);
		nativeLocalStorageContainer->hwnd = 0;

		return EGL_FALSE;
	}

	nativeLocalStorageContainer->ctx = wglCreateContext(nativeLocalStorageContainer->hdc);

	if (!nativeLocalStorageContainer->ctx)
	{
		ReleaseDC(0, nativeLocalStorageContainer->hdc);
		nativeLocalStorageContainer->hdc = 0;

		DestroyWindow(nativeLocalStorageContainer->hwnd);
		nativeLocalStorageContainer->hwnd = 0;

		return EGL_FALSE;
	}

	if (!wglMakeCurrent(nativeLocalStorageContainer->hdc, nativeLocalStorageContainer->ctx))
	{
		wglDeleteContext(nativeLocalStorageContainer->ctx);
		nativeLocalStorageContainer->ctx = 0;

		ReleaseDC(0, nativeLocalStorageContainer->hdc);
		nativeLocalStorageContainer->hdc = 0;

		DestroyWindow(nativeLocalStorageContainer->hwnd);
		nativeLocalStorageContainer->hwnd = 0;

		return EGL_FALSE;
	}

#if !defined(EGL_NO_GLEW)
	glewExperimental = GL_TRUE;
	if (glewInit() != GL_NO_ERROR)
	{
		wglMakeCurrent(0, 0);

		wglDeleteContext(nativeLocalStorageContainer->ctx);
		nativeLocalStorageContainer->ctx = 0;

		ReleaseDC(0, nativeLocalStorageContainer->hdc);
		nativeLocalStorageContainer->hdc = 0;

		DestroyWindow(nativeLocalStorageContainer->hwnd);
		nativeLocalStorageContainer->hwnd = 0;

		return EGL_FALSE;
	}
#else
	wglChoosePixelFormatARB =
      (PFNWGLCHOOSEPIXELFORMATARBPROC)
      __getProcAddress("wglChoosePixelFormatARB");
	wglGetPixelFormatAttribivARB =
      (PFNWGLGETPIXELFORMATATTRIBIVARBPROC)
      __getProcAddress("wglGetPixelFormatAttribivARB");
	wglCreateContextAttribsARB =
      (PFNWGLCREATECONTEXTATTRIBSARBPROC)
      __getProcAddress("wglCreateContextAttribsARB");
	wglSwapIntervalEXT =
      (PFNWGLSWAPINTERVALEXTPROC)__getProcAddress("wglSwapIntervalEXT");
	wglGetExtensionsStringARB =
      (PFNWGLGETEXTENSIONSSTRINGARBPROC)
      __getProcAddress("wglGetExtensionsStringARB");
	glFinish_PTR = (__PFN_glFinish)__getProcAddress("glFinish");
#endif
	return EGL_TRUE;
}

EGLBoolean __internalTerminate(NativeLocalStorageContainer* nativeLocalStorageContainer)
{
	if (!nativeLocalStorageContainer)
	{
		return EGL_FALSE;
	}

	wglMakeCurrent(0, 0);

	if (nativeLocalStorageContainer->ctx)
	{
		wglDeleteContext(nativeLocalStorageContainer->ctx);
		nativeLocalStorageContainer->ctx = 0;
	}

	if (nativeLocalStorageContainer->hdc)
	{
		ReleaseDC(0, nativeLocalStorageContainer->hdc);
		nativeLocalStorageContainer->hdc = 0;
	}

	if (nativeLocalStorageContainer->hwnd)
	{
		DestroyWindow(nativeLocalStorageContainer->hwnd);
		nativeLocalStorageContainer->hwnd = 0;
	}

	UnregisterClass("DummyWindow", NULL);

	return EGL_TRUE;
}

EGLBoolean __deleteContext(const EGLDisplayImpl* walkerDpy, const NativeContextContainer* nativeContextContainer)
{
	if (!walkerDpy || !nativeContextContainer)
	{
		return EGL_FALSE;
	}

	return wglDeleteContext(nativeContextContainer->ctx);
}

EGLBoolean __processAttribList(EGLint* target_attrib_list, const EGLint* attrib_list, EGLint* error)
{
	if (!target_attrib_list || !attrib_list || !error)
	{
		return EGL_FALSE;
	}

	EGLint template_attrib_list[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, 1,
			WGL_CONTEXT_MINOR_VERSION_ARB, 0,
			WGL_CONTEXT_LAYER_PLANE_ARB, 0,
			WGL_CONTEXT_FLAGS_ARB, 0,
			WGL_CONTEXT_PROFILE_MASK_ARB, 0,
			WGL_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB, WGL_NO_RESET_NOTIFICATION_ARB,
			0
	};

	EGLint attribListIndex = 0;

	while (attrib_list[attribListIndex] != EGL_NONE)
	{
		EGLint value = attrib_list[attribListIndex + 1];

		switch (attrib_list[attribListIndex])
		{
			case EGL_CONTEXT_MAJOR_VERSION:
			{
				if (value < 1)
				{
					*error = EGL_BAD_ATTRIBUTE;

					return EGL_FALSE;
				}

				template_attrib_list[1] = value;
			}
			break;
			case EGL_CONTEXT_MINOR_VERSION:
			{
				if (value < 0)
				{
					*error = EGL_BAD_ATTRIBUTE;

					return EGL_FALSE;
				}

				template_attrib_list[3] = value;
			}
			break;
			case EGL_CONTEXT_OPENGL_PROFILE_MASK:
			{
				if (value == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT)
				{
					template_attrib_list[9] = WGL_CONTEXT_CORE_PROFILE_BIT_ARB;
				}
				else if (value == EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT)
				{
					template_attrib_list[9] = WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;
				}
				else
				{
					*error = EGL_BAD_ATTRIBUTE;

					return EGL_FALSE;
				}
			}
			break;
			case EGL_CONTEXT_OPENGL_DEBUG:
			{
				if (value == EGL_TRUE)
				{
					template_attrib_list[7] |= WGL_CONTEXT_DEBUG_BIT_ARB;
				}
				else if (value == EGL_FALSE)
				{
					template_attrib_list[7] &= ~WGL_CONTEXT_DEBUG_BIT_ARB;
				}
				else
				{
					*error = EGL_BAD_ATTRIBUTE;

					return EGL_FALSE;
				}
			}
			break;
			case EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE:
			{
				if (value == EGL_TRUE)
				{
					template_attrib_list[7] |= WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
				}
				else if (value == EGL_FALSE)
				{
					template_attrib_list[7] &= ~WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB;
				}
				else
				{
					*error = EGL_BAD_ATTRIBUTE;

					return EGL_FALSE;
				}
			}
			break;
			case EGL_CONTEXT_OPENGL_ROBUST_ACCESS:
			{
				if (value == EGL_TRUE)
				{
					template_attrib_list[7] |= WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB;
				}
				else if (value == EGL_FALSE)
				{
					template_attrib_list[7] &= ~WGL_CONTEXT_ROBUST_ACCESS_BIT_ARB;
				}
				else
				{
					*error = EGL_BAD_ATTRIBUTE;

					return EGL_FALSE;
				}
			}
			break;
			case EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY:
			{
				if (value == EGL_NO_RESET_NOTIFICATION)
				{
					template_attrib_list[11] = WGL_NO_RESET_NOTIFICATION_ARB;
				}
				else if (value == EGL_LOSE_CONTEXT_ON_RESET)
				{
					template_attrib_list[11] = WGL_LOSE_CONTEXT_ON_RESET_ARB;
				}
				else
				{
					*error = EGL_BAD_ATTRIBUTE;

					return EGL_FALSE;
				}
			}
			break;
			default:
			{
				*error = EGL_BAD_ATTRIBUTE;

				return EGL_FALSE;
			}
			break;
		}

		attribListIndex += 2;

		// More than 14 entries can not exist.
		if (attribListIndex >= 7 * 2)
		{
			*error = EGL_BAD_ATTRIBUTE;

			return EGL_FALSE;
		}
	}

	memcpy(target_attrib_list, template_attrib_list, CONTEXT_ATTRIB_LIST_SIZE * sizeof(EGLint));

	return EGL_TRUE;
}

EGLBoolean __createWindowSurface(EGLSurfaceImpl* newSurface, EGLNativeWindowType win, const EGLint *attrib_list, const EGLDisplayImpl* walkerDpy, const EGLConfigImpl* walkerConfig, EGLint* error)
{
	if (!newSurface || !walkerDpy || !walkerConfig || !error)
	{
		return EGL_FALSE;
	}

	HDC hdc = GetDC(win);

	if (!hdc)
	{
		*error = EGL_BAD_NATIVE_WINDOW;

		return EGL_FALSE;
	}

	// FIXME Check more values.
	EGLint template_attrib_list[] = {
			WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
			WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
			WGL_PIXEL_TYPE_ARB, WGL_TYPE_RGBA_ARB,
			WGL_DOUBLE_BUFFER_ARB, GL_TRUE,
			WGL_COLOR_BITS_ARB, 32,
			WGL_RED_BITS_EXT, 8,
			WGL_GREEN_BITS_EXT, 8,
			WGL_BLUE_BITS_EXT, 8,
			WGL_ALPHA_BITS_EXT, 8,
			WGL_DEPTH_BITS_ARB, 24,
			WGL_STENCIL_BITS_ARB, 8,
			WGL_SAMPLE_BUFFERS_ARB, 0,
			WGL_SAMPLES_ARB, 0,
			0
	};

	if (attrib_list)
	{
		EGLint indexAttribList = 0;

		while (attrib_list[indexAttribList] != EGL_NONE)
		{
			EGLint value = attrib_list[indexAttribList + 1];

			switch (attrib_list[indexAttribList])
			{
				case EGL_GL_COLORSPACE:
				{
					if (value == EGL_GL_COLORSPACE_LINEAR)
					{
						// Do nothing.
					}
					else if (value == EGL_GL_COLORSPACE_SRGB)
					{
						ReleaseDC(win, hdc);

						*error = EGL_BAD_MATCH;

						return EGL_FALSE;
					}
					else
					{
						ReleaseDC(win, hdc);

						*error = EGL_BAD_ATTRIBUTE;

						return EGL_FALSE;
					}
				}
				break;
				case EGL_RENDER_BUFFER:
				{
					if (value == EGL_SINGLE_BUFFER)
					{
						template_attrib_list[7] = GL_FALSE;
					}
					else if (value == EGL_BACK_BUFFER)
					{
						template_attrib_list[7] = GL_TRUE;
					}
					else
					{
						ReleaseDC(win, hdc);

						*error = EGL_BAD_ATTRIBUTE;

						return EGL_FALSE;
					}
				}
				break;
				case EGL_VG_ALPHA_FORMAT:
				{
					ReleaseDC(win, hdc);

					*error = EGL_BAD_MATCH;

					return EGL_FALSE;
				}
				break;
				case EGL_VG_COLORSPACE:
				{
					ReleaseDC(win, hdc);

					*error = EGL_BAD_MATCH;

					return EGL_FALSE;
				}
				break;
			}

			indexAttribList += 2;

			// More than 4 entries can not exist.
			if (indexAttribList >= 4 * 2)
			{
				ReleaseDC(win, hdc);

				*error = EGL_BAD_ATTRIBUTE;

				return EGL_FALSE;
			}
		}
	}

	// Create out of EGL configuration an array of WGL configuration and use it.
	// see https://www.opengl.org/registry/specs/ARB/wgl_pixel_format.txt

	template_attrib_list[9] = walkerConfig->bufferSize;
	template_attrib_list[11] = walkerConfig->redSize;
	template_attrib_list[13] = walkerConfig->blueSize;
	template_attrib_list[15] = walkerConfig->greenSize;
	template_attrib_list[17] = walkerConfig->alphaSize;
	template_attrib_list[19] = walkerConfig->depthSize;
	template_attrib_list[21] = walkerConfig->stencilSize;
	template_attrib_list[23] = walkerConfig->sampleBuffers;
	template_attrib_list[25] = walkerConfig->samples;

	//

	UINT wgl_max_formats = 1;
	INT wgl_formats;
	UINT wgl_num_formats;

	if (!wglChoosePixelFormatARB(hdc, template_attrib_list, 0, wgl_max_formats, &wgl_formats, &wgl_num_formats))
	{
		ReleaseDC(win, hdc);

		*error = EGL_BAD_MATCH;

		return EGL_FALSE;
	}

	if (wgl_num_formats == 0)
	{
		ReleaseDC(win, hdc);

		*error = EGL_BAD_MATCH;

		return EGL_FALSE;
	}

	PIXELFORMATDESCRIPTOR pfd;

	if (!DescribePixelFormat(hdc, wgl_formats, sizeof(PIXELFORMATDESCRIPTOR), &pfd))
	{
		ReleaseDC(win, hdc);

		*error = EGL_BAD_MATCH;

		return EGL_FALSE;
	}

	if (!SetPixelFormat(hdc, wgl_formats, &pfd))
	{
		ReleaseDC(win, hdc);

		*error = EGL_BAD_MATCH;

		return EGL_FALSE;
	}

	newSurface->drawToWindow = EGL_TRUE;
	newSurface->drawToPixmap = EGL_FALSE;
	newSurface->drawToPixmap = EGL_FALSE;
	newSurface->doubleBuffer = (EGLBoolean)template_attrib_list[7];
	newSurface->configId = wgl_formats;

	newSurface->initialized = EGL_TRUE;
	newSurface->destroy = EGL_FALSE;
	newSurface->win = win;
	newSurface->nativeSurfaceContainer.hdc = hdc;

	return EGL_TRUE;
}

EGLBoolean __destroySurface(EGLNativeWindowType win, const NativeSurfaceContainer* nativeSurfaceContainer)
{
	if (!nativeSurfaceContainer)
	{
		return EGL_FALSE;
	}

	ReleaseDC(win, nativeSurfaceContainer->hdc);

	return EGL_TRUE;
}

__eglMustCastToProperFunctionPointerType __getProcAddress(const char *procname)
{
	return (__eglMustCastToProperFunctionPointerType )wglGetProcAddress(procname);
}

EGLBoolean __initialize(EGLDisplayImpl* walkerDpy, const NativeLocalStorageContainer* nativeLocalStorageContainer, EGLint* error)
{
	if (!walkerDpy || !nativeLocalStorageContainer || !error)
	{
		return EGL_FALSE;
	}

	// Create configuration list.

	EGLint numberPixelFormats;

	EGLint attribute = WGL_NUMBER_PIXEL_FORMATS_ARB;
	if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, 1, 0, 1, &attribute, &numberPixelFormats))
	{
		*error = EGL_NOT_INITIALIZED;

		return EGL_FALSE;
	}

  int render_texture_supported = strstr(wglGetExtensionsStringARB(nativeLocalStorageContainer->hdc),
		                                  "WGL_ARB_render_texture") != NULL;

  EGLConfigImpl* lastConfig = 0;
	for (EGLint currentPixelFormat = 1; currentPixelFormat <= numberPixelFormats; currentPixelFormat++)
	{
		EGLint value;

		attribute = WGL_SUPPORT_OPENGL_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &value))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}
		if (!value)
		{
			continue;
		}

		attribute = WGL_PIXEL_TYPE_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &value))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}
		if (value != WGL_TYPE_RGBA_ARB)
		{
			continue;
		}

		//

		EGLConfigImpl* newConfig = (EGLConfigImpl*)malloc(sizeof(EGLConfigImpl));
		if (!newConfig)
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}
		_eglInternalSetDefaultConfig(newConfig);

		// Store in the same order as received.
		newConfig->next = 0;
		if (lastConfig != 0)
		{
			lastConfig->next = newConfig;
		}
		else
		{
			walkerDpy->rootConfig = newConfig;
		}
		lastConfig = newConfig;

		//

		attribute = WGL_DRAW_TO_WINDOW_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->drawToWindow))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = WGL_DRAW_TO_BITMAP_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->drawToPixmap))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = WGL_DOUBLE_BUFFER_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->doubleBuffer))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		//

		newConfig->drawToPBuffer = EGL_FALSE;

		newConfig->conformant = EGL_OPENGL_BIT;
		newConfig->renderableType = EGL_OPENGL_BIT;
		newConfig->surfaceType = 0;
		if (newConfig->drawToWindow)
		{
			newConfig->surfaceType |= EGL_WINDOW_BIT;
		}
		if (newConfig->drawToPixmap)
		{
			newConfig->surfaceType |= EGL_PIXMAP_BIT;
		}
		if (newConfig->drawToPBuffer)
		{
			newConfig->surfaceType |= EGL_PBUFFER_BIT;
		}
		newConfig->colorBufferType = EGL_RGB_BUFFER;
		newConfig->configId = currentPixelFormat;

		attribute = WGL_COLOR_BITS_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->bufferSize))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = WGL_RED_BITS_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->redSize))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = WGL_GREEN_BITS_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->greenSize))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = WGL_BLUE_BITS_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->blueSize))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = WGL_ALPHA_BITS_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->alphaSize))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = WGL_DEPTH_BITS_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->depthSize))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = WGL_STENCIL_BITS_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->stencilSize))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}


		//

		attribute = WGL_SAMPLE_BUFFERS_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->sampleBuffers))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = WGL_SAMPLES_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->samples))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		//

		attribute = WGL_BIND_TO_TEXTURE_RGB_ARB;
		if (render_texture_supported &&
        !wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->bindToTextureRGB))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}
		newConfig->bindToTextureRGB = newConfig->bindToTextureRGB ? EGL_TRUE : EGL_FALSE;

		attribute = WGL_BIND_TO_TEXTURE_RGBA_ARB;
		if (render_texture_supported &&
        !wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->bindToTextureRGBA))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}
		newConfig->bindToTextureRGBA = newConfig->bindToTextureRGBA ? EGL_TRUE : EGL_FALSE;

		//

		attribute = WGL_MAX_PBUFFER_PIXELS_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->maxPBufferPixels))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = WGL_MAX_PBUFFER_WIDTH_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->maxPBufferWidth))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = WGL_MAX_PBUFFER_HEIGHT_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->maxPBufferHeight))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		//

		attribute = WGL_TRANSPARENT_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->transparentType))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}
		newConfig->transparentType = newConfig->transparentType ? EGL_TRANSPARENT_RGB : EGL_NONE;

		attribute = WGL_TRANSPARENT_RED_VALUE_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->transparentRedValue))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = WGL_TRANSPARENT_GREEN_VALUE_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->transparentGreenValue))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		attribute = WGL_TRANSPARENT_BLUE_VALUE_ARB;
		if (!wglGetPixelFormatAttribivARB(nativeLocalStorageContainer->hdc, currentPixelFormat, 0, 1, &attribute, &newConfig->transparentBlueValue))
		{
			*error = EGL_NOT_INITIALIZED;

			return EGL_FALSE;
		}

		// FIXME: Query and save more values.
	}

	return EGL_TRUE;
}

EGLBoolean __createContext(NativeContextContainer* nativeContextContainer, const EGLDisplayImpl* walkerDpy, const NativeSurfaceContainer* nativeSurfaceContainer, const NativeContextContainer* sharedNativeSurfaceContainer, const EGLint* attribList)
{
	if (!walkerDpy || !nativeContextContainer || !nativeSurfaceContainer)
	{
		return EGL_FALSE;
	}

	nativeContextContainer->ctx = wglCreateContextAttribsARB(nativeSurfaceContainer->hdc, sharedNativeSurfaceContainer ? sharedNativeSurfaceContainer->ctx : 0, attribList);

	return nativeContextContainer->ctx != 0;
}

EGLBoolean __makeCurrent(const EGLDisplayImpl* walkerDpy, const NativeSurfaceContainer* nativeSurfaceContainer, const NativeContextContainer* nativeContextContainer)
{
	if (!walkerDpy || !nativeSurfaceContainer || !nativeContextContainer)
	{
		return EGL_FALSE;
	}

	return (EGLBoolean)wglMakeCurrent(nativeSurfaceContainer->hdc, nativeContextContainer->ctx);
}

EGLBoolean __swapBuffers(const EGLDisplayImpl* walkerDpy, const EGLSurfaceImpl* walkerSurface)
{
	if (!walkerDpy || !walkerSurface)
	{
		return EGL_FALSE;
	}

	return (EGLBoolean)SwapBuffers(walkerSurface->nativeSurfaceContainer.hdc);
}

EGLBoolean __swapInterval(const EGLDisplayImpl* walkerDpy, EGLint interval)
{
	if (!walkerDpy)
	{
		return EGL_FALSE;
	}

	return (EGLBoolean)wglSwapIntervalEXT(interval);
}
