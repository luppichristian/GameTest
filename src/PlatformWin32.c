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

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h> /* EnumProcessModules */
#include <xinput.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "winmm.lib")

#include "Internal.h"
#include "Platform.h"
#include "Record.h"

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

// ===== High-Resolution Timer =====

static double g_perf_freq_inv = 0.0;  // 1.0 / QueryPerformanceFrequency
static LARGE_INTEGER g_perf_origin;   // QPC value at GMT_Platform_Init; used as epoch

// ===== Crash / abort safety net globals =====
// Declared here so RemoveInputHooks (below) can reference them before the
// handler functions are defined.
static void (*g_prev_sigabrt_handler)(int) = NULL;
static LPTOP_LEVEL_EXCEPTION_FILTER g_prev_exception_filter = NULL;
static bool g_exception_filter_installed = false;

static LRESULT CALLBACK GMT__MouseLLHook(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION) {
    MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lParam;

    // In REPLAY mode, block ALL real (non-injected) mouse events so that the
    // user's physical mouse cannot interfere with the replayed input.  Injected
    // events produced by our own SendInput calls carry LLMHF_INJECTED and are
    // allowed through.
    // Stop blocking once the test has failed: any dialog (including ones from
    // non-GMT assertions such as CRT assert() or third-party libs) must remain
    // interactive.
    if (g_gmt.initialized && g_gmt.mode == GMT_Mode_REPLAY && !g_gmt.test_failed) {
      if (!(ms->flags & LLMHF_INJECTED)) {
        return 1;  // Swallow real mouse event.
      }
    }

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

    // In REPLAY mode, block ALL real (non-injected) keyboard events so that
    // physical keys never reach the application's message queue.
    // Stop blocking once the test has failed: any dialog (including ones from
    // non-GMT assertions such as CRT assert() or third-party libs) must remain
    // interactive.
    if (g_gmt.initialized && g_gmt.mode == GMT_Mode_REPLAY && !g_gmt.test_failed) {
      if (!(kb->flags & LLKHF_INJECTED)) {
        return 1;  // Swallow real keyboard event.
      }
    }

    // RECORD mode (and any injected-event bookkeeping during REPLAY):
    // count auto-repeat key-down events, ignoring injected events.
    if (!(kb->flags & LLKHF_INJECTED)) {
      DWORD vk = kb->vkCode;
      if (vk < 256) {
        bool was_transition = false;
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
          if (g_hook_key_down[vk]) {
            // Key was already down — this is an auto-repeat event.
            int gmt_key = g_vk_to_gmt_key[vk];
            if (gmt_key > 0) {
              InterlockedExchangeAdd(&g_key_repeats[gmt_key], 1);
            }
          } else {
            g_hook_key_down[vk] = 1;
            was_transition = true;  // Key down transition
          }
        } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
          g_hook_key_down[vk] = 0;
          was_transition = true;  // Key up transition
        }
        // Capture input immediately on key transitions so fast taps that occur
        // between GMT_Update calls are not missed.
        if (was_transition && g_gmt.initialized && g_gmt.mode == GMT_Mode_RECORD) {
          GMT_Record_WriteInputFromKeyEvent();
        }
      }
    }
  }
  return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
}

// ===== IAT Hooking Infrastructure =====
//
// Patches the Import Address Table (IAT) of a loaded PE module to redirect an
// imported function to a replacement.  This allows us to transparently intercept
// Win32 input-polling calls (GetAsyncKeyState, GetKeyState, GetKeyboardState,
// GetCursorPos) so that during replay they return the replayed state instead of
// the real hardware state.
//
// The original function pointer is saved so that (a) we can restore it on
// shutdown and (b) our own code can call through to the real implementation
// when recording or running normally.

// Saved original function pointers (populated before any patching).
typedef SHORT(WINAPI* PFN_GetAsyncKeyState)(int vKey);
typedef SHORT(WINAPI* PFN_GetKeyState)(int nVirtKey);
typedef BOOL(WINAPI* PFN_GetKeyboardState)(PBYTE lpKeyState);
typedef BOOL(WINAPI* PFN_GetCursorPos)(LPPOINT lpPoint);
typedef UINT(WINAPI* PFN_GetRawInputData)(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader);

static PFN_GetAsyncKeyState g_orig_GetAsyncKeyState = NULL;
static PFN_GetKeyState g_orig_GetKeyState = NULL;
static PFN_GetKeyboardState g_orig_GetKeyboardState = NULL;
static PFN_GetCursorPos g_orig_GetCursorPos = NULL;
static PFN_GetRawInputData g_orig_GetRawInputData = NULL;

// ---- XInput hooking ----
//
// XInput is the dominant gamepad API on Windows.  We hook XInputGetState (and
// XInputGetCapabilities / XInputGetKeystroke) via IAT so that during replay the
// game sees the replayed gamepad state instead of real hardware.
//
// During RECORD mode the hooks call through to the original functions so we
// can capture real controller state.

typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD dwUserIndex, XINPUT_STATE* pState);
typedef DWORD(WINAPI* PFN_XInputSetState)(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration);
typedef DWORD(WINAPI* PFN_XInputGetCapabilities)(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities);
typedef DWORD(WINAPI* PFN_XInputGetKeystroke)(DWORD dwUserIndex, DWORD dwReserved, PXINPUT_KEYSTROKE pKeystroke);

static PFN_XInputGetState g_orig_XInputGetState = NULL;
static PFN_XInputSetState g_orig_XInputSetState = NULL;
static PFN_XInputGetCapabilities g_orig_XInputGetCapabilities = NULL;
static PFN_XInputGetKeystroke g_orig_XInputGetKeystroke = NULL;

// ---- DirectInput hooking ----
//
// DirectInput8Create is the factory entry point.  During replay we hook it via
// IAT.  When the game calls it, we let the real DI8 create the real object, then
// wrap the returned IDirectInput8 in a proxy that intercepts CreateDevice.  The
// proxy's CreateDevice wraps each IDirectInputDevice8 so that GetDeviceState and
// GetDeviceData return replayed gamepad state for game-controller devices while
// delegating everything else (keyboard, mouse — already covered by other hooks)
// to the real device.
//
// We define the COM GUIDs and vtable layouts ourselves so we don't require the
// DirectInput SDK headers to be installed (they aren't in the Windows SDK since
// Windows 8).  The ABI is stable.

// Minimal GUID comparison.
typedef struct GMT_GUID {
  uint32_t Data1;
  uint16_t Data2;
  uint16_t Data3;
  uint8_t Data4[8];
} GMT_GUID;

static bool GMT__GuidEqual(const GMT_GUID* a, const GMT_GUID* b) {
  return memcmp(a, b, sizeof(GMT_GUID)) == 0;
}

// IID_IDirectInput8A  = {BF798030-483A-4DA2-AA99-5D64ED369700}
// IID_IDirectInput8W  = {BF798031-483A-4DA2-AA99-5D64ED369700}
static const GMT_GUID k_IID_IDirectInput8A = {
    0xBF798030,
    0x483A,
    0x4DA2,
    {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}
};
static const GMT_GUID k_IID_IDirectInput8W = {
    0xBF798031,
    0x483A,
    0x4DA2,
    {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}
};

// GUID_SysKeyboard = {6F1D2B61-D5A0-11CF-BFC7-444553540000}
// GUID_SysMouse    = {6F1D2B60-D5A0-11CF-BFC7-444553540000}
static const GMT_GUID k_GUID_SysKeyboard = {
    0x6F1D2B61,
    0xD5A0,
    0x11CF,
    {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}
};
static const GMT_GUID k_GUID_SysMouse = {
    0x6F1D2B60,
    0xD5A0,
    0x11CF,
    {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}
};

// DirectInput8Create function pointer type.
typedef HRESULT(WINAPI* PFN_DirectInput8Create)(HINSTANCE hinst, DWORD dwVersion, const GMT_GUID* riidltf, void** ppvOut, void* punkOuter);
static PFN_DirectInput8Create g_orig_DirectInput8Create = NULL;

// ---- Forward declarations for DI8 wrappers (defined later) ----
static HRESULT WINAPI GMT_Hook_DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, const GMT_GUID* riidltf, void** ppvOut, void* punkOuter);

// The replayed input state.  Updated each frame by GMT_Platform_SetReplayedInput.
// Read by the hooked Win32 functions below.
static GMT_InputState g_replayed_input;
static uint8_t g_replayed_toggle_state[256];     // Tracks toggle state (low bit) for keys like Caps Lock.
static volatile LONG g_replay_hooks_active = 0;  // 1 while hooks should return replayed state.

// WH_GETMESSAGE hook handle (strips WM_INPUT during replay).
static HHOOK g_getmessage_hook = NULL;

// ---- Hooked Win32 function implementations ----

static SHORT WINAPI GMT_Hook_GetAsyncKeyState(int vKey) {
  if (InterlockedCompareExchange(&g_replay_hooks_active, 0, 0)) {
    if (vKey < 0 || vKey >= 256) return 0;

    // Mouse button virtual keys.
    if (vKey == VK_LBUTTON) return (g_replayed_input.mouse_buttons & GMT_MouseButton_LEFT) ? (SHORT)0x8000 : 0;
    if (vKey == VK_RBUTTON) return (g_replayed_input.mouse_buttons & GMT_MouseButton_RIGHT) ? (SHORT)0x8000 : 0;
    if (vKey == VK_MBUTTON) return (g_replayed_input.mouse_buttons & GMT_MouseButton_MIDDLE) ? (SHORT)0x8000 : 0;
    if (vKey == VK_XBUTTON1) return (g_replayed_input.mouse_buttons & GMT_MouseButton_X1) ? (SHORT)0x8000 : 0;
    if (vKey == VK_XBUTTON2) return (g_replayed_input.mouse_buttons & GMT_MouseButton_X2) ? (SHORT)0x8000 : 0;

    // Generic modifier keys.
    if (vKey == VK_SHIFT) return ((g_replayed_input.keys[GMT_Key_LEFT_SHIFT] | g_replayed_input.keys[GMT_Key_RIGHT_SHIFT]) & 0x80u) ? (SHORT)0x8000 : 0;
    if (vKey == VK_CONTROL) return ((g_replayed_input.keys[GMT_Key_LEFT_CTRL] | g_replayed_input.keys[GMT_Key_RIGHT_CTRL]) & 0x80u) ? (SHORT)0x8000 : 0;
    if (vKey == VK_MENU) return ((g_replayed_input.keys[GMT_Key_LEFT_ALT] | g_replayed_input.keys[GMT_Key_RIGHT_ALT]) & 0x80u) ? (SHORT)0x8000 : 0;

    // Keyboard keys.
    int gmt_key = g_vk_to_gmt_key[vKey];
    if (gmt_key > 0 && gmt_key < GMT_KEY_COUNT) {
      SHORT state = (g_replayed_input.keys[gmt_key] & 0x80u) ? (SHORT)0x8000 : 0;
      // GetAsyncKeyState low bit indicates if the key was pressed since the last call.
      // We don't track this reliably, so just return 0 for the low bit.
      return state;
    }
    return 0;
  }
  return g_orig_GetAsyncKeyState(vKey);
}

