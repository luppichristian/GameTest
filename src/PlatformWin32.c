/*
 * Platform__Win32.c - Win32 implementation of the platform abstraction layer.
 *
 * Covers: file I/O, directory management, keyboard/mouse capture and injection,
 * and CRITICAL_SECTION-based mutual exclusion.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdlib.h>
#include <string.h>

#include "Platform.h"

// ===== Mouse Wheel Accumulator =====
//
// WH_MOUSE_LL hook accumulates wheel deltas between frames.
// InterlockedExchangeAdd is used so CaptureInput can safely reset to 0
// while the hook (running on the same thread via the message pump) might fire
// concurrently on multi-threaded pumps.

static HHOOK g_mouse_hook = NULL;
static volatile LONG g_wheel_x = 0;  // Horizontal wheel accumulator.
static volatile LONG g_wheel_y = 0;  // Vertical wheel accumulator.

static CRITICAL_SECTION g_mutex;

static LRESULT CALLBACK GMT__MouseLLHook(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION) {
    MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lParam;
    if (wParam == WM_MOUSEWHEEL) {
      SHORT delta = (SHORT)HIWORD(ms->mouseData);
      InterlockedExchangeAdd(&g_wheel_y, (LONG)delta);
    } else if (wParam == WM_MOUSEHWHEEL) {
      SHORT delta = (SHORT)HIWORD(ms->mouseData);
      InterlockedExchangeAdd(&g_wheel_x, (LONG)delta);
    }
  }
  return CallNextHookEx(g_mouse_hook, nCode, wParam, lParam);
}

// ===== GMT_Key → Win32 Virtual Key mapping =====
//
// Indexed by GMT_Key value.  0 means "no Win32 mapping" (GMT_Key_UNKNOWN or any
// key that has no direct VK equivalent on this platform).
// The array must have exactly GMT_KEY_COUNT entries — verified at compile time below.

static const int k_vk[GMT_KEY_COUNT] = {
    0,  // GMT_Key_UNKNOWN

    // Letters (Win32 VK codes for A-Z equal their ASCII uppercase values)
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

    // Top-row digits (VK codes equal ASCII digit values)
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

    // Function keys
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

    // Arrow keys
    VK_UP,
    VK_DOWN,
    VK_LEFT,
    VK_RIGHT,

    // Navigation cluster
    VK_HOME,
    VK_END,
    VK_PRIOR,
    VK_NEXT,
    VK_INSERT,
    VK_DELETE,

    // Editing / whitespace
    VK_BACK,
    VK_TAB,
    VK_RETURN,
    VK_ESCAPE,
    VK_SPACE,
    VK_CAPITAL,

    // Modifiers
    VK_LSHIFT,
    VK_RSHIFT,
    VK_LCONTROL,
    VK_RCONTROL,
    VK_LMENU,
    VK_RMENU,
    VK_LWIN,
    VK_RWIN,

    // Numpad
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
    VK_DECIMAL,
    VK_ADD,
    VK_SUBTRACT,
    VK_MULTIPLY,
    VK_DIVIDE,
    VK_NUMLOCK,

    // Punctuation / symbols (US layout)
    VK_OEM_MINUS,   // -  (_)
    VK_OEM_PLUS,    // =  (+)  note: VK_OEM_PLUS is the = key, not the + key
    VK_OEM_4,       // [  ({)
    VK_OEM_6,       // ]  (})
    VK_OEM_5,       // \  (|)
    VK_OEM_1,       // ;  (:)
    VK_OEM_7,       // '  (")
    VK_OEM_COMMA,   // ,  (<)
    VK_OEM_PERIOD,  // .  (>)
    VK_OEM_2,       // /  (?)
    VK_OEM_3,       // `  (~)

    // Miscellaneous
    VK_SNAPSHOT,  // Print Screen
    VK_SCROLL,    // Scroll Lock
    VK_PAUSE,     // Pause / Break
    VK_APPS,      // Menu / Application key
};

// Compile-time check: the table must cover every GMT_Key.
typedef char GMT__VkTableSizeCheck[(sizeof(k_vk) / sizeof(k_vk[0]) == GMT_KEY_COUNT) ? 1 : -1];

// ===== Key Repeat Accumulator =====
//
// WH_KEYBOARD_LL hook counts auto-repeat key-down events between frames.
// A repeat is identified by seeing WM_KEYDOWN for a VK that the hook already
// considers "down" (i.e. a previous WM_KEYDOWN has not been followed by WM_KEYUP).
// Injected events (LLKHF_INJECTED) are ignored so replay-injected keystrokes do
// not pollute the accumulator.

static HHOOK g_keyboard_hook = NULL;
static volatile LONG g_key_repeats[GMT_KEY_COUNT];  // Per-GMT_Key repeat accumulator.
static int g_vk_to_gmt_key[256];                    // Reverse map: Win32 VK → GMT_Key (0 = unmapped).
static uint8_t g_hook_key_down[256];                // Per-VK "is currently down" state for the hook.

static LRESULT CALLBACK GMT__KeyboardLLHook(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION) {
    KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
    // Ignore events injected by our own replay to avoid feedback.
    if (!(kb->flags & LLKHF_INJECTED)) {
      DWORD vk = kb->vkCode;
      if (vk < 256) {
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
          if (g_hook_key_down[vk]) {
            // Key was already down — this is an auto-repeat event.
            int gmt_key = g_vk_to_gmt_key[vk];
            if (gmt_key > 0) {
              InterlockedExchangeAdd(&g_key_repeats[gmt_key], 1);
            }
          } else {
            g_hook_key_down[vk] = 1;
          }
        } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
          g_hook_key_down[vk] = 0;
        }
      }
    }
  }
  return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
}

void GMT_Platform_Init(void) {
  InitializeCriticalSection(&g_mutex);

  // Build the VK → GMT_Key reverse map from the forward k_vk[] table.
  memset(g_vk_to_gmt_key, 0, sizeof(g_vk_to_gmt_key));
  for (int k = 1; k < GMT_KEY_COUNT; ++k) {
    int vk = k_vk[k];
    if (vk > 0 && vk < 256) {
      g_vk_to_gmt_key[vk] = k;
    }
  }
  memset(g_hook_key_down, 0, sizeof(g_hook_key_down));
  memset((void*)g_key_repeats, 0, sizeof(g_key_repeats));

  if (!g_mouse_hook) {
    g_mouse_hook = SetWindowsHookExA(WH_MOUSE_LL, GMT__MouseLLHook, NULL, 0);
  }
  if (!g_keyboard_hook) {
    g_keyboard_hook = SetWindowsHookExA(WH_KEYBOARD_LL, GMT__KeyboardLLHook, NULL, 0);
  }
}

void GMT_Platform_Quit(void) {
  if (g_mouse_hook) {
    UnhookWindowsHookEx(g_mouse_hook);
    g_mouse_hook = NULL;
  }
  g_wheel_x = 0;
  g_wheel_y = 0;

  if (g_keyboard_hook) {
    UnhookWindowsHookEx(g_keyboard_hook);
    g_keyboard_hook = NULL;
  }
  memset(g_hook_key_down, 0, sizeof(g_hook_key_down));
  memset((void*)g_key_repeats, 0, sizeof(g_key_repeats));

  DeleteCriticalSection(&g_mutex);
}

// ===== Directory =====

void GMT_Platform_SetWorkDir(const char* path) {
  SetCurrentDirectoryA(path);
}

bool GMT_Platform_CreateDirRecursive(const char* path) {
  char buf[MAX_PATH];
  size_t len = strlen(path);
  if (len == 0 || len >= MAX_PATH) return false;

  memcpy(buf, path, len + 1);

  // Normalise separators.
  for (size_t i = 0; i < len; ++i) {
    if (buf[i] == '/') buf[i] = '\\';
  }

  // Walk each component and create it.
  for (size_t i = 1; i <= len; ++i) {
    if (buf[i] == '\\' || buf[i] == '\0') {
      char saved = buf[i];
      buf[i] = '\0';

      // Skip drive roots like "C:".
      if (i > 1) {
        BOOL ok = CreateDirectoryA(buf, NULL);
        if (!ok) {
          DWORD err = GetLastError();
          if (err != ERROR_ALREADY_EXISTS) {
            return false;
          }
        }
      }
      buf[i] = saved;
    }
  }
  return true;
}

// ===== Input Capture =====

void GMT_Platform_CaptureInput(GMT_InputState* out) {
  uint8_t vk_state[256];
  GetKeyboardState(vk_state);

  out->keys[GMT_Key_UNKNOWN] = 0;
  for (int k = 1; k < GMT_KEY_COUNT; ++k) {
    int vk = k_vk[k];
    out->keys[k] = (vk != 0) ? (vk_state[vk] & 0x80u) : 0;
  }

  // Read and reset per-key repeat accumulators atomically, clamping to uint8_t.
  out->key_repeats[GMT_Key_UNKNOWN] = 0;
  for (int k = 1; k < GMT_KEY_COUNT; ++k) {
    LONG r = InterlockedExchange(&g_key_repeats[k], 0);
    out->key_repeats[k] = (r > 255) ? 255 : (uint8_t)r;
  }

  POINT pt = {0, 0};
  GetCursorPos(&pt);
  out->mouse_x = (int32_t)pt.x;
  out->mouse_y = (int32_t)pt.y;

  // Atomically read and reset the wheel accumulators.
  out->mouse_wheel_x = (int32_t)InterlockedExchange(&g_wheel_x, 0);
  out->mouse_wheel_y = (int32_t)InterlockedExchange(&g_wheel_y, 0);

  GMT_MouseButtons buttons = 0;
  if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) buttons |= GMT_MouseButton_LEFT;
  if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) buttons |= GMT_MouseButton_RIGHT;
  if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) buttons |= GMT_MouseButton_MIDDLE;
  if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) buttons |= GMT_MouseButton_X1;
  if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) buttons |= GMT_MouseButton_X2;
  // Bits 5–7 (GMT_MouseButton_5/6/7) have no Win32 mapping; left as 0.
  out->mouse_buttons = buttons;
}

// ===== Input Injection =====

void GMT_Platform_InjectInput(const GMT_InputState* new_input,
                              const GMT_InputState* prev_input) {
  // Count total repeat events to size the dynamic buffer.
  int total_repeat_count = 0;
  for (int k = 1; k < GMT_KEY_COUNT; ++k) {
    total_repeat_count += new_input->key_repeats[k];
  }

  // Allocate: key transitions + repeat key-downs + 5 mouse buttons.
  int capacity = GMT_KEY_COUNT + total_repeat_count + 5;
  INPUT* inputs = (INPUT*)malloc((size_t)capacity * sizeof(INPUT));
  if (!inputs) return;
  UINT count = 0;

  // ---- Keyboard delta ----
  for (int k = 1; k < GMT_KEY_COUNT; ++k) {
    bool was = (prev_input->keys[k] & 0x80u) != 0;
    bool is = (new_input->keys[k] & 0x80u) != 0;
    if (was == is) continue;

    int vk = k_vk[k];
    if (vk == 0) continue;

    INPUT* inp = &inputs[count++];
    inp->type = INPUT_KEYBOARD;
    inp->ki.wVk = (WORD)vk;
    inp->ki.wScan = (WORD)MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
    inp->ki.dwFlags = is ? 0 : KEYEVENTF_KEYUP;
    inp->ki.time = 0;
    inp->ki.dwExtraInfo = 0;

    // Keys that require KEYEVENTF_EXTENDEDKEY on Win32.
    if (k == GMT_Key_RIGHT_CTRL || k == GMT_Key_RIGHT_ALT ||
        k == GMT_Key_LEFT_SUPER || k == GMT_Key_RIGHT_SUPER ||
        k == GMT_Key_INSERT || k == GMT_Key_DELETE ||
        k == GMT_Key_HOME || k == GMT_Key_END ||
        k == GMT_Key_PAGE_UP || k == GMT_Key_PAGE_DOWN ||
        k == GMT_Key_UP || k == GMT_Key_DOWN ||
        k == GMT_Key_LEFT || k == GMT_Key_RIGHT ||
        k == GMT_Key_NUM_LOCK || k == GMT_Key_KP_DIVIDE) {
      inp->ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    }
  }

  // ---- Keyboard repeats ----
  // Emit additional key-down events for keys that were held and generated auto-repeat events.
  for (int k = 1; k < GMT_KEY_COUNT; ++k) {
    int repeats = new_input->key_repeats[k];
    if (repeats == 0) continue;

    int vk = k_vk[k];
    if (vk == 0) continue;

    DWORD ext_flag = 0;
    if (k == GMT_Key_RIGHT_CTRL || k == GMT_Key_RIGHT_ALT ||
        k == GMT_Key_LEFT_SUPER || k == GMT_Key_RIGHT_SUPER ||
        k == GMT_Key_INSERT || k == GMT_Key_DELETE ||
        k == GMT_Key_HOME || k == GMT_Key_END ||
        k == GMT_Key_PAGE_UP || k == GMT_Key_PAGE_DOWN ||
        k == GMT_Key_UP || k == GMT_Key_DOWN ||
        k == GMT_Key_LEFT || k == GMT_Key_RIGHT ||
        k == GMT_Key_NUM_LOCK || k == GMT_Key_KP_DIVIDE) {
      ext_flag = KEYEVENTF_EXTENDEDKEY;
    }

    WORD scan = (WORD)MapVirtualKeyA((UINT)vk, MAPVK_VK_TO_VSC);
    for (int r = 0; r < repeats; ++r) {
      INPUT* inp = &inputs[count++];
      inp->type = INPUT_KEYBOARD;
      inp->ki.wVk = (WORD)vk;
      inp->ki.wScan = scan;
      inp->ki.dwFlags = ext_flag;  // Key-down (no KEYEVENTF_KEYUP).
      inp->ki.time = 0;
      inp->ki.dwExtraInfo = 0;
    }
  }

  // ---- Mouse buttons delta ----
  // L / R / M use dedicated flags; X1 and X2 share XDOWN/XUP with mouseData distinguishing them.
  // Bits 5–7 have no Win32 mapping and are not injected.
  static const struct {
    GMT_MouseButton flag;
    DWORD down_flag;
    DWORD up_flag;
    DWORD mouse_data;  // Non-zero only for XBUTTON events.
  } k_button_map[] = {
      {  GMT_MouseButton_LEFT,   MOUSEEVENTF_LEFTDOWN,   MOUSEEVENTF_LEFTUP,        0},
      { GMT_MouseButton_RIGHT,  MOUSEEVENTF_RIGHTDOWN,  MOUSEEVENTF_RIGHTUP,        0},
      {GMT_MouseButton_MIDDLE, MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_MIDDLEUP,        0},
      {    GMT_MouseButton_X1,      MOUSEEVENTF_XDOWN,      MOUSEEVENTF_XUP, XBUTTON1},
      {    GMT_MouseButton_X2,      MOUSEEVENTF_XDOWN,      MOUSEEVENTF_XUP, XBUTTON2},
  };
  static const int k_button_count = (int)(sizeof(k_button_map) / sizeof(k_button_map[0]));

  for (int b = 0; b < k_button_count; ++b) {
    bool was = (prev_input->mouse_buttons & k_button_map[b].flag) != 0;
    bool is = (new_input->mouse_buttons & k_button_map[b].flag) != 0;
    if (was == is) continue;

    INPUT* inp = &inputs[count++];
    inp->type = INPUT_MOUSE;
    inp->mi.dx = 0;
    inp->mi.dy = 0;
    inp->mi.mouseData = k_button_map[b].mouse_data;
    inp->mi.dwFlags = is ? k_button_map[b].down_flag : k_button_map[b].up_flag;
    inp->mi.time = 0;
    inp->mi.dwExtraInfo = 0;
  }

  if (count > 0) {
    SendInput(count, inputs, sizeof(INPUT));
  }
  free(inputs);

  // ---- Mouse wheel ----
  if (new_input->mouse_wheel_y != 0) {
    INPUT wi = {0};
    wi.type = INPUT_MOUSE;
    wi.mi.dwFlags = MOUSEEVENTF_WHEEL;
    wi.mi.mouseData = (DWORD)(LONG)new_input->mouse_wheel_y;
    SendInput(1, &wi, sizeof(INPUT));
  }
  if (new_input->mouse_wheel_x != 0) {
    INPUT wi = {0};
    wi.type = INPUT_MOUSE;
    wi.mi.dwFlags = MOUSEEVENTF_HWHEEL;
    wi.mi.mouseData = (DWORD)(LONG)new_input->mouse_wheel_x;
    SendInput(1, &wi, sizeof(INPUT));
  }

  // ---- Mouse position ----
  SetCursorPos((int)new_input->mouse_x, (int)new_input->mouse_y);
}

// ===== Mutex =====

void GMT_Platform_MutexLock(void) {
  EnterCriticalSection(&g_mutex);
}

void GMT_Platform_MutexUnlock(void) {
  LeaveCriticalSection(&g_mutex);
}
