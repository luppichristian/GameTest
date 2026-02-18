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

#include <string.h>
#include "UtilityWin32.h"
#include "GameTest/InputRecorder.h"

static FILE*         s_file           = NULL;
static bool          s_recording      = false;
static LARGE_INTEGER s_startTime;
static LARGE_INTEGER s_frequency;
static HHOOK         s_kbHook         = NULL;
static HHOOK         s_mouseHook      = NULL;
static HANDLE        s_hookThread     = NULL;
static DWORD         s_hookThreadId   = 0;
static HANDLE        s_hookReadyEvent = NULL;

static volatile float s_scrollDeltaX = 0.0f;
static volatile float s_scrollDeltaY = 0.0f;
static volatile uint8_t s_repeatCountVK[256];
static volatile uint8_t s_prevKeyDownVK[256];
static volatile uint32_t s_textInput[32];
static volatile size_t s_textInputCount = 0;
static CRITICAL_SECTION s_cs;

// ---------------------------------------------------------------------------
// Low-level keyboard hook
// ---------------------------------------------------------------------------

static LRESULT CALLBACK KbHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION) {
    const KBDLLHOOKSTRUCT* kb = (const KBDLLHOOKSTRUCT*)lParam;
    const DWORD vk = kb->vkCode;
    const bool isUp = (kb->flags & LLKHF_UP) != 0;

    EnterCriticalSection(&s_cs);

    if (!isUp) {
      if (s_prevKeyDownVK[vk]) {
        /* Key-repeat: the key was already held */
        if (s_repeatCountVK[vk] < 255)
          s_repeatCountVK[vk]++;
      } else {
        /* First press */
        s_prevKeyDownVK[vk] = 1;
      }

      /* Translate to Unicode character and accumulate text input. */
      if (s_textInputCount < 32) {
        BYTE ks[256];
        GetKeyboardState(ks);
        WCHAR buf[4] = {0};
        int n = ToUnicode(vk, kb->scanCode, ks, buf, 4, 0);
        if (n > 0) {
          /* Naïve UTF-16 → UTF-32 conversion (BMP only). */
          for (int i = 0; i < n && s_textInputCount < 32; i++) {
            if (buf[i] >= 32) /* skip control characters */
              s_textInput[s_textInputCount++] = (uint32_t)buf[i];
          }
        }
      }
    } else {
      s_prevKeyDownVK[vk] = 0;
    }

    LeaveCriticalSection(&s_cs);
  }
  return CallNextHookEx(s_kbHook, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Low-level mouse hook
// ---------------------------------------------------------------------------

static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION) {
    const MSLLHOOKSTRUCT* ms = (const MSLLHOOKSTRUCT*)lParam;

    EnterCriticalSection(&s_cs);

    if (wParam == WM_MOUSEWHEEL) {
      const short delta = (short)HIWORD(ms->mouseData);
      s_scrollDeltaY += delta / 120.0f;
    } else if (wParam == WM_MOUSEHWHEEL) {
      const short delta = (short)HIWORD(ms->mouseData);
      s_scrollDeltaX += delta / 120.0f;
    }

    LeaveCriticalSection(&s_cs);
  }
  return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Sample one GameTest_InputState from the current Win32 state.
// ---------------------------------------------------------------------------

static void SampleInputState(GameTest_InputState* out) {
  // mouseX/mouseY are stored as ABSOLUTE SCREEN coordinates because this
  // library owns no window handle.  Consumers can subtract the window origin if
  // they need client-area coordinates.

  /* Timestamp ----------------------------------------------------------- */
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  out->timestamp =
      (float)((double)(now.QuadPart - s_startTime.QuadPart) /
              (double)s_frequency.QuadPart);

  /* Mouse position ------------------------------------------------------ */
  POINT pt = {0, 0};
  GetCursorPos(&pt);
  out->mouseX = (int32_t)pt.x;
  out->mouseY = (int32_t)pt.y;

  /* Mouse buttons ------------------------------------------------------- */
  out->buttonsDownBits = 0;
  if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) out->buttonsDownBits |= GameTest_Button_LEFT;
  if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) out->buttonsDownBits |= GameTest_Button_RIGHT;
  if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) out->buttonsDownBits |= GameTest_Button_MIDDLE;
  if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) out->buttonsDownBits |= GameTest_Button_EXTRA1;
  if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) out->buttonsDownBits |= GameTest_Button_EXTRA2;

  /* Keyboard ------------------------------------------------------------ */
  for (int key = 0; key < GameTest_Key_MAX; key++) {
    const UINT vk = s_keyToVK[key];
    if (vk == 0) {
      out->keys[key].isDown = 0;
      out->keys[key].repeatCount = 0;
      continue;
    }
    out->keys[key].isDown = (GetAsyncKeyState((int)vk) & 0x8000) ? 1 : 0;
    /* repeatCount is filled from the LL-hook accumulator (see below). */
  }

  /* Consume hook accumulators ------------------------------------------ */
  EnterCriticalSection(&s_cs);

  out->scrollDeltaX = s_scrollDeltaX;
  out->scrollDeltaY = s_scrollDeltaY;
  s_scrollDeltaX = 0.0f;
  s_scrollDeltaY = 0.0f;

  out->textInputCount = s_textInputCount;
  memcpy((void*)out->textInput, (const void*)s_textInput, s_textInputCount * sizeof(uint32_t));
  s_textInputCount = 0;

  /* Copy per-VK repeat counts into the per-GameTest_Key repeat fields. */
  for (int key = 0; key < GameTest_Key_MAX; key++) {
    const UINT vk = s_keyToVK[key];
    if (vk < 256) {
      out->keys[key].repeatCount = s_repeatCountVK[vk];
      s_repeatCountVK[vk] = 0;
    }
  }

  LeaveCriticalSection(&s_cs);
}

