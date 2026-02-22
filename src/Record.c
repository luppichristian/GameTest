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
#include "Platform.h"
#include <string.h>
#include <stdlib.h>

// ===== RECORD mode =====

bool GMT_Record_OpenForWrite(void) {
  // Ensure parent directory exists.
  const char* path = g_gmt.setup.test_path;
  if (!path || path[0] == '\0') return false;

  // Try to create the directory portion of the path.
  // Extract directory part by finding the last separator.
  char dir_buf[4096];
  size_t len = strlen(path);
  if (len < sizeof(dir_buf)) {
    memcpy(dir_buf, path, len + 1);
    // Walk backwards to find the last separator.
    for (size_t i = len; i > 0; --i) {
      if (dir_buf[i - 1] == '/' || dir_buf[i - 1] == '\\') {
        dir_buf[i - 1] = '\0';
        GMT_Platform_CreateDirRecursive(dir_buf);
        break;
      }
    }
  }

  FILE* fh = fopen(path, "wb");
  if (!fh) {
    GMT_LogError("GMT_Record: failed to open test file for write.");
    return false;
  }

  GMT_FileHeader hdr;
  hdr.magic = GMT_RECORD_MAGIC;
  hdr.version = GMT_RECORD_VERSION;

  if (fwrite(&hdr, sizeof(hdr), 1, fh) != 1) {
    fclose(fh);
    GMT_LogError("GMT_Record: failed to write file header.");
    return false;
  }

  g_gmt.record_file = fh;
  g_gmt.record_input_count = 0;
  g_gmt.record_signal_count = 0;
  g_gmt.record_pin_count = 0;
  g_gmt.record_track_count = 0;
  return true;
}

void GMT_Record_CloseWrite(void) {
  if (!g_gmt.record_file) return;

  uint8_t end_tag = GMT_RECORD_TAG_END;
  fwrite(&end_tag, 1, 1, g_gmt.record_file);
  fclose(g_gmt.record_file);
  g_gmt.record_file = NULL;
}

static void GMT__WriteInputRecord(void) {
  if (!g_gmt.record_file) return;

  GMT_RawInputRecord rec;
  rec.timestamp = GMT_Platform_GetTime() - g_gmt.record_start_time;
  GMT_Platform_CaptureInput(&rec.input);

  // Skip writing if the input state is identical to the previous frame.
  if (GMT_InputState_Compare(&rec.input, &g_gmt.record_prev_input)) return;
  g_gmt.record_prev_input = rec.input;

  uint8_t tag = GMT_RECORD_TAG_INPUT;
  fwrite(&tag, 1, 1, g_gmt.record_file);
  fwrite(&rec, sizeof(rec), 1, g_gmt.record_file);
  g_gmt.record_input_count++;
}

void GMT_Record_WriteInput(void) {
  GMT__WriteInputRecord();
}

void GMT_Record_WriteInputFromKeyEvent(void) {
  if (!g_gmt.initialized || g_gmt.mode != GMT_Mode_RECORD || !g_gmt.record_file) return;
  GMT_Platform_MutexLock();
  GMT__WriteInputRecord();
  GMT_Platform_MutexUnlock();
}

void GMT_Record_WriteSignal(int32_t signal_id) {
  if (!g_gmt.record_file) return;

  GMT_RawSignalRecord rec;
  rec.timestamp = GMT_Platform_GetTime() - g_gmt.record_start_time;
  rec.signal_id = signal_id;

  uint8_t tag = GMT_RECORD_TAG_SIGNAL;
  fwrite(&tag, 1, 1, g_gmt.record_file);
  fwrite(&rec, sizeof(rec), 1, g_gmt.record_file);
  g_gmt.record_signal_count++;
}

void GMT_Record_WriteDataRecord(uint8_t tag, unsigned int key, unsigned int index, const void* data, size_t size) {
  if (!g_gmt.record_file) return;
  if (size > GMT_MAX_DATA_RECORD_PAYLOAD) {
    GMT_LogError("GMT_Record_WriteDataRecord: payload %zu exceeds maximum %d; record skipped.", size, GMT_MAX_DATA_RECORD_PAYLOAD);
    return;
  }

  GMT_RawDataRecordHeader hdr;
  hdr.key = (uint32_t)key;
  hdr.index = (uint32_t)index;
  hdr.size = (uint32_t)size;

  fwrite(&tag, 1, 1, g_gmt.record_file);
  fwrite(&hdr, sizeof(hdr), 1, g_gmt.record_file);
  fwrite(data, 1, size, g_gmt.record_file);

  if (tag == GMT_RECORD_TAG_PIN) g_gmt.record_pin_count++;
  else if (tag == GMT_RECORD_TAG_TRACK)
    g_gmt.record_track_count++;
}

