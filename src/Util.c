/*
 * Util.c - Utility functions: hashing, command-line parsing, and report printing.
 */

#include "Internal.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

// ===== Hashing =====

// FNV-1a 32-bit hash.
int GMT_HashString_(const char* str) {
  if (!str) return 0;
  uint32_t hash = 2166136261u;
  while (*str) {
    hash ^= (uint8_t)(*str++);
    hash *= 16777619u;
  }
  return (int)hash;
}

int GMT_HashCodeLocation_(GMT_CodeLocation loc) {
  // Combine file, line, and function name into a single hash.
  int h = GMT_HashString_(loc.file);
  h ^= loc.line * 2654435761;  // Knuth multiplicative hash for the line.
  h ^= GMT_HashString_(loc.function);
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

// Parses --headless from the given args array.
bool GMT_ParseHeadlessMode(const char** args, size_t arg_count, bool* out_headless) {
  if (!args || !out_headless) return false;
  for (size_t i = 0; i < arg_count; ++i) {
    const char* arg = args[i];
    if (!arg) continue;
    if (strcmp(arg, "--headless") == 0) {
      *out_headless = true;
      return true;
    }
  }
  return false;
}

// Parses --work-dir=<path> from the given args array.
bool GMT_ParseWorkingDirectory(const char** args, size_t arg_count, char* out_work_dir, size_t out_work_dir_size) {
  if (!args || !out_work_dir || out_work_dir_size == 0) return false;
  static const char prefix[] = "--work-dir=";
  const size_t prefix_len = sizeof(prefix) - 1;
  for (size_t i = 0; i < arg_count; ++i) {
    const char* arg = args[i];
    if (!arg) continue;
    if (strncmp(arg, prefix, prefix_len) == 0) {
      const char* value = arg + prefix_len;
      size_t vlen = strlen(value);
      if (vlen >= out_work_dir_size) return false;  // Buffer too small.
      memcpy(out_work_dir, value, vlen + 1);
      return true;
    }
  }
  return false;
}

// ===== Report =====

void GMT_PrintReport_(void) {
  if (g_gmt.mode == GMT_Mode_DISABLED) return;

  size_t failures = 0;
  if (!GMT_GetFailedAssertions_(NULL, 0, &failures)) {
    GMT_LogError("Failed to get failed assertions for report");
    return;
  }

  GMT_Assertion* assertions = GMT_Alloc(sizeof(GMT_Assertion) * failures);
  if (!assertions) {
    GMT_LogError("Failed to allocate memory for failed assertions report");
    return;
  }

  if (!GMT_GetFailedAssertions_(assertions, failures, &failures)) {
    GMT_LogError("Failed to get failed assertions for report");
    GMT_Free(assertions);
    return;
  }

  GMT_LogInfo("Report:");
  GMT_LogInfo("  Frames run     : %" PRIu64, g_gmt.frame_index);
  GMT_LogInfo("  Total asserts  : %zu", g_gmt.total_assertion_count);
  GMT_LogInfo("  Unique asserts : %zu", g_gmt.unique_assertion_count);
  GMT_LogInfo("  Failed asserts : %zu", failures);

  if (failures > 0) {
    GMT_LogInfo("  Failed assertions:");
    for (size_t i = 0; i < failures; ++i) {
      GMT_Assertion* a = &assertions[i];
      GMT_LogInfo("    [%zu] %s  (%s:%d in %s)", i + 1, a->msg ? a->msg : "(no message)", a->loc.file ? a->loc.file : "?", a->loc.line, a->loc.function ? a->loc.function : "?");
    }
  }

  GMT_Free(assertions);
}