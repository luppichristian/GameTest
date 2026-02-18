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
#include "GameTest/InputPlayer.h"

static FILE*               s_file           = NULL;
static bool                s_playing        = false;
static bool                s_inputBlocked   = false;
static HHOOK               s_kbHook         = NULL;
static HHOOK               s_mouseHook      = NULL;
static HANDLE              s_hookThread     = NULL;
static DWORD               s_hookThreadId   = 0;
static HANDLE              s_hookReadyEvent = NULL;
static GameTest_InputState s_prevState;

// ---------------------------------------------------------------------------
// Low-level hooks – block all real input, pass through our injected events
// ---------------------------------------------------------------------------
static LRESULT CALLBACK PlayerKbHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION) {
    const KBDLLHOOKSTRUCT* kb = (const KBDLLHOOKSTRUCT*)lParam;
    if (kb->dwExtraInfo != PLAYER_SENTINEL)
      return 1; /* eat real keystrokes */
  }
  return CallNextHookEx(s_kbHook, nCode, wParam, lParam);
}

static LRESULT CALLBACK PlayerMouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION) {
    const MSLLHOOKSTRUCT* ms = (const MSLLHOOKSTRUCT*)lParam;
    if (ms->dwExtraInfo != PLAYER_SENTINEL)
      return 1; /* eat real mouse events */
  }
  return CallNextHookEx(s_mouseHook, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// Input injection helpers
// ---------------------------------------------------------------------------

static void InjectKeyDown(UINT vk) {
  if (vk == 0) return;
  INPUT inp = {0};
  inp.type = INPUT_KEYBOARD;
  inp.ki.wVk = (WORD)vk;
  inp.ki.dwFlags = VkIsExtended(vk) ? KEYEVENTF_EXTENDEDKEY : 0;
  inp.ki.dwExtraInfo = PLAYER_SENTINEL;
  SendInput(1, &inp, (int)sizeof(INPUT));
}

static void InjectKeyUp(UINT vk) {
  if (vk == 0) return;
  INPUT inp = {0};
  inp.type = INPUT_KEYBOARD;
  inp.ki.wVk = (WORD)vk;
  inp.ki.dwFlags = KEYEVENTF_KEYUP |
                   (VkIsExtended(vk) ? KEYEVENTF_EXTENDEDKEY : 0);
  inp.ki.dwExtraInfo = PLAYER_SENTINEL;
  SendInput(1, &inp, (int)sizeof(INPUT));
}

static void InjectUnicodeChar(uint32_t codepoint) {
  if (codepoint == 0) return;
  INPUT inp = {0};
  inp.type = INPUT_KEYBOARD;
  inp.ki.wScan = (WORD)codepoint;
  inp.ki.dwFlags = KEYEVENTF_UNICODE;
  inp.ki.dwExtraInfo = PLAYER_SENTINEL;
  SendInput(1, &inp, (int)sizeof(INPUT));

  /* Key-up for the unicode event */
  inp.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
  SendInput(1, &inp, (int)sizeof(INPUT));
}

static void InjectMouseMove(int32_t screenX, int32_t screenY) {
  const int vsLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
  const int vsTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int vsWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int vsHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

  if (vsWidth == 0 || vsHeight == 0) return;

  INPUT inp = {0};
  inp.type = INPUT_MOUSE;
  inp.mi.dx = (LONG)(((screenX - vsLeft) * 65535) / vsWidth);
  inp.mi.dy = (LONG)(((screenY - vsTop) * 65535) / vsHeight);
  inp.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
  inp.mi.dwExtraInfo = PLAYER_SENTINEL;
  SendInput(1, &inp, (int)sizeof(INPUT));
}

static void InjectMouseButton(DWORD downFlag, DWORD upFlag, bool pressed) {
  INPUT inp = {0};
  inp.type = INPUT_MOUSE;
  inp.mi.dwFlags = pressed ? downFlag : upFlag;
  inp.mi.dwExtraInfo = PLAYER_SENTINEL;
  SendInput(1, &inp, (int)sizeof(INPUT));
}

static void InjectMouseScroll(float deltaX, float deltaY) {
  if (deltaY != 0.0f) {
    INPUT inp = {0};
    inp.type = INPUT_MOUSE;
    inp.mi.mouseData = (DWORD)(int)(deltaY * 120.0f);
    inp.mi.dwFlags = MOUSEEVENTF_WHEEL;
    inp.mi.dwExtraInfo = PLAYER_SENTINEL;
    SendInput(1, &inp, (int)sizeof(INPUT));
  }
  if (deltaX != 0.0f) {
    INPUT inp = {0};
    inp.type = INPUT_MOUSE;
    inp.mi.mouseData = (DWORD)(int)(deltaX * 120.0f);
    inp.mi.dwFlags = MOUSEEVENTF_HWHEEL;
    inp.mi.dwExtraInfo = PLAYER_SENTINEL;
    SendInput(1, &inp, (int)sizeof(INPUT));
  }
}

// ---------------------------------------------------------------------------
// Synthesise events: diff the new frame against the previous frame and inject
// the minimum set of Win32 input events needed to reproduce it.
// ---------------------------------------------------------------------------
static void SynthesizeFrame(const GameTest_InputState* prev,
                            const GameTest_InputState* next) {
  /* Mouse movement ----------------------------------------------------- */
  if (next->mouseX != prev->mouseX || next->mouseY != prev->mouseY)
    InjectMouseMove(next->mouseX, next->mouseY);

  /* Mouse buttons ------------------------------------------------------ */
  const GameTest_ButtonBits added = (GameTest_ButtonBits)(next->buttonsDownBits & ~prev->buttonsDownBits);
  const GameTest_ButtonBits removed = (GameTest_ButtonBits)(prev->buttonsDownBits & ~next->buttonsDownBits);

#define HANDLE_BUTTON(bit, downF, upF)                        \
  if (added & (bit)) InjectMouseButton((downF), (upF), true); \
  if (removed & (bit)) InjectMouseButton((downF), (upF), false)

  HANDLE_BUTTON(GameTest_Button_LEFT, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP);
  HANDLE_BUTTON(GameTest_Button_RIGHT, MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP);
  HANDLE_BUTTON(GameTest_Button_MIDDLE, MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP);
#undef HANDLE_BUTTON

  /* XBUTTON1 / XBUTTON2 require mouseData to identify the button. */
  if ((added | removed) & (GameTest_Button_EXTRA1 | GameTest_Button_EXTRA2)) {
    INPUT inp = {0};
    inp.type = INPUT_MOUSE;
    inp.mi.dwExtraInfo = PLAYER_SENTINEL;

    if (added & GameTest_Button_EXTRA1) {
      inp.mi.mouseData = XBUTTON1;
      inp.mi.dwFlags = MOUSEEVENTF_XDOWN;
      SendInput(1, &inp, (int)sizeof(INPUT));
    }
    if (removed & GameTest_Button_EXTRA1) {
      inp.mi.mouseData = XBUTTON1;
      inp.mi.dwFlags = MOUSEEVENTF_XUP;
      SendInput(1, &inp, (int)sizeof(INPUT));
    }
    if (added & GameTest_Button_EXTRA2) {
      inp.mi.mouseData = XBUTTON2;
      inp.mi.dwFlags = MOUSEEVENTF_XDOWN;
      SendInput(1, &inp, (int)sizeof(INPUT));
    }
    if (removed & GameTest_Button_EXTRA2) {
      inp.mi.mouseData = XBUTTON2;
      inp.mi.dwFlags = MOUSEEVENTF_XUP;
      SendInput(1, &inp, (int)sizeof(INPUT));
    }
  }

  /* Scroll ------------------------------------------------------------- */
  InjectMouseScroll(next->scrollDeltaX, next->scrollDeltaY);

  /* Keyboard key transitions + repeats --------------------------------- */
  for (int key = 0; key < GameTest_Key_MAX; key++) {
    const UINT vk = s_keyToVK[key];
    const bool wasDown = prev->keys[key].isDown != 0;
    const bool isDown = next->keys[key].isDown != 0;

    if (!wasDown && isDown) {
      InjectKeyDown(vk);
    } else if (wasDown && !isDown) {
      InjectKeyUp(vk);
    } else if (isDown && next->keys[key].repeatCount > 0) {
      /* Key is held – inject the recorded number of repeat events. */
      for (uint8_t r = 0; r < next->keys[key].repeatCount; r++)
        InjectKeyDown(vk);
    }
  }

  /* Text / Unicode input ----------------------------------------------- */
  for (size_t i = 0; i < next->textInputCount; i++)
    InjectUnicodeChar(next->textInput[i]);
}

// ---------------------------------------------------------------------------
// Dedicated hook thread – owns the LL hooks and runs a message loop to keep
// them alive, completely isolated from the calling thread's message queue.
// ---------------------------------------------------------------------------

static DWORD WINAPI PlayerHookThreadProc(LPVOID unused) {
  (void)unused;
  s_kbHook    = SetWindowsHookExW(WH_KEYBOARD_LL, PlayerKbHookProc,    NULL, 0);
  s_mouseHook = SetWindowsHookExW(WH_MOUSE_LL,    PlayerMouseHookProc, NULL, 0);
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

GAME_TEST_API bool GameTest_InputPlayer_Start(const char* filename) {
  if (s_playing)  return false;
  if (!filename)  return false;

  s_file = fopen(filename, "rb");
  if (!s_file) return false;

  memset(&s_prevState, 0, sizeof(s_prevState));

  s_hookReadyEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
  if (!s_hookReadyEvent) { fclose(s_file); s_file = NULL; return false; }

  s_hookThread = CreateThread(NULL, 0, PlayerHookThreadProc, NULL, 0, &s_hookThreadId);
  if (!s_hookThread) {
    CloseHandle(s_hookReadyEvent); s_hookReadyEvent = NULL;
    fclose(s_file); s_file = NULL;
    return false;
  }

  /* Wait until PlayerHookThreadProc has called SetWindowsHookExW. */
  WaitForSingleObject(s_hookReadyEvent, INFINITE);
  CloseHandle(s_hookReadyEvent); s_hookReadyEvent = NULL;

  if (!s_kbHook || !s_mouseHook) {
    PostThreadMessageW(s_hookThreadId, WM_QUIT, 0, 0);
    WaitForSingleObject(s_hookThread, INFINITE);
    CloseHandle(s_hookThread); s_hookThread = NULL; s_hookThreadId = 0;
    fclose(s_file); s_file = NULL;
    return false;
  }

  /* Full physical input isolation: block hardware events from reaching
     the message queue and GetAsyncKeyState.
     Requires the process to be elevated; we continue even if it fails
     (the LL hooks still provide message-level isolation). */
  s_inputBlocked = BlockInput(TRUE);

  s_playing = true;
  return true;
}

GAME_TEST_API bool GameTest_InputPlayer_Stop(void) {
  if (!s_playing) return false;

  /* Release all keys/buttons that are still held so the app sees no stuck inputs. */
  GameTest_InputState empty;
  memset(&empty, 0, sizeof(empty));
  SynthesizeFrame(&s_prevState, &empty);

  /* Restore physical input before the hooks are torn down. */
  if (s_inputBlocked) { BlockInput(FALSE); s_inputBlocked = false; }

  /* Ask the hook thread to exit, then wait for it to UnhookWindowsHookEx. */
  PostThreadMessageW(s_hookThreadId, WM_QUIT, 0, 0);
  WaitForSingleObject(s_hookThread, INFINITE);
  CloseHandle(s_hookThread); s_hookThread = NULL; s_hookThreadId = 0;

  fclose(s_file);
  s_file    = NULL;
  s_playing = false;
  return true;
}

GAME_TEST_API bool GameTest_InputPlayer_Update(void) {
  if (!s_playing) return false;

  /* The hook thread drives its own message loop – no pump needed here.
     Just read the next recorded frame and inject it. */
  GameTest_InputState nextState;
  if (fread(&nextState, sizeof(nextState), 1, s_file) != 1) {
    /* End of file or read error – stop playback automatically. */
    GameTest_InputPlayer_Stop();
    return false;
  }

  SynthesizeFrame(&s_prevState, &nextState);
  s_prevState = nextState;
  return true;
}
