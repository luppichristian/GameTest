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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ToolPlatform.h"

/*
 * Usage:
 *   GameTest-Tool <mode> <executable> <test_name_or_path> [options] [-- arg ...]
 *   GameTest-Tool <mode> <executable> [test1.gmt ...]    [options] [-- arg ...]
 *
 * Modes:
 *   record   Record a single test (exactly one test path required).
 *   replay   Replay one or more tests (0 = auto-discover tests\*.gmt).
 *   disabled Run the game without test framework involvement.
 *
 * Options:
 *   --jobs N     Max concurrent replays (0 = all at once; replay only).
 *   --headless   Append --headless to every test process.
 *   --isolated   Launch each child in its own Win32 window station (headless only).
 *   -- arg ...   Pass remaining arguments verbatim to every test process.
 *
 * Notes:
 *   - record requires exactly one test; it is an error to specify more.
 *   - A bare test name maps to tests\<name>.gmt relative to the working directory.
 *   - replay with no tests auto-discovers tests\*.gmt recursively.
 */

typedef struct {
  char** items;
  size_t count;
  size_t capacity;
} StringList;

typedef struct {
  GmtProcessHandle process;
  char* name;
} RunningProcess;

static void print_usage(void) {
  fprintf(stderr,
          "Usage:\n"
          "  GameTest-Tool record   <executable> <test>         [--isolated] [--headless] [-- arg ...]\n"
          "  GameTest-Tool replay   <executable> [test1.gmt ...]  [--jobs N] [--isolated] [--headless] [-- arg ...]\n"
          "  GameTest-Tool disabled <executable> <test>         [--isolated] [--headless] [-- arg ...]\n"
          "\n"
          "Notes:\n"
          "  - record requires exactly one test.\n"
          "  - replay with no tests auto-discovers tests\\*.gmt recursively.\n"
          "  - A bare test name maps to tests\\<name>.gmt.\n"
          "  - --jobs 1 runs tests sequentially.\n");
}

/* ---- string helpers ---- */

static int str_ieq(const char* a, const char* b) {
  while (*a && *b) {
    char ca = *a++;
    char cb = *b++;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
    if (ca != cb) return 0;
  }
  return *a == '\0' && *b == '\0';
}

static int str_ends_with_i(const char* str, const char* suffix) {
  size_t ls = strlen(str);
  size_t lx = strlen(suffix);
  if (lx > ls) return 0;
  return str_ieq(str + (ls - lx), suffix);
}

static int is_mode_name(const char* s) {
  return str_ieq(s, "record") || str_ieq(s, "replay") || str_ieq(s, "disabled");
}

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

/* ---- StringList ---- */

static int list_push(StringList* list, const char* item) {
  if (list->count == list->capacity) {
    size_t next_cap = (list->capacity == 0) ? 16 : list->capacity * 2;
    char** next = (char**)realloc(list->items, next_cap * sizeof(char*));
    if (!next) return 0;
    list->items = next;
    list->capacity = next_cap;
  }
  list->items[list->count] = xstrdup(item);
  if (!list->items[list->count]) return 0;
  list->count++;
  return 1;
}

static void list_free(StringList* list) {
  size_t i;
  for (i = 0; i < list->count; ++i) free(list->items[i]);
  free(list->items);
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
}

static int append_path_to_list(void* ctx, const char* path) {
  return list_push((StringList*)ctx, path);
}

/* ---- path helpers ---- */

static char* resolve_from_repo(const char* repo_root, const char* path_or_rel) {
  char* out = gmt_platform_is_absolute_path(path_or_rel)
                  ? xstrdup(path_or_rel)
                  : join_path(repo_root, path_or_rel);
  if (out) normalize_slashes(out);
  return out;
}

