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

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

// ===== API Export =====

#if defined(GMT_SHARED)
#  if defined(_WIN32) || defined(_WIN64)
#    if defined(GMT_EXPORTS)
#      define GMT_API __declspec(dllexport)
#    else
#      define GMT_API __declspec(dllimport)
#    endif
#  elif __GNUC__ >= 4
#    define GMT_API __attribute__((visibility("default")))
#  else
#    define GMT_API
#  endif
#else
#  define GMT_API
#endif

// ===== Code Location =====

typedef struct GMT_CodeLocation {
  const char* file;
  int line;
  const char* function;
} GMT_CodeLocation;

#define GMT_LOCATION()           \
  (GMT_CodeLocation) {           \
    __FILE__, __LINE__, __func__ \
  }

// ===== Logging =====

typedef enum GMT_Severity {
  GMT_Severity_INFO,
  GMT_Severity_WARNING,
  GMT_Severity_ERROR,
} GMT_Severity;

// Log callback. Defaults to stdout/stderr; override to integrate with your own logging.
typedef void (*GMT_LogCallback)(
    GMT_Severity severity,
    const char* msg,
    GMT_CodeLocation loc);

GMT_API void GMT_Log_(GMT_Severity severity, GMT_CodeLocation loc, const char* fmt, ...);  // Internal, use macros below.

#ifndef GMT_DISABLE
#  define GMT_LogInfo(fmt, ...)    GMT_Log_(GMT_Severity_INFO, GMT_LOCATION(), fmt, ##__VA_ARGS__)
#  define GMT_LogWarning(fmt, ...) GMT_Log_(GMT_Severity_WARNING, GMT_LOCATION(), fmt, ##__VA_ARGS__)
#  define GMT_LogError(fmt, ...)   GMT_Log_(GMT_Severity_ERROR, GMT_LOCATION(), fmt, ##__VA_ARGS__)
#else
#  define GMT_LogInfo(fmt, ...)    ((void)0)
#  define GMT_LogWarning(fmt, ...) ((void)0)
#  define GMT_LogError(fmt, ...)   ((void)0)
#endif

// ===== Memory Management =====

// Override these to integrate with your own allocator.
typedef void* (*GMT_AllocCallback)(size_t size, GMT_CodeLocation loc);
typedef void (*GMT_FreeCallback)(void* ptr, GMT_CodeLocation loc);
typedef void* (*GMT_ReallocCallback)(void* ptr, size_t new_size, GMT_CodeLocation loc);

GMT_API void* GMT_Alloc_(size_t size, GMT_CodeLocation loc);                   // Internal, use macros below.
GMT_API void GMT_Free_(void* ptr, GMT_CodeLocation loc);                       // Internal, use macros below.
GMT_API void* GMT_Realloc_(void* ptr, size_t new_size, GMT_CodeLocation loc);  // Internal, use macros below.

#ifndef GMT_DISABLE
#  define GMT_Alloc(size)        GMT_Alloc_(size, GMT_LOCATION())
#  define GMT_Free(ptr)          GMT_Free_(ptr, GMT_LOCATION())
#  define GMT_Realloc(ptr, size) GMT_Realloc_(ptr, size, GMT_LOCATION())
#else
#  define GMT_Alloc(size)        ((void*)0)
#  define GMT_Free(ptr)          ((void)0)
#  define GMT_Realloc(ptr, size) ((void*)0)
#endif

// ===== Assertion Data =====

typedef struct GMT_Assertion {
  const char* condition_str;
  const char* msg;
  GMT_CodeLocation loc;
} GMT_Assertion;

// Called when an assertion fails.
typedef void (*GMT_AssertionTriggerCallback)(GMT_Assertion assertion);

// ===== Setup =====

typedef enum GMT_Mode {
  GMT_Mode_DISABLED = 0,
  GMT_Mode_RECORD,
  GMT_Mode_REPLAY,
} GMT_Mode;

// Maps a path to a redirected path so the framework can read/write test files without affecting game files.
typedef struct GMT_DirectoryMapping {
  const char* path;
  const char* redirected_path;
} GMT_DirectoryMapping;

// Called on signal sync. Used to synchronize recording/replay with game events across runs.
typedef void (*GMT_SignalCallback)(GMT_Mode mode, int id, GMT_CodeLocation loc);

typedef void (*GMT_FailCallback)();

typedef struct GMT_Setup {
  GMT_Mode mode;
  const char* test_path;  // Path to the test file to record or replay.
  GMT_LogCallback log_callback;
  GMT_AllocCallback alloc_callback;
  GMT_FreeCallback free_callback;
  GMT_ReallocCallback realloc_callback;
  const char* work_dir;  // Optional working directory for the executable.
  GMT_DirectoryMapping* directory_mappings;
  size_t directory_mapping_count;
  GMT_SignalCallback* signal_callback;
  GMT_FailCallback* fail_callback;  // Default: print + exit.
  GMT_AssertionTriggerCallback* assertion_trigger_callback;
  // Fail the test after this many assertion failures to prevent infinite loops.
  // If <= 1, the test fails on the first failed assertion.
  int fail_assertion_trigger_count;
} GMT_Setup;

