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

#pragma once

#include "InputState.h"

typedef uint8_t GameTest_InputPlayer_ModeBits;
typedef enum GameTest_InputPlayer_Mode {
  GameTest_InputPlayer_Mode_NO_BUTTONS = (1 << 0),
  GameTest_InputPlayer_Mode_NO_KEYS = (1 << 1),
  GameTest_InputPlayer_Mode_NO_MOUSE = (1 << 2),
  GameTest_InputPlayer_Mode_NO_TEXT = (1 << 3),
  GameTest_InputPlayer_Mode_REPEAT = (1 << 4),     // Loop back to the beginning of the file when the end is reached, instead of stopping playback.
  GameTest_InputPlayer_Mode_REVERSE = (1 << 5),    // Play the input in reverse (e.g. for rewinding). This will be ignored if REPEAT is not set, since it doesn't make sense to reverse a non-looping playback.
  GameTest_InputPlayer_Mode_PING_PONG = (1 << 6),  // When the end of the file is reached, reverse direction and play backwards until the beginning is reached, then reverse again, etc. This implies REPEAT.
} GameTest_InputPlayer_Mode;

// Playback input from a file. Returns true on success.
// Only one playback can be active at a time.
// If a playback is already active, this will fail.
// The file remains open until Stop() is called.
GAME_TEST_API bool GameTest_InputPlayer_Start(const char* filename, GameTest_InputPlayer_ModeBits mode);

// Stop playback input. Returns true on success.
GAME_TEST_API bool GameTest_InputPlayer_Stop(void);

// Update the player (e.g. read the current frame's input from the file). Returns true on success.
// This should be called once per frame while playback is active.
// You can pause/resume playback by skipping calls to Update() while keeping the file open.
GAME_TEST_API bool GameTest_InputPlayer_Update(void);

// Returns true if playback is currently active (i.e. a file is open and Update() should be called).
GAME_TEST_API bool GameTest_InputPlayer_IsPlaying(void);

// Get the current frame number (0-based) and total number of frames in the file. Returns -1 on error (e.g. if no file is open).
// These always reference the original file's frame count and position, even if REPEAT or PING_PONG mode is enabled and the player is currently looping back to the beginning of the file.
GAME_TEST_API int GameTest_InputPlayer_GetCurrentFrame(void);
GAME_TEST_API int GameTest_InputPlayer_GetTotalFrames(void);

// Returns how many times the player has looped back to the beginning of the file (0-based). This is only relevant if REPEAT or PING_PONG mode is enabled. Returns -1 on error (e.g. if no file is open).
GAME_TEST_API int GameTest_InputPlayer_GetCurrentLoopCount(void);