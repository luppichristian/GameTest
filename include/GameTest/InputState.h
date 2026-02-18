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

#include "Base.h"

// Raw data buffer
typedef struct GameTest_RawInput {
  void* rawData;
  size_t rawDataSize;
} GameTest_RawInput;

// Mouse buttons
typedef uint8_t GameTest_ButtonBits;
typedef enum GameTest_Button {
  GameTest_Button_LEFT = (1 << 0),
  GameTest_Button_RIGHT = (1 << 1),
  GameTest_Button_MIDDLE = (1 << 2),
  GameTest_Button_EXTRA1 = (1 << 3),
  GameTest_Button_EXTRA2 = (1 << 4),
  GameTest_Button_EXTRA3 = (1 << 5),
  GameTest_Button_EXTRA4 = (1 << 6),
  GameTest_Button_EXTRA5 = (1 << 7),
} GameTest_Button;

// Keyboard keys
typedef enum GameTest_Key {
  // Letters
  GameTest_Key_A = 0,
  GameTest_Key_B,
  GameTest_Key_C,
  GameTest_Key_D,
  GameTest_Key_E,
  GameTest_Key_F,
  GameTest_Key_G,
  GameTest_Key_H,
  GameTest_Key_I,
  GameTest_Key_J,
  GameTest_Key_K,
  GameTest_Key_L,
  GameTest_Key_M,
  GameTest_Key_N,
  GameTest_Key_O,
  GameTest_Key_P,
  GameTest_Key_Q,
  GameTest_Key_R,
  GameTest_Key_S,
  GameTest_Key_T,
  GameTest_Key_U,
  GameTest_Key_V,
  GameTest_Key_W,
  GameTest_Key_X,
  GameTest_Key_Y,
  GameTest_Key_Z,

  // Digits (top row)
  GameTest_Key_0,
  GameTest_Key_1,
  GameTest_Key_2,
  GameTest_Key_3,
  GameTest_Key_4,
  GameTest_Key_5,
  GameTest_Key_6,
  GameTest_Key_7,
  GameTest_Key_8,
  GameTest_Key_9,

  // Function keys (F1-F24)
  GameTest_Key_F1,
  GameTest_Key_F2,
  GameTest_Key_F3,
  GameTest_Key_F4,
  GameTest_Key_F5,
  GameTest_Key_F6,
  GameTest_Key_F7,
  GameTest_Key_F8,
  GameTest_Key_F9,
  GameTest_Key_F10,
  GameTest_Key_F11,
  GameTest_Key_F12,
  GameTest_Key_F13,
  GameTest_Key_F14,
  GameTest_Key_F15,
  GameTest_Key_F16,
  GameTest_Key_F17,
  GameTest_Key_F18,
  GameTest_Key_F19,
  GameTest_Key_F20,
  GameTest_Key_F21,
  GameTest_Key_F22,
  GameTest_Key_F23,
  GameTest_Key_F24,

  // Modifier keys (left/right variants)
  GameTest_Key_LEFT_SHIFT,
  GameTest_Key_RIGHT_SHIFT,
  GameTest_Key_LEFT_CTRL,
  GameTest_Key_RIGHT_CTRL,
  GameTest_Key_LEFT_ALT,
  GameTest_Key_RIGHT_ALT,
  GameTest_Key_LEFT_SUPER,
  GameTest_Key_RIGHT_SUPER,

  // Control keys
  GameTest_Key_ESCAPE,
  GameTest_Key_ENTER,
  GameTest_Key_TAB,
  GameTest_Key_BACKSPACE,
  GameTest_Key_DELETE,
  GameTest_Key_INSERT,
  GameTest_Key_SPACE,
  GameTest_Key_CAPSLOCK,
  GameTest_Key_NUMLOCK,
  GameTest_Key_SCROLLLOCK,
  GameTest_Key_PRINTSCREEN,
  GameTest_Key_PAUSE,
  GameTest_Key_MENU,

  // Navigation
  GameTest_Key_HOME,
  GameTest_Key_END,
  GameTest_Key_PAGE_UP,
  GameTest_Key_PAGE_DOWN,

  // Arrow keys
  GameTest_Key_UP,
  GameTest_Key_DOWN,
  GameTest_Key_LEFT,
  GameTest_Key_RIGHT,

  // Numpad
  GameTest_Key_NUMPAD_0,
  GameTest_Key_NUMPAD_1,
  GameTest_Key_NUMPAD_2,
  GameTest_Key_NUMPAD_3,
  GameTest_Key_NUMPAD_4,
  GameTest_Key_NUMPAD_5,
  GameTest_Key_NUMPAD_6,
  GameTest_Key_NUMPAD_7,
  GameTest_Key_NUMPAD_8,
  GameTest_Key_NUMPAD_9,
  GameTest_Key_NUMPAD_ADD,
  GameTest_Key_NUMPAD_SUB,
  GameTest_Key_NUMPAD_MUL,
  GameTest_Key_NUMPAD_DIV,
  GameTest_Key_NUMPAD_DECIMAL,
  GameTest_Key_NUMPAD_ENTER,
  GameTest_Key_NUMPAD_EQUAL,

  // Punctuation / symbols
  GameTest_Key_MINUS,          // -
  GameTest_Key_EQUAL,          // =
  GameTest_Key_BRACKET_LEFT,   // [
  GameTest_Key_BRACKET_RIGHT,  // ]
  GameTest_Key_SEMICOLON,      // ;
  GameTest_Key_APOSTROPHE,     // '
  GameTest_Key_GRAVE,          // `
  GameTest_Key_COMMA,          // ,
  GameTest_Key_PERIOD,         // .
  GameTest_Key_SLASH,          // /
  GameTest_Key_BACKSLASH,      // \

  // Media keys
  GameTest_Key_MEDIA_PLAY_PAUSE,
  GameTest_Key_MEDIA_STOP,
  GameTest_Key_MEDIA_NEXT,
  GameTest_Key_MEDIA_PREV,
  GameTest_Key_MEDIA_MUTE,
  GameTest_Key_VOLUME_UP,
  GameTest_Key_VOLUME_DOWN,

  // Browser shortcut keys (present on multimedia keyboards)
  GameTest_Key_BROWSER_BACK,
  GameTest_Key_BROWSER_FORWARD,
  GameTest_Key_BROWSER_REFRESH,
  GameTest_Key_BROWSER_STOP,
  GameTest_Key_BROWSER_SEARCH,
  GameTest_Key_BROWSER_FAVORITES,
  GameTest_Key_BROWSER_HOME,

  // System
  GameTest_Key_SLEEP,

  GameTest_Key_MAX = 255,
} GameTest_Key;