/* Resolves a test argument: bare name -> tests\<name>.gmt, .gmt path -> resolved path. */
static char* resolve_test_path(const char* repo_root, const char* test_arg) {
  if (str_ends_with_i(test_arg, ".gmt")) {
    return resolve_from_repo(repo_root, test_arg);
  } else {
    char* tests_dir = join_path(repo_root, "tests");
    char* named;
    char* result;
    if (!tests_dir) return NULL;
    named = (char*)malloc(strlen(test_arg) + 5);
    if (!named) {
      free(tests_dir);
      return NULL;
    }
    sprintf(named, "%s.gmt", test_arg);
    result = join_path(tests_dir, named);
    free(named);
    free(tests_dir);
    return result;
  }
}

static char* file_stem(const char* path) {
  const char* slash_a = strrchr(path, '\\');
  const char* slash_b = strrchr(path, '/');
  const char* slash = slash_a;
  const char* base;
  const char* dot;
  size_t len;
  char* out;
  if (!slash || (slash_b && slash_b > slash)) slash = slash_b;
  base = slash ? slash + 1 : path;
  dot = strrchr(base, '.');
  len = dot ? (size_t)(dot - base) : strlen(base);
  out = (char*)malloc(len + 1);
  if (!out) return NULL;
  memcpy(out, base, len);
  out[len] = '\0';
  return out;
}

/* ---- child arg builder ---- */

/* Returns a malloc'd array of const char* (caller must free). Pointers inside are NOT owned. */
static const char** build_child_args(const char* exe,
                                     const char* const* fixed,
                                     int fixed_count,
                                     int headless,
                                     const char* const* extra,
                                     int extra_count,
                                     int* out_count) {
  int total = 1 + fixed_count + (headless ? 1 : 0) + extra_count;
  int i, pos = 0;
  const char** out = (const char**)malloc((size_t)total * sizeof(const char*));
  if (!out) return NULL;
  out[pos++] = exe;
  for (i = 0; i < fixed_count; ++i) out[pos++] = fixed[i];
  if (headless) out[pos++] = "--headless";
  for (i = 0; i < extra_count; ++i) out[pos++] = extra[i];
  *out_count = total;
  return out;
}

/* ---- runners ---- */

/* Runs a single test process and waits for it to finish. test_path must already be resolved. */
static int run_single(const char* mode, const char* exe_path, const char* test_path, int isolated, int headless, const char* const* extra_args, int extra_argc) {
  char mode_flag[64];
  char* test_flag;
  const char* fixed[2];
  const char** child_args;
  int child_argc;
  GmtProcessHandle process;
  int exit_code;

  snprintf(mode_flag, sizeof(mode_flag), "--test-mode=%s", mode);
  test_flag = (char*)malloc(strlen(test_path) + 8);
  if (!test_flag) return 1;
  sprintf(test_flag, "--test=%s", test_path);

  fixed[0] = mode_flag;
  fixed[1] = test_flag;
  child_args = build_child_args(exe_path, fixed, 2, headless, extra_args, extra_argc, &child_argc);
  if (!child_args) {
    free(test_flag);
    return 1;
  }

  if (!gmt_platform_spawn_process(child_args, child_argc, isolated, &process)) {
    free(child_args);
    free(test_flag);
    return 1;
  }
  free(child_args);

  if (!gmt_platform_wait_process(&process, &exit_code)) {
    gmt_platform_close_process(&process);
    free(test_flag);
    return 1;
  }
  gmt_platform_close_process(&process);
  free(test_flag);

  if (exit_code != 0) fprintf(stderr, "Test exited with code %d\n", exit_code);
  return exit_code;
}

