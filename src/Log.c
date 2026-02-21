/*
 * Log.c - Logging subsystem.
 *
 * GMT_Log_ routes a message to the user-supplied GMT_LogCallback, or falls back
 * to the built-in default that prints to stdout (INFO/WARNING) or stderr (ERROR).
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

  fprintf(out, "[GameTest-%s] [%s] %s  (%s:%d in %s)\n", mode, prefix, msg ? msg : "(null)", loc.file ? loc.file : "?", loc.line, loc.function ? loc.function : "?");
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