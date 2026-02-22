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
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

static void GMT_DefaultLogCallback(GMT_Severity severity, const char* msg, GMT_CodeLocation loc) {
  FILE* out = (severity == GMT_Severity_ERROR) ? stderr : stdout;
  const char* prefix = "";
  switch (severity) {
    case GMT_Severity_ERROR:   prefix = "ERROR"; break;
    case GMT_Severity_WARNING: prefix = "WARNING"; break;
    case GMT_Severity_INFO:    prefix = "INFO"; break;
  }

  const char* mode = "DISABLED";
  switch (g_gmt.mode) {
    case GMT_Mode_RECORD: mode = "RECORD"; break;
    case GMT_Mode_REPLAY: mode = "REPLAY"; break;
    case GMT_Mode_DISABLED:
    default:
      break;
  }

#ifdef GMT_VERBOSE
  fprintf(out, "[GameTest-%s] [%s] %s  (%s:%d in %s)\n", mode, prefix, msg ? msg : "(null)", loc.file ? loc.file : "?", loc.line, loc.function ? loc.function : "?");
#else
  fprintf(out, "[GameTest-%s] [%s] %s\n", mode, prefix, msg ? msg : "(null)");
#endif

  fflush(out);
}

void GMT_Log_(GMT_Severity severity, GMT_CodeLocation loc, const char* fmt, ...) {
  const char* safe_fmt = fmt ? fmt : "(null)";
  va_list args;

  va_start(args, fmt);
  int needed = vsnprintf(NULL, 0, safe_fmt, args);
  va_end(args);

  char* buf = GMT_Alloc(needed + 1);
  if (buf) {
    va_start(args, fmt);
    vsnprintf(buf, (size_t)needed + 1, safe_fmt, args);
    va_end(args);
  }

  GMT_LogCallback cb = g_gmt.setup.log_callback;
  if (cb) {
    cb(severity, buf ? buf : safe_fmt, loc);
  } else {
    GMT_DefaultLogCallback(severity, buf ? buf : safe_fmt, loc);
  }

  GMT_Free(buf);
}