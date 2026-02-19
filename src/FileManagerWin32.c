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
#include <stdio.h>
#include "UtilityWin32.h"
#include "GameTest/FileManager.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
#define MAX_REDIRECTS  64
#define MAX_FIND_PATHS 32

// ---------------------------------------------------------------------------
// Internal types
// ---------------------------------------------------------------------------
typedef struct {
  char original[MAX_PATH];
  char replacement[MAX_PATH];
} Redirect_t;

typedef HANDLE(WINAPI* PFN_CreateFileA)(
    LPCSTR,
    DWORD,
    DWORD,
    LPSECURITY_ATTRIBUTES,
    DWORD,
    DWORD,
    HANDLE);
typedef HANDLE(WINAPI* PFN_CreateFileW)(
    LPCWSTR,
    DWORD,
    DWORD,
    LPSECURITY_ATTRIBUTES,
    DWORD,
    DWORD,
    HANDLE);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static Redirect_t s_redirects[MAX_REDIRECTS];
static int s_redirectCount = 0;
static char s_findPaths[MAX_FIND_PATHS][MAX_PATH];
static int s_findPathCount = 0;
static bool s_initialized = false;
static PFN_CreateFileA s_origCreateFileA = NULL;
static PFN_CreateFileW s_origCreateFileW = NULL;

// ---------------------------------------------------------------------------
// Path-redirect lookup helpers
// ---------------------------------------------------------------------------
static const char* FindRedirectA(const char* path) {
  if (!path) return NULL;
  for (int i = 0; i < s_redirectCount; i++) {
    if (_stricmp(s_redirects[i].original, path) == 0)
      return s_redirects[i].replacement;
  }
  return NULL;
}

static const wchar_t* FindRedirectW(const wchar_t* path, wchar_t* outBuf, int outLen) {
  if (!path) return NULL;
  char narrow[MAX_PATH] = {0};
  WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, MAX_PATH, NULL, NULL);
  for (int i = 0; i < s_redirectCount; i++) {
    if (_stricmp(s_redirects[i].original, narrow) == 0) {
      MultiByteToWideChar(CP_UTF8, 0, s_redirects[i].replacement, -1, outBuf, outLen);
      return outBuf;
    }
  }
  return NULL;
}

// ---------------------------------------------------------------------------
// Hook implementations
// ---------------------------------------------------------------------------
static HANDLE WINAPI HookCreateFileA(
    LPCSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecAttr,
    DWORD dwCreationDisp,
    DWORD dwFlagsAndAttribs,
    HANDLE hTemplate) {
  const char* redirected = FindRedirectA(lpFileName);
  return s_origCreateFileA(
      redirected ? redirected : lpFileName,
      dwDesiredAccess,
      dwShareMode,
      lpSecAttr,
      dwCreationDisp,
      dwFlagsAndAttribs,
      hTemplate);
}

static HANDLE WINAPI HookCreateFileW(
    LPCWSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecAttr,
    DWORD dwCreationDisp,
    DWORD dwFlagsAndAttribs,
    HANDLE hTemplate) {
  wchar_t wideBuf[MAX_PATH];
  const wchar_t* redirected = FindRedirectW(lpFileName, wideBuf, MAX_PATH);
  return s_origCreateFileW(
      redirected ? redirected : lpFileName,
      dwDesiredAccess,
      dwShareMode,
      lpSecAttr,
      dwCreationDisp,
      dwFlagsAndAttribs,
      hTemplate);
}

// ---------------------------------------------------------------------------
// IAT patching helpers
// ---------------------------------------------------------------------------

