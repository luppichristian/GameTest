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

// ===== File I/O =====

typedef void* GMT_FileHandle;
#define GMT_INVALID_FILE_HANDLE ((GMT_FileHandle)(NULL))

// Opens `path` for sequential binary write. Creates the file (or truncates it if it already exists).
// Returns true and sets *out_handle on success.
bool GMT_Platform_FileOpenWrite(const char* path, GMT_FileHandle* out_handle);

// Opens `path` for sequential binary read.
// Returns true and sets *out_handle on success.
bool GMT_Platform_FileOpenRead(const char* path, GMT_FileHandle* out_handle);

// Closes a file handle previously opened by GMT_Platform_FileOpenWrite/Read.
void GMT_Platform_FileClose(GMT_FileHandle handle);

// Writes exactly `size` bytes from `data` to the file.  Returns false on failure.
bool GMT_Platform_FileWrite(GMT_FileHandle handle, const void* data, size_t size);

// Reads exactly `size` bytes into `out_data`.  Returns false if fewer bytes are available.
bool GMT_Platform_FileRead(GMT_FileHandle handle, void* out_data, size_t size);

// Reads the entire contents of `path` into a heap buffer allocated with malloc.
// The caller must free the buffer with GMT_Platform_FreeBuffer.
// Returns false if the file cannot be opened.
bool GMT_Platform_FileReadAll(const char* path, uint8_t** out_data, size_t* out_size);

// Frees a buffer previously returned by GMT_Platform_FileReadAll.
void GMT_Platform_FreeBuffer(uint8_t* data);

// Returns true if the file at `path` exists.
bool GMT_Platform_FileExists(const char* path);

// ===== Directory =====

// Sets the process working directory.
void GMT_Platform_SetWorkDir(const char* path);

// Creates `path` and all intermediate directories (like mkdir -p).
// Returns true on success or if the directory already exists.
bool GMT_Platform_CreateDirRecursive(const char* path);

// ===== Normalized Key Identifiers =====
//
// GMT_Key is a platform-independent key identifier used in the test file format.
// The Win32 layer (and any future platform layer) maps between GMT_Key values and
// the OS-specific key representation at capture and injection time, so recorded
// files never contain platform-specific codes and remain portable across platforms.

typedef enum {
  GMT_Key_UNKNOWN = 0,

  // Letters
  GMT_Key_A,
  GMT_Key_B,
  GMT_Key_C,
  GMT_Key_D,
  GMT_Key_E,
  GMT_Key_F,
  GMT_Key_G,
  GMT_Key_H,
  GMT_Key_I,
  GMT_Key_J,
  GMT_Key_K,
  GMT_Key_L,
  GMT_Key_M,
  GMT_Key_N,
  GMT_Key_O,
  GMT_Key_P,
  GMT_Key_Q,
  GMT_Key_R,
  GMT_Key_S,
  GMT_Key_T,
  GMT_Key_U,
  GMT_Key_V,
  GMT_Key_W,
  GMT_Key_X,
  GMT_Key_Y,
  GMT_Key_Z,

  // Top-row digits
  GMT_Key_0,
  GMT_Key_1,
  GMT_Key_2,
  GMT_Key_3,
  GMT_Key_4,
  GMT_Key_5,
  GMT_Key_6,
  GMT_Key_7,
  GMT_Key_8,
  GMT_Key_9,

  // Function keys
  GMT_Key_F1,
  GMT_Key_F2,
  GMT_Key_F3,
  GMT_Key_F4,
  GMT_Key_F5,
  GMT_Key_F6,
  GMT_Key_F7,
  GMT_Key_F8,
  GMT_Key_F9,
  GMT_Key_F10,
  GMT_Key_F11,
  GMT_Key_F12,

  // Arrow keys
  GMT_Key_UP,
  GMT_Key_DOWN,
  GMT_Key_LEFT,
  GMT_Key_RIGHT,

  // Navigation cluster
  GMT_Key_HOME,
  GMT_Key_END,
  GMT_Key_PAGE_UP,
  GMT_Key_PAGE_DOWN,
  GMT_Key_INSERT,
  GMT_Key_DELETE,

  // Editing / whitespace
  GMT_Key_BACKSPACE,
  GMT_Key_TAB,
  GMT_Key_ENTER,
  GMT_Key_ESCAPE,
  GMT_Key_SPACE,
  GMT_Key_CAPS_LOCK,

  // Modifiers
  GMT_Key_LEFT_SHIFT,
  GMT_Key_RIGHT_SHIFT,
  GMT_Key_LEFT_CTRL,
  GMT_Key_RIGHT_CTRL,
  GMT_Key_LEFT_ALT,
  GMT_Key_RIGHT_ALT,
  GMT_Key_LEFT_SUPER,
  GMT_Key_RIGHT_SUPER,

  // Numpad  (KP_ENTER intentionally omitted: Win32 shares VK_RETURN with ENTER)
  GMT_Key_KP_0,
  GMT_Key_KP_1,
  GMT_Key_KP_2,
  GMT_Key_KP_3,
  GMT_Key_KP_4,
  GMT_Key_KP_5,
  GMT_Key_KP_6,
  GMT_Key_KP_7,
  GMT_Key_KP_8,
  GMT_Key_KP_9,
  GMT_Key_KP_DECIMAL,
  GMT_Key_KP_ADD,
  GMT_Key_KP_SUBTRACT,
  GMT_Key_KP_MULTIPLY,
  GMT_Key_KP_DIVIDE,
  GMT_Key_NUM_LOCK,

  // Punctuation / symbols (US layout names)
  GMT_Key_MINUS,          // -  (_)
  GMT_Key_EQUAL,          // =  (+)
  GMT_Key_LEFT_BRACKET,   // [  ({)
  GMT_Key_RIGHT_BRACKET,  // ]  (})
  GMT_Key_BACKSLASH,      // \  (|)
  GMT_Key_SEMICOLON,      // ;  (:)
  GMT_Key_APOSTROPHE,     // '  (")
  GMT_Key_COMMA,          // ,  (<)
  GMT_Key_PERIOD,         // .  (>)
  GMT_Key_SLASH,          // /  (?)
  GMT_Key_GRAVE,          // `  (~)

  // Miscellaneous
  GMT_Key_PRINT_SCREEN,
  GMT_Key_SCROLL_LOCK,
  GMT_Key_PAUSE,
  GMT_Key_MENU,

  GMT_KEY_COUNT  // Sentinel — total number of key identifiers.
} GMT_Key;

