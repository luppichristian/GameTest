#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ToolPlatform.h"

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
          "  GameTest-Tool <mode> <test_name_or_path> <executable> [--isolated]\n"
          "  GameTest-Tool <executable> [test1.gmt test2.gmt ...] [--jobs N] [--isolated]\n"
          "\n"
          "Modes:\n"
          "  record | replay | disabled\n"
          "\n"
          "Notes:\n"
          "  - Mode is auto-detected from the first argument.\n"
          "  - A bare test name maps to tests\\\\<name>.gmt.\n"
          "  - For multi, if no tests are provided all tests\\\\*.gmt are discovered recursively.\n"
          "  - Use --jobs 1 to run sequentially.\n"
          "  - --isolated launches each child in a separate Win32 window station (headless only).\n");
}

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

static int append_path_adapter(void* ctx, const char* path) {
  return list_push((StringList*)ctx, path);
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

static char* resolve_from_repo(const char* repo_root, const char* path_or_rel) {
  char* out;
  if (gmt_platform_is_absolute_path(path_or_rel)) {
    out = xstrdup(path_or_rel);
  } else {
    out = join_path(repo_root, path_or_rel);
  }
  if (out) normalize_slashes(out);
  return out;
}

static int run_single(const char* repo_root, int argc, char** argv, int arg_start) {
  const char* mode;
  const char* test_arg;
  const char* exe_arg;
  int isolated = 0;
  char* exe_path;
  char* test_path = NULL;
  GmtProcessHandle process;
  const char* child_args[3];
  char mode_arg[64];
  char* test_arg_runtime;
  int i;
  int exit_code;

  if ((argc - arg_start) < 3) {
    print_usage();
    return 1;
  }

  mode = argv[arg_start];
  test_arg = argv[arg_start + 1];
  exe_arg = argv[arg_start + 2];
  for (i = arg_start + 3; i < argc; ++i) {
    if (strcmp(argv[i], "--isolated") == 0) {
      isolated = 1;
    } else {
      fprintf(stderr, "Unknown option for single: %s\n", argv[i]);
      return 1;
    }
  }

  if (!(str_ieq(mode, "record") || str_ieq(mode, "replay") || str_ieq(mode, "disabled"))) {
    fprintf(stderr, "Invalid mode '%s'. Must be record, replay, or disabled.\n", mode);
    return 1;
  }

  exe_path = resolve_from_repo(repo_root, exe_arg);
  if (!exe_path) return 1;
  if (!gmt_platform_file_exists(exe_path)) {
    fprintf(stderr, "Executable not found: %s\n", exe_path);
    free(exe_path);
    return 1;
  }

  if (str_ends_with_i(test_arg, ".gmt")) {
    test_path = resolve_from_repo(repo_root, test_arg);
  } else {
    char* tests_dir = join_path(repo_root, "tests");
    char* named = NULL;
    if (!tests_dir) {
      free(exe_path);
      return 1;
    }
    named = (char*)malloc(strlen(test_arg) + 5);
    if (!named) {
      free(tests_dir);
      free(exe_path);
      return 1;
    }
    sprintf(named, "%s.gmt", test_arg);
    test_path = join_path(tests_dir, named);
    free(named);
    free(tests_dir);
  }
  if (!test_path) {
    free(exe_path);
    return 1;
  }
  normalize_slashes(test_path);

  if (str_ieq(mode, "replay") && !gmt_platform_file_exists(test_path)) {
    fprintf(stderr, "Test file not found for replay: %s\n", test_path);
    free(test_path);
    free(exe_path);
    return 1;
  }
  if (str_ieq(mode, "record")) gmt_platform_ensure_parent_dirs(test_path);

  snprintf(mode_arg, sizeof(mode_arg), "--test-mode=%s", mode);
  test_arg_runtime = (char*)malloc(strlen(test_path) + 8);
  if (!test_arg_runtime) {
    free(test_path);
    free(exe_path);
    return 1;
  }
  sprintf(test_arg_runtime, "--test=%s", test_path);

  fprintf(stdout, "[%s] %s -> %s\n", mode, test_arg, test_path);

  child_args[0] = exe_path;
  child_args[1] = mode_arg;
  child_args[2] = test_arg_runtime;
  if (!gmt_platform_spawn_process(child_args, 3, isolated, &process)) {
    free(test_arg_runtime);
    free(test_path);
    free(exe_path);
    return 1;
  }

  if (!gmt_platform_wait_process(&process, &exit_code)) {
    gmt_platform_close_process(&process);
    free(test_arg_runtime);
    free(test_path);
    free(exe_path);
    return 1;
  }
  gmt_platform_close_process(&process);

  if (exit_code != 0) fprintf(stderr, "Test exited with code %d\n", exit_code);

  free(test_arg_runtime);
  free(test_path);
  free(exe_path);
  return exit_code;
}

static int run_multi(const char* repo_root, int argc, char** argv, int arg_start) {
  const char* exe_arg;
  int jobs = 0;
  int isolated = 0;
  int i;
  char* exe_path;
  StringList tests = {0};
  size_t queue_index = 0;
  RunningProcess* running = NULL;
  size_t running_cap = 0;
  size_t running_count = 0;
  int failed = 0;
  int passed = 0;

  if ((argc - arg_start) < 1) {
    print_usage();
    return 1;
  }

  exe_arg = argv[arg_start];
  for (i = arg_start + 1; i < argc; ++i) {
    if (strcmp(argv[i], "--isolated") == 0) {
      isolated = 1;
    } else if (strcmp(argv[i], "--jobs") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "--jobs requires a numeric value\n");
        return 1;
      }
      jobs = atoi(argv[++i]);
      if (jobs < 0) jobs = 0;
    } else if (argv[i][0] == '-' && argv[i][1] == '-') {
      fprintf(stderr, "Unknown option for multi: %s\n", argv[i]);
      return 1;
    } else {
      char* path = resolve_from_repo(repo_root, argv[i]);
      if (path && gmt_platform_file_exists(path)) {
        list_push(&tests, path);
      } else {
        fprintf(stderr, "Warning: test file not found, skipping: %s\n", argv[i]);
      }
      free(path);
    }
  }

  exe_path = resolve_from_repo(repo_root, exe_arg);
  if (!exe_path) return 1;
  if (!gmt_platform_file_exists(exe_path)) {
    fprintf(stderr, "Executable not found: %s\n", exe_path);
    free(exe_path);
    list_free(&tests);
    return 1;
  }

  if (tests.count == 0) {
    char* tests_dir = join_path(repo_root, "tests");
    if (!tests_dir) {
      free(exe_path);
      return 1;
    }
    if (!gmt_platform_directory_exists(tests_dir)) {
      fprintf(stderr, "No tests provided and tests directory not found: %s\n", tests_dir);
      free(tests_dir);
      free(exe_path);
      return 1;
    }
    gmt_platform_discover_gmt_recursive(tests_dir, &tests, append_path_adapter);
    free(tests_dir);
    if (tests.count == 0) {
      fprintf(stderr, "No .gmt test files found.\n");
      free(exe_path);
      return 1;
    }
  }

  if (jobs <= 0 || jobs > (int)tests.count) jobs = (int)tests.count;
  running_cap = (size_t)jobs;
  running = (RunningProcess*)calloc(running_cap, sizeof(RunningProcess));
  if (!running) {
    free(exe_path);
    list_free(&tests);
    return 1;
  }

  fprintf(stdout, "Running %zu test(s) with up to %d parallel process(es)%s...\n",
          tests.count, jobs, isolated ? " (isolated desktops)" : "");

  while (queue_index < tests.count || running_count > 0) {
    while (queue_index < tests.count && running_count < running_cap) {
      const char* test_path = tests.items[queue_index];
      char* test_name = file_stem(test_path);
      const char* child_args[3];
      char* test_arg = NULL;
      size_t slot;

      if (!test_name) break;
      test_arg = (char*)malloc(strlen(test_path) + 8);
      if (!test_arg) {
        free(test_name);
        break;
      }
      sprintf(test_arg, "--test=%s", test_path);
      child_args[0] = exe_path;
      child_args[1] = "--test-mode=replay";
      child_args[2] = test_arg;

      for (slot = 0; slot < running_cap; ++slot) {
        if (running[slot].process.process_handle == NULL) break;
      }

      if (slot < running_cap &&
          gmt_platform_spawn_process(child_args, 3, isolated, &running[slot].process)) {
        running[slot].name = test_name;
        running_count++;
        fprintf(stdout, "  Started [%s] (pid %lu)\n", running[slot].name,
                running[slot].process.process_id);
        queue_index++;
      } else {
        free(test_name);
      }
      free(test_arg);
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

  fprintf(stdout, "Finished. Passed: %d  Failed: %d  Total: %zu\n", passed, failed, tests.count);

  free(running);
  free(exe_path);
  list_free(&tests);
  return failed == 0 ? 0 : 1;
}

int main(int argc, char** argv) {
  char repo_root[4096];

  if (argc < 2) {
    print_usage();
    return 1;
  }

  if (!gmt_platform_get_current_dir(repo_root, sizeof(repo_root))) {
    fprintf(stderr, "Failed to read current directory.\n");
    return 1;
  }

  if (is_mode_name(argv[1])) {
    return run_single(repo_root, argc, argv, 1);
  }

  return run_multi(repo_root, argc, argv, 1);
}