static SHORT WINAPI GMT_Hook_GetKeyState(int nVirtKey) {
  if (InterlockedCompareExchange(&g_replay_hooks_active, 0, 0)) {
    if (nVirtKey < 0 || nVirtKey >= 256) return 0;

    // Mouse button virtual keys.
    if (nVirtKey == VK_LBUTTON) return (g_replayed_input.mouse_buttons & GMT_MouseButton_LEFT) ? (SHORT)0x8000 : 0;
    if (nVirtKey == VK_RBUTTON) return (g_replayed_input.mouse_buttons & GMT_MouseButton_RIGHT) ? (SHORT)0x8000 : 0;
    if (nVirtKey == VK_MBUTTON) return (g_replayed_input.mouse_buttons & GMT_MouseButton_MIDDLE) ? (SHORT)0x8000 : 0;
    if (nVirtKey == VK_XBUTTON1) return (g_replayed_input.mouse_buttons & GMT_MouseButton_X1) ? (SHORT)0x8000 : 0;
    if (nVirtKey == VK_XBUTTON2) return (g_replayed_input.mouse_buttons & GMT_MouseButton_X2) ? (SHORT)0x8000 : 0;

    // Generic modifier keys.
    if (nVirtKey == VK_SHIFT) return (((g_replayed_input.keys[GMT_Key_LEFT_SHIFT] | g_replayed_input.keys[GMT_Key_RIGHT_SHIFT]) & 0x80u) ? (SHORT)0x8000 : 0) | g_replayed_toggle_state[VK_SHIFT];
    if (nVirtKey == VK_CONTROL) return (((g_replayed_input.keys[GMT_Key_LEFT_CTRL] | g_replayed_input.keys[GMT_Key_RIGHT_CTRL]) & 0x80u) ? (SHORT)0x8000 : 0) | g_replayed_toggle_state[VK_CONTROL];
    if (nVirtKey == VK_MENU) return (((g_replayed_input.keys[GMT_Key_LEFT_ALT] | g_replayed_input.keys[GMT_Key_RIGHT_ALT]) & 0x80u) ? (SHORT)0x8000 : 0) | g_replayed_toggle_state[VK_MENU];

    int gmt_key = g_vk_to_gmt_key[nVirtKey];
    if (gmt_key > 0 && gmt_key < GMT_KEY_COUNT) {
      SHORT state = (g_replayed_input.keys[gmt_key] & 0x80u) ? (SHORT)0x8000 : 0;
      state |= g_replayed_toggle_state[nVirtKey];
      return state;
    }
    return 0;
  }
  return g_orig_GetKeyState(nVirtKey);
}

static BOOL WINAPI GMT_Hook_GetKeyboardState(PBYTE lpKeyState) {
  if (InterlockedCompareExchange(&g_replay_hooks_active, 0, 0)) {
    if (!lpKeyState) return FALSE;
    memset(lpKeyState, 0, 256);

    // Set keyboard keys from replayed state.
    for (int k = 1; k < GMT_KEY_COUNT; ++k) {
      int vk = k_vk[k];
      if (vk > 0 && vk < 256) {
        lpKeyState[vk] = g_replayed_input.keys[k] | g_replayed_toggle_state[vk];
      }
    }

    // Set generic modifier keys.
    lpKeyState[VK_SHIFT] = ((g_replayed_input.keys[GMT_Key_LEFT_SHIFT] | g_replayed_input.keys[GMT_Key_RIGHT_SHIFT]) & 0x80u) | g_replayed_toggle_state[VK_SHIFT];
    lpKeyState[VK_CONTROL] = ((g_replayed_input.keys[GMT_Key_LEFT_CTRL] | g_replayed_input.keys[GMT_Key_RIGHT_CTRL]) & 0x80u) | g_replayed_toggle_state[VK_CONTROL];
    lpKeyState[VK_MENU] = ((g_replayed_input.keys[GMT_Key_LEFT_ALT] | g_replayed_input.keys[GMT_Key_RIGHT_ALT]) & 0x80u) | g_replayed_toggle_state[VK_MENU];

    // Set mouse button virtual keys.
    if (g_replayed_input.mouse_buttons & GMT_MouseButton_LEFT) lpKeyState[VK_LBUTTON] = 0x80;
    if (g_replayed_input.mouse_buttons & GMT_MouseButton_RIGHT) lpKeyState[VK_RBUTTON] = 0x80;
    if (g_replayed_input.mouse_buttons & GMT_MouseButton_MIDDLE) lpKeyState[VK_MBUTTON] = 0x80;
    if (g_replayed_input.mouse_buttons & GMT_MouseButton_X1) lpKeyState[VK_XBUTTON1] = 0x80;
    if (g_replayed_input.mouse_buttons & GMT_MouseButton_X2) lpKeyState[VK_XBUTTON2] = 0x80;

    return TRUE;
  }
  return g_orig_GetKeyboardState(lpKeyState);
}

static BOOL WINAPI GMT_Hook_GetCursorPos(LPPOINT lpPoint) {
  if (InterlockedCompareExchange(&g_replay_hooks_active, 0, 0)) {
    if (!lpPoint) return FALSE;
    lpPoint->x = g_replayed_input.mouse_x;
    lpPoint->y = g_replayed_input.mouse_y;
    return TRUE;
  }
  return g_orig_GetCursorPos(lpPoint);
}

static UINT WINAPI GMT_Hook_GetRawInputData(HRAWINPUT hRawInput, UINT uiCommand, LPVOID pData, PUINT pcbSize, UINT cbSizeHeader) {
  if (InterlockedCompareExchange(&g_replay_hooks_active, 0, 0)) {
    // During replay, report no raw input data.  Games that rely exclusively on
    // Raw Input are still covered by the IAT hooks on GetAsyncKeyState etc. and
    // the SendInput-based message injection.
    if (pcbSize) *pcbSize = 0;
    return 0;
  }
  return g_orig_GetRawInputData(hRawInput, uiCommand, pData, pcbSize, cbSizeHeader);
}

// ---- XInput hook implementations ----

static DWORD WINAPI GMT_Hook_XInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
  if (InterlockedCompareExchange(&g_replay_hooks_active, 0, 0)) {
    if (dwUserIndex >= GMT_MAX_GAMEPADS || !pState) return ERROR_DEVICE_NOT_CONNECTED;

    const GMT_GamepadState* gp = &g_replayed_input.gamepads[dwUserIndex];
    if (!gp->connected) return ERROR_DEVICE_NOT_CONNECTED;

    // Map GMT_GamepadState → XINPUT_STATE.
    memset(pState, 0, sizeof(*pState));
    pState->dwPacketNumber = (DWORD)g_gmt.frame_index;
    pState->Gamepad.wButtons = gp->buttons;
    pState->Gamepad.bLeftTrigger = gp->left_trigger;
    pState->Gamepad.bRightTrigger = gp->right_trigger;
    pState->Gamepad.sThumbLX = gp->left_stick_x;
    pState->Gamepad.sThumbLY = gp->left_stick_y;
    pState->Gamepad.sThumbRX = gp->right_stick_x;
    pState->Gamepad.sThumbRY = gp->right_stick_y;
    return ERROR_SUCCESS;
  }
  return g_orig_XInputGetState ? g_orig_XInputGetState(dwUserIndex, pState) : ERROR_DEVICE_NOT_CONNECTED;
}

static DWORD WINAPI GMT_Hook_XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration) {
  if (InterlockedCompareExchange(&g_replay_hooks_active, 0, 0)) {
    // During replay, silently succeed — no real vibration.
    if (dwUserIndex >= GMT_MAX_GAMEPADS) return ERROR_DEVICE_NOT_CONNECTED;
    if (!g_replayed_input.gamepads[dwUserIndex].connected) return ERROR_DEVICE_NOT_CONNECTED;
    return ERROR_SUCCESS;
  }
  return g_orig_XInputSetState ? g_orig_XInputSetState(dwUserIndex, pVibration) : ERROR_DEVICE_NOT_CONNECTED;
}

static DWORD WINAPI GMT_Hook_XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities) {
  if (InterlockedCompareExchange(&g_replay_hooks_active, 0, 0)) {
    if (dwUserIndex >= GMT_MAX_GAMEPADS || !pCapabilities) return ERROR_DEVICE_NOT_CONNECTED;
    if (!g_replayed_input.gamepads[dwUserIndex].connected) return ERROR_DEVICE_NOT_CONNECTED;

    // Report a standard Xbox 360-style controller.
    memset(pCapabilities, 0, sizeof(*pCapabilities));
    pCapabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
    pCapabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
    pCapabilities->Flags = 0;
    // Report full range capabilities.
    pCapabilities->Gamepad.wButtons = 0xFFFF;
    pCapabilities->Gamepad.bLeftTrigger = 0xFF;
    pCapabilities->Gamepad.bRightTrigger = 0xFF;
    pCapabilities->Gamepad.sThumbLX = (SHORT)0x7FFF;
    pCapabilities->Gamepad.sThumbLY = (SHORT)0x7FFF;
    pCapabilities->Gamepad.sThumbRX = (SHORT)0x7FFF;
    pCapabilities->Gamepad.sThumbRY = (SHORT)0x7FFF;
    pCapabilities->Vibration.wLeftMotorSpeed = 0xFFFF;
    pCapabilities->Vibration.wRightMotorSpeed = 0xFFFF;
    return ERROR_SUCCESS;
  }
  return g_orig_XInputGetCapabilities ? g_orig_XInputGetCapabilities(dwUserIndex, dwFlags, pCapabilities) : ERROR_DEVICE_NOT_CONNECTED;
}

