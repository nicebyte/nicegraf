nicegraf
========

![Run tests](https://github.com/nicebyte/nicegraf/workflows/Run%20tests/badge.svg)


An abstraction layer for low-level platform-specific GPU APIs.

* Reference documentation: http://wiki.gpfault.net/docs/nicegraf
* Sample code: https://github.com/nicebyte/nicegraf/tree/master/samples

# prerequisites

- CMake
- C99 and C++14-capable compiler

*Optional*:
- [Vulkan SDK](https://www.lunarg.com/vulkan-sdk/) (for debug purposes)

# running the samples

## Windows

- Make sure CMake is within system path
- Run <b>build-samples.bat</b>
- Open the **.sln** file from **samples-build-files** and build the solution
- Open **samples/binaries** and run the samples

# credits

## current maintainers

* nicebyte · [@nice_byte](http://twitter.com/nice_byte)
* Bagrat 'dBuger' Dabaghyan · [@dBagrat](http://twitter.com/dBagrat)
* Andranik 'HedgeTheHog' Melikyan · [@andranik3949](http://twitter.com/andranik3949)

## dependencies

* The Vulkan backend uses SPIRV-Reflect, maintained by the Khronos Group, and the Vulkan Memory Allocator, maintained by AMD.
* The sample code uses GLFW, maintained by Camilla Berglund, and ImGui, maintained by Omar Cornut.

