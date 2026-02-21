/*
 * Assert.c - Assertion subsystem.
 *
 * GMT_Assert_ accumulates failed assertions up to GMT_MAX_FAILED_ASSERTIONS,
 * fires the user-supplied assertion trigger callback if set, and fails the test
 * (via GMT_Fail) when the configured trigger count is reached.
 */

#include "Internal.h"
#include <string.h>

void GMT_Assert_(bool condition, const char* msg, GMT_CodeLocation loc) {
  if (condition) return;
  if (!g_gmt.initialized || g_gmt.mode == GMT_Mode_DISABLED) return;

  GMT_Platform_MutexLock();

  g_gmt.assertion_fire_count++;

  // Store the failed assertion (bounded by GMT_MAX_FAILED_ASSERTIONS).
  if (g_gmt.failed_assertion_count < GMT_MAX_FAILED_ASSERTIONS) {
    GMT_Assertion* a = &g_gmt.failed_assertions[g_gmt.failed_assertion_count++];
    a->condition_str = NULL;  // condition string is encoded in msg by the macros
    a->msg = msg;
    a->loc = loc;
  }

  // Snapshot what we need while holding the lock, then release before calling
  // any user-provided callbacks to avoid re-entrant deadlocks.
  GMT_AssertionTriggerCallback trigger_cb = NULL;
  if (g_gmt.setup.assertion_trigger_callback) {
    trigger_cb = *g_gmt.setup.assertion_trigger_callback;
  }
  int trigger_count = g_gmt.setup.fail_assertion_trigger_count;
  int fire_count = g_gmt.assertion_fire_count;

  GMT_Platform_MutexUnlock();

  // Log and notify outside the lock.
  GMT_Log_(GMT_Severity_ERROR, loc, msg);

  if (trigger_cb) {
    GMT_Assertion a = {NULL, msg, loc};
    trigger_cb(a);
  }

  if (trigger_count <= 1) trigger_count = 1;
  if (fire_count >= trigger_count) {
    GMT_LogError("Assertion failure count %d has reached the trigger threshold of %d; failing test.", fire_count, trigger_count);
    GMT_Fail_();
  }
}

bool GMT_GetFailedAssertions_(GMT_Assertion* out_assertions, size_t max_assertions, size_t* out_count) {
  if (!out_count) return false;
  if (g_gmt.mode == GMT_Mode_DISABLED) {
    *out_count = 0;
    return true;
  }

  GMT_Platform_MutexLock();
  size_t n = g_gmt.failed_assertion_count;
  if (out_assertions) {
    if (n > max_assertions) n = max_assertions;
    for (size_t i = 0; i < n; ++i) {
      out_assertions[i] = g_gmt.failed_assertions[i];
    }
  }
  *out_count = g_gmt.failed_assertion_count;
  GMT_Platform_MutexUnlock();
  return true;
}

void GMT_ClearFailedAssertions_(void) {
  if (g_gmt.mode == GMT_Mode_DISABLED) return;
  GMT_Platform_MutexLock();
  g_gmt.failed_assertion_count = 0;
  g_gmt.assertion_fire_count = 0;
  GMT_Platform_MutexUnlock();
}
