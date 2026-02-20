/*
 * Log.c - Logging subsystem.
 *
 * GMT_Log_ routes a message to the user-supplied GMT_LogCallback, or falls back
 * to the built-in default that prints to stdout (INFO/WARNING) or stderr (ERROR).
 */

#include "Internal.h"
#include <stdio.h>

static void GMT_DefaultLogCallback(GMT_Severity severity, const char* msg, GMT_CodeLocation loc) {
  FILE* out = (severity == GMT_Severity_ERROR) ? stderr : stdout;
  const char* prefix =
      (severity == GMT_Severity_ERROR) ? "ERROR" : (severity == GMT_Severity_WARNING) ? "WARNING"
                                                                                      : "INFO";
  fprintf(out, "[GameTest] [%s] %s  (%s:%d in %s)\n", prefix, msg ? msg : "(null)", loc.file ? loc.file : "?", loc.line, loc.function ? loc.function : "?");
  fflush(out);
}

void GMT_Log_(GMT_Severity severity, const char* msg, GMT_CodeLocation loc) {
  GMT_LogCallback cb = g_gmt.setup.log_callback;
  if (cb) {
    cb(severity, msg, loc);
  } else {
    GMT_DefaultLogCallback(severity, msg, loc);
  }
}