// Initializes the framework with the given setup.
GMT_API bool GMT_Init_(const GMT_Setup* setup);

// Shuts down the framework and frees resources.
GMT_API void GMT_Quit_(void);

#ifndef GMT_DISABLE
#  define GMT_Init(setup) GMT_Init_(setup)
#  define GMT_Quit()      GMT_Quit_()
#else
#  define GMT_Init(setup) (true)
#  define GMT_Quit()      ((void)0)
#endif

// ===== Runtime =====

// Call once per frame, before polling input or processing game logic.
GMT_API void GMT_Update_(void);

// Discards the current recording/replay and starts fresh from the next frame.
GMT_API void GMT_Reset_(void);

// Immediately fails the current test.
GMT_API void GMT_Fail_(void);

#ifndef GMT_DISABLE
#  define GMT_Update() GMT_Update_()
#  define GMT_Reset()  GMT_Reset_()
#  define GMT_Fail()   GMT_Fail_()
#else
#  define GMT_Update() ((void)0)
#  define GMT_Reset()  ((void)0)
#  define GMT_Fail()   ((void)0)
#endif

// ===== Assertions =====

#ifndef GMT_FLOAT_EPSILON
#  define GMT_FLOAT_EPSILON 0.00001f
#endif
#ifndef GMT_DOUBLE_EPSILON
#  define GMT_DOUBLE_EPSILON 0.00000000001
#endif

GMT_API void GMT_Assert_(bool condition, const char* msg, GMT_CodeLocation loc);  // Internal, use macros below.

#ifndef GMT_DISABLE

// Assert macros with a custom message:
#  define GMT_AssertMsg(condition, msg)      GMT_Assert_(condition, msg, GMT_LOCATION())
#  define GMT_AssertTrueMsg(condition, msg)  GMT_Assert_((condition), msg, GMT_LOCATION())
#  define GMT_AssertFalseMsg(condition, msg) GMT_Assert_(!(condition), msg, GMT_LOCATION())
#  define GMT_AssertEqualMsg(a, b, msg)      GMT_Assert_((a) == (b), msg, GMT_LOCATION())
#  define GMT_AssertNotEqualMsg(a, b, msg)   GMT_Assert_((a) != (b), msg, GMT_LOCATION())
#  define GMT_AssertZeroMsg(value, msg)      GMT_Assert_((value) == 0, msg, GMT_LOCATION())
#  define GMT_AssertNonZeroMsg(value, msg)   GMT_Assert_((value) != 0, msg, GMT_LOCATION())
#  define GMT_AssertNearFloatMsg(a, b, msg)  GMT_Assert_(fabsf((a) - (b)) < GMT_FLOAT_EPSILON, msg, GMT_LOCATION())
#  define GMT_AssertNearDoubleMsg(a, b, msg) GMT_Assert_(fabs((a) - (b)) < GMT_DOUBLE_EPSILON, msg, GMT_LOCATION())

// Assert macros with a default message:
#  define GMT_Assert(condition)      GMT_AssertMsg((condition), "Expected condition to be true: " #condition)
#  define GMT_AssertTrue(condition)  GMT_AssertTrueMsg((condition), "Expected condition to be true: " #condition)
#  define GMT_AssertFalse(condition) GMT_AssertFalseMsg((condition), "Expected condition to be false: " #condition)
#  define GMT_AssertEqual(a, b)      GMT_AssertEqualMsg((a), (b), "Expected values to be equal: " #a " == " #b)
#  define GMT_AssertNotEqual(a, b)   GMT_AssertNotEqualMsg((a), (b), "Expected values to be not equal: " #a " != " #b)
#  define GMT_AssertZero(value)      GMT_AssertZeroMsg((value), "Expected value to be zero: " #value)
#  define GMT_AssertNonZero(value)   GMT_AssertNonZeroMsg((value), "Expected value to be non-zero: " #value)
#  define GMT_AssertNearFloat(a, b)  GMT_AssertNearFloatMsg((a), (b), "Expected values to be approximately equal (float): " #a " ≈ " #b)
#  define GMT_AssertNearDouble(a, b) GMT_AssertNearDoubleMsg((a), (b), "Expected values to be approximately equal (double): " #a " ≈ " #b)