GMT_DecodedDataRecord* GMT_Record_FindDecoded(GMT_DecodedDataRecord* arr, size_t count, unsigned int key, unsigned int index) {
  for (size_t i = 0; i < count; i++) {
    if (arr[i].key == (uint32_t)key && arr[i].index == (uint32_t)index) return &arr[i];
  }
  return NULL;
}

// ===== REPLAY mode =====

bool GMT_Record_LoadReplay(void) {
  const char* path = g_gmt.setup.test_path;
  if (!path || path[0] == '\0') {
    GMT_LogError("GMT_Record: test_path is NULL or empty.");
    return false;
  }
  FILE* f = fopen(path, "rb");
  if (!f) {
    GMT_LogError("GMT_Record: test file does not exist.");
    return false;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    GMT_LogError("GMT_Record: failed to read test file.");
    return false;
  }
  long file_sz = ftell(f);
  rewind(f);
  if (file_sz < 0) {
    fclose(f);
    GMT_LogError("GMT_Record: failed to read test file.");
    return false;
  }
  size_t total = (size_t)file_sz;
  uint8_t* data = GMT_Alloc(total);
  if (!data) {
    fclose(f);
    GMT_LogError("GMT_Record: failed to read test file.");
    return false;
  }
  if (fread(data, 1, total, f) != total) {
    GMT_Free(data);
    fclose(f);
    GMT_LogError("GMT_Record: failed to read test file.");
    return false;
  }
  fclose(f);

  bool ok = false;

  if (total < sizeof(GMT_FileHeader)) {
    GMT_LogError("GMT_Record: test file is too small to contain a valid header.");
    goto cleanup;
  }

  const uint8_t* cursor = data;
  const uint8_t* end = data + total;

  GMT_FileHeader hdr;
  memcpy(&hdr, cursor, sizeof(hdr));
  cursor += sizeof(hdr);

  if (hdr.magic != GMT_RECORD_MAGIC) {
    GMT_LogError("GMT_Record: invalid file magic.");
    goto cleanup;
  }
  if (hdr.version != GMT_RECORD_VERSION) {
    GMT_LogError("GMT_Record: unsupported file version.");
    goto cleanup;
  }

  // First pass: count records.
  size_t input_count = 0;
  size_t signal_count = 0;
  size_t pin_count = 0;
  size_t track_count = 0;
  {
    const uint8_t* scan = cursor;
    while (scan < end) {
      uint8_t tag = *scan++;
      if (tag == GMT_RECORD_TAG_END) break;
      if (tag == GMT_RECORD_TAG_INPUT) {
        if (scan + sizeof(GMT_RawInputRecord) > end) {
          GMT_LogError("GMT_Record: truncated input record.");
          goto cleanup;
        }
        scan += sizeof(GMT_RawInputRecord);
        ++input_count;
      } else if (tag == GMT_RECORD_TAG_SIGNAL) {
        if (scan + sizeof(GMT_RawSignalRecord) > end) {
          GMT_LogError("GMT_Record: truncated signal record.");
          goto cleanup;
        }
        scan += sizeof(GMT_RawSignalRecord);
        ++signal_count;
      } else if (tag == GMT_RECORD_TAG_PIN || tag == GMT_RECORD_TAG_TRACK) {
        if (scan + sizeof(GMT_RawDataRecordHeader) > end) {
          GMT_LogError("GMT_Record: truncated pin/track header.");
          goto cleanup;
        }
        GMT_RawDataRecordHeader drh;
        memcpy(&drh, scan, sizeof(drh));
        scan += sizeof(drh);
        if ((size_t)drh.size > (size_t)(end - scan)) {
          GMT_LogError("GMT_Record: truncated pin/track payload.");
          goto cleanup;
        }
        scan += drh.size;
        if (tag == GMT_RECORD_TAG_PIN) ++pin_count;
        else
          ++track_count;
      } else {
        GMT_LogError("GMT_Record: unknown tag in test file.");
        goto cleanup;
      }
    }
  }

  // Allocate decoded arrays.
  if (input_count > 0) {
    g_gmt.replay_inputs = (GMT_DecodedInput*)GMT_Alloc(input_count * sizeof(GMT_DecodedInput));
    if (!g_gmt.replay_inputs) {
      GMT_LogError("GMT_Record: allocation failed for replay inputs.");
      goto cleanup;
    }
  }
  if (signal_count > 0) {
    g_gmt.replay_signals = (GMT_DecodedSignal*)GMT_Alloc(signal_count * sizeof(GMT_DecodedSignal));
    if (!g_gmt.replay_signals) {
      GMT_LogError("GMT_Record: allocation failed for replay signals.");
      goto cleanup;
    }
  }
  if (pin_count > 0) {
    g_gmt.replay_pins = (GMT_DecodedDataRecord*)GMT_Alloc(pin_count * sizeof(GMT_DecodedDataRecord));
    if (!g_gmt.replay_pins) {
      GMT_LogError("GMT_Record: allocation failed for replay pins.");
      goto cleanup;
    }
  }
  if (track_count > 0) {
    g_gmt.replay_tracks = (GMT_DecodedDataRecord*)GMT_Alloc(track_count * sizeof(GMT_DecodedDataRecord));
    if (!g_gmt.replay_tracks) {
      GMT_LogError("GMT_Record: allocation failed for replay tracks.");
      goto cleanup;
    }
  }

  // Second pass: decode.
  {
    size_t ii = 0, si = 0, pi = 0, ti = 0;
    while (cursor < end) {
      uint8_t tag = *cursor++;
      if (tag == GMT_RECORD_TAG_END) break;
      if (tag == GMT_RECORD_TAG_INPUT) {
        GMT_RawInputRecord raw;
        memcpy(&raw, cursor, sizeof(raw));
        cursor += sizeof(raw);

        GMT_DecodedInput* di = &g_gmt.replay_inputs[ii++];
        di->timestamp = raw.timestamp;
        di->input = raw.input;
      } else if (tag == GMT_RECORD_TAG_SIGNAL) {
        GMT_RawSignalRecord raw;
        memcpy(&raw, cursor, sizeof(raw));
        cursor += sizeof(raw);

        GMT_DecodedSignal* ds = &g_gmt.replay_signals[si++];
        ds->timestamp = raw.timestamp;
        ds->signal_id = raw.signal_id;
      } else if (tag == GMT_RECORD_TAG_PIN || tag == GMT_RECORD_TAG_TRACK) {
        GMT_RawDataRecordHeader hdr;
        memcpy(&hdr, cursor, sizeof(hdr));
        cursor += sizeof(hdr);

        GMT_DecodedDataRecord* dr = (tag == GMT_RECORD_TAG_PIN)
                                        ? &g_gmt.replay_pins[pi++]
                                        : &g_gmt.replay_tracks[ti++];
        dr->key = hdr.key;
        dr->index = hdr.index;
        dr->size = hdr.size;
        if (hdr.size > 0) memcpy(dr->data, cursor, hdr.size);
        cursor += hdr.size;
      }
    }
  }

  g_gmt.replay_input_count = input_count;
  g_gmt.replay_signal_count = signal_count;
  g_gmt.replay_pin_count = pin_count;
  g_gmt.replay_track_count = track_count;
  ok = true;

cleanup:
  GMT_Free(data);
  return ok;
}