// Walk the PE import table of hModule and return the IAT slot for funcName
// imported from dllName (case-insensitive).
static void** FindIATEntry(HMODULE hModule, const char* dllName, const char* funcName) {
  BYTE* base = (BYTE*)hModule;

  IMAGE_DOS_HEADER* dosHdr = (IMAGE_DOS_HEADER*)base;
  if (dosHdr->e_magic != IMAGE_DOS_SIGNATURE)
    return NULL;

  IMAGE_NT_HEADERS* ntHdr = (IMAGE_NT_HEADERS*)(base + dosHdr->e_lfanew);
  if (ntHdr->Signature != IMAGE_NT_SIGNATURE)
    return NULL;

  IMAGE_DATA_DIRECTORY* importDir =
      &ntHdr->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (importDir->VirtualAddress == 0)
    return NULL;

  IMAGE_IMPORT_DESCRIPTOR* desc =
      (IMAGE_IMPORT_DESCRIPTOR*)(base + importDir->VirtualAddress);

  for (; desc->Name != 0; desc++) {
    const char* modName = (const char*)(base + desc->Name);
    if (_stricmp(modName, dllName) != 0)
      continue;

    IMAGE_THUNK_DATA* origThunk =
        (IMAGE_THUNK_DATA*)(base + desc->OriginalFirstThunk);
    IMAGE_THUNK_DATA* iatThunk =
        (IMAGE_THUNK_DATA*)(base + desc->FirstThunk);

    for (; origThunk->u1.AddressOfData != 0; origThunk++, iatThunk++) {
      if (IMAGE_SNAP_BY_ORDINAL(origThunk->u1.Ordinal))
        continue;
      IMAGE_IMPORT_BY_NAME* byName =
          (IMAGE_IMPORT_BY_NAME*)(base + origThunk->u1.AddressOfData);
      if (strcmp((const char*)byName->Name, funcName) == 0)
        return (void**)&iatThunk->u1.Function;
    }
  }
  return NULL;
}

static bool PatchIATEntry(HMODULE hModule, const char* dllName, const char* funcName, void* newFunc, void** outOld) {
  void** slot = FindIATEntry(hModule, dllName, funcName);
  if (!slot) return false;

  if (outOld) *outOld = *slot;

  DWORD oldProt;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProt))
    return false;
  *slot = newFunc;
  VirtualProtect(slot, sizeof(void*), oldProt, &oldProt);
  return true;
}

static void RestoreIATEntry(HMODULE hModule, const char* dllName, const char* funcName, void* origFunc) {
  void** slot = FindIATEntry(hModule, dllName, funcName);
  if (!slot) return;

  DWORD oldProt;
  VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProt);
  *slot = origFunc;
  VirtualProtect(slot, sizeof(void*), oldProt, &oldProt);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

GAME_TEST_API bool GameTest_FileManager_Init(void) {
  if (s_initialized) return true;

  s_redirectCount = 0;
  s_findPathCount = 0;

  HMODULE hExe = GetModuleHandleA(NULL);

  // Attempt to hook the main executable's IAT for both ANSI and Wide
  // CreateFile variants.  We try the two most common DLL name casings.
  static const char* const k_kernelNames[] = {
      "KERNEL32.dll", "kernel32.dll", "api-ms-win-core-file-l1-1-0.dll", NULL};

  for (int i = 0; k_kernelNames[i] && !s_origCreateFileA; i++)
    PatchIATEntry(hExe, k_kernelNames[i], "CreateFileA", HookCreateFileA, (void**)&s_origCreateFileA);

  for (int i = 0; k_kernelNames[i] && !s_origCreateFileW; i++)
    PatchIATEntry(hExe, k_kernelNames[i], "CreateFileW", HookCreateFileW, (void**)&s_origCreateFileW);

  // Fall back to direct proc addresses so the redirect table is still
  // consulted even if the IAT patch failed (e.g. the app uses LoadLibrary).
  if (!s_origCreateFileA)
    s_origCreateFileA = (PFN_CreateFileA)(void*)
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateFileA");
  if (!s_origCreateFileW)
    s_origCreateFileW = (PFN_CreateFileW)(void*)
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateFileW");

  s_initialized = true;
  return (s_origCreateFileA != NULL && s_origCreateFileW != NULL);
}

GAME_TEST_API void GameTest_FileManager_Quit(void) {
  if (!s_initialized) return;

  GameTest_FileManager_ClearRedirects();
  GameTest_FileManager_ClearFindPaths();

  HMODULE hExe = GetModuleHandleA(NULL);

  static const char* const k_kernelNames[] = {
      "KERNEL32.dll", "kernel32.dll", "api-ms-win-core-file-l1-1-0.dll", NULL};

  if (s_origCreateFileA) {
    for (int i = 0; k_kernelNames[i]; i++)
      RestoreIATEntry(hExe, k_kernelNames[i], "CreateFileA", (void*)s_origCreateFileA);
    s_origCreateFileA = NULL;
  }
  if (s_origCreateFileW) {
    for (int i = 0; k_kernelNames[i]; i++)
      RestoreIATEntry(hExe, k_kernelNames[i], "CreateFileW", (void*)s_origCreateFileW);
    s_origCreateFileW = NULL;
  }

  s_initialized = false;
}

