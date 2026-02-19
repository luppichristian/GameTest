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
#include <commdlg.h>
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
static HICON s_uiIcon = NULL;
/* Manual tooltip */
static HWND s_ttWnd = NULL; /* floating label window       */
static int s_ttBtn = -1;    /* button tip is pending/shown for */

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
#define GT_BTN_COUNT  5
#define GT_TT_TIMER   42
#define GT_TT_DELAY   450

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

/* ---------------------------------------------------------------------------
   Icon rendering helpers
   Each icon is drawn at GT_SS× resolution into a memory DC, then
   StretchBlt'd back with HALFTONE mode to produce smooth anti-aliased edges.
   --------------------------------------------------------------------------- */

#define GT_SS 4 /* supersampling factor */

/* Null-pen / null-brush helpers */
static HPEN s_nullPen = NULL;
static HBRUSH s_nullBrush = NULL;

static void SS_Pen(HDC dc) {
  SelectObject(dc, s_nullPen);
}
static void SS_Brush(HDC dc) {
  SelectObject(dc, s_nullBrush);
}

/* Draw icon at 4× scale into memDc (sz×sz), filled with bgCol already. */
static void IconRecord(HDC dc, int sz, COLORREF col) {
  int m = sz * 20 / 100;
  HBRUSH br = CreateSolidBrush(col);
  SelectObject(dc, br);
  SS_Pen(dc);
  Ellipse(dc, m, m, sz - m, sz - m);
  SS_Brush(dc);
  DeleteObject(br);
}

static void IconStop(HDC dc, int sz, COLORREF col) {
  int m = sz * 25 / 100;
  int rr = sz * 10 / 100;
  HBRUSH br = CreateSolidBrush(col);
  SelectObject(dc, br);
  SS_Pen(dc);
  RoundRect(dc, m, m, sz - m, sz - m, rr, rr);
  SS_Brush(dc);
  DeleteObject(br);
}

static void IconPause(HDC dc, int sz, COLORREF col) {
  int padH = sz * 22 / 100;
  int padV = sz * 20 / 100;
  int gap = sz * 13 / 100;
  int barW = (sz - 2 * padH - gap) / 2;
  int rr = sz * 8 / 100;
  HBRUSH br = CreateSolidBrush(col);
  SelectObject(dc, br);
  SS_Pen(dc);
  RoundRect(dc, padH, padV, padH + barW, sz - padV, rr, rr);
  RoundRect(dc, sz - padH - barW, padV, sz - padH, sz - padV, rr, rr);
  SS_Brush(dc);
  DeleteObject(br);
}

static void IconPlay(HDC dc, int sz, COLORREF col) {
  int padV = sz * 18 / 100;
  int padL = sz * 22 / 100;
  int padR = sz * 16 / 100;
  HBRUSH br = CreateSolidBrush(col);
  SelectObject(dc, br);
  SS_Pen(dc);
  POINT pts[3] = {
      {     padL,      padV},
      {     padL, sz - padV},
      {sz - padR,    sz / 2},
  };
  Polygon(dc, pts, 3);
  SS_Brush(dc);
  DeleteObject(br);
}

static void IconFolder(HDC dc, int sz, COLORREF col) {
  int padH = sz * 14 / 100;
  int padVb = sz * 16 / 100;
  int tabT = sz * 20 / 100;
  int split = sz * 45 / 100; /* y where tab meets body */
  int tabW = sz * 44 / 100;
  int tabSlant = sz * 6 / 100;
  int rr = sz * 6 / 100;
  HBRUSH br = CreateSolidBrush(col);
  SelectObject(dc, br);
  SS_Pen(dc);
  /* Body */
  RoundRect(dc, padH, split, sz - padH, sz - padVb, rr, rr);
  /* Tab (trapezoid: slanted right edge) */
  POINT tab[4] = {
      {                  padH, split},
      {                  padH,  tabT},
      {padH + tabW - tabSlant,  tabT},
      {           padH + tabW, split},
  };
  Polygon(dc, tab, 4);
  SS_Brush(dc);
  DeleteObject(br);
}

