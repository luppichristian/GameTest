/*
 * InputState.h - Snapshot of all input for a single frame.
 *
 * GMT_InputState bundles every piece of per-frame input data (keyboard, mouse
 * position, mouse wheel, and mouse buttons) into one struct.  It is the unit of
 * data passed to GMT_Platform_CaptureInput, GMT_Platform_InjectInput, and
 * written / read by the record / replay engine.
 */

#pragma once

#include <stdint.h>

// ===== Normalized Key Identifiers =====
//
// GMT_Key is a platform-independent key identifier used in the test file format.
// The Win32 layer (and any future platform layer) maps between GMT_Key values and
// the OS-specific key representation at capture and injection time, so recorded
// files never contain platform-specific codes and remain portable across platforms.

typedef enum GMT_Key {
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

typedef enum GMT_MouseButton {
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

// ===== Per-frame input snapshot =====

typedef struct GMT_InputState {
  // Per-key pressed state: 0x80 if pressed, 0 otherwise.  Indexed by GMT_Key.
  uint8_t keys[GMT_KEY_COUNT];

  // Per-key auto-repeat count: number of additional key-down events accumulated
  // since the previous frame (0 for a key that was just pressed or not held).
  uint8_t key_repeats[GMT_KEY_COUNT];

  // Absolute screen position of the cursor in pixels.
  int32_t mouse_x;
  int32_t mouse_y;

  // Wheel delta accumulated this frame (positive = right / up).
  int32_t mouse_wheel_x;
  int32_t mouse_wheel_y;

  // Bitmask of currently pressed mouse buttons (GMT_MouseButton flags).
  GMT_MouseButtons mouse_buttons;
} GMT_InputState;

// Zeroes every field in *s.
void GMT_InputState_Clear(GMT_InputState* s);