#else
#  define GMT_AssertMsg(condition, msg)      ((void)0)
#  define GMT_AssertTrueMsg(condition, msg)  ((void)0)
#  define GMT_AssertFalseMsg(condition, msg) ((void)0)
#  define GMT_AssertEqualMsg(a, b, msg)      ((void)0)
#  define GMT_AssertNotEqualMsg(a, b, msg)   ((void)0)
#  define GMT_AssertZeroMsg(value, msg)      ((void)0)
#  define GMT_AssertNonZeroMsg(value, msg)   ((void)0)
#  define GMT_AssertNearFloatMsg(a, b, msg)  ((void)0)
#  define GMT_AssertNearDoubleMsg(a, b, msg) ((void)0)
#  define GMT_Assert(condition)              ((void)0)
#  define GMT_AssertTrue(condition)          ((void)0)
#  define GMT_AssertFalse(condition)         ((void)0)
#  define GMT_AssertEqual(a, b)              ((void)0)
#  define GMT_AssertNotEqual(a, b)           ((void)0)
#  define GMT_AssertZero(value)              ((void)0)
#  define GMT_AssertNonZero(value)           ((void)0)
#  define GMT_AssertNearFloat(a, b)          ((void)0)
#  define GMT_AssertNearDouble(a, b)         ((void)0)
#endif

// Retrieves failed assertions from the current test run.
GMT_API bool GMT_GetFailedAssertions_(GMT_Assertion* out_assertions, size_t max_assertions, size_t* out_count);

// Clears the record of failed assertions for the current test run. Called automatically on GMT_Reset.
GMT_API void GMT_ClearFailedAssertions_(void);

#ifndef GMT_DISABLE
#  define GMT_GetFailedAssertions(out_assertions, max_assertions, out_count) GMT_GetFailedAssertions_(out_assertions, max_assertions, out_count)
#  define GMT_ClearFailedAssertions()                                        GMT_ClearFailedAssertions_()
#else
#  define GMT_GetFailedAssertions(out_assertions, max_assertions, out_count) (false)
#  define GMT_ClearFailedAssertions()                                        ((void)0)
#endif

// ===== Utilities =====

GMT_API int GMT_HashString_(const char* str);
GMT_API int GMT_HashCodeLocation_(GMT_CodeLocation loc);

#ifndef GMT_DISABLE
#  define GMT_HashString(str)       GMT_HashString_(str)
#  define GMT_HashCodeLocation(loc) GMT_HashCodeLocation_(loc)
#else
#  define GMT_HashString(str)       (0)
#  define GMT_HashCodeLocation(loc) (0)
#endif

// Parses --test=<path> from args. Returns false if not found.
GMT_API bool GMT_ParseTestFilePath(const char** args, size_t arg_count, char* out_path, size_t out_path_size);

// Parses --test-mode=record|replay|disabled from args. Returns false if not found.
GMT_API bool GMT_ParseTestMode(const char** args, size_t arg_count, GMT_Mode* out_mode);

// Prints a summary report. Called automatically at the end of a test run.
GMT_API void GMT_PrintReport_(void);

#ifndef GMT_DISABLE
#  define GMT_PrintReport() GMT_PrintReport_()
#else
#  define GMT_PrintReport() ((void)0)
#endif

// ===== Signals & Sync =====

// Marks a synchronization point for events that take variable time (e.g. loading screens, menu transitions).
// Place this call right after the event completes (e.g. when a menu finishes opening).
//
// Record mode: writes the signal into the test file at the current timestamp and continues recording.
// Replay mode: the framework reads sync points from the file and suspends input injection when one is
//              reached.  When the game calls this function with the matching id, replay resumes and the
//              internal clock is adjusted so that all subsequent input is played back at the correct
//              relative timing, regardless of how long the game took to reach this point.
// Disabled mode: no-op.
GMT_API void GMT_SyncSignal_(int id, GMT_CodeLocation loc);

#ifndef GMT_DISABLE
#  define GMT_SyncSignal(id)           GMT_SyncSignal_(id, GMT_LOCATION())
#  define GMT_SyncSignalString(string) GMT_SyncSignal_(GMT_HashString_(string), GMT_LOCATION())                // String key hashed to int ID.
#  define GMT_SyncSignalAuto()         GMT_SyncSignal_(GMT_HashCodeLocation_(GMT_LOCATION()), GMT_LOCATION())  // Call-site location hashed to int ID.
#else
#  define GMT_SyncSignal(id)           ((void)0)
#  define GMT_SyncSignalString(string) ((void)0)
#  define GMT_SyncSignalAuto()         ((void)0)
#endif

// ===== Details =====

// Thread safety: Yes.

// How do I set the mode and test path?
// Parse command-line args with GMT_ParseTestFilePath and GMT_ParseTestMode, then pass the results to GMT_Init.

// How do I run multiple tests?
// Create a script (batch file or shell script) per test that launches your app with the appropriate --test and --test-mode args.
// Tests can run concurrently if they use non-overlapping directory mappings.

// How to truly make this powerful?
// Your game should have a "HEADLESS" mode that runs the game loop and processes input but doesn't render anything or open windows. This allows tests to run much faster and more reliably, and makes it easier to run tests in CI.
// You can still use the framework in non-headless mode for debugging or testing things that require rendering, but headless mode is ideal for automated testing.
// Also ensure tests dont write to the same files.