static DWORD WINAPI GMT_Hook_XInputGetKeystroke(DWORD dwUserIndex, DWORD dwReserved, PXINPUT_KEYSTROKE pKeystroke) {
  if (InterlockedCompareExchange(&g_replay_hooks_active, 0, 0)) {
    // No keystroke data during replay.
    return ERROR_EMPTY;
  }
  return g_orig_XInputGetKeystroke ? g_orig_XInputGetKeystroke(dwUserIndex, dwReserved, pKeystroke) : ERROR_DEVICE_NOT_CONNECTED;
}

// ---- DirectInput8 COM wrappers ----
//
// We wrap the real IDirectInput8 and IDirectInputDevice8 COM objects so that
// gamepad devices return replayed state from GetDeviceState / GetDeviceData
// while all other calls (and non-gamepad devices) delegate to the real objects.

// DIJOYSTATE2 layout — the default data format for joysticks in DirectInput.
// We only need to fill it; we don't need the full DI headers.
#pragma pack(push, 1)
typedef struct GMT_DIJOYSTATE2 {
  LONG lX, lY, lZ;
  LONG lRx, lRy, lRz;
  LONG rglSlider[2];
  DWORD rgdwPOV[4];
  BYTE rgbButtons[128];
  LONG lVX, lVY, lVZ;
  LONG lVRx, lVRy, lVRz;
  LONG rglVSlider[2];
  LONG lAX, lAY, lAZ;
  LONG lARx, lARy, lARz;
  LONG rglASlider[2];
  LONG lFX, lFY, lFZ;
  LONG lFRx, lFRy, lFRz;
  LONG rglFSlider[2];
} GMT_DIJOYSTATE2;
#pragma pack(pop)

// IDirectInputDevice8 vtable indices (same for A and W variants).
// The COM vtable is a flat array of function pointers; these indices are stable.
enum {
  GMT_DID8_QueryInterface = 0,
  GMT_DID8_AddRef = 1,
  GMT_DID8_Release = 2,
  GMT_DID8_GetCapabilities = 3,
  GMT_DID8_EnumObjects = 4,
  GMT_DID8_GetProperty = 5,
  GMT_DID8_SetProperty = 6,
  GMT_DID8_Acquire = 7,
  GMT_DID8_Unacquire = 8,
  GMT_DID8_GetDeviceState = 9,
  GMT_DID8_GetDeviceData = 10,
  GMT_DID8_SetDataFormat = 11,
  GMT_DID8_SetEventNotification = 12,
  GMT_DID8_SetCooperativeLevel = 13,
  GMT_DID8_GetObjectInfo = 14,
  GMT_DID8_GetDeviceInfo = 15,
  GMT_DID8_RunControlPanel = 16,
  GMT_DID8_Initialize = 17,
  GMT_DID8_CreateEffect = 18,
  GMT_DID8_EnumEffects = 19,
  GMT_DID8_GetEffectInfo = 20,
  GMT_DID8_GetForceFeedbackState = 21,
  GMT_DID8_SendForceFeedbackCommand = 22,
  GMT_DID8_EnumCreatedEffectObjects = 23,
  GMT_DID8_Escape = 24,
  GMT_DID8_Poll = 25,
  GMT_DID8_SendDeviceData = 26,
  GMT_DID8_EnumEffectsInFile = 27,
  GMT_DID8_WriteEffectToFile = 28,
  GMT_DID8_BuildActionMap = 29,
  GMT_DID8_SetActionMap = 30,
  GMT_DID8_GetImageInfo = 31,
  GMT_DID8_VTABLE_SIZE = 32,
};

// IDirectInput8 vtable indices.
enum {
  GMT_DI8_QueryInterface = 0,
  GMT_DI8_AddRef = 1,
  GMT_DI8_Release = 2,
  GMT_DI8_CreateDevice = 3,
  GMT_DI8_EnumDevices = 4,
  GMT_DI8_GetDeviceStatus = 5,
  GMT_DI8_RunControlPanel = 6,
  GMT_DI8_Initialize = 7,
  GMT_DI8_FindDevice = 8,
  GMT_DI8_EnumDevicesBySemantics = 9,
  GMT_DI8_ConfigureDevices = 10,
  GMT_DI8_VTABLE_SIZE = 11,
};

// ---- IDirectInputDevice8 Wrapper ----

typedef struct GMT_DIDeviceWrapper {
  void** vtable;       // Points to our custom vtable below.
  void* real_device;   // The real IDirectInputDevice8*.
  void** real_vtable;  // Cached pointer to the real object's vtable.
  LONG ref_count;
  int gamepad_index;        // Which GMT_MAX_GAMEPADS slot this maps to (-1 = not a gamepad).
  bool is_gamepad;          // True if this device was identified as a game controller.
  size_t data_format_size;  // Size of the data format set by SetDataFormat.
} GMT_DIDeviceWrapper;

// Helper: get the wrapper from a "this" pointer (first arg to every COM method).
#define GMT_DID_WRAPPER(p) ((GMT_DIDeviceWrapper*)(p))

// We keep a small counter to assign gamepad indices to DI devices.
static volatile LONG g_di_gamepad_counter = 0;

// All device wrapper COM methods: delegate to real device, except GetDeviceState / GetDeviceData.
// We use a macro to generate simple forwarding thunks.

// Generic forwarder: calls real_vtable[index](real_device, ...).
// For variadic COM methods we just forward using the real device pointer and vtable.
// NOTE: COM methods use __stdcall on x86 and the default calling convention (__fastcall-like)
// on x64.  Since we replace the vtable pointer, "this" in the wrapper IS our wrapper struct,
// and we need to substitute the real pointer.

// For simplicity and correctness across 32/64-bit, we implement each method we care about
// explicitly and forward the rest through a helper.

typedef HRESULT(WINAPI* PFN_COMGeneric)(void* pThis);

// Forward declaration of the wrapper vtable.
static void* g_di_device_vtable[GMT_DID8_VTABLE_SIZE];
static bool g_di_device_vtable_initialized = false;

// Helper to build a DIJOYSTATE2 from replayed gamepad state.
static void GMT__FillDIJoyState2(GMT_DIJOYSTATE2* js, const GMT_GamepadState* gp) {
  memset(js, 0, sizeof(*js));

  if (!gp->connected) return;

  // Map thumbsticks: XInput [-32768, 32767] → DI [-1000, 1000] (default range).
  js->lX = (LONG)(((double)gp->left_stick_x / 32767.0) * 1000.0);
  js->lY = (LONG)(((double)-gp->left_stick_y / 32767.0) * 1000.0);  // DI Y is inverted vs XInput.
  js->lRx = (LONG)(((double)gp->right_stick_x / 32767.0) * 1000.0);
  js->lRy = (LONG)(((double)-gp->right_stick_y / 32767.0) * 1000.0);
  js->lZ = (LONG)(((double)(gp->right_trigger - gp->left_trigger) / 255.0) * 1000.0);

  // Triggers as separate axes in slider[0] and slider[1].
  js->rglSlider[0] = (LONG)(((double)gp->left_trigger / 255.0) * 1000.0);
  js->rglSlider[1] = (LONG)(((double)gp->right_trigger / 255.0) * 1000.0);

  // D-pad → POV hat (0 = up, in hundredths of a degree, -1 = centred).
  if (gp->buttons & GMT_GamepadButton_DPAD_UP) {
    if (gp->buttons & GMT_GamepadButton_DPAD_RIGHT) js->rgdwPOV[0] = 4500;
    else if (gp->buttons & GMT_GamepadButton_DPAD_LEFT)
      js->rgdwPOV[0] = 31500;
    else
      js->rgdwPOV[0] = 0;
  } else if (gp->buttons & GMT_GamepadButton_DPAD_DOWN) {
    if (gp->buttons & GMT_GamepadButton_DPAD_RIGHT) js->rgdwPOV[0] = 13500;
    else if (gp->buttons & GMT_GamepadButton_DPAD_LEFT)
      js->rgdwPOV[0] = 22500;
    else
      js->rgdwPOV[0] = 18000;
  } else if (gp->buttons & GMT_GamepadButton_DPAD_RIGHT) {
    js->rgdwPOV[0] = 9000;
  } else if (gp->buttons & GMT_GamepadButton_DPAD_LEFT) {
    js->rgdwPOV[0] = 27000;
  } else {
    js->rgdwPOV[0] = (DWORD)-1;  // Centred.
  }
  js->rgdwPOV[1] = js->rgdwPOV[2] = js->rgdwPOV[3] = (DWORD)-1;

  // Buttons (A=0, B=1, X=2, Y=3, LB=4, RB=5, Back=6, Start=7, LT=8, RT=9, Guide=10).
  if (gp->buttons & GMT_GamepadButton_A) js->rgbButtons[0] = 0x80;
  if (gp->buttons & GMT_GamepadButton_B) js->rgbButtons[1] = 0x80;
  if (gp->buttons & GMT_GamepadButton_X) js->rgbButtons[2] = 0x80;
  if (gp->buttons & GMT_GamepadButton_Y) js->rgbButtons[3] = 0x80;
  if (gp->buttons & GMT_GamepadButton_LEFT_SHOULDER) js->rgbButtons[4] = 0x80;
  if (gp->buttons & GMT_GamepadButton_RIGHT_SHOULDER) js->rgbButtons[5] = 0x80;
  if (gp->buttons & GMT_GamepadButton_BACK) js->rgbButtons[6] = 0x80;
  if (gp->buttons & GMT_GamepadButton_START) js->rgbButtons[7] = 0x80;
  if (gp->buttons & GMT_GamepadButton_LEFT_THUMB) js->rgbButtons[8] = 0x80;
  if (gp->buttons & GMT_GamepadButton_RIGHT_THUMB) js->rgbButtons[9] = 0x80;
  if (gp->buttons & GMT_GamepadButton_GUIDE) js->rgbButtons[10] = 0x80;
}

