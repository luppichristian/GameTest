#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ToolPlatform.h"

#ifndef DESKTOP_ALL_ACCESS
#define DESKTOP_ALL_ACCESS (STANDARD_RIGHTS_REQUIRED | 0x01FF)
#endif

static void normalize_slashes(char* s) {
  while (*s) {
    if (*s == '/') *s = '\\';
    ++s;
  }
}

static char* xstrdup(const char* s) {
  size_t n = strlen(s) + 1;
  char* p = (char*)malloc(n);
  if (!p) return NULL;
  memcpy(p, s, n);
  return p;
}

static char* join_path(const char* a, const char* b) {
  size_t la = strlen(a);
  size_t lb = strlen(b);
  int need_sep = (la > 0 && a[la - 1] != '\\' && a[la - 1] != '/');
  char* out = (char*)malloc(la + (size_t)need_sep + lb + 1);
  if (!out) return NULL;
  memcpy(out, a, la);
  if (need_sep) out[la++] = '\\';
  memcpy(out + la, b, lb);
  out[la + lb] = '\0';
  normalize_slashes(out);
  return out;
}

static WCHAR* a2w(const char* s) {
  int n = MultiByteToWideChar(CP_ACP, 0, s, -1, NULL, 0);
  WCHAR* out;
  if (n <= 0) return NULL;
  out = (WCHAR*)malloc((size_t)n * sizeof(WCHAR));
  if (!out) return NULL;
  if (MultiByteToWideChar(CP_ACP, 0, s, -1, out, n) <= 0) {
    free(out);
    return NULL;
  }
  return out;
}

static WCHAR* build_cmdline_w(int argc, const char* const* argv) {
  size_t needed = 1;
  int i;
  WCHAR* dst;
  WCHAR* out;

  for (i = 0; i < argc; ++i) {
    const char* p = argv[i];
    needed += 3;
    while (*p) {
      needed += (*p == '"') ? 2 : 1;
      ++p;
    }
  }

  out = (WCHAR*)malloc(needed * sizeof(WCHAR));
  if (!out) return NULL;

  dst = out;
  for (i = 0; i < argc; ++i) {
    WCHAR* wide;
    const WCHAR* s;
    if (i > 0) *dst++ = L' ';
    *dst++ = L'"';
    wide = a2w(argv[i]);
    if (!wide) {
      free(out);
      return NULL;
    }
    for (s = wide; *s; ++s) {
      if (*s == L'"') *dst++ = L'\\';
      *dst++ = *s;
    }
    free(wide);
    *dst++ = L'"';
  }
  *dst = L'\0';
  return out;
}

int gmt_platform_is_absolute_path(const char* path) {
  if (!path || !path[0]) return 0;
  if ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) {
    return path[1] == ':';
  }
  return (path[0] == '\\' && path[1] == '\\');
}

int gmt_platform_get_current_dir(char* out, size_t out_size) {
  DWORD len = GetCurrentDirectoryA((DWORD)out_size, out);
  if (len == 0 || len >= out_size) return 0;
  normalize_slashes(out);
  return 1;
}

int gmt_platform_file_exists(const char* path) {
  DWORD attrs = GetFileAttributesA(path);
  return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

int gmt_platform_directory_exists(const char* path) {
  DWORD attrs = GetFileAttributesA(path);
  return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

int gmt_platform_ensure_parent_dirs(const char* file_path) {
  char* temp = xstrdup(file_path);
  char* p;
  if (!temp) return 0;
  normalize_slashes(temp);
  p = strrchr(temp, '\\');
  if (!p) {
    free(temp);
    return 1;
  }
  *p = '\0';
  for (p = temp; *p; ++p) {
    if (*p == '\\') {
      char saved = *p;
      *p = '\0';
      if (strlen(temp) > 0 && !(strlen(temp) == 2 && temp[1] == ':') &&
          !gmt_platform_directory_exists(temp)) {
        CreateDirectoryA(temp, NULL);
      }
      *p = saved;
    }
  }
  if (strlen(temp) > 0 && !(strlen(temp) == 2 && temp[1] == ':') &&
      !gmt_platform_directory_exists(temp)) {
    CreateDirectoryA(temp, NULL);
  }
  free(temp);
  return 1;
}

static int ends_with_gmt_ci(const char* filename) {
  size_t len = strlen(filename);
  if (len < 4) return 0;
  return ((filename[len - 4] == '.' || filename[len - 4] == '.') &&
          (filename[len - 3] == 'g' || filename[len - 3] == 'G') &&
          (filename[len - 2] == 'm' || filename[len - 2] == 'M') &&
          (filename[len - 1] == 't' || filename[len - 1] == 'T'));
}

int gmt_platform_discover_gmt_recursive(const char* dir, void* ctx, GmtAppendPathFn append_path) {
  WIN32_FIND_DATAA ffd;
  HANDLE h_find;
  char* pattern = join_path(dir, "*");
  int count = 0;

  if (!pattern) return 0;
  h_find = FindFirstFileA(pattern, &ffd);
  free(pattern);
  if (h_find == INVALID_HANDLE_VALUE) return 0;

  do {
    char* child;
    if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) continue;

    child = join_path(dir, ffd.cFileName);
    if (!child) continue;

    if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      count += gmt_platform_discover_gmt_recursive(child, ctx, append_path);
    } else if (ends_with_gmt_ci(ffd.cFileName)) {
      if (append_path(ctx, child)) count++;
    }
    free(child);
  } while (FindNextFileA(h_find, &ffd));

  FindClose(h_find);
  return count;
}