GAME_TEST_API bool GameTest_FileManager_Redirect(const char* original_path, const char* new_path) {
  if (!s_initialized || !original_path || !new_path) return false;

  // Update an existing entry if the original path is already registered.
  for (int i = 0; i < s_redirectCount; i++) {
    if (_stricmp(s_redirects[i].original, original_path) == 0) {
      strncpy(s_redirects[i].replacement, new_path, MAX_PATH - 1);
      s_redirects[i].replacement[MAX_PATH - 1] = '\0';
      return true;
    }
  }

  if (s_redirectCount >= MAX_REDIRECTS) return false;

  strncpy(s_redirects[s_redirectCount].original, original_path, MAX_PATH - 1);
  strncpy(s_redirects[s_redirectCount].replacement, new_path, MAX_PATH - 1);
  s_redirects[s_redirectCount].original[MAX_PATH - 1] = '\0';
  s_redirects[s_redirectCount].replacement[MAX_PATH - 1] = '\0';
  s_redirectCount++;
  return true;
}

GAME_TEST_API bool GameTest_FileManager_ClearRedirects(void) {
  s_redirectCount = 0;
  return true;
}

GAME_TEST_API bool GameTest_FileManager_SetWorkingDirectory(const char* path) {
  if (!path) return false;
  return SetCurrentDirectoryA(path) != FALSE;
}

GAME_TEST_API bool GameTest_FileManager_GetWorkingDirectory(char* buffer, size_t buffer_size) {
  if (!buffer || buffer_size == 0) return false;
  return GetCurrentDirectoryA((DWORD)buffer_size, buffer) != 0;
}

GAME_TEST_API bool GameTest_FileManager_AddFindPath(const char* path) {
  if (!path) return false;
  if (s_findPathCount >= MAX_FIND_PATHS) return false;
  strncpy(s_findPaths[s_findPathCount], path, MAX_PATH - 1);
  s_findPaths[s_findPathCount][MAX_PATH - 1] = '\0';
  s_findPathCount++;
  return true;
}

GAME_TEST_API bool GameTest_FileManager_ClearFindPaths(void) {
  s_findPathCount = 0;
  return true;
}

GAME_TEST_API bool GameTest_FileManager_Find(const char* filename, char* buffer, size_t buffer_size) {
  if (!filename || !buffer || buffer_size == 0) return false;

  char candidate[MAX_PATH];

  // 1. Try the filename as-is (absolute path or already resolvable).
  if (GetFileAttributesA(filename) != INVALID_FILE_ATTRIBUTES) {
    strncpy(buffer, filename, buffer_size - 1);
    buffer[buffer_size - 1] = '\0';
    return true;
  }

  // 2. Search the current working directory.
  char cwd[MAX_PATH];
  if (GetCurrentDirectoryA(MAX_PATH, cwd)) {
    if (_snprintf_s(candidate, MAX_PATH, _TRUNCATE, "%s\\%s", cwd, filename) > 0) {
      if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
        strncpy(buffer, candidate, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return true;
      }
    }
  }

  // 3. Search additional paths registered via AddFindPath().
  for (int i = 0; i < s_findPathCount; i++) {
    if (_snprintf_s(candidate, MAX_PATH, _TRUNCATE, "%s\\%s", s_findPaths[i], filename) > 0) {
      if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
        strncpy(buffer, candidate, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
        return true;
      }
    }
  }

  // 4. Search the PATH environment variable.
  char pathEnv[32768];
  DWORD pathLen = GetEnvironmentVariableA("PATH", pathEnv, (DWORD)sizeof(pathEnv));
  if (pathLen > 0 && pathLen < sizeof(pathEnv)) {
    char* ctx = NULL;
    char* token = strtok_s(pathEnv, ";", &ctx);
    while (token) {
      if (_snprintf_s(candidate, MAX_PATH, _TRUNCATE, "%s\\%s", token, filename) > 0) {
        if (GetFileAttributesA(candidate) != INVALID_FILE_ATTRIBUTES) {
          strncpy(buffer, candidate, buffer_size - 1);
          buffer[buffer_size - 1] = '\0';
          return true;
        }
      }
      token = strtok_s(NULL, ";", &ctx);
    }
  }

  return false;
}
