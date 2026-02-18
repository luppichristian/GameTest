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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "GameTest/InputState.h"

// ---------------------------------------------------------------------------
// Key mapping: GameTest_Key (index) → Win32 Virtual Key code (0 = unmapped)
// Keep in sync with the GameTest_Key enum in InputState.h.
// ---------------------------------------------------------------------------
static const UINT s_keyToVK[GameTest_Key_MAX] = {
    /* A–Z : 0–25 */
    'A',
    'B',
    'C',
    'D',
    'E',
    'F',
    'G',
    'H',
    'I',
    'J',
    'K',
    'L',
    'M',
    'N',
    'O',
    'P',
    'Q',
    'R',
    'S',
    'T',
    'U',
    'V',
    'W',
    'X',
    'Y',
    'Z',
    /* 0–9 : 26–35 */
    '0',
    '1',
    '2',
    '3',
    '4',
    '5',
    '6',
    '7',
    '8',
    '9',
    /* F1–F24 : 36–59 */
    VK_F1,
    VK_F2,
    VK_F3,
    VK_F4,
    VK_F5,
    VK_F6,
    VK_F7,
    VK_F8,
    VK_F9,
    VK_F10,
    VK_F11,
    VK_F12,
    VK_F13,
    VK_F14,
    VK_F15,
    VK_F16,
    VK_F17,
    VK_F18,
    VK_F19,
    VK_F20,
    VK_F21,
    VK_F22,
    VK_F23,
    VK_F24,
    /* Modifier keys : 60–67 */
    VK_LSHIFT,
    VK_RSHIFT,
    VK_LCONTROL,
    VK_RCONTROL,
    VK_LMENU,
    VK_RMENU,
    VK_LWIN,
    VK_RWIN,
    /* Control keys : 68–80 */
    VK_ESCAPE,
    VK_RETURN,
    VK_TAB,
    VK_BACK,
    VK_DELETE,
    VK_INSERT,
    VK_SPACE,
    VK_CAPITAL,
    VK_NUMLOCK,
    VK_SCROLL,
    VK_SNAPSHOT,
    VK_PAUSE,
    VK_APPS,
    /* Navigation : 81–84 */
    VK_HOME,
    VK_END,
    VK_PRIOR,
    VK_NEXT,
    /* Arrow keys : 85–88 */
    VK_UP,
    VK_DOWN,
    VK_LEFT,
    VK_RIGHT,
    /* Numpad : 89–105 */
    VK_NUMPAD0,
    VK_NUMPAD1,
    VK_NUMPAD2,
    VK_NUMPAD3,
    VK_NUMPAD4,
    VK_NUMPAD5,
    VK_NUMPAD6,
    VK_NUMPAD7,
    VK_NUMPAD8,
    VK_NUMPAD9,
    VK_ADD,
    VK_SUBTRACT,
    VK_MULTIPLY,
    VK_DIVIDE,
    VK_DECIMAL,
    VK_RETURN, /* NUMPAD_ENTER shares VK_RETURN */
    0,         /* NUMPAD_EQUAL – no Win32 VK */
    /* Punctuation / symbols : 106–116 */
    VK_OEM_MINUS,  /* -  */
    VK_OEM_PLUS,   /* =  */
    VK_OEM_4,      /* [  */
    VK_OEM_6,      /* ]  */
    VK_OEM_1,      /* ;  */
    VK_OEM_7,      /* '  */
    VK_OEM_3,      /* `  */
    VK_OEM_COMMA,  /* ,  */
    VK_OEM_PERIOD, /* .  */
    VK_OEM_2,      /* /  */
    VK_OEM_5,      /* \  */
    /* Media keys : 117–123 */
    VK_MEDIA_PLAY_PAUSE,
    VK_MEDIA_STOP,
    VK_MEDIA_NEXT_TRACK,
    VK_MEDIA_PREV_TRACK,
    VK_VOLUME_MUTE,
    VK_VOLUME_UP,
    VK_VOLUME_DOWN,
    /* Browser shortcut keys : 124–130 */
    VK_BROWSER_BACK,
    VK_BROWSER_FORWARD,
    VK_BROWSER_REFRESH,
    VK_BROWSER_STOP,
    VK_BROWSER_SEARCH,
    VK_BROWSER_FAVORITES,
    VK_BROWSER_HOME,
    /* System : 131 */
    VK_SLEEP,
    /* Indices 132–254 : intentionally 0 (unmapped) */
};

// ---------------------------------------------------------------------------
// Returns true for VKs that require the KEYEVENTF_EXTENDEDKEY flag when
// injected via SendInput / keybd_event.
// ---------------------------------------------------------------------------
static inline bool VkIsExtended(UINT vk) {
  switch (vk) {
    case VK_RCONTROL:
    case VK_RMENU:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_DIVIDE: /* Numpad / */
    case VK_NUMLOCK:
      return true;
    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// Sentinel written into dwExtraInfo for every SendInput call made by the
// InputPlayer.  The low-level hooks check for this value to distinguish
// injected (playback) events from real user input, which is blocked.
// ---------------------------------------------------------------------------
#define PLAYER_SENTINEL ((ULONG_PTR)0x47414D54) /* "GAMT" */