static void IconTrash(HDC dc, int sz, COLORREF col, COLORREF bgCol) {
  int padH = sz * 18 / 100;
  int lidT = sz * 22 / 100;
  int lidB = sz * 34 / 100;
  int bodyT = sz * 37 / 100;
  int bodyB = sz * 84 / 100;
  int bodyPad = sz * 22 / 100;
  int cx = sz / 2;
  int hndW = sz * 20 / 100;
  int hndT = sz * 12 / 100;
  int rr = sz * 7 / 100;
  int rrSm = sz * 12 / 100;

  HBRUSH br = CreateSolidBrush(col);
  HBRUSH bgBr = CreateSolidBrush(bgCol);
  SelectObject(dc, br);
  SS_Pen(dc);

  /* Handle arc (small rounded rect at top-center) */
  RoundRect(dc, cx - hndW / 2, hndT, cx + hndW / 2, lidT, rrSm, rrSm);
  /* Lid */
  RoundRect(dc, padH, lidT, sz - padH, lidB, rr, rr);
  /* Body */
  RoundRect(dc, bodyPad, bodyT, sz - bodyPad, bodyB, rr, rr);

  /* Three vertical slits in body (cut with bg color) */
  SelectObject(dc, bgBr);
  int slitW = sz * 5 / 100;
  int slitT = bodyT + sz * 8 / 100;
  int slitB = bodyB - sz * 8 / 100;
  int innerW = sz - 2 * bodyPad - 2 * rr;
  int step = innerW / 4;
  for (int i = 1; i <= 3; i++) {
    int lx = bodyPad + rr + i * step - slitW / 2;
    RECT lr = {lx, slitT, lx + slitW, slitB};
    FillRect(dc, &lr, bgBr);
  }

  SS_Brush(dc);
  DeleteObject(br);
  DeleteObject(bgBr);
}

/* Blit an icon drawn by `drawFn` onto `destHdc` at rect `r`, supersampled. */
typedef void (*IconFn)(HDC, int, void*);

static void BlitIcon(HDC destHdc, RECT r, COLORREF bgCol, IconFn fn, void* ctx) {
  int sz = GT_BTN_SIZE * GT_SS;
  HDC memDc = CreateCompatibleDC(destHdc);
  HBITMAP bmp = CreateCompatibleBitmap(destHdc, sz, sz);
  HGDIOBJ oldBmp = SelectObject(memDc, bmp);

  /* Pre-fill with button background so edges blend correctly */
  HBRUSH bgBr = CreateSolidBrush(bgCol);
  RECT fill = {0, 0, sz, sz};
  FillRect(memDc, &fill, bgBr);
  DeleteObject(bgBr);

  /* Ensure null objects are ready */
  if (!s_nullPen) s_nullPen = CreatePen(PS_NULL, 0, 0);
  if (!s_nullBrush) s_nullBrush = (HBRUSH)GetStockObject(NULL_BRUSH);

  fn(memDc, sz, ctx);

  /* Downsample with HALFTONE for anti-aliasing */
  SetStretchBltMode(destHdc, HALFTONE);
  SetBrushOrgEx(destHdc, 0, 0, NULL);
  StretchBlt(destHdc, r.left, r.top, GT_BTN_SIZE, GT_BTN_SIZE, memDc, 0, 0, sz, sz, SRCCOPY);

  SelectObject(memDc, oldBmp);
  DeleteObject(bmp);
  DeleteDC(memDc);
}

/* Context structs passed through BlitIcon's void* */
typedef struct {
  bool state;
  COLORREF col;
  COLORREF bg;
} IconCtxBool;

static void CB_Record(HDC dc, int sz, void* ctx) {
  IconCtxBool* c = (IconCtxBool*)ctx;
  if (c->state) IconStop(dc, sz, c->col);
  else
    IconRecord(dc, sz, c->col);
}
static void CB_PauseResume(HDC dc, int sz, void* ctx) {
  IconCtxBool* c = (IconCtxBool*)ctx;
  if (c->state) IconPlay(dc, sz, c->col);
  else
    IconPause(dc, sz, c->col);
}
static void CB_Folder(HDC dc, int sz, void* ctx) {
  IconCtxBool* c = (IconCtxBool*)ctx;
  IconFolder(dc, sz, c->col);
}
static void CB_Trash(HDC dc, int sz, void* ctx) {
  IconCtxBool* c = (IconCtxBool*)ctx;
  IconTrash(dc, sz, c->col, c->bg);
}

