/*
MIT License

Copyright (c) 2026 Christian Luppi

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

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

#include "Internal.h"
#include <stdlib.h>

void* GMT_Alloc_(size_t size, GMT_CodeLocation loc) {
  GMT_AllocCallback cb = g_gmt.setup.alloc_callback;
  if (cb) return cb(size, loc);
  (void)loc;
  return malloc(size);
}

void GMT_Free_(void* ptr, GMT_CodeLocation loc) {
  GMT_FreeCallback cb = g_gmt.setup.free_callback;
  if (cb) {
    cb(ptr, loc);
    return;
  }
  (void)loc;
  free(ptr);
}

void* GMT_Realloc_(void* ptr, size_t new_size, GMT_CodeLocation loc) {
  GMT_ReallocCallback cb = g_gmt.setup.realloc_callback;
  if (cb) return cb(ptr, new_size, loc);
  (void)loc;
  return realloc(ptr, new_size);
}
