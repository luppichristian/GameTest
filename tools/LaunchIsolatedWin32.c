/*
 * gmt-launch-isolated.c  —  Launch a process in an isolated Win32 window station.
 *
 * Each Win32 window station owns a completely separate cursor position, foreground
 * window, clipboard, and atom table.  By launching every concurrent replay run in
 * its own station the SendInput absolute mouse-movement injections from different
 * game processes can no longer corrupt each other's cursor state.
 *
 * IMPORTANT: Non-interactive window stations have no access to the physical display.
 * Hardware-accelerated rendering (OpenGL, Direct3D) will typically fail inside them.
 * Use this launcher for headless / off-screen game builds only.  If the game opens
 * a visible window, run tests sequentially or accept the cursor-race trade-off.
 *
 * Usage
 * -----
 *   gmt-launch-isolated.exe <executable> [arg1 arg2 ...]
 *
 * Exit code
 * ---------
 *   Forwards the child's exit code, or exits with 1 if setup fails.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winuser.h> /* DESKTOP_ALL_ACCESS, CreateDesktopW, etc. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fallback in case the SDK/toolchain still doesn't expose it. */
#ifndef DESKTOP_ALL_ACCESS
#  define DESKTOP_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | 0x01FF)
#endif

/* ---- helpers ---- */

/* Converts an ANSI string to a newly-allocated wide string. Caller must free(). */
static WCHAR* a2w(const char* s) {
  int n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
  if (n <= 0) return NULL;
  WCHAR* w = (WCHAR*)malloc((size_t)n * sizeof(WCHAR));
  if (w) MultiByteToWideChar(CP_ACP, 0, s, -1, w, n);
  return w;
}

/*
 * Build a single quoted command-line string from argv[0..argc-1].
 * Each argument is wrapped in double-quotes; embedded double-quotes are
 * escaped as \".  Caller must free() the result.
 */
static WCHAR* build_cmdline(int argc, char** argv) {
  /* First pass: measure required buffer. */
  size_t needed = 1; /* null terminator */
  for (int i = 0; i < argc; ++i) {
    needed += 3; /* opening " + closing " + space/null */
    for (const char* p = argv[i]; *p; ++p) {
      needed += (*p == '"') ? 2 : 1; /* \" needs 2 chars */
    }
  }

  WCHAR* buf = (WCHAR*)malloc(needed * sizeof(WCHAR));
  if (!buf) return NULL;

  WCHAR* dst = buf;
  for (int i = 0; i < argc; ++i) {
    if (i > 0) *dst++ = L' ';
    *dst++ = L'"';
    WCHAR* wide = a2w(argv[i]);
    if (wide) {
      for (const WCHAR* s = wide; *s; ++s) {
        if (*s == L'"') *dst++ = L'\\';
        *dst++ = *s;
      }
      free(wide);
    }
    *dst++ = L'"';
  }
  *dst = L'\0';
  return buf;
}

/* ---- main ---- */

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr,
            "Usage: gmt-launch-isolated <executable> [args...]\n"
            "\n"
            "Launches <executable> in an isolated Win32 window station so that\n"
            "simultaneous replay runs do not share cursor state with each other.\n"
            "\n"
            "NOTE: non-interactive stations have no display access.\n"
            "      Only suitable for headless / off-screen game builds.\n");
    return 1;
  }

  /* Save the calling process's current window station. */
  HWINSTA hOrigSta = GetProcessWindowStation();

  /*
   * Create a new window station.  Passing NULL as the name lets Windows
   * generate a unique name automatically (e.g. "Service-0x0-...").
   */
  HWINSTA hNewSta = CreateWindowStationW(NULL, 0, WINSTA_ALL_ACCESS, NULL);
  if (!hNewSta) {
    fprintf(stderr, "gmt-launch-isolated: CreateWindowStation failed (%lu)\n", GetLastError());
    return 1;
  }

  /*
   * Temporarily switch to the new station so that CreateDesktop attaches
   * the desktop to it.  We switch back immediately afterwards so the
   * launcher process is unaffected.
   */
  if (!SetProcessWindowStation(hNewSta)) {
    fprintf(stderr, "gmt-launch-isolated: SetProcessWindowStation failed (%lu)\n", GetLastError());
    CloseWindowStation(hNewSta);
    return 1;
  }

  HDESK hDesk = CreateDesktopW(L"Default", NULL, NULL, 0, DESKTOP_ALL_ACCESS, NULL);
  DWORD deskErr = GetLastError();

  /* Restore immediately — do not leave the launcher on the new station. */
  SetProcessWindowStation(hOrigSta);

  if (!hDesk) {
    fprintf(stderr, "gmt-launch-isolated: CreateDesktop failed (%lu)\n", deskErr);
    CloseWindowStation(hNewSta);
    return 1;
  }

  /* Retrieve the station name to build the lpDesktop string. */
  WCHAR staName[256] = {0};
  if (!GetUserObjectInformationW(hNewSta, UOI_NAME, staName, (DWORD)sizeof(staName), NULL)) {
    fprintf(stderr,
            "gmt-launch-isolated: GetUserObjectInformation (station name) failed (%lu)\n",
            GetLastError());
    CloseDesktop(hDesk);
    CloseWindowStation(hNewSta);
    return 1;
  }

  /* lpDesktop must be "StationName\DesktopName". */
  WCHAR desktopArg[512] = {0};
  swprintf(desktopArg, 512, L"%s\\Default", staName);

  /* Build the child command line from argv[1..argc-1]. */
  WCHAR* cmdLine = build_cmdline(argc - 1, argv + 1);
  if (!cmdLine) {
    fprintf(stderr, "gmt-launch-isolated: out of memory\n");
    CloseDesktop(hDesk);
    CloseWindowStation(hNewSta);
    return 1;
  }

  STARTUPINFOW si;
  memset(&si, 0, sizeof(si));
  si.cb = sizeof(si);
  si.lpDesktop = desktopArg; /* child will run on the isolated station/desktop */

  PROCESS_INFORMATION pi;
  memset(&pi, 0, sizeof(pi));

  BOOL ok = CreateProcessW(
      NULL, /* let the command line supply the exe name */
      cmdLine,
      NULL,
      NULL,
      FALSE, /* do not inherit handles */
      0,
      NULL, /* inherit parent environment */
      NULL, /* inherit parent current directory */
      &si,
      &pi);

  free(cmdLine);

  if (!ok) {
    fprintf(stderr, "gmt-launch-isolated: CreateProcess failed (%lu)\n", GetLastError());
    CloseDesktop(hDesk);
    CloseWindowStation(hNewSta);
    return 1;
  }

  /* Wait for the child to finish. */
  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exitCode = 1;
  GetExitCodeProcess(pi.hProcess, &exitCode);

  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  CloseDesktop(hDesk);
  CloseWindowStation(hNewSta);

  return (int)exitCode;
}