void GMT_Record_FreeReplay(void) {
  if (g_gmt.replay_inputs) {
    GMT_Free(g_gmt.replay_inputs);
    g_gmt.replay_inputs = NULL;
  }
  if (g_gmt.replay_signals) {
    GMT_Free(g_gmt.replay_signals);
    g_gmt.replay_signals = NULL;
  }
  if (g_gmt.replay_pins) {
    GMT_Free(g_gmt.replay_pins);
    g_gmt.replay_pins = NULL;
  }
  if (g_gmt.replay_tracks) {
    GMT_Free(g_gmt.replay_tracks);
    g_gmt.replay_tracks = NULL;
  }
  g_gmt.replay_input_count = 0;
  g_gmt.replay_signal_count = 0;
  g_gmt.replay_pin_count = 0;
  g_gmt.replay_track_count = 0;
  g_gmt.replay_input_cursor = 0;
  g_gmt.replay_signal_cursor = 0;
}

GMT_FileMetrics GMT_Record_GetReplayMetrics(void) {
  GMT_FileMetrics m;
  memset(&m, 0, sizeof(m));
  m.input_count = g_gmt.replay_input_count;
  m.signal_count = g_gmt.replay_signal_count;
  m.pin_count = g_gmt.replay_pin_count;
  m.track_count = g_gmt.replay_track_count;
  m.duration = (g_gmt.replay_input_count > 0)
                   ? g_gmt.replay_inputs[g_gmt.replay_input_count - 1].timestamp
                   : 0.0;
  m.input_density = (m.duration > 0.0) ? (double)m.input_count / m.duration : 0.0;
  return m;
}

