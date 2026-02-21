/*
 * Internal.h - Shared internal types and global state declaration.
 *
 * Include this (instead of GameTest.h directly) in every .c implementation file
 * that needs access to g_gmt.  Do *not* expose this header to library users.
 */

#pragma once

#include <stdio.h>
#include "GameTest.h"
#include "Platform.h"

// ===== Limits =====

#define GMT_MAX_FAILED_ASSERTIONS 1024
#define GMT_MAX_UNIQUE_ASSERTIONS 2048

// ===== Record File Format =====
//
// Layout:
//   [GMT_FileHeader]
//   N × tagged record:
//   TAG_INPUT  (0x01) → GMT_RawInputRecord
//     TAG_SIGNAL (0x02) → GMT_RawSignalRecord
//   TAG_END (0xFF)       → (no body)
//
// All multi-byte integers are little-endian.

#define GMT_RECORD_MAGIC   0x54534D47u  // 'GMST' in memory (little-endian)
#define GMT_RECORD_VERSION 2u

#define GMT_RECORD_TAG_INPUT  ((uint8_t)0x01)
#define GMT_RECORD_TAG_SIGNAL ((uint8_t)0x02)
#define GMT_RECORD_TAG_END    ((uint8_t)0xFF)

// Fixed-size file header written at the start of every test file.
#pragma pack(push, 1)
typedef struct GMT_FileHeader {
  uint16_t magic;
  uint16_t version;
} GMT_FileHeader;

// Body of a TAG_INPUT record (written without the tag byte).
typedef struct GMT_RawInputRecord {
  double timestamp;  // Seconds since start of recording.
  GMT_InputState input;
} GMT_RawInputRecord;

// Body of a TAG_SIGNAL record (written without the tag byte).
typedef struct GMT_RawSignalRecord {
  double timestamp;  // Seconds since start of recording.
  int32_t signal_id;
} GMT_RawSignalRecord;
#pragma pack(pop)

// ===== File metrics (used for logging after load/before close) =====

typedef struct GMT_FileMetrics {
  long file_size_bytes;  // File size in bytes (RECORD: incl. pending TAG_END; REPLAY: 0)
  size_t input_count;    // Number of input records (RECORD: estimated from file size)
  size_t signal_count;   // Number of signal records (RECORD: not tracked, always 0)
  double duration;       // Recording length in seconds
  double input_density;  // Input records per second
  uint64_t frame_count;  // Frames processed (RECORD only; 0 for REPLAY)
} GMT_FileMetrics;

// ===== In-memory decoded records (used during REPLAY) =====

typedef struct GMT_DecodedInput {
  double timestamp;  // Seconds since start of recording.
  GMT_InputState input;
} GMT_DecodedInput;

typedef struct GMT_DecodedSignal {
  double timestamp;  // Seconds since start of recording.
  int32_t signal_id;
} GMT_DecodedSignal;

// ===== Global framework state =====

typedef struct GMT_State {
  bool initialized;
  GMT_Mode mode;

  // Shallow copy of the user's GMT_Setup provided to GMT_Init.
  // Pointers inside (test_path, work_dir, directory_mappings) should remain
  // valid for the lifetime of the framework.
  GMT_Setup setup;

  // ----- Failed assertions -----
  GMT_Assertion failed_assertions[GMT_MAX_FAILED_ASSERTIONS];
  size_t failed_assertion_count;
  // Running count of assertion failures this run (reset by GMT_Reset).
  int assertion_fire_count;
  // Total number of GMT_Assert_ calls (pass + fail) this run.
  size_t total_assertion_count;
  // Number of distinct call-site locations seen this run.
  size_t unique_assertion_count;
  // Open-addressing hash set used to track unique assertion call sites.
  int seen_assertion_hashes[GMT_MAX_UNIQUE_ASSERTIONS];
  bool seen_assertion_occupied[GMT_MAX_UNIQUE_ASSERTIONS];
  bool test_failed;

  // ----- Runtime -----
  // Monotonically increasing counter incremented by each GMT_Update call.
  uint64_t frame_index;

  // ----- Timing -----
  // Platform time (seconds) when recording or replay started.
  // Used as the epoch for all recorded timestamps.
  double record_start_time;
  // Accumulated time (seconds) spent waiting for sync signals during replay.
  // Subtracted from elapsed wall-clock time to produce the effective replay time.
  double replay_time_offset;
  // Platform time when the current signal wait began (used to compute offset on unblock).
  double signal_wait_start;

  // ----- RECORD mode -----
  FILE* record_file;  // Open for streaming write while recording.

  // Previous input state written to disk; used to skip duplicate frames.
  GMT_InputState record_prev_input;

  // ----- REPLAY mode -----
  GMT_DecodedInput* replay_inputs;
  size_t replay_input_count;
  size_t replay_input_cursor;  // Index of next input record to inject.

  GMT_DecodedSignal* replay_signals;
  size_t replay_signal_count;
  size_t replay_signal_cursor;  // Index of next expected signal.

  // Previous per-frame input state, used to compute deltas for injection.
  GMT_InputState replay_prev_input;

  // Current replayed input state for this frame.  Updated by InjectInput each
  // frame and read by the hooked Win32 input functions (GetAsyncKeyState etc.)
  // so that polling-based games see the replayed state instead of real hardware.
  GMT_InputState replay_current_input;

  // True while the IAT hooks should return replayed state instead of calling
  // through to the original Win32 functions.
  bool replay_hooks_active;

  // Whether replay is blocked waiting for a game-side sync signal.
  bool waiting_for_signal;
  int32_t waiting_signal_id;

} GMT_State;

// Defined in GameTest.c.
extern GMT_State g_gmt;
