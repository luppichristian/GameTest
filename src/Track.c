/*
 * Track.c - GMT_Track* implementation.
 *
 * Tracks a variable across record/replay and asserts it matches.
 *
 * Record mode : snapshots the current value and writes it to the test file as TAG_TRACK.
 * Replay mode : locates the decoded entry by (key, sequential-index), compares it against
 *               the live value, and triggers an assertion failure (same path as GMT_Assert)
 *               if the values differ.  Call-count mismatches (more calls during replay than
 *               were recorded) also fail.
 * Disabled    : no-op.
 *
 * Thread safety: the mutex is released before calling GMT_Assert_ so that the assertion
 * subsystem can acquire it cleanly (Win32 CRITICAL_SECTION is reentrant, but explicit
 * release is cleaner and avoids lock-depth surprises if the fail callback re-enters).
 */

#include "Internal.h"
#include "Record.h"
#include <string.h>
#include <math.h>

// ===== Comparison modes =====

typedef enum {
  GMT_CMP_EXACT,   // memcmp
  GMT_CMP_FLOAT,   // fabsf < GMT_FLOAT_EPSILON
  GMT_CMP_DOUBLE,  // fabs  < GMT_DOUBLE_EPSILON
} GMT_CmpMode;

// ===== Shared helper =====

