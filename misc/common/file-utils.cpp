/**
 * Copyright (c) 2023 nicegraf contributors
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

#if defined(__APPLE__)
#include "TargetConditionals.h"
#endif

#if defined(TARGET_OS_IPHONE)
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "file-utils.h"

#include <fstream>
#include <stdexcept>
#include <string>

namespace ngf_misc {

std::vector<char> load_file(const char* file_name) {
  const char* path_cstr = nullptr;
#if defined(TARGET_OS_IPHONE)
  CFBundleRef mainBundle = CFBundleGetMainBundle();
  if (!mainBundle) { throw std::runtime_error {"Failed to get the main bundle."}; }

  CFStringRef fileNameCStr = CFStringCreateWithCString(nullptr, file_name, kCFStringEncodingUTF8);

  CFURLRef fileURL = CFBundleCopyResourceURL(mainBundle, fileNameCStr, nullptr, nullptr);
  if (!fileURL) { throw std::runtime_error {"Failed to locate the file in the main bundle."}; }

  // Convert the URL to a filesystem path
  char filePath[256];
  if (!CFURLGetFileSystemRepresentation(fileURL, true, (UInt8*)filePath, sizeof(filePath))) {
    throw std::runtime_error {"Failed to convert URL to filesystem path."};
  }

  CFRelease(fileURL);
  CFRelease(fileNameCStr);

  path_cstr = filePath;
#else
  path_cstr = file_name;
#endif

  std::basic_ifstream<char> fs(path_cstr, std::ios::binary | std::ios::in);
  if (!fs.is_open()) { throw std::runtime_error {file_name}; }
  return std::vector<char> {std::istreambuf_iterator<char>(fs), std::istreambuf_iterator<char>()};
}

}  // namespace ngf_misc