/* Legacy thin wrappers – kept so call sites in PaintRecorderWindow compile */
static void DrawRecordStopIcon(HDC hdc, RECT r, bool isRecording, COLORREF bg) {
  IconCtxBool ctx = {isRecording, s_cRed, bg};
  BlitIcon(hdc, r, bg, CB_Record, &ctx);
}
static void DrawPauseResumeIcon(HDC hdc, RECT r, bool isPaused, bool enabled, COLORREF bg) {
  COLORREF col = enabled ? s_cYellow : s_cDisabled;
  IconCtxBool ctx = {isPaused, col, bg};
  BlitIcon(hdc, r, bg, CB_PauseResume, &ctx);
}
static void DrawFolderIcon(HDC hdc, RECT r, COLORREF bg) {
  IconCtxBool ctx = {false, s_cIcon, bg};
  BlitIcon(hdc, r, bg, CB_Folder, &ctx);
}
static void DrawDeleteIcon(HDC hdc, RECT r, bool enabled, COLORREF bg) {
  COLORREF col = enabled ? RGB(220, 70, 70) : s_cDisabled;
  IconCtxBool ctx = {false, col, bg};
  BlitIcon(hdc, r, bg, CB_Trash, &ctx);
}
static void IconClose(HDC dc, int sz, COLORREF col, COLORREF bgCol) {
  /* Two diagonal rounded bars forming an × */
  int m = sz * 20 / 100; /* margin from edge  */
  int hw = sz * 8 / 100; /* half-width of bar */
  HBRUSH br = CreateSolidBrush(col);
  HBRUSH bgBr = CreateSolidBrush(bgCol);
  HPEN pen = CreatePen(PS_NULL, 0, 0);
  SelectObject(dc, br);
  SelectObject(dc, pen);
  /* Draw a filled square rotated 45° approximation via polygon pair */
  /* Bar 1: top-left → bottom-right */
  POINT b1[4] = {
      {          m,      m + hw},
      {     m + hw,           m},
      {     sz - m, sz - m - hw},
      {sz - m - hw,      sz - m},
  };
  Polygon(dc, b1, 4);
  /* Bar 2: top-right → bottom-left */
  POINT b2[4] = {
      {sz - m - hw,           m},
      {     sz - m,      m + hw},
      {     m + hw,      sz - m},
      {          m, sz - m - hw},
  };
  Polygon(dc, b2, 4);
  SelectObject(dc, GetStockObject(NULL_BRUSH));
  SelectObject(dc, GetStockObject(NULL_PEN));
  DeleteObject(br);
  DeleteObject(bgBr);
  DeleteObject(pen);
}
static void CB_Close(HDC dc, int sz, void* ctx) {
  IconCtxBool* c = (IconCtxBool*)ctx;
  IconClose(dc, sz, c->col, c->bg);
}
static void DrawCloseIcon(HDC hdc, RECT r, COLORREF bg) {
  IconCtxBool ctx = {false, RGB(180, 180, 180), bg};
  BlitIcon(hdc, r, bg, CB_Close, &ctx);
}

/* ---------------------------------------------------------------------------
   Manual tooltip
   --------------------------------------------------------------------------- */

static const WCHAR* GetTipText(int btn) {
  if (btn == 0) return s_recording ? L"Stop" : L"Record";
  if (btn == 1) return s_paused ? L"Resume" : L"Pause";
  if (btn == 2) return L"Select output file";
  if (btn == 3) return L"Delete output file";
  if (btn == 4) return L"Close";
  return NULL;
}