// ---------------------------------------------------------------------------
// Dedicated hook thread – owns the LL hooks and runs a message loop to keep
// them alive, completely isolated from the calling thread's message queue.
// ---------------------------------------------------------------------------

static DWORD WINAPI RecorderHookThreadProc(LPVOID unused) {
  (void)unused;
  s_kbHook    = SetWindowsHookExW(WH_KEYBOARD_LL, KbHookProc,    NULL, 0);
  s_mouseHook = SetWindowsHookExW(WH_MOUSE_LL,    MouseHookProc, NULL, 0);
  /* Signal Start() – hooks are now installed (or failed). */
  SetEvent(s_hookReadyEvent);
  /* Run the message loop that keeps LL hook callbacks firing. */
  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (s_kbHook)    { UnhookWindowsHookEx(s_kbHook);    s_kbHook    = NULL; }
  if (s_mouseHook) { UnhookWindowsHookEx(s_mouseHook); s_mouseHook = NULL; }
  return 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

GAME_TEST_API bool GameTest_InputRecorder_Start(const char* filename) {
  if (s_recording) {
    return false;
  }

  if (!filename) {
    return false;
  }

  s_file = fopen(filename, "wb");
  if (!s_file) {
    return false;
  }

  InitializeCriticalSection(&s_cs);
  memset((void*)s_repeatCountVK, 0, sizeof(s_repeatCountVK));
  memset((void*)s_prevKeyDownVK, 0, sizeof(s_prevKeyDownVK));
  s_scrollDeltaX = 0.0f;
  s_scrollDeltaY = 0.0f;
  s_textInputCount = 0;

  QueryPerformanceFrequency(&s_frequency);
  QueryPerformanceCounter(&s_startTime);

  s_hookReadyEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
  if (!s_hookReadyEvent) { fclose(s_file); s_file = NULL; DeleteCriticalSection(&s_cs); return false; }

  s_hookThread = CreateThread(NULL, 0, RecorderHookThreadProc, NULL, 0, &s_hookThreadId);
  if (!s_hookThread) {
    CloseHandle(s_hookReadyEvent); s_hookReadyEvent = NULL;
    fclose(s_file); s_file = NULL;
    DeleteCriticalSection(&s_cs);
    return false;
  }

  /* Wait until RecorderHookThreadProc has called SetWindowsHookExW. */
  WaitForSingleObject(s_hookReadyEvent, INFINITE);
  CloseHandle(s_hookReadyEvent); s_hookReadyEvent = NULL;

  if (!s_kbHook || !s_mouseHook) {
    PostThreadMessageW(s_hookThreadId, WM_QUIT, 0, 0);
    WaitForSingleObject(s_hookThread, INFINITE);
    CloseHandle(s_hookThread); s_hookThread = NULL; s_hookThreadId = 0;
    fclose(s_file); s_file = NULL;
    DeleteCriticalSection(&s_cs);
    return false;
  }

  s_recording = true;
  return true;
}

GAME_TEST_API bool GameTest_InputRecorder_Stop(void) {
  if (!s_recording) return false;

  /* Ask the hook thread to exit, then wait for it to UnhookWindowsHookEx. */
  PostThreadMessageW(s_hookThreadId, WM_QUIT, 0, 0);
  WaitForSingleObject(s_hookThread, INFINITE);
  CloseHandle(s_hookThread); s_hookThread = NULL; s_hookThreadId = 0;

  fclose(s_file);
  s_file      = NULL;
  s_recording = false;
  DeleteCriticalSection(&s_cs);
  return true;
}

GAME_TEST_API bool GameTest_InputRecorder_Update(void) {
  if (!s_recording) return false;

  /* The hook thread drives its own message loop – no pump needed here.
     Just sample the current input state and write it to the file. */
  GameTest_InputState state;
  memset(&state, 0, sizeof(state));
  SampleInputState(&state);

  if (fwrite(&state, sizeof(state), 1, s_file) != 1) {
    return false;
  }
  fflush(s_file);
  return true;
}
