EGL 1.5 desktop implementation:
-------------------------------

Since EGL version 1.5 (https://www.khronos.org/registry/egl/), it is possible to create an OpenGL context with EGL.
This library implements EGL 1.5 for Windows and X11 by wrapping WGL and GLX. Only OpenGL is supported.
The purpose of this library and wrapping the existing APIs is to have the same source code on embedded and desktop systems
when developing OpenGL applications.

The project is in early stage and not well tested, but an initialization as seen on
https://www.khronos.org/registry/egl/sdk/docs/man/html/eglIntro.xhtml under Windows and X11 does already work.

How to build EGL:

1. Install Eclipse IDE for C/C++ Developers and a GNU Compiler Collection for your operating system.
2. Import EGL as an existing project.
3. Set the build configuration in Eclipse to your operating system.
4. Build EGL.

If you get build errors:

- Please make sure, that you install all the needed header and libraries.

SDKs and Libraries:

- GLEW 1.12.0 http://glew.sourceforge.net/

Build configuration naming:

[CPU]_[GPU]_[Window System]_[OpenGL]_[Compiler]_[Configuration]

CPU:								x64, x86
GPU/Emulator (Optional):
Window System: 						Windows, X11
OpenGL (Optional):
Compiler:							GCC, MinGW
Configuration:						Debug, Release

e.g. x86__Windows__MinGW_Debug


Yours Norbert Nopper


TODOs:

- Check, if needed GL/WGL version is available. Otherwise this EGL lib will crash or just not work!

- Implement TODOs as marked in the source code.

- Implement FIXMEs as marked in the source code.

- Check / implement correct error codes.

- Cleanup source code.

Changelog:

29.01.2015 - Updated to GLEW 1.12.0. Current version: v0.3.3.

25.01.2015 - Fixed bug during initialization under Windows. Current version: v0.3.3.

20.01.2015 - Added GLX version check. Fixed bug in window creation under X11. Current version: v0.3.2.

05.12.2014 - Removed duplicate code. Current version: v0.3.1.

04.12.2014 - Working X11 version. Current version: v0.3.0.

28.11.2014 - Continued on X11 version. Current version: v0.2.3.

22.11.2014 - X11 compiling but not complete. Current version: v0.2.2.

18.11.2014 - Added X11 build configuration and started implementing it. X11 not compiling yet. Current version: v0.2.1.

17.11.2014 - Released first public version: v0.2.