static LRESULT CALLBACK TipWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_PAINT) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT cr;
    GetClientRect(hwnd, &cr);
    /* background */
    HBRUSH bg = CreateSolidBrush(RGB(44, 44, 44));
    FillRect(hdc, &cr, bg);
    DeleteObject(bg);
    /* border */
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(110, 110, 110));
    HGDIOBJ op = SelectObject(hdc, pen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, cr.left, cr.top, cr.right - 1, cr.bottom - 1);
    SelectObject(hdc, op);
    DeleteObject(pen);
    /* text */
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(220, 220, 220));
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HGDIOBJ of = SelectObject(hdc, font);
    WCHAR buf[64];
    GetWindowTextW(hwnd, buf, 64);
    RECT tr = {cr.left + 7, cr.top + 4, cr.right - 7, cr.bottom - 4};
    DrawTextW(hdc, buf, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, of);
    EndPaint(hwnd, &ps);
    return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

static void EnsureTipClass(void) {
  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = TipWndProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hbrBackground = NULL;
  wc.lpszClassName = L"GameTest_Tip";
  RegisterClassExW(&wc); /* silently fails if already registered */
}

static void ShowTip(HWND parent, int btn) {
  const WCHAR* text = GetTipText(btn);
  if (!text) return;
  EnsureTipClass();
  if (!s_ttWnd) {
    s_ttWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE,
        L"GameTest_Tip",
        text,
        WS_POPUP,
        0,
        0,
        10,
        10,
        parent,
        NULL,
        GetModuleHandleW(NULL),
        NULL);
    if (!s_ttWnd) return;
  } else {
    SetWindowTextW(s_ttWnd, text);
  }
  /* Measure text width with the stock GUI font */
  HDC hdc = GetDC(s_ttWnd);
  HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  HGDIOBJ of = SelectObject(hdc, font);
  SIZE sz = {0};
  GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
  SelectObject(hdc, of);
  ReleaseDC(s_ttWnd, hdc);
  int w = sz.cx + 16;
  int h = sz.cy + 10;
  POINT pt;
  GetCursorPos(&pt);
  SetWindowPos(s_ttWnd, HWND_TOPMOST, pt.x + 14, pt.y + 20, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(s_ttWnd, NULL, FALSE);
}

