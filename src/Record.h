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

// Captures system input and appends a TAG_FRAME record to the open record file.
// Called once per GMT_Update in RECORD mode.
void GMT_Record_WriteFrame(void);

// Appends a TAG_SIGNAL record for the given signal id at the current timestamp.
// Called from GMT_SyncSignal_ in RECORD mode.
void GMT_Record_WriteSignal(int32_t signal_id);

// Loads the test file and decodes all frame and signal records into g_gmt arrays.
// Allocates replay_frames and replay_signals via GMT_Alloc.
// Called during GMT_Init when mode == GMT_Mode_REPLAY.
bool GMT_Record_LoadReplay(void);

// Frees replay_frames and replay_signals if allocated.
// Called during GMT_Quit when mode == GMT_Mode_REPLAY.
void GMT_Record_FreeReplay(void);

// Injects the input snapshot for the current replay frame and advances the cursor.
// Handles sync-signal gating: refuses to inject if waiting_for_signal is set.
// Called once per GMT_Update in REPLAY mode.
void GMT_Record_InjectFrame(void);
