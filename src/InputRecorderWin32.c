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
#include <commdlg.h>
#include "UtilityWin32.h"
#include "GameTest/InputRecorder.h"

static FILE* s_file = NULL;
static bool s_recording = false;
static LARGE_INTEGER s_startTime;
static LARGE_INTEGER s_frequency;
static HHOOK s_kbHook = NULL;
static HHOOK s_mouseHook = NULL;
static HANDLE s_hookThread = NULL;
static DWORD s_hookThreadId = 0;
static HANDLE s_hookReadyEvent = NULL;

static volatile float s_scrollDeltaX = 0.0f;
static volatile float s_scrollDeltaY = 0.0f;
static volatile uint8_t s_repeatCountVK[256];
static volatile uint8_t s_prevKeyDownVK[256];
static volatile uint32_t s_textInput[32];
static volatile size_t s_textInputCount = 0;
static CRITICAL_SECTION s_cs;

/* ---- UI window state --------------------------------------------------- */
static volatile bool s_paused = false;
static char s_wndFilename[MAX_PATH] = "recording.gtrec";
static HWND s_uiHwnd = NULL;
static HANDLE s_uiThread = NULL;

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
  s_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, KbHookProc, NULL, 0);
  s_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, NULL, 0);
  /* Signal Start() – hooks are now installed (or failed). */
  SetEvent(s_hookReadyEvent);
  /* Run the message loop that keeps LL hook callbacks firing. */
  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (s_kbHook) {
    UnhookWindowsHookEx(s_kbHook);
    s_kbHook = NULL;
  }
  if (s_mouseHook) {
    UnhookWindowsHookEx(s_mouseHook);
    s_mouseHook = NULL;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// UI window – minimal no-title-bar toolbar with 3 icon buttons
// ---------------------------------------------------------------------------

#define GT_BTN_SIZE   48
#define GT_BTN_MARGIN 8
#define GT_BTN_BORDER 6
#define GT_BTN_COUNT  3

/* Total window client size */
#define GT_WND_W (GT_BTN_BORDER * 2 + GT_BTN_COUNT * (GT_BTN_SIZE + GT_BTN_MARGIN) + GT_BTN_MARGIN)
#define GT_WND_H (GT_BTN_BORDER * 2 + GT_BTN_MARGIN * 2 + GT_BTN_SIZE)

static const COLORREF s_cBg = RGB(28, 28, 28);
static const COLORREF s_cBtnNorm = RGB(50, 50, 50);
static const COLORREF s_cBtnHover = RGB(72, 72, 72);
static const COLORREF s_cBtnBorder = RGB(90, 90, 90);
static const COLORREF s_cRed = RGB(210, 50, 50);
static const COLORREF s_cYellow = RGB(200, 190, 50);
static const COLORREF s_cIcon = RGB(210, 210, 210);
static const COLORREF s_cDisabled = RGB(80, 80, 80);

static int s_hotBtn = -1; /* index of hovered button, -1 = none */

static RECT BtnRect(int idx) {
  RECT r;
  r.left = GT_BTN_BORDER + GT_BTN_MARGIN + idx * (GT_BTN_SIZE + GT_BTN_MARGIN);
  r.top = GT_BTN_BORDER + GT_BTN_MARGIN;
  r.right = r.left + GT_BTN_SIZE;
  r.bottom = r.top + GT_BTN_SIZE;
  return r;
}

/* Draw record (circle) or stop (square) icon */
static void DrawRecordStopIcon(HDC hdc, RECT r, bool isRecording) {
  int pad = 11;
  if (isRecording) {
    HBRUSH br = CreateSolidBrush(s_cRed);
    RECT sq = {r.left + pad, r.top + pad, r.right - pad, r.bottom - pad};
    FillRect(hdc, &sq, br);
    DeleteObject(br);
  } else {
    HBRUSH br = CreateSolidBrush(s_cRed);
    HPEN pen = CreatePen(PS_NULL, 0, 0);
    SelectObject(hdc, br);
    SelectObject(hdc, pen);
    Ellipse(hdc, r.left + pad, r.top + pad, r.right - pad, r.bottom - pad);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    SelectObject(hdc, GetStockObject(NULL_PEN));
    DeleteObject(br);
    DeleteObject(pen);
  }
}

/* Draw pause (two bars) or resume (triangle) icon */
static void DrawPauseResumeIcon(HDC hdc, RECT r, bool isPaused, bool enabled) {
  COLORREF col = enabled ? s_cYellow : s_cDisabled;
  int pad = 11;
  int cy = (r.top + r.bottom) / 2;
  if (isPaused) {
    /* Resume: right-pointing triangle */
    HBRUSH br = CreateSolidBrush(col);
    HPEN pen = CreatePen(PS_NULL, 0, 0);
    SelectObject(hdc, br);
    SelectObject(hdc, pen);
    POINT pts[3] = {
        {     r.left + pad,    r.top + pad},
        {     r.left + pad, r.bottom - pad},
        {r.right - pad + 1,             cy}
    };
    Polygon(hdc, pts, 3);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    SelectObject(hdc, GetStockObject(NULL_PEN));
    DeleteObject(br);
    DeleteObject(pen);
  } else {
    /* Pause: two vertical bars */
    HBRUSH br = CreateSolidBrush(col);
    int barW = (r.right - r.left - pad * 2 - 4) / 2;
    RECT b1 = {r.left + pad, r.top + pad, r.left + pad + barW, r.bottom - pad};
    RECT b2 = {r.right - pad - barW, r.top + pad, r.right - pad, r.bottom - pad};
    FillRect(hdc, &b1, br);
    FillRect(hdc, &b2, br);
    DeleteObject(br);
  }
}

/* Draw folder icon for "select output name" */
static void DrawFolderIcon(HDC hdc, RECT r) {
  int pad = 10;
  int cx = (r.left + r.right) / 2;
  int cy = (r.top + r.bottom) / 2;
  HBRUSH br = CreateSolidBrush(s_cIcon);
  HPEN pen = CreatePen(PS_NULL, 0, 0);
  SelectObject(hdc, br);
  SelectObject(hdc, pen);
  /* Body */
  RECT body = {r.left + pad, cy - 4, r.right - pad, r.bottom - pad};
  FillRect(hdc, &body, br);
  /* Tab */
  RECT tab = {r.left + pad, r.top + pad + 4, cx, cy - 4};
  FillRect(hdc, &tab, br);
  SelectObject(hdc, GetStockObject(NULL_BRUSH));
  SelectObject(hdc, GetStockObject(NULL_PEN));
  DeleteObject(br);
  DeleteObject(pen);
}

static void PaintRecorderWindow(HWND hwnd) {
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hwnd, &ps);
  RECT cr;
  GetClientRect(hwnd, &cr);

  /* Background */
  HBRUSH bgBr = CreateSolidBrush(s_cBg);
  FillRect(hdc, &cr, bgBr);
  DeleteObject(bgBr);

  bool rec = s_recording;
  bool paused = (bool)s_paused;

  for (int i = 0; i < GT_BTN_COUNT; i++) {
    RECT br = BtnRect(i);

    /* Button fill */
    HBRUSH btnBr = CreateSolidBrush((s_hotBtn == i) ? s_cBtnHover : s_cBtnNorm);
    FillRect(hdc, &br, btnBr);
    DeleteObject(btnBr);

    /* Button border */
    HPEN borderPen = CreatePen(PS_SOLID, 1, s_cBtnBorder);
    HGDIOBJ op = SelectObject(hdc, borderPen);
    HGDIOBJ ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, br.left, br.top, br.right, br.bottom);
    SelectObject(hdc, op);
    SelectObject(hdc, ob);
    DeleteObject(borderPen);

    /* Icon */
    if (i == 0) DrawRecordStopIcon(hdc, br, rec);
    else if (i == 1)
      DrawPauseResumeIcon(hdc, br, paused, rec);
    else
      DrawFolderIcon(hdc, br);
  }

  EndPaint(hwnd, &ps);
}

