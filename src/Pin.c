/*
 * Pin.c - GMT_Pin* implementation.
 *
 * Pins a variable to its recorded value so that it is consistent across record
 * and replay runs.  Each typed public function delegates to a single shared
 * helper that handles the mode dispatch and sequential key accounting.
 *
 * Record mode : reads *data and writes it to the test file tagged as TAG_PIN.
 * Replay mode : locates the decoded entry by (key, sequential-index) and
 *               overwrites *data with the stored bytes.
 * Disabled    : no-op.
 *
 * Thread safety: guarded by the framework mutex (Win32 CRITICAL_SECTION,
 * which is reentrant on the owning thread).
 */

#include "Internal.h"
#include "Record.h"
#include <string.h>

// ===== Shared helper =====

static void GMT_Pin_(unsigned int key, void* data, size_t size, GMT_CodeLocation loc) {
  (void)loc;  // Available for future diagnostic use.

  if (!g_gmt.initialized || g_gmt.mode == GMT_Mode_DISABLED) return;
  if (!data || size == 0) return;
  if (size > (size_t)GMT_MAX_DATA_RECORD_PAYLOAD) {
    GMT_LogError("GMT_Pin: payload size %zu exceeds maximum %d; call ignored.", size, GMT_MAX_DATA_RECORD_PAYLOAD);
    return;
  }

  GMT_Platform_MutexLock();

  unsigned int index = GMT_KeyCounter_Next(&g_gmt.pin_counter, key);

  switch (g_gmt.mode) {
    case GMT_Mode_RECORD:
      GMT_Record_WriteDataRecord(GMT_RECORD_TAG_PIN, key, index, data, size);
      break;

    case GMT_Mode_REPLAY: {
      GMT_DecodedDataRecord* rec = GMT_Record_FindDecoded(g_gmt.replay_pins, g_gmt.replay_pin_count, key, index);
      if (!rec) {
        GMT_LogError("GMT_Pin: no recorded value for key %u index %u; *value unchanged.", key, index);
      } else if (rec->size != (uint32_t)size) {
        GMT_LogError("GMT_Pin: size mismatch for key %u index %u: recorded %u bytes, got %zu bytes; *value unchanged.",
                     key,
                     index,
                     rec->size,
                     size);
      } else {
        memcpy(data, rec->data, size);
      }
      break;
    }

    default:
      break;
  }

  GMT_Platform_MutexUnlock();
}

// ===== Typed public functions =====

void GMT_PinInt_(unsigned int key, int* value, GMT_CodeLocation loc) {
  GMT_Pin_(key, value, sizeof(*value), loc);
}

void GMT_PinUInt_(unsigned int key, unsigned int* value, GMT_CodeLocation loc) {
  GMT_Pin_(key, value, sizeof(*value), loc);
}

void GMT_PinFloat_(unsigned int key, float* value, GMT_CodeLocation loc) {
  GMT_Pin_(key, value, sizeof(*value), loc);
}

void GMT_PinDouble_(unsigned int key, double* value, GMT_CodeLocation loc) {
  GMT_Pin_(key, value, sizeof(*value), loc);
}

void GMT_PinBool_(unsigned int key, bool* value, GMT_CodeLocation loc) {
  GMT_Pin_(key, value, sizeof(*value), loc);
}

void GMT_PinBytes_(unsigned int key, void* data, size_t size, GMT_CodeLocation loc) {
  GMT_Pin_(key, data, size, loc);
}