// --- COM method implementations for the device wrapper ---

static HRESULT WINAPI GMT_DIDev_QueryInterface(void* pThis, const GMT_GUID* riid, void** ppvObject) {
  GMT_DIDeviceWrapper* w = GMT_DID_WRAPPER(pThis);
  typedef HRESULT(WINAPI * Fn)(void*, const GMT_GUID*, void**);
  HRESULT hr = ((Fn)w->real_vtable[GMT_DID8_QueryInterface])(w->real_device, riid, ppvObject);
  if (SUCCEEDED(hr) && ppvObject && *ppvObject == w->real_device) {
    // Return ourselves instead of the real device.
    *ppvObject = pThis;
    InterlockedIncrement(&w->ref_count);
  }
  return hr;
}

static ULONG WINAPI GMT_DIDev_AddRef(void* pThis) {
  GMT_DIDeviceWrapper* w = GMT_DID_WRAPPER(pThis);
  return (ULONG)InterlockedIncrement(&w->ref_count);
}

static ULONG WINAPI GMT_DIDev_Release(void* pThis) {
  GMT_DIDeviceWrapper* w = GMT_DID_WRAPPER(pThis);
  LONG rc = InterlockedDecrement(&w->ref_count);
  if (rc <= 0) {
    // Release the real device.
    typedef ULONG(WINAPI * Fn)(void*);
    ((Fn)w->real_vtable[GMT_DID8_Release])(w->real_device);
    GMT_Free(w);
    return 0;
  }
  return (ULONG)rc;
}

static HRESULT WINAPI GMT_DIDev_GetDeviceState(void* pThis, DWORD cbData, void* lpvData) {
  GMT_DIDeviceWrapper* w = GMT_DID_WRAPPER(pThis);

  if (w->is_gamepad && InterlockedCompareExchange(&g_replay_hooks_active, 0, 0)) {
    if (!lpvData) return E_POINTER;
    // Fill with replayed gamepad state.
    int idx = w->gamepad_index;
    if (idx < 0 || idx >= GMT_MAX_GAMEPADS) idx = 0;
    const GMT_GamepadState* gp = &g_replayed_input.gamepads[idx];

    if (cbData >= sizeof(GMT_DIJOYSTATE2)) {
      GMT_DIJOYSTATE2 js;
      GMT__FillDIJoyState2(&js, gp);
      memset(lpvData, 0, cbData);
      memcpy(lpvData, &js, sizeof(js));
    } else {
      // Smaller data format; fill what we can.
      GMT_DIJOYSTATE2 js;
      GMT__FillDIJoyState2(&js, gp);
      memset(lpvData, 0, cbData);
      memcpy(lpvData, &js, cbData < sizeof(js) ? cbData : sizeof(js));
    }
    return S_OK;
  }

  typedef HRESULT(WINAPI * Fn)(void*, DWORD, void*);
  return ((Fn)w->real_vtable[GMT_DID8_GetDeviceState])(w->real_device, cbData, lpvData);
}

static HRESULT WINAPI GMT_DIDev_GetDeviceData(void* pThis, DWORD cbObjectData, void* rgdod, DWORD* pdwInOut, DWORD dwFlags) {
  GMT_DIDeviceWrapper* w = GMT_DID_WRAPPER(pThis);

  if (w->is_gamepad && InterlockedCompareExchange(&g_replay_hooks_active, 0, 0)) {
    // During replay, report no buffered data.
    if (pdwInOut) *pdwInOut = 0;
    return S_OK;
  }

  typedef HRESULT(WINAPI * Fn)(void*, DWORD, void*, DWORD*, DWORD);
  return ((Fn)w->real_vtable[GMT_DID8_GetDeviceData])(w->real_device, cbObjectData, rgdod, pdwInOut, dwFlags);
}

static HRESULT WINAPI GMT_DIDev_Poll(void* pThis) {
  GMT_DIDeviceWrapper* w = GMT_DID_WRAPPER(pThis);
  if (w->is_gamepad && InterlockedCompareExchange(&g_replay_hooks_active, 0, 0)) {
    return S_OK;  // No-op during replay.
  }
  typedef HRESULT(WINAPI * Fn)(void*);
  return ((Fn)w->real_vtable[GMT_DID8_Poll])(w->real_device);
}

static HRESULT WINAPI GMT_DIDev_SetDataFormat(void* pThis, const void* lpdf) {
  GMT_DIDeviceWrapper* w = GMT_DID_WRAPPER(pThis);
  // Track the data format size if available.
  // The DIDATAFORMAT struct's first DWORD is dwSize, second is dwObjSize, third is dwFlags, fourth is dwDataSize.
  if (lpdf) {
    const DWORD* p = (const DWORD*)lpdf;
    // dwDataSize is at offset 12 (4th DWORD).
    w->data_format_size = (size_t)p[3];
  }
  typedef HRESULT(WINAPI * Fn)(void*, const void*);
  return ((Fn)w->real_vtable[GMT_DID8_SetDataFormat])(w->real_device, lpdf);
}

// Generic forwarder macro for methods we don't need to intercept.
// We generate thunks that swap "this" to the real device and call through.
#define GMT_DI_FORWARD_0(NAME, IDX)                     \
  static HRESULT WINAPI GMT_DIDev_##NAME(void* pThis) { \
    GMT_DIDeviceWrapper* w = GMT_DID_WRAPPER(pThis);    \
    typedef HRESULT(WINAPI* Fn)(void*);                 \
    return ((Fn)w->real_vtable[IDX])(w->real_device);   \
  }

#define GMT_DI_FORWARD_1(NAME, IDX, T1)                        \
  static HRESULT WINAPI GMT_DIDev_##NAME(void* pThis, T1 a1) { \
    GMT_DIDeviceWrapper* w = GMT_DID_WRAPPER(pThis);           \
    typedef HRESULT(WINAPI* Fn)(void*, T1);                    \
    return ((Fn)w->real_vtable[IDX])(w->real_device, a1);      \
  }

#define GMT_DI_FORWARD_2(NAME, IDX, T1, T2)                           \
  static HRESULT WINAPI GMT_DIDev_##NAME(void* pThis, T1 a1, T2 a2) { \
    GMT_DIDeviceWrapper* w = GMT_DID_WRAPPER(pThis);                  \
    typedef HRESULT(WINAPI* Fn)(void*, T1, T2);                       \
    return ((Fn)w->real_vtable[IDX])(w->real_device, a1, a2);         \
  }

#define GMT_DI_FORWARD_3(NAME, IDX, T1, T2, T3)                              \
  static HRESULT WINAPI GMT_DIDev_##NAME(void* pThis, T1 a1, T2 a2, T3 a3) { \
    GMT_DIDeviceWrapper* w = GMT_DID_WRAPPER(pThis);                         \
    typedef HRESULT(WINAPI* Fn)(void*, T1, T2, T3);                          \
    return ((Fn)w->real_vtable[IDX])(w->real_device, a1, a2, a3);            \
  }

#define GMT_DI_FORWARD_4(NAME, IDX, T1, T2, T3, T4)                                 \
  static HRESULT WINAPI GMT_DIDev_##NAME(void* pThis, T1 a1, T2 a2, T3 a3, T4 a4) { \
    GMT_DIDeviceWrapper* w = GMT_DID_WRAPPER(pThis);                                \
    typedef HRESULT(WINAPI* Fn)(void*, T1, T2, T3, T4);                             \
    return ((Fn)w->real_vtable[IDX])(w->real_device, a1, a2, a3, a4);               \
  }

GMT_DI_FORWARD_1(GetCapabilities, GMT_DID8_GetCapabilities, void*)
GMT_DI_FORWARD_3(EnumObjects, GMT_DID8_EnumObjects, void*, void*, DWORD)
GMT_DI_FORWARD_2(GetProperty, GMT_DID8_GetProperty, const GMT_GUID*, void*)
GMT_DI_FORWARD_2(SetProperty, GMT_DID8_SetProperty, const GMT_GUID*, const void*)
GMT_DI_FORWARD_0(Acquire, GMT_DID8_Acquire)
GMT_DI_FORWARD_0(Unacquire, GMT_DID8_Unacquire)
GMT_DI_FORWARD_1(SetEventNotification, GMT_DID8_SetEventNotification, HANDLE)
GMT_DI_FORWARD_2(SetCooperativeLevel, GMT_DID8_SetCooperativeLevel, HWND, DWORD)
GMT_DI_FORWARD_3(GetObjectInfo, GMT_DID8_GetObjectInfo, void*, DWORD, DWORD)
GMT_DI_FORWARD_1(GetDeviceInfo, GMT_DID8_GetDeviceInfo, void*)
GMT_DI_FORWARD_2(RunControlPanel, GMT_DID8_RunControlPanel, HWND, DWORD)
GMT_DI_FORWARD_2(Initialize, GMT_DID8_Initialize, HINSTANCE, DWORD)
GMT_DI_FORWARD_4(CreateEffect, GMT_DID8_CreateEffect, const GMT_GUID*, const void*, void**, void*)
GMT_DI_FORWARD_3(EnumEffects, GMT_DID8_EnumEffects, void*, void*, DWORD)
GMT_DI_FORWARD_2(GetEffectInfo, GMT_DID8_GetEffectInfo, void*, const GMT_GUID*)
GMT_DI_FORWARD_1(GetForceFeedbackState, GMT_DID8_GetForceFeedbackState, DWORD*)
GMT_DI_FORWARD_1(SendForceFeedbackCommand, GMT_DID8_SendForceFeedbackCommand, DWORD)
GMT_DI_FORWARD_3(EnumCreatedEffectObjects, GMT_DID8_EnumCreatedEffectObjects, void*, void*, DWORD)
GMT_DI_FORWARD_1(Escape, GMT_DID8_Escape, void*)
GMT_DI_FORWARD_4(SendDeviceData, GMT_DID8_SendDeviceData, DWORD, const void*, DWORD*, DWORD)
GMT_DI_FORWARD_4(EnumEffectsInFile, GMT_DID8_EnumEffectsInFile, const void*, void*, void*, DWORD)
GMT_DI_FORWARD_4(WriteEffectToFile, GMT_DID8_WriteEffectToFile, const void*, DWORD, void*, DWORD)
GMT_DI_FORWARD_3(BuildActionMap, GMT_DID8_BuildActionMap, void*, const void*, DWORD)
GMT_DI_FORWARD_3(SetActionMap, GMT_DID8_SetActionMap, const void*, const void*, DWORD)
GMT_DI_FORWARD_1(GetImageInfo, GMT_DID8_GetImageInfo, void*)