// Key states
typedef struct GameTest_KeyState {
  uint8_t isDown;       // currently held
  uint8_t repeatCount;  // OS key-repeat events fired this frame (0 on first press, >0 while held)
} GameTest_KeyState;

// Input state
typedef struct GameTest_InputState {
  // Mouse buttons: bitmask of currently held buttons
  GameTest_ButtonBits buttonsDownBits;

  // Mouse position (absolute, in pixels relative to the window client area)
  int32_t mouseX;
  int32_t mouseY;

  // Scroll wheel delta (positive = scroll up/right)
  float scrollDeltaX;
  float scrollDeltaY;

  // Per-key states
  GameTest_KeyState keys[GameTest_Key_MAX];

  // Text input: UTF-32 codepoints entered this frame (handles IME, dead keys, etc.)
  uint32_t textInput[32];
  size_t textInputCount;

  // Raw input buffers for devices not covered by the structured fields above
  // (e.g. gamepads, joysticks, steering wheels, stylus).
  // NOTE: when recording, serialize the bytes rawData points to via rawDataSize,
  // not the pointer value itself.
  GameTest_RawInput* rawInputBuffers[32];
  size_t rawInputBufferCount;
} GameTest_InputState;

// Read an InputState from a file (e.g. a recording).
// The caller is responsible for freeing the returned InputState.
GAME_TEST_API GameTest_InputState* GameTest_InputState_Read(FILE* file);

// Duplicate an InputState (e.g. for storing in a recording).
GAME_TEST_API GameTest_InputState* GameTest_InputState_Duplicate(const GameTest_InputState* inputState);

// Create an empty InputState (e.g. for synthesizing input).
GAME_TEST_API GameTest_InputState* GameTest_InputState_Empty(void);

// Free an InputState.
GAME_TEST_API bool GameTest_InputState_Free(GameTest_InputState* inputState);

// Write an InputState to a file (e.g. for recording).
GAME_TEST_API bool GameTest_InputState_Write(const GameTest_InputState* inputState, FILE* file);

// Add a raw input buffer to an InputState (e.g. for recording gamepad input).
GAME_TEST_API bool GameTest_InputState_AddRawInput(
    GameTest_InputState* inputState,
    const void* rawData,
    size_t rawDataSize);

// Clear all raw input buffers from an InputState (e.g. to free memory after processing).
GAME_TEST_API bool GameTest_InputState_ClearRawInput(GameTest_InputState* inputState);

// Clear all input state (e.g. to reset after processing).
GAME_TEST_API bool GameTest_InputState_Clear(GameTest_InputState* inputState);