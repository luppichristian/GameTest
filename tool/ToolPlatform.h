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

#pragma once
#include <stddef.h>

typedef int (*GmtAppendPathFn)(void* ctx, const char* path);

typedef struct GmtProcessHandle {
  void* process_handle;
  void* thread_handle;
  void* station_handle;
  void* desktop_handle;
  unsigned long process_id;
} GmtProcessHandle;

int gmt_platform_is_absolute_path(const char* path);
int gmt_platform_get_current_dir(char* out, size_t out_size);
int gmt_platform_file_exists(const char* path);
int gmt_platform_is_executable(const char* path);
int gmt_platform_directory_exists(const char* path);
int gmt_platform_ensure_parent_dirs(const char* file_path);
int gmt_platform_discover_gmt_recursive(const char* dir, void* ctx, GmtAppendPathFn append_path);

int gmt_platform_spawn_process(const char* const* args, int arg_count, int isolated,
                               GmtProcessHandle* out_process);
int gmt_platform_poll_process(GmtProcessHandle* process, int* has_exited, int* exit_code);
int gmt_platform_wait_process(GmtProcessHandle* process, int* exit_code);
void gmt_platform_close_process(GmtProcessHandle* process);
void gmt_platform_sleep_ms(unsigned int milliseconds);