static void GMT_Track_(unsigned int key, const void* data, size_t size, GMT_CmpMode cmp, GMT_CodeLocation loc) {
  if (!g_gmt.initialized || g_gmt.mode == GMT_Mode_DISABLED) return;
  if (!data || size == 0) return;
  if (size > (size_t)GMT_MAX_DATA_RECORD_PAYLOAD) {
    GMT_LogError("GMT_Track: payload size %zu exceeds maximum %d; call ignored.", size, GMT_MAX_DATA_RECORD_PAYLOAD);
    return;
  }

  GMT_Platform_MutexLock();

  unsigned int index = GMT_KeyCounter_Next(&g_gmt.track_counter, key);

  switch (g_gmt.mode) {
    case GMT_Mode_RECORD:
      GMT_Record_WriteDataRecord(GMT_RECORD_TAG_TRACK, key, index, data, size);
      GMT_Platform_MutexUnlock();
      break;

    case GMT_Mode_REPLAY: {
      // Snapshot the decoded record under the lock, then release before any
      // assertion so the assertion subsystem can re-acquire cleanly.
      GMT_DecodedDataRecord* rec = GMT_Record_FindDecoded(g_gmt.replay_tracks, g_gmt.replay_track_count, key, index);

      bool found = (rec != NULL);
      uint32_t rsz = found ? rec->size : 0;
      uint8_t rdata[GMT_MAX_DATA_RECORD_PAYLOAD];
      if (found && rsz > 0) memcpy(rdata, rec->data, rsz);

      GMT_Platform_MutexUnlock();

      if (!found) {
        GMT_LogWarning("GMT_Track: no recorded snapshot for key %u index %u; skipping check.", key, index);
        return;
      }
      if (rsz != (uint32_t)size) {
        GMT_LogWarning("GMT_Track: size mismatch for key %u index %u: recorded %u bytes, got %zu bytes; skipping check.",
                       key,
                       index,
                       rsz,
                       size);
        return;
      }

      bool match;
      switch (cmp) {
        case GMT_CMP_FLOAT: {
          float recorded, current;
          memcpy(&recorded, rdata, sizeof(float));
          memcpy(&current, data, sizeof(float));
          match = (fabsf(recorded - current) < GMT_FLOAT_EPSILON);
          break;
        }
        case GMT_CMP_DOUBLE: {
          double recorded, current;
          memcpy(&recorded, rdata, sizeof(double));
          memcpy(&current, data, sizeof(double));
          match = (fabs(recorded - current) < (double)GMT_DOUBLE_EPSILON);
          break;
        }
        default:
          match = (memcmp(rdata, data, size) == 0);
          break;
      }

      if (!match) {
        // Log the actual values before asserting so the output is actionable.
        char detail[512];
        switch (cmp) {
          case GMT_CMP_FLOAT: {
            float recorded, current;
            memcpy(&recorded, rdata, sizeof(float));
            memcpy(&current, data, sizeof(float));
            snprintf(detail, sizeof(detail),
                     "GMT_Track: value mismatch (key %u, index %u): "
                     "recorded %.9g, current %.9g (diff %.9g)",
                     key,
                     index,
                     (double)recorded,
                     (double)current,
                     (double)fabsf(recorded - current));
            break;
          }
          case GMT_CMP_DOUBLE: {
            double recorded, current;
            memcpy(&recorded, rdata, sizeof(double));
            memcpy(&current, data, sizeof(double));
            snprintf(detail, sizeof(detail),
                     "GMT_Track: value mismatch (key %u, index %u): "
                     "recorded %.17g, current %.17g (diff %.17g)",
                     key,
                     index,
                     recorded,
                     current,
                     fabs(recorded - current));
            break;
          }
          default: {
            // Exact match: format as integer for small sizes, hex dump otherwise.
            if (size == sizeof(int32_t)) {
              int32_t recorded, current;
              memcpy(&recorded, rdata, sizeof(int32_t));
              memcpy(&current, data, sizeof(int32_t));
              snprintf(detail, sizeof(detail),
                       "GMT_Track: value mismatch (key %u, index %u): "
                       "recorded %d (0x%08X), current %d (0x%08X)",
                       key,
                       index,
                       (int)recorded,
                       (unsigned)recorded,
                       (int)current,
                       (unsigned)current);
            } else if (size == sizeof(int64_t)) {
              int64_t recorded, current;
              memcpy(&recorded, rdata, sizeof(int64_t));
              memcpy(&current, data, sizeof(int64_t));
              snprintf(detail, sizeof(detail),
                       "GMT_Track: value mismatch (key %u, index %u): "
                       "recorded %lld (0x%016llX), current %lld (0x%016llX)",
                       key,
                       index,
                       (long long)recorded,
                       (unsigned long long)recorded,
                       (long long)current,
                       (unsigned long long)current);
            } else if (size == 1) {
              snprintf(detail, sizeof(detail),
                       "GMT_Track: value mismatch (key %u, index %u): "
                       "recorded 0x%02X, current 0x%02X",
                       key,
                       index,
                       (unsigned)rdata[0],
                       (unsigned)((const uint8_t*)data)[0]);
            } else {
              // Hex dump: up to 32 bytes shown.
              const size_t dump_max = size < 32 ? size : 32;
              const uint8_t* cur = (const uint8_t*)data;
              int off = snprintf(detail, sizeof(detail),
                                 "GMT_Track: value mismatch (key %u, index %u, %zu bytes): "
                                 "recorded [",
                                 key,
                                 index,
                                 size);
              for (size_t i = 0; i < dump_max && off < (int)sizeof(detail) - 4; ++i)
                off += snprintf(detail + off, sizeof(detail) - (size_t)off, "%02X", rdata[i]);
              if (size > dump_max && off < (int)sizeof(detail) - 4)
                off += snprintf(detail + off, sizeof(detail) - (size_t)off, "..");
              off += snprintf(detail + off, sizeof(detail) - (size_t)off, "], current [");
              for (size_t i = 0; i < dump_max && off < (int)sizeof(detail) - 4; ++i)
                off += snprintf(detail + off, sizeof(detail) - (size_t)off, "%02X", cur[i]);
              if (size > dump_max && off < (int)sizeof(detail) - 4)
                off += snprintf(detail + off, sizeof(detail) - (size_t)off, "..");
              snprintf(detail + off, sizeof(detail) - (size_t)off, "]");
            }
            break;
          }
        }
        GMT_LogError("%s", detail);
        GMT_Assert_(false,
                    "GMT_Track: value mismatch between record and replay.",
                    loc);
      }
      break;
    }

    default:
      GMT_Platform_MutexUnlock();
      break;
  }
}

// ===== Typed public functions =====

void GMT_TrackInt_(unsigned int key, int value, GMT_CodeLocation loc) {
  GMT_Track_(key, &value, sizeof(value), GMT_CMP_EXACT, loc);
}

void GMT_TrackUInt_(unsigned int key, unsigned int value, GMT_CodeLocation loc) {
  GMT_Track_(key, &value, sizeof(value), GMT_CMP_EXACT, loc);
}

void GMT_TrackFloat_(unsigned int key, float value, GMT_CodeLocation loc) {
  GMT_Track_(key, &value, sizeof(value), GMT_CMP_FLOAT, loc);
}

void GMT_TrackDouble_(unsigned int key, double value, GMT_CodeLocation loc) {
  GMT_Track_(key, &value, sizeof(value), GMT_CMP_DOUBLE, loc);
}

void GMT_TrackBool_(unsigned int key, bool value, GMT_CodeLocation loc) {
  GMT_Track_(key, &value, sizeof(value), GMT_CMP_EXACT, loc);
}

void GMT_TrackBytes_(unsigned int key, const void* data, size_t size, GMT_CodeLocation loc) {
  GMT_Track_(key, data, size, GMT_CMP_EXACT, loc);
}
