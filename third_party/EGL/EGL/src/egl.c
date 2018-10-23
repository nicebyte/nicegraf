/**
 * EGL desktop implementation.
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

#include <EGL/egl.h>

//
// Native external implementations.
//

//
// EGL_VERSION_1_0
//

extern EGLBoolean _eglChooseConfig (EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config);

extern EGLContext _eglCreateContext (EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list);

extern EGLSurface _eglCreateWindowSurface (EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint *attrib_list);

extern EGLBoolean _eglDestroyContext (EGLDisplay dpy, EGLContext ctx);

extern EGLBoolean _eglDestroySurface (EGLDisplay dpy, EGLSurface surface);

extern EGLBoolean _eglGetConfigAttrib (EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value);

extern EGLBoolean _eglGetConfigs (EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config);

extern EGLDisplay _eglGetCurrentDisplay (void);

extern EGLSurface _eglGetCurrentSurface (EGLint readdraw);

extern EGLDisplay _eglGetDisplay (EGLNativeDisplayType display_id);

extern EGLint _eglGetError (void);

extern __eglMustCastToProperFunctionPointerType _eglGetProcAddress (const char *procname);

extern EGLBoolean _eglInitialize (EGLDisplay dpy, EGLint *major, EGLint *minor);

extern EGLBoolean _eglMakeCurrent (EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);

extern EGLBoolean _eglQueryContext (EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value);

extern const char *_eglQueryString (EGLDisplay dpy, EGLint name);

extern EGLBoolean _eglQuerySurface (EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value);

extern EGLBoolean _eglSwapBuffers (EGLDisplay dpy, EGLSurface surface);

extern EGLBoolean _eglTerminate (EGLDisplay dpy);

extern EGLBoolean _eglWaitNative (EGLint engine);

//
// EGL_VERSION_1_1
//

extern EGLBoolean _eglSwapInterval (EGLDisplay dpy, EGLint interval);

//
// EGL_VERSION_1_2
//

extern EGLBoolean _eglBindAPI (EGLenum api);

extern EGLenum _eglQueryAPI (void);

extern EGLBoolean _eglWaitClient (void);

//
// EGL_VERSION_1_3
//

//
// EGL_VERSION_1_4
//

EGLContext _eglGetCurrentContext (void);

//
// EGL_VERSION_1_5
//

//
// Wrapper.
//

//
// EGL_VERSION_1_0
//

EGLAPI EGLBoolean EGLAPIENTRY eglChooseConfig (EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
	return _eglChooseConfig (dpy, attrib_list, configs, config_size, num_config);
}

EGLAPI EGLBoolean EGLAPIENTRY eglCopyBuffers (EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target)
{
	// TODO Implement.

	return EGL_FALSE;
}

EGLAPI EGLContext EGLAPIENTRY eglCreateContext (EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list)
{
	return _eglCreateContext (dpy, config, share_context, attrib_list);
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferSurface (EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list)
{
	// TODO Implement.

	return EGL_NO_SURFACE;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePixmapSurface (EGLDisplay dpy, EGLConfig config, EGLNativePixmapType pixmap, const EGLint *attrib_list)
{
	// TODO Implement.

	return EGL_NO_SURFACE;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface (EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint *attrib_list)
{
	return _eglCreateWindowSurface (dpy, config, win, attrib_list);
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyContext (EGLDisplay dpy, EGLContext ctx)
{
	return _eglDestroyContext (dpy, ctx);
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySurface (EGLDisplay dpy, EGLSurface surface)
{
	return _eglDestroySurface (dpy, surface);
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigAttrib (EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value)
{
	return _eglGetConfigAttrib (dpy, config, attribute, value);
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigs (EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
	return _eglGetConfigs (dpy, configs, config_size, num_config);
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetCurrentDisplay (void)
{
	return _eglGetCurrentDisplay();
}

EGLAPI EGLSurface EGLAPIENTRY eglGetCurrentSurface (EGLint readdraw)
{
	return _eglGetCurrentSurface(readdraw);
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetDisplay (EGLNativeDisplayType display_id)
{
	return _eglGetDisplay (display_id);
}

EGLAPI EGLint EGLAPIENTRY eglGetError (void)
{
	return _eglGetError();
}

EGLAPI __eglMustCastToProperFunctionPointerType EGLAPIENTRY eglGetProcAddress (const char *procname)
{
	return _eglGetProcAddress (procname);
}

EGLAPI EGLBoolean EGLAPIENTRY eglInitialize (EGLDisplay dpy, EGLint *major, EGLint *minor)
{
	return _eglInitialize (dpy, major, minor);
}

EGLAPI EGLBoolean EGLAPIENTRY eglMakeCurrent (EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx)
{
	return _eglMakeCurrent (dpy, draw, read, ctx);
}

EGLAPI EGLBoolean EGLAPIENTRY eglQueryContext (EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value)
{
	return _eglQueryContext (dpy, ctx, attribute, value);
}

EGLAPI const char *EGLAPIENTRY eglQueryString (EGLDisplay dpy, EGLint name)
{
	return _eglQueryString(dpy, name);
}

EGLAPI EGLBoolean EGLAPIENTRY eglQuerySurface (EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint *value)
{
	return _eglQuerySurface (dpy, surface, attribute, value);
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffers (EGLDisplay dpy, EGLSurface surface)
{
	return _eglSwapBuffers (dpy, surface);
}

EGLAPI EGLBoolean EGLAPIENTRY eglTerminate (EGLDisplay dpy)
{
	return _eglTerminate (dpy);
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitGL (void)
{
	EGLBoolean result;

    EGLenum api = _eglQueryAPI();

    _eglBindAPI(EGL_OPENGL_ES_API);

    result = _eglWaitClient();

    _eglBindAPI(api);

    return result;
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitNative (EGLint engine)
{
	return _eglWaitNative (engine);
}

//
// EGL_VERSION_1_1
//

EGLAPI EGLBoolean EGLAPIENTRY eglBindTexImage (EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
	// TODO Implement.

	return EGL_FALSE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseTexImage (EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
	// TODO Implement.

	return EGL_FALSE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSurfaceAttrib (EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value)
{
	// TODO Implement.

	return EGL_FALSE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapInterval (EGLDisplay dpy, EGLint interval)
{
	return _eglSwapInterval (dpy, interval);
}

//
// EGL_VERSION_1_2
//

EGLAPI EGLBoolean EGLAPIENTRY eglBindAPI (EGLenum api)
{
	return _eglBindAPI (api);
}

EGLAPI EGLenum EGLAPIENTRY eglQueryAPI (void)
{
	return _eglQueryAPI();
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferFromClientBuffer (EGLDisplay dpy, EGLenum buftype, EGLClientBuffer buffer, EGLConfig config, const EGLint *attrib_list)
{
	// TODO Implement.

	return EGL_NO_SURFACE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseThread (void)
{
	// TODO Implement.

	return EGL_FALSE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitClient (void)
{
	return _eglWaitClient ();
}

//
// EGL_VERSION_1_3
//

//
// EGL_VERSION_1_4
//

EGLAPI EGLContext EGLAPIENTRY eglGetCurrentContext (void)
{
	return _eglGetCurrentContext();
}

//
// EGL_VERSION_1_5
//

EGLAPI EGLSync EGLAPIENTRY eglCreateSync (EGLDisplay dpy, EGLenum type, const EGLAttrib *attrib_list)
{
	// TODO Implement.

	return EGL_NO_SYNC;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySync (EGLDisplay dpy, EGLSync sync)
{
	// TODO Implement.

	return EGL_FALSE;
}

EGLAPI EGLint EGLAPIENTRY eglClientWaitSync (EGLDisplay dpy, EGLSync sync, EGLint flags, EGLTime timeout)
{
	// TODO Implement.

	return EGL_FALSE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetSyncAttrib (EGLDisplay dpy, EGLSync sync, EGLint attribute, EGLAttrib *value)
{
	// TODO Implement.

	return EGL_FALSE;
}

EGLAPI EGLImage EGLAPIENTRY eglCreateImage (EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list)
{
	// TODO Implement.

	return EGL_NO_IMAGE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyImage (EGLDisplay dpy, EGLImage image)
{
	// TODO Implement.

	return EGL_FALSE;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetPlatformDisplay (EGLenum platform, void *native_display, const EGLAttrib *attrib_list)
{
	// TODO Implement.

	return EGL_NO_DISPLAY;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePlatformWindowSurface (EGLDisplay dpy, EGLConfig config, void *native_window, const EGLAttrib *attrib_list)
{
	// TODO Implement.

	return EGL_NO_SURFACE;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePlatformPixmapSurface (EGLDisplay dpy, EGLConfig config, void *native_pixmap, const EGLAttrib *attrib_list)
{
	// TODO Implement.

	return EGL_NO_SURFACE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitSync (EGLDisplay dpy, EGLSync sync, EGLint flags)
{
	// TODO Implement.

	return EGL_FALSE;
}
