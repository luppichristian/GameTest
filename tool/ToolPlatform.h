#ifndef TOOL_PLATFORM_H
#define TOOL_PLATFORM_H

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
int gmt_platform_directory_exists(const char* path);
int gmt_platform_ensure_parent_dirs(const char* file_path);
int gmt_platform_discover_gmt_recursive(const char* dir, void* ctx, GmtAppendPathFn append_path);

int gmt_platform_spawn_process(const char* const* args, int arg_count, int isolated,
                               GmtProcessHandle* out_process);
int gmt_platform_poll_process(GmtProcessHandle* process, int* has_exited, int* exit_code);
int gmt_platform_wait_process(GmtProcessHandle* process, int* exit_code);
void gmt_platform_close_process(GmtProcessHandle* process);
void gmt_platform_sleep_ms(unsigned int milliseconds);

#endif