static void GMT__InitDIDeviceVtable(void) {
  if (g_di_device_vtable_initialized) return;
  g_di_device_vtable[GMT_DID8_QueryInterface] = (void*)GMT_DIDev_QueryInterface;
  g_di_device_vtable[GMT_DID8_AddRef] = (void*)GMT_DIDev_AddRef;
  g_di_device_vtable[GMT_DID8_Release] = (void*)GMT_DIDev_Release;
  g_di_device_vtable[GMT_DID8_GetCapabilities] = (void*)GMT_DIDev_GetCapabilities;
  g_di_device_vtable[GMT_DID8_EnumObjects] = (void*)GMT_DIDev_EnumObjects;
  g_di_device_vtable[GMT_DID8_GetProperty] = (void*)GMT_DIDev_GetProperty;
  g_di_device_vtable[GMT_DID8_SetProperty] = (void*)GMT_DIDev_SetProperty;
  g_di_device_vtable[GMT_DID8_Acquire] = (void*)GMT_DIDev_Acquire;
  g_di_device_vtable[GMT_DID8_Unacquire] = (void*)GMT_DIDev_Unacquire;
  g_di_device_vtable[GMT_DID8_GetDeviceState] = (void*)GMT_DIDev_GetDeviceState;
  g_di_device_vtable[GMT_DID8_GetDeviceData] = (void*)GMT_DIDev_GetDeviceData;
  g_di_device_vtable[GMT_DID8_SetDataFormat] = (void*)GMT_DIDev_SetDataFormat;
  g_di_device_vtable[GMT_DID8_SetEventNotification] = (void*)GMT_DIDev_SetEventNotification;
  g_di_device_vtable[GMT_DID8_SetCooperativeLevel] = (void*)GMT_DIDev_SetCooperativeLevel;
  g_di_device_vtable[GMT_DID8_GetObjectInfo] = (void*)GMT_DIDev_GetObjectInfo;
  g_di_device_vtable[GMT_DID8_GetDeviceInfo] = (void*)GMT_DIDev_GetDeviceInfo;
  g_di_device_vtable[GMT_DID8_RunControlPanel] = (void*)GMT_DIDev_RunControlPanel;
  g_di_device_vtable[GMT_DID8_Initialize] = (void*)GMT_DIDev_Initialize;
  g_di_device_vtable[GMT_DID8_CreateEffect] = (void*)GMT_DIDev_CreateEffect;
  g_di_device_vtable[GMT_DID8_EnumEffects] = (void*)GMT_DIDev_EnumEffects;
  g_di_device_vtable[GMT_DID8_GetEffectInfo] = (void*)GMT_DIDev_GetEffectInfo;
  g_di_device_vtable[GMT_DID8_GetForceFeedbackState] = (void*)GMT_DIDev_GetForceFeedbackState;
  g_di_device_vtable[GMT_DID8_SendForceFeedbackCommand] = (void*)GMT_DIDev_SendForceFeedbackCommand;
  g_di_device_vtable[GMT_DID8_EnumCreatedEffectObjects] = (void*)GMT_DIDev_EnumCreatedEffectObjects;
  g_di_device_vtable[GMT_DID8_Escape] = (void*)GMT_DIDev_Escape;
  g_di_device_vtable[GMT_DID8_Poll] = (void*)GMT_DIDev_Poll;
  g_di_device_vtable[GMT_DID8_SendDeviceData] = (void*)GMT_DIDev_SendDeviceData;
  g_di_device_vtable[GMT_DID8_EnumEffectsInFile] = (void*)GMT_DIDev_EnumEffectsInFile;
  g_di_device_vtable[GMT_DID8_WriteEffectToFile] = (void*)GMT_DIDev_WriteEffectToFile;
  g_di_device_vtable[GMT_DID8_BuildActionMap] = (void*)GMT_DIDev_BuildActionMap;
  g_di_device_vtable[GMT_DID8_SetActionMap] = (void*)GMT_DIDev_SetActionMap;
  g_di_device_vtable[GMT_DID8_GetImageInfo] = (void*)GMT_DIDev_GetImageInfo;
  g_di_device_vtable_initialized = true;
}

static GMT_DIDeviceWrapper* GMT__WrapDIDevice(void* real_device, bool is_gamepad) {
  GMT__InitDIDeviceVtable();
  GMT_DIDeviceWrapper* w = (GMT_DIDeviceWrapper*)GMT_Alloc(sizeof(GMT_DIDeviceWrapper));
  if (!w) return NULL;
  memset(w, 0, sizeof(*w));
  w->vtable = g_di_device_vtable;
  w->real_device = real_device;
  w->real_vtable = *(void***)real_device;  // First pointer in a COM object is the vtable.
  w->ref_count = 1;
  w->is_gamepad = is_gamepad;
  w->gamepad_index = is_gamepad ? (int)(InterlockedIncrement(&g_di_gamepad_counter) - 1) : -1;
  if (w->gamepad_index >= GMT_MAX_GAMEPADS) w->gamepad_index = GMT_MAX_GAMEPADS - 1;
  w->data_format_size = 0;
  return w;
}

// ---- IDirectInput8 Wrapper ----

typedef struct GMT_DI8Wrapper {
  void** vtable;
  void* real_di8;
  void** real_vtable;
  LONG ref_count;
} GMT_DI8Wrapper;

#define GMT_DI8_WRAPPER(p) ((GMT_DI8Wrapper*)(p))

static void* g_di8_vtable[GMT_DI8_VTABLE_SIZE];
static bool g_di8_vtable_initialized = false;

static HRESULT WINAPI GMT_DI8_QueryInterfaceImpl(void* pThis, const GMT_GUID* riid, void** ppvObject) {
  GMT_DI8Wrapper* w = GMT_DI8_WRAPPER(pThis);
  typedef HRESULT(WINAPI * Fn)(void*, const GMT_GUID*, void**);
  HRESULT hr = ((Fn)w->real_vtable[GMT_DI8_QueryInterface])(w->real_di8, riid, ppvObject);
  if (SUCCEEDED(hr) && ppvObject && *ppvObject == w->real_di8) {
    *ppvObject = pThis;
    InterlockedIncrement(&w->ref_count);
  }
  return hr;
}

static ULONG WINAPI GMT_DI8_AddRefImpl(void* pThis) {
  return (ULONG)InterlockedIncrement(&GMT_DI8_WRAPPER(pThis)->ref_count);
}

static ULONG WINAPI GMT_DI8_ReleaseImpl(void* pThis) {
  GMT_DI8Wrapper* w = GMT_DI8_WRAPPER(pThis);
  LONG rc = InterlockedDecrement(&w->ref_count);
  if (rc <= 0) {
    typedef ULONG(WINAPI * Fn)(void*);
    ((Fn)w->real_vtable[GMT_DI8_Release])(w->real_di8);
    GMT_Free(w);
    return 0;
  }
  return (ULONG)rc;
}

static HRESULT WINAPI GMT_DI8_CreateDeviceImpl(void* pThis, const GMT_GUID* rguid, void** lplpDirectInputDevice, void* pUnkOuter) {
  GMT_DI8Wrapper* w = GMT_DI8_WRAPPER(pThis);
  typedef HRESULT(WINAPI * Fn)(void*, const GMT_GUID*, void**, void*);
  HRESULT hr = ((Fn)w->real_vtable[GMT_DI8_CreateDevice])(w->real_di8, rguid, lplpDirectInputDevice, pUnkOuter);
  if (SUCCEEDED(hr) && lplpDirectInputDevice && *lplpDirectInputDevice) {
    // Determine if this is a gamepad (not GUID_SysKeyboard and not GUID_SysMouse).
    bool is_gamepad = true;
    if (rguid) {
      if (GMT__GuidEqual(rguid, &k_GUID_SysKeyboard) || GMT__GuidEqual(rguid, &k_GUID_SysMouse)) {
        is_gamepad = false;
      }
    }
    GMT_DIDeviceWrapper* wrapped = GMT__WrapDIDevice(*lplpDirectInputDevice, is_gamepad);
    if (wrapped) {
      *lplpDirectInputDevice = wrapped;
    }
  }
  return hr;
}

// Forwarding thunks for remaining IDirectInput8 methods.
#define GMT_DI8_FWD_1(NAME, IDX, T1)                         \
  static HRESULT WINAPI GMT_DI8_##NAME(void* pThis, T1 a1) { \
    GMT_DI8Wrapper* w = GMT_DI8_WRAPPER(pThis);              \
    typedef HRESULT(WINAPI* Fn)(void*, T1);                  \
    return ((Fn)w->real_vtable[IDX])(w->real_di8, a1);       \
  }

#define GMT_DI8_FWD_2(NAME, IDX, T1, T2)                            \
  static HRESULT WINAPI GMT_DI8_##NAME(void* pThis, T1 a1, T2 a2) { \
    GMT_DI8Wrapper* w = GMT_DI8_WRAPPER(pThis);                     \
    typedef HRESULT(WINAPI* Fn)(void*, T1, T2);                     \
    return ((Fn)w->real_vtable[IDX])(w->real_di8, a1, a2);          \
  }

