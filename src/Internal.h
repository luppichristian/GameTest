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

// ===== Record File Format =====
//
// Layout:
//   [GMT_FileHeader]
//   N × tagged record:
//     TAG_FRAME  (0x01) → GMT_RawFrameRecord
//     TAG_SIGNAL (0x02) → GMT_RawSignalRecord
//   TAG_END (0xFF)       → (no body)
//
// All multi-byte integers are little-endian.

#define GMT_RECORD_MAGIC   0x54534D47u  // 'GMST' in memory (little-endian)
#define GMT_RECORD_VERSION 0u

#define GMT_RECORD_TAG_FRAME  ((uint8_t)0x01)
#define GMT_RECORD_TAG_SIGNAL ((uint8_t)0x02)
#define GMT_RECORD_TAG_END    ((uint8_t)0xFF)

// Fixed-size file header written at the start of every test file.
#pragma pack(push, 1)
typedef struct GMT_FileHeader {
  uint16_t magic;
  uint16_t version;
} GMT_FileHeader;

// Body of a TAG_FRAME record (written without the tag byte).
// GMT_InputState has no internal padding (key arrays size 104*2=208 aligns
// the int32_t fields naturally), so the binary layout is identical to the
// original flat field list and file-format compatibility is preserved.
typedef struct GMT_RawFrameRecord {
  uint64_t frame_index;
  GMT_InputState input;
} GMT_RawFrameRecord;

// Body of a TAG_SIGNAL record (written without the tag byte).
typedef struct GMT_RawSignalRecord {
  uint64_t frame_index;
  int32_t signal_id;
} GMT_RawSignalRecord;
#pragma pack(pop)

// ===== In-memory decoded records (used during REPLAY) =====

typedef struct GMT_DecodedFrame {
  uint64_t frame_index;
  GMT_InputState input;
} GMT_DecodedFrame;

typedef struct GMT_DecodedSignal {
  uint64_t frame_index;
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
  bool test_failed;

  // ----- Runtime -----
  // Monotonically increasing counter incremented by each GMT_Update call.
  uint64_t frame_index;

  // ----- RECORD mode -----
  FILE* record_file;  // Open for streaming write while recording.

  // ----- REPLAY mode -----
  GMT_DecodedFrame* replay_frames;
  size_t replay_frame_count;
  size_t replay_frame_cursor;  // Index of next frame to inject.

  GMT_DecodedSignal* replay_signals;
  size_t replay_signal_count;
  size_t replay_signal_cursor;  // Index of next expected signal.

  // Previous per-frame input state, used to compute deltas for injection.
  GMT_InputState replay_prev_input;

  // Whether replay is blocked waiting for a game-side sync signal.
  bool waiting_for_signal;
  int32_t waiting_signal_id;

} GMT_State;

// Defined in GameTest.c.
extern GMT_State g_gmt;