GMT_FileMetrics GMT_Record_GetRecordMetrics(void) {
  GMT_FileMetrics m;
  memset(&m, 0, sizeof(m));
  long file_pos = g_gmt.record_file ? ftell(g_gmt.record_file) : 0;
  /* +1 accounts for the TAG_END byte that CloseWrite is about to append. */
  m.file_size_bytes = (file_pos >= 0) ? file_pos + 1 : 0;
  m.duration = GMT_Platform_GetTime() - g_gmt.record_start_time;
  m.frame_count = g_gmt.frame_index;
  m.input_count = g_gmt.record_input_count;
  m.signal_count = g_gmt.record_signal_count;
  m.pin_count = g_gmt.record_pin_count;
  m.track_count = g_gmt.record_track_count;
  m.input_density = (m.duration > 0.0) ? (double)m.input_count / m.duration : 0.0;
  return m;
}

// ===== Replay injection =====

// Maximum number of input records collected per injection call.
// Increased from 8 to 64 to prevent delayed injection when many inputs are due
// in a single frame (e.g. rapid key taps), which could cause tick-boundary drift.
#define GMT__MAX_INJECT_BATCH 64

// Collects all pending input records whose timestamps have elapsed into
// out_new[]/out_prev[] pairs (up to GMT__MAX_INJECT_BATCH entries).
// Advances g_gmt cursors and prev/current state, but does NOT call SendInput.
// Must be called with the mutex held.  Returns the number of pairs collected.
static int GMT__CollectPendingInjections(GMT_InputState* out_new, GMT_InputState* out_prev) {
  if (g_gmt.waiting_for_signal) return 0;

  double now = GMT_Platform_GetTime();
  double replay_time = (now - g_gmt.record_start_time) - g_gmt.replay_time_offset;
  int count = 0;

  while (count < GMT__MAX_INJECT_BATCH) {
    bool have_input = g_gmt.replay_input_cursor < g_gmt.replay_input_count;
    bool have_signal = g_gmt.replay_signal_cursor < g_gmt.replay_signal_count;

    if (!have_input && !have_signal) break;

    double it = have_input ? g_gmt.replay_inputs[g_gmt.replay_input_cursor].timestamp : 1e18;
    double st = have_signal ? g_gmt.replay_signals[g_gmt.replay_signal_cursor].timestamp : 1e18;

    // Signal wins ties — it must gate before a same-timestamp input record.
    bool signal_first = have_signal && st <= it;

    if (signal_first) {
      if (st > replay_time) break;
      g_gmt.waiting_for_signal = true;
      g_gmt.waiting_signal_id = g_gmt.replay_signals[g_gmt.replay_signal_cursor].signal_id;
      g_gmt.signal_wait_start = now;
      break;
    }

    if (it > replay_time) break;

    GMT_DecodedInput* di = &g_gmt.replay_inputs[g_gmt.replay_input_cursor];
    out_prev[count] = (count == 0) ? g_gmt.replay_prev_input : out_new[count - 1];
    out_new[count] = di->input;
    g_gmt.replay_prev_input = di->input;
    g_gmt.replay_current_input = di->input;
    g_gmt.replay_input_cursor++;
    count++;
  }

  // Warn if batch limit caused us to defer input records (may cause timing drift).
  if (count == GMT__MAX_INJECT_BATCH &&
      g_gmt.replay_input_cursor < g_gmt.replay_input_count &&
      g_gmt.replay_inputs[g_gmt.replay_input_cursor].timestamp <= replay_time) {
    GMT_LogWarning("GMT_Record: batch limit (%d) reached; input records deferred to next frame (may cause replay drift).",
                   GMT__MAX_INJECT_BATCH);
  }

  return count;
}

void GMT_Record_InjectInput(void) {
  GMT_InputState new_states[GMT__MAX_INJECT_BATCH];
  GMT_InputState prev_states[GMT__MAX_INJECT_BATCH];
  int count = GMT__CollectPendingInjections(new_states, prev_states);
  for (int i = 0; i < count; i++) {
    GMT_Platform_SetReplayedInput(&new_states[i]);
    GMT_Platform_InjectInput(&new_states[i], &prev_states[i]);
  }
}