static void HideTip(HWND parent) {
  (void)parent;
  if (s_ttWnd) ShowWindow(s_ttWnd, SW_HIDE);
  s_ttBtn = -1;
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
    COLORREF btnBg = (s_hotBtn == i) ? s_cBtnHover : s_cBtnNorm;
    HBRUSH btnBr = CreateSolidBrush(btnBg);
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
    if (i == 0) DrawRecordStopIcon(hdc, br, rec, btnBg);
    else if (i == 1)
      DrawPauseResumeIcon(hdc, br, paused, rec, btnBg);
    else if (i == 2)
      DrawFolderIcon(hdc, br, btnBg);
    else if (i == 3)
      DrawDeleteIcon(hdc, br, !s_recording, btnBg);
    else
      DrawCloseIcon(hdc, br, btnBg);
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
      /* Let the background act as a caption for dragging,
         but let button areas receive normal client-area clicks. */
      LRESULT hit = DefWindowProcW(hwnd, msg, wParam, lParam);
      if (hit == HTCLIENT) {
        POINT pt = {(int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam)};
        ScreenToClient(hwnd, &pt);
        if (HitTestBtn(pt.x, pt.y) >= 0) return HTCLIENT;
        return HTCAPTION;
      }
      return hit;
    }

    case WM_MOUSEMOVE: {
      int x = (int)(short)LOWORD(lParam);
      int y = (int)(short)HIWORD(lParam);
      int prev = s_hotBtn;
      s_hotBtn = HitTestBtn(x, y);
      if (s_hotBtn != prev) {
        /* Button changed: cancel any pending/shown tip, restart timer */
        KillTimer(hwnd, GT_TT_TIMER);
        HideTip(hwnd);
        if (s_hotBtn >= 0) {
          s_ttBtn = s_hotBtn;
          SetTimer(hwnd, GT_TT_TIMER, GT_TT_DELAY, NULL);
        }
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        InvalidateRect(hwnd, NULL, FALSE);
      }
      return 0;
    }

    case WM_TIMER:
      if (wParam == GT_TT_TIMER) {
        KillTimer(hwnd, GT_TT_TIMER);
        if (s_ttBtn >= 0) ShowTip(hwnd, s_ttBtn);
        return 0;
      }
      break;

    case WM_MOUSELEAVE:
      KillTimer(hwnd, GT_TT_TIMER);
      HideTip(hwnd);
      s_hotBtn = -1;
      InvalidateRect(hwnd, NULL, FALSE);
      return 0;

    case WM_LBUTTONDOWN: {
      KillTimer(hwnd, GT_TT_TIMER);
      HideTip(hwnd);
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
          InvalidateRect(hwnd, NULL, FALSE);
        }
      } else if (btn == 3) {
        /* Delete output file (only when not recording) */
        if (!s_recording) {
          WCHAR wpath[MAX_PATH];
          MultiByteToWideChar(CP_UTF8, 0, s_wndFilename, -1, wpath, MAX_PATH);
          WCHAR prompt[MAX_PATH + 64];
          wsprintfW(prompt, L"Delete file?\n%s", wpath);
          if (MessageBoxW(hwnd, prompt, L"GameTest Recorder", MB_YESNO | MB_ICONWARNING) == IDYES) {
            DeleteFileW(wpath);
          }
        }
      } else if (btn == 4) {
        /* Close */
        DestroyWindow(hwnd);
      }
      return 0;
    }

    case WM_DESTROY:
      KillTimer(hwnd, GT_TT_TIMER);
      HideTip(hwnd);
      if (s_uiIcon) {
        DestroyIcon(s_uiIcon);
        s_uiIcon = NULL;
      }
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static HICON CreateRecorderIcon(void) {
  /* Build a 32×32 32-bpp DIB: dark background + red circle */
  const int SZ = 32;
  BITMAPV5HEADER bmi = {0};
  bmi.bV5Size = sizeof(bmi);
  bmi.bV5Width = SZ;
  bmi.bV5Height = -SZ; /* top-down */
  bmi.bV5Planes = 1;
  bmi.bV5BitCount = 32;
  bmi.bV5Compression = BI_RGB;

  void* bits = NULL;
  HDC dc = GetDC(NULL);
  HBITMAP hColor = CreateDIBSection(dc, (BITMAPINFO*)&bmi, DIB_RGB_COLORS, &bits, NULL, 0);
  ReleaseDC(NULL, dc);
  if (!hColor) return NULL;

  DWORD* px = (DWORD*)bits;
  const DWORD bg = 0xFF1C1C1C;  /* dark background, fully opaque */
  const DWORD red = 0xFFD23232; /* record red */
  const float cx = (SZ - 1) / 2.0f;
  const float r = SZ * 0.30f; /* circle radius */

  for (int y = 0; y < SZ; y++) {
    for (int x = 0; x < SZ; x++) {
      float dx = x - cx, dy = y - cx;
      px[y * SZ + x] = (dx * dx + dy * dy <= r * r) ? red : bg;
    }
  }

  /* All-zeros mask = color bitmap is always authoritative */
  HBITMAP hMask = CreateBitmap(SZ, SZ, 1, 1, NULL);

  ICONINFO ii = {0};
  ii.fIcon = TRUE;
  ii.hbmColor = hColor;
  ii.hbmMask = hMask;
  HICON hIcon = CreateIconIndirect(&ii);
  DeleteObject(hColor);
  DeleteObject(hMask);
  return hIcon;
}

static DWORD WINAPI RecorderUIThreadProc(LPVOID unused) {
  (void)unused;

  WNDCLASSEXW wc = {0};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = RecorderWndProc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
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

  /* Set programmatic icon (visible in Alt-Tab switcher) */
  s_uiIcon = CreateRecorderIcon();
  if (s_uiIcon) {
    SendMessageW(s_uiHwnd, WM_SETICON, ICON_BIG, (LPARAM)s_uiIcon);
    SendMessageW(s_uiHwnd, WM_SETICON, ICON_SMALL, (LPARAM)s_uiIcon);
  }

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