/* Runs multiple tests, up to `jobs` in parallel. tests must already contain resolved paths. */
static int run_multi(const char* mode, const char* exe_path, StringList* tests, int jobs, int isolated, int headless, const char* const* extra_args, int extra_argc) {
  size_t queue_index = 0;
  RunningProcess* running;
  size_t running_cap;
  size_t running_count = 0;
  int failed = 0;
  int passed = 0;

  if (jobs <= 0 || jobs > (int)tests->count) jobs = (int)tests->count;
  running_cap = (size_t)jobs;
  running = (RunningProcess*)calloc(running_cap, sizeof(RunningProcess));
  if (!running) return 1;

  fprintf(stdout, "Running %zu test(s) [%s] with up to %d parallel process(es)%s...\n", tests->count, mode, jobs, isolated ? " (isolated)" : "");

  while (queue_index < tests->count || running_count > 0) {
    while (queue_index < tests->count && running_count < running_cap) {
      const char* test_path = tests->items[queue_index];
      char* test_name = file_stem(test_path);
      char mode_flag[64];
      char* test_flag;
      const char* fixed[2];
      const char** child_args;
      int child_argc;
      size_t slot;

      if (!test_name) break;
      test_flag = (char*)malloc(strlen(test_path) + 8);
      if (!test_flag) {
        free(test_name);
        break;
      }
      snprintf(mode_flag, sizeof(mode_flag), "--test-mode=%s", mode);
      sprintf(test_flag, "--test=%s", test_path);
      fixed[0] = mode_flag;
      fixed[1] = test_flag;
      child_args = build_child_args(exe_path, fixed, 2, headless, extra_args, extra_argc, &child_argc);

      for (slot = 0; slot < running_cap; ++slot) {
        if (running[slot].process.process_handle == NULL) break;
      }

      if (child_args && slot < running_cap &&
          gmt_platform_spawn_process(child_args, child_argc, isolated, &running[slot].process)) {
        running[slot].name = test_name;
        running_count++;
        fprintf(stdout, "  Started [%s] (pid %lu)\n", test_name, running[slot].process.process_id);
      } else {
        fprintf(stderr, "  [FAIL] %s (spawn setup error)\n", test_name);
        failed++;
        free(test_name);
      }
      free(child_args);
      free(test_flag);
      queue_index++;
    }

    if (running_count == 0) break;

    gmt_platform_sleep_ms(120);

    {
      size_t slot;
      for (slot = 0; slot < running_cap; ++slot) {
        int has_exited = 0;
        int exit_code = 1;
        if (running[slot].process.process_handle == NULL) continue;
        if (!gmt_platform_poll_process(&running[slot].process, &has_exited, &exit_code)) continue;
        if (!has_exited) continue;

        if (exit_code == 0) {
          fprintf(stdout, "  [PASS] %s\n", running[slot].name);
          passed++;
        } else {
          fprintf(stderr, "  [FAIL] %s (exit %d)\n", running[slot].name, exit_code);
          failed++;
        }

        gmt_platform_close_process(&running[slot].process);
        free(running[slot].name);
        memset(&running[slot], 0, sizeof(running[slot]));
        running_count--;
      }
    }
  }

  fprintf(stdout, "\nFinished. Passed: %d  Failed: %d  Total: %zu\n", passed, failed, tests->count);
  free(running);
  return failed == 0 ? 0 : 1;
}

/* ---- main ---- */