#define GMT_DI8_FWD_3(NAME, IDX, T1, T2, T3)                               \
  static HRESULT WINAPI GMT_DI8_##NAME(void* pThis, T1 a1, T2 a2, T3 a3) { \
    GMT_DI8Wrapper* w = GMT_DI8_WRAPPER(pThis);                            \
    typedef HRESULT(WINAPI* Fn)(void*, T1, T2, T3);                        \
    return ((Fn)w->real_vtable[IDX])(w->real_di8, a1, a2, a3);             \
  }

#define GMT_DI8_FWD_4(NAME, IDX, T1, T2, T3, T4)                                  \
  static HRESULT WINAPI GMT_DI8_##NAME(void* pThis, T1 a1, T2 a2, T3 a3, T4 a4) { \
    GMT_DI8Wrapper* w = GMT_DI8_WRAPPER(pThis);                                   \
    typedef HRESULT(WINAPI* Fn)(void*, T1, T2, T3, T4);                           \
    return ((Fn)w->real_vtable[IDX])(w->real_di8, a1, a2, a3, a4);                \
  }

GMT_DI8_FWD_4(EnumDevicesImpl, GMT_DI8_EnumDevices, DWORD, void*, void*, DWORD)
GMT_DI8_FWD_1(GetDeviceStatusImpl, GMT_DI8_GetDeviceStatus, const GMT_GUID*)
GMT_DI8_FWD_2(RunControlPanelImpl, GMT_DI8_RunControlPanel, HWND, DWORD)
GMT_DI8_FWD_2(InitializeImpl, GMT_DI8_Initialize, HINSTANCE, DWORD)
GMT_DI8_FWD_3(FindDeviceImpl, GMT_DI8_FindDevice, const GMT_GUID*, const void*, GMT_GUID*)
// EnumDevicesBySemantics has 5 args — define inline.
static HRESULT WINAPI GMT_DI8_EnumDeviceBySemanticsImpl(void* pThis, const void* a1, void* a2, void* a3, void* a4, DWORD a5) {
  GMT_DI8Wrapper* w = GMT_DI8_WRAPPER(pThis);
  typedef HRESULT(WINAPI * Fn)(void*, const void*, void*, void*, void*, DWORD);
  return ((Fn)w->real_vtable[GMT_DI8_EnumDevicesBySemantics])(w->real_di8, a1, a2, a3, a4, a5);
}
GMT_DI8_FWD_4(ConfigureDevicesImpl, GMT_DI8_ConfigureDevices, void*, void*, DWORD, void*)

static void GMT__InitDI8Vtable(void) {
  if (g_di8_vtable_initialized) return;
  g_di8_vtable[GMT_DI8_QueryInterface] = (void*)GMT_DI8_QueryInterfaceImpl;
  g_di8_vtable[GMT_DI8_AddRef] = (void*)GMT_DI8_AddRefImpl;
  g_di8_vtable[GMT_DI8_Release] = (void*)GMT_DI8_ReleaseImpl;
  g_di8_vtable[GMT_DI8_CreateDevice] = (void*)GMT_DI8_CreateDeviceImpl;
  g_di8_vtable[GMT_DI8_EnumDevices] = (void*)GMT_DI8_EnumDevicesImpl;
  g_di8_vtable[GMT_DI8_GetDeviceStatus] = (void*)GMT_DI8_GetDeviceStatusImpl;
  g_di8_vtable[GMT_DI8_RunControlPanel] = (void*)GMT_DI8_RunControlPanelImpl;
  g_di8_vtable[GMT_DI8_Initialize] = (void*)GMT_DI8_InitializeImpl;
  g_di8_vtable[GMT_DI8_FindDevice] = (void*)GMT_DI8_FindDeviceImpl;
  g_di8_vtable[GMT_DI8_EnumDevicesBySemantics] = (void*)GMT_DI8_EnumDeviceBySemanticsImpl;
  g_di8_vtable[GMT_DI8_ConfigureDevices] = (void*)GMT_DI8_ConfigureDevicesImpl;
  g_di8_vtable_initialized = true;
}

// ---- DirectInput8Create hook ----

static HRESULT WINAPI GMT_Hook_DirectInput8Create(HINSTANCE hinst, DWORD dwVersion, const GMT_GUID* riidltf, void** ppvOut, void* punkOuter) {
  HRESULT hr = g_orig_DirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
  if (FAILED(hr) || !ppvOut || !*ppvOut) return hr;

  // Wrap the returned IDirectInput8 so we can intercept CreateDevice.
  GMT__InitDI8Vtable();
  GMT_DI8Wrapper* w = (GMT_DI8Wrapper*)GMT_Alloc(sizeof(GMT_DI8Wrapper));
  if (!w) return hr;  // Can't wrap; return the real object.
  memset(w, 0, sizeof(*w));
  w->vtable = g_di8_vtable;
  w->real_di8 = *ppvOut;
  w->real_vtable = *(void***)*ppvOut;
  w->ref_count = 1;
  *ppvOut = w;
  return hr;
}

// ---- WH_GETMESSAGE hook: strips WM_INPUT during replay ----
//
// Raw Input messages (WM_INPUT) bypass the LL hooks and arrive directly in the
// application's message queue.  This hook fires after GetMessage / PeekMessage
// retrieves a message and nullifies any WM_INPUT so the game never processes
// real raw-input events during replay.

static LRESULT CALLBACK GMT__GetMessageHook(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode >= 0 && InterlockedCompareExchange(&g_replay_hooks_active, 0, 0)) {
    MSG* msg = (MSG*)lParam;
    if (msg && msg->message == WM_INPUT) {
      msg->message = WM_NULL;  // Neutralise the message.
    }
  }
  return CallNextHookEx(g_getmessage_hook, nCode, wParam, lParam);
}

// ---- IAT patching helpers ----

// Patches one IAT entry in a single PE module.  Returns true if the entry was
// found and replaced.  `orig_func` receives the original function pointer if
// it was not already saved (i.e. if *orig_func == NULL on entry).
static bool GMT__PatchIATEntry(HMODULE hModule, const char* dll_name, const void* target_func, const void* new_func, void** orig_func) {
  // Walk the PE headers to find the import directory.
  PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)hModule;
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

  PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((BYTE*)hModule + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

  PIMAGE_DATA_DIRECTORY import_dir =
      &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (import_dir->Size == 0 || import_dir->VirtualAddress == 0) return false;

  PIMAGE_IMPORT_DESCRIPTOR imp =
      (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)hModule + import_dir->VirtualAddress);

  for (; imp->Name != 0; ++imp) {
    const char* name = (const char*)((BYTE*)hModule + imp->Name);
    if (_stricmp(name, dll_name) != 0) continue;

    PIMAGE_THUNK_DATA iat =
        (PIMAGE_THUNK_DATA)((BYTE*)hModule + imp->FirstThunk);

    for (; iat->u1.Function != 0; ++iat) {
      if ((void*)(uintptr_t)iat->u1.Function == target_func) {
        // Save original (only once).
        if (orig_func && *orig_func == NULL) {
          *orig_func = (void*)(uintptr_t)iat->u1.Function;
        }

        // Make the IAT entry writable, patch it, then restore protection.
        DWORD old_protect;
        VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), PAGE_READWRITE, &old_protect);
        iat->u1.Function = (ULONG_PTR)new_func;
        VirtualProtect(&iat->u1.Function, sizeof(iat->u1.Function), old_protect, &old_protect);
        return true;
      }
    }
  }
  return false;
}

// Iterates all loaded modules and patches the IAT entry for a given function.
static void GMT__PatchAllModules(const char* dll_name, const void* target_func, const void* new_func, void** orig_func) {
  HANDLE proc = GetCurrentProcess();
  HMODULE modules[1024];
  DWORD needed = 0;
  if (!EnumProcessModules(proc, modules, sizeof(modules), &needed)) return;

  DWORD count = needed / sizeof(HMODULE);
  for (DWORD i = 0; i < count; ++i) {
    GMT__PatchIATEntry(modules[i], dll_name, target_func, new_func, orig_func);
  }
}

// Restores a single IAT entry across all modules to the original function.
static void GMT__UnpatchAllModules(const char* dll_name, const void* hooked_func, void* orig_func) {
  if (!orig_func) return;

  HANDLE proc = GetCurrentProcess();
  HMODULE modules[1024];
  DWORD needed = 0;
  if (!EnumProcessModules(proc, modules, sizeof(modules), &needed)) return;

  DWORD count = needed / sizeof(HMODULE);
  for (DWORD i = 0; i < count; ++i) {
    // Reuse the same patcher but swap the direction.
    GMT__PatchIATEntry(modules[i], dll_name, hooked_func, orig_func, NULL);
  }
}

// ---- Public helpers called from Platform.h ----

