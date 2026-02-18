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

// Playback input from a file. Returns true on success.
// Only one playback can be active at a time.
// If a playback is already active, this will fail.
// The file remains open until Stop() is called.
GAME_TEST_API bool GameTest_InputPlayer_Start(const char* filename);

// Stop playback input. Returns true on success.
GAME_TEST_API bool GameTest_InputPlayer_Stop(void);

// Update the player (e.g. read the current frame's input from the file). Returns true on success.
// This should be called once per frame while playback is active.
// You can pause/resume playback by skipping calls to Update() while keeping the file open.
GAME_TEST_API bool GameTest_InputPlayer_Update(void);