// ===== Mouse Button Identifiers =====
//
// GMT_MouseButton values are bit flags that compose into a GMT_MouseButtons bitmask.
// Each platform layer maps its OS-specific button identifiers to these bits at
// capture and injection time, so the file format remains platform-independent.
// Bits 5–7 (GMT_MouseButton_5/6/7) are reserved for platforms with extra buttons;
// Win32 captures them as 0 since there is no standard VK mapping beyond X2.

typedef enum {
  GMT_MouseButton_LEFT = (1u << 0),    // Primary button.
  GMT_MouseButton_RIGHT = (1u << 1),   // Secondary button.
  GMT_MouseButton_MIDDLE = (1u << 2),  // Middle / scroll-wheel click.
  GMT_MouseButton_X1 = (1u << 3),      // Extended button 1 (browser back).
  GMT_MouseButton_X2 = (1u << 4),      // Extended button 2 (browser forward).
  GMT_MouseButton_5 = (1u << 5),       // Platform-specific extra button.
  GMT_MouseButton_6 = (1u << 6),       // Platform-specific extra button.
  GMT_MouseButton_7 = (1u << 7),       // Platform-specific extra button.
} GMT_MouseButton;

// Bitmask of zero or more GMT_MouseButton flags packed into a single byte.
typedef uint8_t GMT_MouseButtons;

// Installs any platform hooks required for input capture (e.g. mouse wheel).
// Must be called once before the first GMT_Platform_CaptureInput.
void GMT_Platform_InputInit(void);

// Removes the hooks installed by GMT_Platform_InputInit.
void GMT_Platform_InputQuit(void);

// ===== Input Capture (RECORD mode) =====

// Captures the current key state into out_keys[GMT_KEY_COUNT].
// Each byte is 0x80 if that GMT_Key is pressed, 0 otherwise.
// out_key_repeats[GMT_KEY_COUNT] receives the number of auto-repeat key-down events
// accumulated since the last call (0 for a key that was just pressed this frame or not held).
// Mouse coordinates are absolute screen pixel positions.
// Mouse wheel x/y are the accumulated delta since the last call (positive = right/up), then reset to 0.
// Mouse buttons: a GMT_MouseButtons bitmask of currently pressed buttons.
void GMT_Platform_CaptureInput(
    uint8_t out_keys[GMT_KEY_COUNT],
    uint8_t out_key_repeats[GMT_KEY_COUNT],
    int32_t* out_mouse_x,
    int32_t* out_mouse_y,
    int32_t* out_mouse_wheel_x,
    int32_t* out_mouse_wheel_y,
    GMT_MouseButtons* out_mouse_buttons);

// ===== Input Injection (REPLAY mode) =====

// Injects a delta of input events for one frame.
//   new_keys / prev_keys : current and previous GMT_Key state arrays.
//   key_repeats          : per-key count of additional key-down (auto-repeat) events to inject.
// Mouse coordinates are absolute screen pixel positions.
// Mouse wheel x/y are the delta to inject for this frame (positive = right/up).
// Only keys whose state changed are emitted so the application's input queue stays consistent.
void GMT_Platform_InjectInput(
    const uint8_t new_keys[GMT_KEY_COUNT],
    const uint8_t prev_keys[GMT_KEY_COUNT],
    const uint8_t key_repeats[GMT_KEY_COUNT],
    int32_t mouse_x,
    int32_t mouse_y,
    int32_t mouse_wheel_x,
    int32_t mouse_wheel_y,
    GMT_MouseButtons new_mouse_buttons,
    GMT_MouseButtons prev_mouse_buttons);

// ===== Mutex =====

// Thin wrapper around a platform recursive lock.
typedef struct {
  void* opaque;
} GMT_Mutex;

void GMT_Platform_MutexCreate(GMT_Mutex* m);
void GMT_Platform_MutexDestroy(GMT_Mutex* m);
void GMT_Platform_MutexLock(GMT_Mutex* m);
void GMT_Platform_MutexUnlock(GMT_Mutex* m);
