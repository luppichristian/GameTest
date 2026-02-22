/*
 * Record.h - Internal recording/replay engine declarations.
 *
 * These functions are called by GameTest.c and Signal.c.
 * They must not be called before GMT_Init completes setup.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Opens the test file for streaming write and writes the file header.
// Called during GMT_Init when mode == GMT_Mode_RECORD.
bool GMT_Record_OpenForWrite(void);

// Writes the TAG_END marker and closes the record file.
// Called during GMT_Quit when mode == GMT_Mode_RECORD.
void GMT_Record_CloseWrite(void);

// Captures system input and appends a TAG_INPUT record to the open record file.
// Called once per GMT_Update in RECORD mode.
void GMT_Record_WriteInput(void);

// Same as GMT_Record_WriteInput but can be called from platform hooks (e.g. keyboard
// LL hook) to capture key events with sub-frame accuracy. Handles its own mutex.
// Call when a real (non-injected) key down/up is observed to avoid missing fast taps.
void GMT_Record_WriteInputFromKeyEvent(void);

// Appends a TAG_SIGNAL record for the given signal id at the current timestamp.
// Called from GMT_SyncSignal_ in RECORD mode.
void GMT_Record_WriteSignal(int32_t signal_id);

// Appends a TAG_PIN or TAG_TRACK record with the given key, sequential index, and raw payload.
// Called from GMT_Pin_* and GMT_Track_* in RECORD mode.
void GMT_Record_WriteDataRecord(uint8_t tag, unsigned int key, unsigned int index, const void* data, size_t size);

// Searches a decoded pin/track array for an entry matching (key, index).
// Returns a pointer to the matching GMT_DecodedDataRecord, or NULL if not found.
GMT_DecodedDataRecord* GMT_Record_FindDecoded(GMT_DecodedDataRecord* arr, size_t count, unsigned int key, unsigned int index);

// Loads the test file and decodes all input and signal records into g_gmt arrays.
// Allocates replay_inputs and replay_signals via GMT_Alloc.
// Called during GMT_Init when mode == GMT_Mode_REPLAY.
bool GMT_Record_LoadReplay(void);

// Frees replay_inputs and replay_signals if allocated.
// Called during GMT_Quit when mode == GMT_Mode_REPLAY.
void GMT_Record_FreeReplay(void);

// Injects the input snapshot for the current replay input record and advances the cursor.
// Handles sync-signal gating: refuses to inject if waiting_for_signal is set.
// Called once per GMT_Update in REPLAY mode.
void GMT_Record_InjectInput(void);

// Returns metrics computed from the currently loaded replay data (g_gmt arrays).
// Call only after a successful GMT_Record_LoadReplay.
GMT_FileMetrics GMT_Record_GetReplayMetrics(void);

// Returns metrics computed from the currently open recording file (g_gmt.record_file).
// Estimates input_count from the file size.  Call before GMT_Record_CloseWrite.
GMT_FileMetrics GMT_Record_GetRecordMetrics(void);