int main(int argc, char** argv) {
  char repo_root[4096];
  const char* mode;
  const char* exe_arg;
  char* exe_path;
  int jobs = 0;
  int isolated = 0;
  int headless = 0;
  int tool_argc = argc;
  const char* const* extra_args = NULL;
  int extra_argc = 0;
  StringList tests = {0};
  int i;
  int result;

  /* Split argv on '--': tool args before, forwarded args after. */
  for (i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--") == 0) {
      tool_argc = i;
      extra_args = (const char* const*)(argv + i + 1);
      extra_argc = argc - i - 1;
      break;
    }
  }

  if (tool_argc < 3) {
    print_usage();
    return 1;
  }

  mode = argv[1];
  exe_arg = argv[2];

  if (!is_mode_name(mode)) {
    fprintf(stderr, "Unknown mode '%s'. Must be record, replay, or disabled.\n", mode);
    print_usage();
    return 1;
  }

  if (!gmt_platform_get_current_dir(repo_root, sizeof(repo_root))) {
    fprintf(stderr, "Failed to read current directory.\n");
    return 1;
  }

  /* Parse options and test arguments (after mode and exe). */
  for (i = 3; i < tool_argc; ++i) {
    if (strcmp(argv[i], "--isolated") == 0) {
      isolated = 1;
    } else if (strcmp(argv[i], "--headless") == 0) {
      headless = 1;
    } else if (strcmp(argv[i], "--jobs") == 0) {
      if (i + 1 >= tool_argc) {
        fprintf(stderr, "--jobs requires a numeric value\n");
        return 1;
      }
      jobs = atoi(argv[++i]);
      if (jobs < 0) jobs = 0;
    } else if (argv[i][0] == '-') {
      fprintf(stderr, "Unknown option: %s\n", argv[i]);
      return 1;
    } else {
      /* Resolve test path now that repo_root is known. */
      char* path = resolve_test_path(repo_root, argv[i]);
      if (!path) {
        list_free(&tests);
        return 1;
      }
      if (!list_push(&tests, path)) {
        fprintf(stderr, "Out of memory while collecting test paths.\n");
        free(path);
        list_free(&tests);
        return 1;
      }
      free(path);
    }
  }

  /* Validate mode-specific constraints. */
  if (str_ieq(mode, "record")) {
    if (tests.count == 0) {
      fprintf(stderr, "Error: 'record' requires a test name or path.\n");
      list_free(&tests);
      return 1;
    }
    if (tests.count > 1) {
      fprintf(stderr, "Error: 'record' accepts only one test; %zu were given.\n", tests.count);
      list_free(&tests);
      return 1;
    }
  }

  exe_path = resolve_from_repo(repo_root, exe_arg);
  if (!exe_path) {
    list_free(&tests);
    return 1;
  }
  if (!gmt_platform_file_exists(exe_path)) {
    fprintf(stderr, "Executable not found: %s\n", exe_path);
    free(exe_path);
    list_free(&tests);
    return 1;
  }
  if (!gmt_platform_is_executable(exe_path)) {
    fprintf(stderr, "Not a valid executable (bad format or wrong argument order?): %s\n", exe_path);
    fprintf(stderr, "Usage: GameTest-Tool <mode> <executable> [tests...]\n");
    free(exe_path);
    list_free(&tests);
    return 1;
  }

  if (tests.count == 1) {
    /* Single test: record, replay, or disabled with exactly one path. */
    const char* test_path = tests.items[0];
    if (str_ieq(mode, "replay") && !gmt_platform_file_exists(test_path)) {
      fprintf(stderr, "Test file not found: %s\n", test_path);
      free(exe_path);
      list_free(&tests);
      return 1;
    }
    if (str_ieq(mode, "record")) gmt_platform_ensure_parent_dirs(test_path);
    fprintf(stdout, "[%s] -> %s\n", mode, test_path);
    result = run_single(mode, exe_path, test_path, isolated, headless, extra_args, extra_argc);
  } else {
    /* Multi-test path: replay or disabled with 0 or 2+ tests. */
    if (tests.count == 0) {
      /* Auto-discover. */
      char* tests_dir = join_path(repo_root, "tests");
      if (!tests_dir) {
        free(exe_path);
        return 1;
      }
      if (!gmt_platform_directory_exists(tests_dir)) {
        fprintf(stderr, "No tests provided and tests\\ not found: %s\n", tests_dir);
        free(tests_dir);
        free(exe_path);
        return 1;
      }
      gmt_platform_discover_gmt_recursive(tests_dir, &tests, append_path_to_list);
      free(tests_dir);
      if (tests.count == 0) {
        fprintf(stderr, "No .gmt test files found.\n");
        free(exe_path);
        return 1;
      }
    }
    result = run_multi(mode, exe_path, &tests, jobs, isolated, headless, extra_args, extra_argc);
  }

  free(exe_path);
  list_free(&tests);
  return result;
}