int gmt_platform_spawn_process(const char* const* args, int arg_count, int isolated,
                               GmtProcessHandle* out_process) {
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  WCHAR* cmdline = NULL;
  BOOL ok;

  memset(&si, 0, sizeof(si));
  memset(&pi, 0, sizeof(pi));
  memset(out_process, 0, sizeof(*out_process));
  si.cb = sizeof(si);

  cmdline = build_cmdline_w(arg_count, args);
  if (!cmdline) {
    fprintf(stderr, "GameTest-Tool: out of memory building command line\n");
    return 0;
  }

  if (!isolated) {
    ok = CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    free(cmdline);
    if (!ok) {
      fprintf(stderr, "GameTest-Tool: CreateProcess failed (%lu)\n", GetLastError());
      return 0;
    }
  } else {
    HWINSTA original_station = GetProcessWindowStation();
    HWINSTA station = CreateWindowStationW(NULL, 0, WINSTA_ALL_ACCESS, NULL);
    HDESK desktop;
    WCHAR station_name[256] = {0};
    WCHAR desktop_arg[512] = {0};
    DWORD desk_err = 0;

    if (!station) {
      fprintf(stderr, "GameTest-Tool: CreateWindowStation failed (%lu)\n", GetLastError());
      free(cmdline);
      return 0;
    }

    if (!SetProcessWindowStation(station)) {
      fprintf(stderr, "GameTest-Tool: SetProcessWindowStation failed (%lu)\n", GetLastError());
      CloseWindowStation(station);
      free(cmdline);
      return 0;
    }

    desktop = CreateDesktopW(L"Default", NULL, NULL, 0, DESKTOP_ALL_ACCESS, NULL);
    desk_err = GetLastError();
    SetProcessWindowStation(original_station);
    if (!desktop) {
      fprintf(stderr, "GameTest-Tool: CreateDesktop failed (%lu)\n", desk_err);
      CloseWindowStation(station);
      free(cmdline);
      return 0;
    }

    if (!GetUserObjectInformationW(station, UOI_NAME, station_name, sizeof(station_name), NULL)) {
      fprintf(stderr, "GameTest-Tool: GetUserObjectInformation failed (%lu)\n", GetLastError());
      CloseDesktop(desktop);
      CloseWindowStation(station);
      free(cmdline);
      return 0;
    }

    swprintf(desktop_arg, 512, L"%s\\Default", station_name);
    si.lpDesktop = desktop_arg;

    ok = CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    free(cmdline);
    if (!ok) {
      fprintf(stderr, "GameTest-Tool: CreateProcess (isolated) failed (%lu)\n", GetLastError());
      CloseDesktop(desktop);
      CloseWindowStation(station);
      return 0;
    }

    out_process->station_handle = station;
    out_process->desktop_handle = desktop;
  }

  out_process->process_handle = pi.hProcess;
  out_process->thread_handle = pi.hThread;
  out_process->process_id = (unsigned long)pi.dwProcessId;
  return 1;
}

int gmt_platform_poll_process(GmtProcessHandle* process, int* has_exited, int* exit_code) {
  HANDLE proc = (HANDLE)process->process_handle;
  DWORD wait_result;
  DWORD code = 1;

  if (!proc) return 0;
  wait_result = WaitForSingleObject(proc, 0);
  if (wait_result == WAIT_TIMEOUT) {
    *has_exited = 0;
    return 1;
  }
  if (wait_result != WAIT_OBJECT_0) return 0;

  if (!GetExitCodeProcess(proc, &code)) return 0;
  *has_exited = 1;
  *exit_code = (int)code;
  return 1;
}

int gmt_platform_wait_process(GmtProcessHandle* process, int* exit_code) {
  HANDLE proc = (HANDLE)process->process_handle;
  DWORD code = 1;
  if (!proc) return 0;
  WaitForSingleObject(proc, INFINITE);
  if (!GetExitCodeProcess(proc, &code)) return 0;
  *exit_code = (int)code;
  return 1;
}

void gmt_platform_close_process(GmtProcessHandle* process) {
  if (process->thread_handle) CloseHandle((HANDLE)process->thread_handle);
  if (process->process_handle) CloseHandle((HANDLE)process->process_handle);
  if (process->desktop_handle) CloseDesktop((HDESK)process->desktop_handle);
  if (process->station_handle) CloseWindowStation((HWINSTA)process->station_handle);
  memset(process, 0, sizeof(*process));
}

void gmt_platform_sleep_ms(unsigned int milliseconds) { Sleep(milliseconds); }