void GMT_Platform_InstallInputHooks(void) {
  // Resolve the original function addresses from user32.dll *before* patching.
  HMODULE user32 = GetModuleHandleA("user32.dll");
  if (user32) {
    if (!g_orig_GetAsyncKeyState)
      g_orig_GetAsyncKeyState = (PFN_GetAsyncKeyState)GetProcAddress(user32, "GetAsyncKeyState");
    if (!g_orig_GetKeyState)
      g_orig_GetKeyState = (PFN_GetKeyState)GetProcAddress(user32, "GetKeyState");
    if (!g_orig_GetKeyboardState)
      g_orig_GetKeyboardState = (PFN_GetKeyboardState)GetProcAddress(user32, "GetKeyboardState");
    if (!g_orig_GetCursorPos)
      g_orig_GetCursorPos = (PFN_GetCursorPos)GetProcAddress(user32, "GetCursorPos");
    if (!g_orig_GetRawInputData)
      g_orig_GetRawInputData = (PFN_GetRawInputData)GetProcAddress(user32, "GetRawInputData");
  }

  // Resolve XInput functions.  Try versioned DLLs, then the generic name.
  {
    const char* xinput_dlls[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll", "XInput1_4.dll", NULL};
    HMODULE xinput = NULL;
    for (int i = 0; xinput_dlls[i]; ++i) {
      xinput = GetModuleHandleA(xinput_dlls[i]);
      if (xinput) break;
    }
    // If no module is loaded yet, try loading the preferred version.
    if (!xinput) xinput = LoadLibraryA("xinput1_4.dll");
    if (xinput) {
      if (!g_orig_XInputGetState)
        g_orig_XInputGetState = (PFN_XInputGetState)GetProcAddress(xinput, "XInputGetState");
      if (!g_orig_XInputSetState)
        g_orig_XInputSetState = (PFN_XInputSetState)GetProcAddress(xinput, "XInputSetState");
      if (!g_orig_XInputGetCapabilities)
        g_orig_XInputGetCapabilities = (PFN_XInputGetCapabilities)GetProcAddress(xinput, "XInputGetCapabilities");
      if (!g_orig_XInputGetKeystroke)
        g_orig_XInputGetKeystroke = (PFN_XInputGetKeystroke)GetProcAddress(xinput, "XInputGetKeystroke");
    }
  }

  // Resolve DirectInput8Create.
  {
    HMODULE dinput8 = GetModuleHandleA("dinput8.dll");
    if (!dinput8) dinput8 = LoadLibraryA("dinput8.dll");
    if (dinput8 && !g_orig_DirectInput8Create) {
      g_orig_DirectInput8Create = (PFN_DirectInput8Create)GetProcAddress(dinput8, "DirectInput8Create");
    }
  }

  // Patch IATs of all loaded modules — user32 functions.
  if (g_orig_GetAsyncKeyState)
    GMT__PatchAllModules("user32.dll", (const void*)g_orig_GetAsyncKeyState, (const void*)GMT_Hook_GetAsyncKeyState, (void**)&g_orig_GetAsyncKeyState);
  if (g_orig_GetKeyState)
    GMT__PatchAllModules("user32.dll", (const void*)g_orig_GetKeyState, (const void*)GMT_Hook_GetKeyState, (void**)&g_orig_GetKeyState);
  if (g_orig_GetKeyboardState)
    GMT__PatchAllModules("user32.dll", (const void*)g_orig_GetKeyboardState, (const void*)GMT_Hook_GetKeyboardState, (void**)&g_orig_GetKeyboardState);
  if (g_orig_GetCursorPos)
    GMT__PatchAllModules("user32.dll", (const void*)g_orig_GetCursorPos, (const void*)GMT_Hook_GetCursorPos, (void**)&g_orig_GetCursorPos);
  if (g_orig_GetRawInputData)
    GMT__PatchAllModules("user32.dll", (const void*)g_orig_GetRawInputData, (const void*)GMT_Hook_GetRawInputData, (void**)&g_orig_GetRawInputData);

  // Patch IATs — XInput functions.
  // XInput is imported under various DLL names; patch all variants.
  {
    const char* xinput_names[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll", NULL};
    for (int i = 0; xinput_names[i]; ++i) {
      if (g_orig_XInputGetState)
        GMT__PatchAllModules(xinput_names[i], (const void*)g_orig_XInputGetState, (const void*)GMT_Hook_XInputGetState, (void**)&g_orig_XInputGetState);
      if (g_orig_XInputSetState)
        GMT__PatchAllModules(xinput_names[i], (const void*)g_orig_XInputSetState, (const void*)GMT_Hook_XInputSetState, (void**)&g_orig_XInputSetState);
      if (g_orig_XInputGetCapabilities)
        GMT__PatchAllModules(xinput_names[i], (const void*)g_orig_XInputGetCapabilities, (const void*)GMT_Hook_XInputGetCapabilities, (void**)&g_orig_XInputGetCapabilities);
      if (g_orig_XInputGetKeystroke)
        GMT__PatchAllModules(xinput_names[i], (const void*)g_orig_XInputGetKeystroke, (const void*)GMT_Hook_XInputGetKeystroke, (void**)&g_orig_XInputGetKeystroke);
    }
  }

  // Patch IATs — DirectInput8Create.
  if (g_orig_DirectInput8Create)
    GMT__PatchAllModules("dinput8.dll", (const void*)g_orig_DirectInput8Create, (const void*)GMT_Hook_DirectInput8Create, (void**)&g_orig_DirectInput8Create);

  // Install WH_GETMESSAGE hook to strip WM_INPUT messages.
  if (!g_getmessage_hook) {
    g_getmessage_hook = SetWindowsHookExA(WH_GETMESSAGE, GMT__GetMessageHook, NULL, GetCurrentThreadId());
  }

  // LL hooks (mouse wheel, keyboard repeat, real-input blocking) are already
  // installed by GMT_Platform_Init for both RECORD and REPLAY modes.
}

void GMT_Platform_RemoveInputHooks(void) {
  // Deactivate hooks first so any in-flight calls fall through.
  InterlockedExchange(&g_replay_hooks_active, 0);

  // Restore abort-signal and unhandled-exception handlers installed by Init.
  // Guards prevent double-restoration if this function is called more than once.
  if (g_prev_sigabrt_handler) {
    signal(SIGABRT, g_prev_sigabrt_handler);
    g_prev_sigabrt_handler = NULL;
  }
  // g_prev_exception_filter may legitimately be NULL (meaning no prior filter
  // was installed); we still call SetUnhandledExceptionFilter to restore the
  // OS default.  The flag guards against double-restoration.
  if (g_exception_filter_installed) {
    SetUnhandledExceptionFilter(g_prev_exception_filter);
    g_prev_exception_filter = NULL;
    g_exception_filter_installed = false;
  }

  // Restore IAT entries — user32 functions.
  GMT__UnpatchAllModules("user32.dll", (const void*)GMT_Hook_GetAsyncKeyState, (void*)g_orig_GetAsyncKeyState);
  GMT__UnpatchAllModules("user32.dll", (const void*)GMT_Hook_GetKeyState, (void*)g_orig_GetKeyState);
  GMT__UnpatchAllModules("user32.dll", (const void*)GMT_Hook_GetKeyboardState, (void*)g_orig_GetKeyboardState);
  GMT__UnpatchAllModules("user32.dll", (const void*)GMT_Hook_GetCursorPos, (void*)g_orig_GetCursorPos);
  GMT__UnpatchAllModules("user32.dll", (const void*)GMT_Hook_GetRawInputData, (void*)g_orig_GetRawInputData);

  g_orig_GetAsyncKeyState = NULL;
  g_orig_GetKeyState = NULL;
  g_orig_GetKeyboardState = NULL;
  g_orig_GetCursorPos = NULL;
  g_orig_GetRawInputData = NULL;

  // Restore IAT entries — XInput functions.
  {
    const char* xinput_names[] = {"xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll", NULL};
    for (int i = 0; xinput_names[i]; ++i) {
      if (g_orig_XInputGetState)
        GMT__UnpatchAllModules(xinput_names[i], (const void*)GMT_Hook_XInputGetState, (void*)g_orig_XInputGetState);
      if (g_orig_XInputSetState)
        GMT__UnpatchAllModules(xinput_names[i], (const void*)GMT_Hook_XInputSetState, (void*)g_orig_XInputSetState);
      if (g_orig_XInputGetCapabilities)
        GMT__UnpatchAllModules(xinput_names[i], (const void*)GMT_Hook_XInputGetCapabilities, (void*)g_orig_XInputGetCapabilities);
      if (g_orig_XInputGetKeystroke)
        GMT__UnpatchAllModules(xinput_names[i], (const void*)GMT_Hook_XInputGetKeystroke, (void*)g_orig_XInputGetKeystroke);
    }
  }
  g_orig_XInputGetState = NULL;
  g_orig_XInputSetState = NULL;
  g_orig_XInputGetCapabilities = NULL;
  g_orig_XInputGetKeystroke = NULL;

  // Restore IAT entries — DirectInput8Create.
  if (g_orig_DirectInput8Create)
    GMT__UnpatchAllModules("dinput8.dll", (const void*)GMT_Hook_DirectInput8Create, (void*)g_orig_DirectInput8Create);
  g_orig_DirectInput8Create = NULL;
  g_di_gamepad_counter = 0;

  // Remove WH_GETMESSAGE hook.
  if (g_getmessage_hook) {
    UnhookWindowsHookEx(g_getmessage_hook);
    g_getmessage_hook = NULL;
  }

  // Remove low-level hooks (only active during replay).
  if (g_mouse_hook) {
    UnhookWindowsHookEx(g_mouse_hook);
    g_mouse_hook = NULL;
  }
  if (g_keyboard_hook) {
    UnhookWindowsHookEx(g_keyboard_hook);
    g_keyboard_hook = NULL;
  }
}

void GMT_Platform_SetReplayedInput(const GMT_InputState* input) {
  if (input) {
    // Update toggle states for keys that transitioned from up to down.
    for (int k = 1; k < GMT_KEY_COUNT; ++k) {
      int vk = k_vk[k];
      if (vk > 0 && vk < 256) {
        bool was_down = (g_replayed_input.keys[k] & 0x80u) != 0;
        bool is_down = (input->keys[k] & 0x80u) != 0;
        if (!was_down && is_down) {
          g_replayed_toggle_state[vk] ^= 1;
        }
      }
    }

    // Update generic modifier toggle states.
    bool shift_was = ((g_replayed_input.keys[GMT_Key_LEFT_SHIFT] | g_replayed_input.keys[GMT_Key_RIGHT_SHIFT]) & 0x80u) != 0;
    bool shift_is = ((input->keys[GMT_Key_LEFT_SHIFT] | input->keys[GMT_Key_RIGHT_SHIFT]) & 0x80u) != 0;
    if (!shift_was && shift_is) g_replayed_toggle_state[VK_SHIFT] ^= 1;

    bool ctrl_was = ((g_replayed_input.keys[GMT_Key_LEFT_CTRL] | g_replayed_input.keys[GMT_Key_RIGHT_CTRL]) & 0x80u) != 0;
    bool ctrl_is = ((input->keys[GMT_Key_LEFT_CTRL] | input->keys[GMT_Key_RIGHT_CTRL]) & 0x80u) != 0;
    if (!ctrl_was && ctrl_is) g_replayed_toggle_state[VK_CONTROL] ^= 1;

    bool alt_was = ((g_replayed_input.keys[GMT_Key_LEFT_ALT] | g_replayed_input.keys[GMT_Key_RIGHT_ALT]) & 0x80u) != 0;
    bool alt_is = ((input->keys[GMT_Key_LEFT_ALT] | input->keys[GMT_Key_RIGHT_ALT]) & 0x80u) != 0;
    if (!alt_was && alt_is) g_replayed_toggle_state[VK_MENU] ^= 1;

    memcpy(&g_replayed_input, input, sizeof(g_replayed_input));
  }
}

void GMT_Platform_SetReplayHooksActive(bool active) {
  InterlockedExchange(&g_replay_hooks_active, active ? 1 : 0);
}

// ===== Crash / abort safety net for non-GMT assertions =====
//
// Any assertion outside the GMT framework (CRT assert(), third-party libs,
// custom abort() calls, access violations, etc.) will not set test_failed and
// therefore won't release the input-blocking LL hooks through the normal path.
// Two OS-level handlers catch these cases and call RemoveInputHooks() so that
// the resulting dialog is fully interactive.

// Called when any code calls abort() or the CRT assert() macro fires.
static void GMT__AbortSignalHandler(int sig) {
  (void)sig;
  // Unhook first (also restores the original SIGABRT handler and SEH filter).
  GMT_Platform_RemoveInputHooks();
  // Re-raise with the default handler so the normal abort dialog / core dump
  // appears exactly as it would without GameTest.
  signal(SIGABRT, SIG_DFL);
  raise(SIGABRT);
}

// Called for unhandled SEH exceptions: access violations, __debugbreak() with
// no debugger attached, and similar crashes.
static LONG WINAPI GMT__UnhandledExceptionFilter(EXCEPTION_POINTERS* ep) {
  GMT_Platform_RemoveInputHooks();
  // Pass control to whatever filter was installed before us (or the OS default).
  if (g_prev_exception_filter) {
    return g_prev_exception_filter(ep);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

double GMT_Platform_GetTime(void) {
  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  return (double)(now.QuadPart - g_perf_origin.QuadPart) * g_perf_freq_inv;
}

void GMT_Platform_Init(void) {
  InitializeCriticalSection(&g_mutex);

  // Initialize high-resolution timer.  Store the current counter as origin so
  // that GMT_Platform_GetTime returns values relative to init, keeping the
  // integer-to-double conversion small and maximizing floating-point precision.
  {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_perf_freq_inv = 1.0 / (double)freq.QuadPart;
    QueryPerformanceCounter(&g_perf_origin);
  }

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

  // Install low-level mouse and keyboard hooks for wheel-delta and key-repeat
  // accumulation.  Needed in both RECORD and REPLAY modes: in REPLAY the hooks
  // also block real (non-injected) events; in RECORD they pass events through
  // while still accumulating wheel and repeat counters.
  if (!g_mouse_hook) {
    g_mouse_hook = SetWindowsHookExA(WH_MOUSE_LL, GMT__MouseLLHook, NULL, 0);
  }
  if (!g_keyboard_hook) {
    g_keyboard_hook = SetWindowsHookExA(WH_KEYBOARD_LL, GMT__KeyboardLLHook, NULL, 0);
  }

  // Install crash / abort safety nets so that non-GMT assertions (CRT assert(),
  // third-party libs, raw abort() calls, SEH crashes) still remove the
  // input-blocking hooks before any dialog is displayed.
  if (!g_prev_sigabrt_handler) {
    void (*prev)(int) = signal(SIGABRT, GMT__AbortSignalHandler);
    if (prev != SIG_ERR) {
      g_prev_sigabrt_handler = prev ? prev : SIG_DFL;
    }
  }
  if (!g_exception_filter_installed) {
    // SetUnhandledExceptionFilter returns NULL when there was no previous
    // filter; we save that too so we can restore the same state on cleanup.
    g_prev_exception_filter = SetUnhandledExceptionFilter(GMT__UnhandledExceptionFilter);
    g_exception_filter_installed = true;
  }
}

void GMT_Platform_Quit(void) {
  // Remove IAT hooks, WH_GETMESSAGE hook, and LL hooks before anything else.
  // RemoveInputHooks also handles the LL hooks when they were installed for replay.
  GMT_Platform_RemoveInputHooks();

  g_wheel_x = 0;
  g_wheel_y = 0;
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
  // When IAT hooks are installed our own calls would be intercepted too.
  // Call through the saved original pointers to always get real hardware state.
  PFN_GetCursorPos fn_gcp = g_orig_GetCursorPos ? g_orig_GetCursorPos : GetCursorPos;
  PFN_GetAsyncKeyState fn_gaks = g_orig_GetAsyncKeyState ? g_orig_GetAsyncKeyState : GetAsyncKeyState;

  // Use GetAsyncKeyState (physical hardware state) instead of GetKeyboardState
  // (message-synchronised state) so that key presses are captured on the frame
  // they physically occur, not delayed until the message queue is pumped.  This
  // eliminates a one-frame recording lag that caused replay divergence.
  out->keys[GMT_Key_UNKNOWN] = 0;
  for (int k = 1; k < GMT_KEY_COUNT; ++k) {
    int vk = k_vk[k];
    out->keys[k] = (vk != 0 && (fn_gaks(vk) & 0x8000)) ? 0x80u : 0;
  }

  // Read and reset per-key repeat accumulators atomically, clamping to uint8_t.
  out->key_repeats[GMT_Key_UNKNOWN] = 0;
  for (int k = 1; k < GMT_KEY_COUNT; ++k) {
    LONG r = InterlockedExchange(&g_key_repeats[k], 0);
    out->key_repeats[k] = (r > 255) ? 255 : (uint8_t)r;
  }

  POINT pt = {0, 0};
  fn_gcp(&pt);
  out->mouse_x = (int32_t)pt.x;
  out->mouse_y = (int32_t)pt.y;

  // Atomically read and reset the wheel accumulators.
  out->mouse_wheel_x = (int32_t)InterlockedExchange(&g_wheel_x, 0);
  out->mouse_wheel_y = (int32_t)InterlockedExchange(&g_wheel_y, 0);

  GMT_MouseButtons buttons = 0;
  if (fn_gaks(VK_LBUTTON) & 0x8000) buttons |= GMT_MouseButton_LEFT;
  if (fn_gaks(VK_RBUTTON) & 0x8000) buttons |= GMT_MouseButton_RIGHT;
  if (fn_gaks(VK_MBUTTON) & 0x8000) buttons |= GMT_MouseButton_MIDDLE;
  if (fn_gaks(VK_XBUTTON1) & 0x8000) buttons |= GMT_MouseButton_X1;
  if (fn_gaks(VK_XBUTTON2) & 0x8000) buttons |= GMT_MouseButton_X2;
  // Bits 5–7 (GMT_MouseButton_5/6/7) have no Win32 mapping; left as 0.
  out->mouse_buttons = buttons;

  // ---- Gamepad state (XInput) ----
  {
    PFN_XInputGetState fn_xgs = g_orig_XInputGetState;
    for (int i = 0; i < GMT_MAX_GAMEPADS; ++i) {
      GMT_GamepadState* gp = &out->gamepads[i];
      if (fn_xgs) {
        XINPUT_STATE xs;
        memset(&xs, 0, sizeof(xs));
        DWORD res = fn_xgs((DWORD)i, &xs);
        if (res == ERROR_SUCCESS) {
          gp->connected = 1;
          gp->buttons = xs.Gamepad.wButtons;
          gp->left_trigger = xs.Gamepad.bLeftTrigger;
          gp->right_trigger = xs.Gamepad.bRightTrigger;
          gp->left_stick_x = xs.Gamepad.sThumbLX;
          gp->left_stick_y = xs.Gamepad.sThumbLY;
          gp->right_stick_x = xs.Gamepad.sThumbRX;
          gp->right_stick_y = xs.Gamepad.sThumbRY;
        } else {
          memset(gp, 0, sizeof(*gp));
        }
      } else {
        memset(gp, 0, sizeof(*gp));
      }
    }
  }
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
  INPUT* inputs = GMT_Alloc((size_t)capacity * sizeof(INPUT));
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
  GMT_Free(inputs);

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
  // Use SendInput with MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK so that
  // the resulting event is flagged as LLMHF_INJECTED and passes through our
  // mouse LL hook (which blocks non-injected movement during replay).
  // Coordinates are normalised to [0, 65535] across the entire virtual desktop.
  {
    int vsx = GetSystemMetrics(SM_XVIRTUALSCREEN);   // Left edge.
    int vsy = GetSystemMetrics(SM_YVIRTUALSCREEN);   // Top edge.
    int vsw = GetSystemMetrics(SM_CXVIRTUALSCREEN);  // Width.
    int vsh = GetSystemMetrics(SM_CYVIRTUALSCREEN);  // Height.

    if (vsw > 0 && vsh > 0) {
      INPUT mi = {0};
      mi.type = INPUT_MOUSE;
      mi.mi.dx = (LONG)((((double)new_input->mouse_x - vsx) / vsw) * 65535.0);
      mi.mi.dy = (LONG)((((double)new_input->mouse_y - vsy) / vsh) * 65535.0);
      mi.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
      mi.mi.time = 0;
      mi.mi.dwExtraInfo = 0;
      SendInput(1, &mi, sizeof(INPUT));
    }
  }
}

// ===== Mutex =====

void GMT_Platform_MutexLock(void) {
  EnterCriticalSection(&g_mutex);
}

void GMT_Platform_MutexUnlock(void) {
  LeaveCriticalSection(&g_mutex);
}

// ===== Threading =====