static int HitTestBtn(int x, int y) {
  for (int i = 0; i < GT_BTN_COUNT; i++) {
    RECT r = BtnRect(i);
    POINT pt = {x, y};
    if (PtInRect(&r, pt)) return i;
  }
  return -1;
}

static LRESULT CALLBACK RecorderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_PAINT:
      PaintRecorderWindow(hwnd);
      return 0;

    case WM_NCHITTEST: {
      /* Let the background act as a caption so the user can drag the window */
      LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
      if (hit == HTCLIENT) return HTCAPTION;
      return hit;
    }

    case WM_MOUSEMOVE: {
      int x = (int)(short)LOWORD(lParam);
      int y = (int)(short)HIWORD(lParam);
      int prev = s_hotBtn;
      s_hotBtn = HitTestBtn(x, y);
      if (s_hotBtn != prev) {
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;
    }

    case WM_MOUSELEAVE:
      s_hotBtn = -1;
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;

    case WM_LBUTTONDOWN: {
      int x = (int)(short)LOWORD(lParam);
      int y = (int)(short)HIWORD(lParam);
      int btn = HitTestBtn(x, y);
      if (btn == 0) {
        /* Record / Stop */
        if (!s_recording) {
          GameTest_InputRecorder_Start(s_wndFilename);
        } else {
          GameTest_InputRecorder_Stop();
          s_paused = false;
        }
        InvalidateRect(hwnd, NULL, FALSE);
      } else if (btn == 1) {
        /* Pause / Resume */
        if (s_paused) {
          GameTest_InputRecorder_Resume();
        } else {
          GameTest_InputRecorder_Pause();
        }
      } else if (btn == 2) {
        /* Select output name */
        OPENFILENAMEW ofn;
        WCHAR wpath[MAX_PATH] = L"recording.gtrec";
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = wpath;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = L"GameTest Recording\0*.gtrec\0All Files\0*.*\0";
        ofn.lpstrDefExt = L"gtrec";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (GetSaveFileNameW(&ofn)) {
          WideCharToMultiByte(CP_UTF8, 0, wpath, -1, s_wndFilename, MAX_PATH, NULL, NULL);
        }
      }
      return 0;
    }

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static DWORD WINAPI RecorderUIThreadProc(LPVOID unused) {
  (void)unused;

  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = RecorderWndProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
  wc.hbrBackground = NULL;
  wc.lpszClassName = L"GameTest_RecorderUI";
  RegisterClassExW(&wc);

  /* Position: top-right corner of primary monitor */
  int x = GetSystemMetrics(SM_CXSCREEN) - GT_WND_W - 20;
  int y = 20;

  s_uiHwnd = CreateWindowExW(
      WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
      L"GameTest_RecorderUI",
      L"",
      WS_POPUP | WS_BORDER,
      x,
      y,
      GT_WND_W,
      GT_WND_H,
      NULL,
      NULL,
      GetModuleHandleW(NULL),
      NULL);
  if (!s_uiHwnd) return 1;

  ShowWindow(s_uiHwnd, SW_SHOWNOACTIVATE);
  UpdateWindow(s_uiHwnd);

  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  s_uiHwnd = NULL;
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
  if (!s_hookReadyEvent) {
    fclose(s_file);
    s_file = NULL;
    DeleteCriticalSection(&s_cs);
    return false;
  }

  s_hookThread = CreateThread(NULL, 0, RecorderHookThreadProc, NULL, 0, &s_hookThreadId);
  if (!s_hookThread) {
    CloseHandle(s_hookReadyEvent);
    s_hookReadyEvent = NULL;
    fclose(s_file);
    s_file = NULL;
    DeleteCriticalSection(&s_cs);
    return false;
  }

  /* Wait until RecorderHookThreadProc has called SetWindowsHookExW. */
  WaitForSingleObject(s_hookReadyEvent, INFINITE);
  CloseHandle(s_hookReadyEvent);
  s_hookReadyEvent = NULL;

  if (!s_kbHook || !s_mouseHook) {
    PostThreadMessageW(s_hookThreadId, WM_QUIT, 0, 0);
    WaitForSingleObject(s_hookThread, INFINITE);
    CloseHandle(s_hookThread);
    s_hookThread = NULL;
    s_hookThreadId = 0;
    fclose(s_file);
    s_file = NULL;
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
  CloseHandle(s_hookThread);
  s_hookThread = NULL;
  s_hookThreadId = 0;

  fclose(s_file);
  s_file = NULL;
  s_recording = false;
  DeleteCriticalSection(&s_cs);
  return true;
}

GAME_TEST_API bool GameTest_InputRecorder_Update(void) {
  if (!s_recording) return false;
  if (s_paused) return true; /* paused: keep recording open but skip this frame */

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

GAME_TEST_API bool GameTest_InputRecorder_IsRecording(void) {
  return s_recording;
}

GAME_TEST_API bool GameTest_InputRecorder_Pause(void) {
  if (!s_recording || s_paused) return false;
  s_paused = true;
  if (s_uiHwnd) InvalidateRect(s_uiHwnd, NULL, FALSE);
  return true;
}

GAME_TEST_API bool GameTest_InputRecorder_Resume(void) {
  if (!s_recording || !s_paused) return false;
  s_paused = false;
  if (s_uiHwnd) InvalidateRect(s_uiHwnd, NULL, FALSE);
  return true;
}

GAME_TEST_API bool GameTest_InputRecorder_IsPaused(void) {
  return (bool)s_paused;
}

GAME_TEST_API bool GameTest_InputRecorder_OpenWindow(void) {
  if (s_uiThread) return false; /* already open */
  s_uiThread = CreateThread(NULL, 0, RecorderUIThreadProc, NULL, 0, NULL);
  return s_uiThread != NULL;
}