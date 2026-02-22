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

// ===== Shared helper =====

static void GMT_Pin_(unsigned int key, void* data, size_t size, const char* type_name, const char* value_str, GMT_CodeLocation loc) {
  (void)loc;  // Available for future diagnostic use.

  if (!g_gmt.initialized || g_gmt.mode == GMT_Mode_DISABLED) return;
  if (!data || size == 0) return;
  if (size > (size_t)GMT_MAX_DATA_RECORD_PAYLOAD) {
    GMT_LogError("GMT_Pin<%s>: payload size %zu exceeds maximum %d; call ignored.", type_name, size, GMT_MAX_DATA_RECORD_PAYLOAD);
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
        GMT_LogError("GMT_Pin<%s>: no recorded value for key %u index %u; keeping current value %s.", type_name, key, index, value_str);
      } else if (rec->size != (uint32_t)size) {
        GMT_LogError("GMT_Pin<%s>: size mismatch for key %u index %u: recorded %u bytes, got %zu bytes; *value unchanged.",
                     type_name, key, index, rec->size, size);
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
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", *value);
  GMT_Pin_(key, value, sizeof(*value), "int", buf, loc);
}

void GMT_PinUInt_(unsigned int key, unsigned int* value, GMT_CodeLocation loc) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%u", *value);
  GMT_Pin_(key, value, sizeof(*value), "uint", buf, loc);
}

void GMT_PinFloat_(unsigned int key, float* value, GMT_CodeLocation loc) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.9g", (double)*value);
  GMT_Pin_(key, value, sizeof(*value), "float", buf, loc);
}

void GMT_PinDouble_(unsigned int key, double* value, GMT_CodeLocation loc) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%.17g", *value);
  GMT_Pin_(key, value, sizeof(*value), "double", buf, loc);
}

void GMT_PinBool_(unsigned int key, bool* value, GMT_CodeLocation loc) {
  GMT_Pin_(key, value, sizeof(*value), "bool", *value ? "true" : "false", loc);
}

void GMT_PinBytes_(unsigned int key, void* data, size_t size, GMT_CodeLocation loc) {
  GMT_Pin_(key, data, size, "bytes", "(blob)", loc);
}
