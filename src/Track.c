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
#include "Record.h"
#include <string.h>
#include <math.h>

// ===== Comparison modes =====

typedef enum {
  GMT_CMP_INT,     // int32_t:  signed decimal, memcmp equality
  GMT_CMP_UINT,    // uint32_t: unsigned decimal, memcmp equality
  GMT_CMP_BOOL,    // bool (1 byte): true/false, memcmp equality
  GMT_CMP_FLOAT,   // float:  fabsf < GMT_FLOAT_EPSILON
  GMT_CMP_DOUBLE,  // double: fabs  < GMT_DOUBLE_EPSILON
  GMT_CMP_EXACT,   // arbitrary bytes: memcmp, hex-dump on mismatch
} GMT_CmpMode;

static const char* GMT_CmpModeName(GMT_CmpMode cmp) {
  switch (cmp) {
    case GMT_CMP_INT:    return "int";
    case GMT_CMP_UINT:   return "uint";
    case GMT_CMP_BOOL:   return "bool";
    case GMT_CMP_FLOAT:  return "float";
    case GMT_CMP_DOUBLE: return "double";
    default:             return "bytes";
  }
}

// ===== Shared helper =====

static void GMT_Track_(unsigned int key, const void* data, size_t size, GMT_CmpMode cmp, GMT_CodeLocation loc) {
  if (!g_gmt.initialized || g_gmt.mode == GMT_Mode_DISABLED) return;
  if (!data || size == 0) return;
  if (size > (size_t)GMT_MAX_DATA_RECORD_PAYLOAD) {
    GMT_LogError("GMT_Track<%s>: payload size %zu exceeds maximum %d; call ignored.", GMT_CmpModeName(cmp), size, GMT_MAX_DATA_RECORD_PAYLOAD);
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
        GMT_LogWarning("GMT_Track<%s>: no recorded snapshot for key %u index %u; skipping check.", GMT_CmpModeName(cmp), key, index);
        return;
      }
      if (rsz != (uint32_t)size) {
        GMT_LogWarning("GMT_Track<%s>: size mismatch for key %u index %u: recorded %u bytes, got %zu bytes; skipping check.",
                       GMT_CmpModeName(cmp), key, index, rsz, size);
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
          case GMT_CMP_INT: {
            int32_t recorded, current;
            memcpy(&recorded, rdata, sizeof(int32_t));
            memcpy(&current, data, sizeof(int32_t));
            snprintf(detail, sizeof(detail),
                     "GMT_Track<int>: value mismatch (key %u, index %u): %d != %d",
                     key, index, (int)recorded, (int)current);
            break;
          }
          case GMT_CMP_UINT: {
            uint32_t recorded, current;
            memcpy(&recorded, rdata, sizeof(uint32_t));
            memcpy(&current, data, sizeof(uint32_t));
            snprintf(detail, sizeof(detail),
                     "GMT_Track<uint>: value mismatch (key %u, index %u): %u != %u",
                     key, index, (unsigned)recorded, (unsigned)current);
            break;
          }
          case GMT_CMP_BOOL: {
            bool recorded = (rdata[0] != 0);
            bool current = (((const uint8_t*)data)[0] != 0);
            snprintf(detail, sizeof(detail),
                     "GMT_Track<bool>: value mismatch (key %u, index %u): %s != %s",
                     key, index,
                     recorded ? "true" : "false",
                     current ? "true" : "false");
            break;
          }
          case GMT_CMP_FLOAT: {
            float recorded, current;
            memcpy(&recorded, rdata, sizeof(float));
            memcpy(&current, data, sizeof(float));
            snprintf(detail, sizeof(detail),
                     "GMT_Track<float>: value mismatch (key %u, index %u): %.9g != %.9g (diff %.9g)",
                     key, index,
                     (double)recorded, (double)current,
                     (double)fabsf(recorded - current));
            break;
          }
          case GMT_CMP_DOUBLE: {
            double recorded, current;
            memcpy(&recorded, rdata, sizeof(double));
            memcpy(&current, data, sizeof(double));
            snprintf(detail, sizeof(detail),
                     "GMT_Track<double>: value mismatch (key %u, index %u): %.17g != %.17g (diff %.17g)",
                     key, index, recorded, current, fabs(recorded - current));
            break;
          }
          default: {
            // Hex dump: up to 32 bytes shown.
            const size_t dump_max = size < 32 ? size : 32;
            const uint8_t* cur = (const uint8_t*)data;
            int off = snprintf(detail, sizeof(detail),
                               "GMT_Track<bytes>: value mismatch (key %u, index %u, %zu bytes): recorded [",
                               key, index, size);
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
  GMT_Track_(key, &value, sizeof(value), GMT_CMP_INT, loc);
}

void GMT_TrackUInt_(unsigned int key, unsigned int value, GMT_CodeLocation loc) {
  GMT_Track_(key, &value, sizeof(value), GMT_CMP_UINT, loc);
}

void GMT_TrackFloat_(unsigned int key, float value, GMT_CodeLocation loc) {
  GMT_Track_(key, &value, sizeof(value), GMT_CMP_FLOAT, loc);
}

void GMT_TrackDouble_(unsigned int key, double value, GMT_CodeLocation loc) {
  GMT_Track_(key, &value, sizeof(value), GMT_CMP_DOUBLE, loc);
}

void GMT_TrackBool_(unsigned int key, bool value, GMT_CodeLocation loc) {
  GMT_Track_(key, &value, sizeof(value), GMT_CMP_BOOL, loc);
}

void GMT_TrackBytes_(unsigned int key, const void* data, size_t size, GMT_CodeLocation loc) {
  GMT_Track_(key, data, size, GMT_CMP_EXACT, loc);
}
