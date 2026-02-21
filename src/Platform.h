/*
 * Platform.h - Platform abstraction layer (Win32 implementation in Platform__Win32.c).
 *
 * This header declares all OS-specific capabilities needed by the framework:
 * file I/O, directory operations, input capture/injection, and mutual exclusion.
 * No public GameTest types are used here; only primitive C types.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "GameTest.h"
#include "InputState.h"

// ===== Directory =====

// Sets the process working directory.
void GMT_Platform_SetWorkDir(const char* path);

// Creates `path` and all intermediate directories (like mkdir -p).
// Returns true on success or if the directory already exists.
bool GMT_Platform_CreateDirRecursive(const char* path);

// Installs any platform-specific hooks or initializations required.
void GMT_Platform_Init(void);

// Removes the hooks installed by GMT_Platform_Init.
void GMT_Platform_Quit(void);

// ===== Input Capture (RECORD mode) =====

// Captures the current keyboard and mouse state into *out.
// Key bytes are 0x80 if pressed, 0 otherwise.  key_repeats counts auto-repeat
// key-down events accumulated since the last call.  Mouse coordinates are
// absolute screen pixels.  Wheel deltas are accumulated since the last call
// (positive = right/up) then reset to zero.  mouse_buttons is a GMT_MouseButtons
// bitmask of currently pressed buttons.
void GMT_Platform_CaptureInput(GMT_InputState* out);

// ===== Input Injection (REPLAY mode) =====

// Injects a delta of input events for one frame.
//   new_input  : the target input snapshot for this frame.
//   prev_input : the snapshot from the previous frame (used to compute deltas).
// Only keys and buttons whose state changed are emitted so the application's
// input queue stays consistent.  Mouse position is set unconditionally.
void GMT_Platform_InjectInput(const GMT_InputState* new_input, const GMT_InputState* prev_input);

// ===== High-Resolution Timer =====

// Returns the current time in seconds from an arbitrary fixed epoch.
// Used to stamp recorded frames and signals with floating-point timestamps
// and to drive time-based replay.
double GMT_Platform_GetTime(void);

// ===== Mutex =====

// Lock/unlock the single framework-wide recursive mutex.
// The mutex is created in GMT_Platform_Init and destroyed in GMT_Platform_Quit.
void GMT_Platform_MutexLock(void);
void GMT_Platform_MutexUnlock(void);