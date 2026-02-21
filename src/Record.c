/*
 * Record.c - Recording and replay engine.
 *
 * In RECORD mode: captures system input once per frame and streams tagged binary
 * records to disk.  Signals are embedded inline as TAG_SIGNAL records.
 *
 * In REPLAY mode: the test file is fully loaded at init time, decoded into
 * per-frame and per-signal arrays, then fed back frame-by-frame via
 * GMT_Record_InjectFrame.  If a sync signal gates the next block of frames,
 * injection is paused until the game emits that signal via GMT_SyncSignal_.
 */

#include "Internal.h"
#include "Record.h"
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
  return true;
}

void GMT_Record_CloseWrite(void) {
  if (!g_gmt.record_file) return;

  uint8_t end_tag = GMT_RECORD_TAG_END;
  fwrite(&end_tag, 1, 1, g_gmt.record_file);
  fclose(g_gmt.record_file);
  g_gmt.record_file = NULL;
}

void GMT_Record_WriteFrame(void) {
  if (!g_gmt.record_file) return;

  GMT_RawFrameRecord rec;
  rec.frame_index = g_gmt.frame_index;
  GMT_Platform_CaptureInput(&rec.input);

  uint8_t tag = GMT_RECORD_TAG_FRAME;
  fwrite(&tag, 1, 1, g_gmt.record_file);
  fwrite(&rec, sizeof(rec), 1, g_gmt.record_file);
}

void GMT_Record_WriteSignal(int32_t signal_id, uint64_t frame_index) {
  if (!g_gmt.record_file) return;

  GMT_RawSignalRecord rec;
  rec.frame_index = frame_index;
  rec.signal_id = signal_id;

  uint8_t tag = GMT_RECORD_TAG_SIGNAL;
  fwrite(&tag, 1, 1, g_gmt.record_file);
  fwrite(&rec, sizeof(rec), 1, g_gmt.record_file);
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
  uint8_t* data = (uint8_t*)malloc(total);
  if (!data) {
    fclose(f);
    GMT_LogError("GMT_Record: failed to read test file.");
    return false;
  }
  if (fread(data, 1, total, f) != total) {
    free(data);
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
  size_t frame_count = 0;
  size_t signal_count = 0;
  {
    const uint8_t* scan = cursor;
    while (scan < end) {
      uint8_t tag = *scan++;
      if (tag == GMT_RECORD_TAG_END) break;
      if (tag == GMT_RECORD_TAG_FRAME) {
        if (scan + sizeof(GMT_RawFrameRecord) > end) {
          GMT_LogError("GMT_Record: truncated frame record.");
          goto cleanup;
        }
        scan += sizeof(GMT_RawFrameRecord);
        ++frame_count;
      } else if (tag == GMT_RECORD_TAG_SIGNAL) {
        if (scan + sizeof(GMT_RawSignalRecord) > end) {
          GMT_LogError("GMT_Record: truncated signal record.");
          goto cleanup;
        }
        scan += sizeof(GMT_RawSignalRecord);
        ++signal_count;
      } else {
        GMT_LogError("GMT_Record: unknown tag in test file.");
        goto cleanup;
      }
    }
  }

  // Allocate decoded arrays.
  if (frame_count > 0) {
    g_gmt.replay_frames = (GMT_DecodedFrame*)GMT_Alloc(frame_count * sizeof(GMT_DecodedFrame));
    if (!g_gmt.replay_frames) {
      GMT_LogError("GMT_Record: allocation failed for replay frames.");
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

  // Second pass: decode.
  {
    size_t fi = 0, si = 0;
    while (cursor < end) {
      uint8_t tag = *cursor++;
      if (tag == GMT_RECORD_TAG_END) break;
      if (tag == GMT_RECORD_TAG_FRAME) {
        GMT_RawFrameRecord raw;
        memcpy(&raw, cursor, sizeof(raw));
        cursor += sizeof(raw);

        GMT_DecodedFrame* df = &g_gmt.replay_frames[fi++];
        df->frame_index = raw.frame_index;
        df->input = raw.input;
      } else if (tag == GMT_RECORD_TAG_SIGNAL) {
        GMT_RawSignalRecord raw;
        memcpy(&raw, cursor, sizeof(raw));
        cursor += sizeof(raw);

        GMT_DecodedSignal* ds = &g_gmt.replay_signals[si++];
        ds->frame_index = raw.frame_index;
        ds->signal_id = raw.signal_id;
      }
    }
  }

  g_gmt.replay_frame_count = frame_count;
  g_gmt.replay_signal_count = signal_count;
  ok = true;

cleanup:
  free(data);
  return ok;
}

void GMT_Record_FreeReplay(void) {
  if (g_gmt.replay_frames) {
    GMT_Free(g_gmt.replay_frames);
    g_gmt.replay_frames = NULL;
  }
  if (g_gmt.replay_signals) {
    GMT_Free(g_gmt.replay_signals);
    g_gmt.replay_signals = NULL;
  }
  g_gmt.replay_frame_count = 0;
  g_gmt.replay_signal_count = 0;
  g_gmt.replay_frame_cursor = 0;
  g_gmt.replay_signal_cursor = 0;
}

void GMT_Record_InjectFrame(void) {
  // If we're waiting for a sync signal, do not inject yet.
  if (g_gmt.waiting_for_signal) return;

  // Check whether the upcoming frame is gated by the next signal.
  // A signal record with frame_index == replay_frame_cursor means
  // "do not replay this frame block until the game emits this signal."
  while (g_gmt.replay_signal_cursor < g_gmt.replay_signal_count) {
    GMT_DecodedSignal* sig = &g_gmt.replay_signals[g_gmt.replay_signal_cursor];
    if (sig->frame_index == g_gmt.replay_frame_cursor) {
      // Block injection until the game calls GMT_SyncSignal_ with this id.
      g_gmt.waiting_for_signal = true;
      g_gmt.waiting_signal_id = sig->signal_id;
      return;
    }
    // The signal is in the past (cursor advanced past it); skip.
    if (sig->frame_index < g_gmt.replay_frame_cursor) {
      g_gmt.replay_signal_cursor++;
      continue;
    }
    // Signal is in the future; proceed with injection.
    break;
  }

  if (g_gmt.replay_frame_cursor >= g_gmt.replay_frame_count) return;  // Replay finished.

  GMT_DecodedFrame* df = &g_gmt.replay_frames[g_gmt.replay_frame_cursor];

  GMT_Platform_InjectInput(&df->input, &g_gmt.replay_prev_input);

  g_gmt.replay_prev_input = df->input;
  g_gmt.replay_frame_cursor++;
}
