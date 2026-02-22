/*
 * Record.c - Recording and replay engine.
 *
 * In RECORD mode: captures system input once per update and streams tagged binary
 * records to disk.  Signals are embedded inline as TAG_SIGNAL records.
 * All records carry a floating-point timestamp (seconds since start of recording)
 * so that replay is framerate-independent.
 *
 * In REPLAY mode: the test file is fully loaded at init time, decoded into
 * per-input and per-signal arrays, then fed back based on wall-clock time via
 * GMT_Record_InjectInput.  If a sync signal gates the next block of events,
 * injection is paused until the game emits that signal via GMT_SyncSignal_.
 * Time spent waiting for a signal is accumulated in replay_time_offset so
 * that subsequent timestamps remain consistent.
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

void GMT_Record_WriteInput(void) {
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
  else if (tag == GMT_RECORD_TAG_TRACK) g_gmt.record_track_count++;
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

// ===== Background replay injection thread =====
//
// Calling GMT_Record_InjectInput once per frame (every ~16 ms at 60 fps) means
// injection can miss a frame boundary by up to one full frame when there is any
// drift between the recording and replay clocks.  This thread calls the same
// function every 1 ms so that injection is decoupled from frame timing entirely.
//
// Thread safety: g_gmt state is read/written under the framework mutex.  The
// thread acquires the mutex only to collect the next batch of pending input
// records (updating cursors and prev/current state), then releases it BEFORE
// calling SendInput.  This is critical: SendInput triggers the Windows raw-input
// pipeline which needs to dispatch WH_KEYBOARD_LL / WH_MOUSE_LL callbacks to the
// main thread.  If the mutex were held during SendInput and the main thread were
// blocked on EnterCriticalSection waiting for that same mutex, neither thread
// could make progress — the main thread can't pump messages (needed for LL hook
// delivery) and the replay thread won't release the lock until SendInput returns.
// Windows eventually times out the hook (~200 ms default), causing visible lag.
//
// Keeping the mutex held only for the fast collect phase eliminates this stall.
//
// The thread must be stopped (GMT_Record_StopReplayThread) before any of the
// replay data it accesses is freed (GMT_Record_FreeReplay).

// Maximum number of input records collected per 1-ms iteration.
// In normal use at most one record elapses per ms; 8 is a generous catch-up cap.
#define GMT__MAX_INJECT_BATCH 8

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

  return count;
}

void GMT_Record_InjectInput(void) {
  // Used by the per-frame fallback path in GMT_Update_ (called with mutex held
  // on the main thread).  On the main thread SendInput is safe to call under the
  // mutex because the main thread IS the LL hook owner; the hook will fire on the
  // next PeekMessage call (glfwPollEvents) after the mutex is released.
  GMT_InputState new_states[GMT__MAX_INJECT_BATCH];
  GMT_InputState prev_states[GMT__MAX_INJECT_BATCH];
  int count = GMT__CollectPendingInjections(new_states, prev_states);
  for (int i = 0; i < count; i++) {
    GMT_Platform_SetReplayedInput(&new_states[i]);
    GMT_Platform_InjectInput(&new_states[i], &prev_states[i]);
  }
}

static int GMT__ReplayThreadFunc(void* arg) {
  (void)arg;
  GMT_InputState new_states[GMT__MAX_INJECT_BATCH];
  GMT_InputState prev_states[GMT__MAX_INJECT_BATCH];

  while (g_gmt.replay_thread_active) {
    // Collect under the mutex — fast: only pointer reads and state updates.
    GMT_Platform_MutexLock();
    int count = GMT__CollectPendingInjections(new_states, prev_states);
    GMT_Platform_MutexUnlock();

    // Inject OUTSIDE the mutex so SendInput never holds the lock while Windows
    // dispatches LL hook callbacks to the main thread.
    for (int i = 0; i < count; i++) {
      GMT_Platform_SetReplayedInput(&new_states[i]);
      GMT_Platform_InjectInput(&new_states[i], &prev_states[i]);
    }

    GMT_Platform_SleepMs(1);
  }
  return 0;
}

void GMT_Record_StartReplayThread(void) {
  g_gmt.replay_thread_active = 1;
  g_gmt.replay_thread_handle = GMT_Platform_CreateThread(GMT__ReplayThreadFunc, NULL);
  if (!g_gmt.replay_thread_handle) {
    GMT_LogError("GMT_Record: failed to create replay injection thread; falling back to per-frame injection.");
    g_gmt.replay_thread_active = 0;
  }
}

void GMT_Record_StopReplayThread(void) {
  // Signal the thread to stop.  This write is visible to the thread because
  // replay_thread_active is volatile and x86/x64 stores are sequentially
  // consistent for naturally-aligned int-sized locations.
  g_gmt.replay_thread_active = 0;
  if (g_gmt.replay_thread_handle) {
    GMT_Platform_JoinThread(g_gmt.replay_thread_handle);
    g_gmt.replay_thread_handle = NULL;
  }
}
