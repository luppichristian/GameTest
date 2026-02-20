/*
 * Util.c - Utility functions: hashing, command-line parsing, and report printing.
 */

#include "Internal.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

// ===== Hashing =====

// FNV-1a 32-bit hash.
int GMT_HashString(const char* str) {
  if (!str) return 0;
  uint32_t hash = 2166136261u;
  while (*str) {
    hash ^= (uint8_t)(*str++);
    hash *= 16777619u;
  }
  return (int)hash;
}

int GMT_HashCodeLocation(GMT_CodeLocation loc) {
  // Combine file, line, and function name into a single hash.
  int h = GMT_HashString(loc.file);
  h ^= loc.line * 2654435761;  // Knuth multiplicative hash for the line.
  h ^= GMT_HashString(loc.function);
  return h;
}

// ===== Command-line parsing =====

// Parses --test=<path> from the given args array.
bool GMT_ParseTestFilePath(const char** args, size_t arg_count, char* out_path, size_t out_path_size) {
  if (!args || !out_path || out_path_size == 0) return false;
  static const char prefix[] = "--test=";
  const size_t prefix_len = sizeof(prefix) - 1;
  for (size_t i = 0; i < arg_count; ++i) {
    const char* arg = args[i];
    if (!arg) continue;
    if (strncmp(arg, prefix, prefix_len) == 0) {
      const char* value = arg + prefix_len;
      size_t vlen = strlen(value);
      if (vlen >= out_path_size) return false;  // Buffer too small.
      memcpy(out_path, value, vlen + 1);
      return true;
    }
  }
  return false;
}

// Parses --test-mode=record|replay|disabled from the given args array.
bool GMT_ParseTestMode(const char** args, size_t arg_count, GMT_Mode* out_mode) {
  if (!args || !out_mode) return false;
  static const char prefix[] = "--test-mode=";
  const size_t prefix_len = sizeof(prefix) - 1;
  for (size_t i = 0; i < arg_count; ++i) {
    const char* arg = args[i];
    if (!arg) continue;
    if (strncmp(arg, prefix, prefix_len) == 0) {
      const char* value = arg + prefix_len;
      if (strcmp(value, "record") == 0) {
        *out_mode = GMT_Mode_RECORD;
        return true;
      }
      if (strcmp(value, "replay") == 0) {
        *out_mode = GMT_Mode_REPLAY;
        return true;
      }
      if (strcmp(value, "disabled") == 0) {
        *out_mode = GMT_Mode_DISABLED;
        return true;
      }
    }
  }
  return false;
}

// ===== Report =====

void GMT_PrintReport(void) {
  GMT_Platform_MutexLock(&g_gmt.mutex);

  size_t failures = g_gmt.failed_assertion_count;
  bool failed = g_gmt.test_failed;

  // Snapshot the failure list so we can print it outside the lock.
  GMT_Assertion snapshot[GMT_MAX_FAILED_ASSERTIONS];
  size_t snap_count = failures < GMT_MAX_FAILED_ASSERTIONS ? failures : GMT_MAX_FAILED_ASSERTIONS;
  for (size_t i = 0; i < snap_count; ++i) {
    snapshot[i] = g_gmt.failed_assertions[i];
  }

  GMT_Platform_MutexUnlock(&g_gmt.mutex);

  fprintf(stdout, "\n========== GameTest Report ==========\n");

  const char* mode_str = "DISABLED";
  switch (g_gmt.mode) {
    case GMT_Mode_RECORD:   mode_str = "RECORD"; break;
    case GMT_Mode_REPLAY:   mode_str = "REPLAY"; break;
    case GMT_Mode_DISABLED: mode_str = "DISABLED"; break;
  }

  fprintf(stdout, "  Mode       : %s\n", mode_str);

  if (g_gmt.setup.test_path) {
    fprintf(stdout, "  Test file  : %s\n", g_gmt.setup.test_path);
  }

  fprintf(stdout, "  Frames run : %" PRIu64 "\n", g_gmt.frame_index);
  fprintf(stdout, "  Failures   : %zu\n", failures);

  if (snap_count > 0) {
    fprintf(stdout, "\n  Failed assertions:\n");
    for (size_t i = 0; i < snap_count; ++i) {
      GMT_Assertion* a = &snapshot[i];
      fprintf(stdout, "    [%zu] %s  (%s:%d in %s)\n", i + 1, a->msg ? a->msg : "(no message)", a->loc.file ? a->loc.file : "?", a->loc.line, a->loc.function ? a->loc.function : "?");
    }
  }

  fprintf(stdout, "\n  Result : %s\n", (failed || failures > 0) ? "FAIL" : "PASS");
  fprintf(stdout, "=====================================\n\n");
  fflush(stdout);
}