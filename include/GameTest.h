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
#include <string.h>

// ===== Usage Notes =====
//
// Platform: Win32 only. No other platforms are supported at this time.
//
// Functions whose names end with _ (e.g. GMT_Update_) are internal entry points.
// Always call the corresponding macro instead (e.g. GMT_Update()). The macros
// attach source location, respect GMT_DISABLE, and are the stable public API.
//
// Input recording and replay involve many platform-level edge cases (focus loss,
// UAC prompts, OS-level input grabs, alt-tab, etc.) and may produce unexpected
// results. Any external event that steals input focus from the application during
// a recording or replay can cause the captured state to diverge from what the
// game actually received. Use at your own risk.

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

#define __GMT_FILENAME__ (strrchr(__FILE__, '\\')  ? strrchr(__FILE__, '\\') + 1 \
                          : strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1  \
                                                   : (const char*)__FILE__)

static inline GMT_CodeLocation GMT_MakeLocation_(const char* file, int line, const char* function) {
  GMT_CodeLocation loc;
  loc.file = file;
  loc.line = line;
  loc.function = function;
  return loc;
}
#define GMT_LOCATION() GMT_MakeLocation_(__GMT_FILENAME__, __LINE__, __func__)

// ===== Logging =====

typedef enum GMT_Severity {
  GMT_Severity_INFO,     // Diagnostic messages; default output: stdout.
  GMT_Severity_WARNING,  // Recoverable anomalies (e.g. deferred input, ignored signals); default output: stderr.
  GMT_Severity_ERROR,    // Unrecoverable problems (e.g. file I/O failure, payload too large); default output: stderr.
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

// Override these to integrate with your own allocator. NULL uses malloc/free/realloc.
// loc carries the call-site (file, line, function) for allocator diagnostics.
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
  const char* condition_str;  // Stringified source expression passed to the assert macro.
  const char* msg;            // Human-readable failure message (auto-generated or user-supplied).
  GMT_CodeLocation loc;
} GMT_Assertion;

// Called on every assertion failure, before the fail-trigger count is checked.
// Does not itself fail the test; use GMT_Fail() inside the callback if needed.
typedef void (*GMT_AssertionTriggerCallback)(GMT_Assertion assertion);

// ===== Setup =====

typedef enum GMT_Mode {
  GMT_Mode_DISABLED = 0,  // No recording or replay; all GMT_ calls are no-ops.
  GMT_Mode_RECORD,        // Captures input and data to the test file each frame.
  GMT_Mode_REPLAY,        // Loads the test file and injects captured input each frame.
} GMT_Mode;

// Maps a path to a redirected path so the framework can read/write test files without affecting game files.
typedef struct GMT_DirectoryMapping {
  const char* path;
  const char* redirected_path;
} GMT_DirectoryMapping;

// Fired on every GMT_SyncSignal() call in all modes. Useful for logging or driving
// external tooling that needs to know when the game reaches a sync point.
typedef void (*GMT_SignalCallback)(GMT_Mode mode, int id, GMT_CodeLocation loc);

// Called when the test fails. Default: prints the assertion report and calls exit(1).
typedef void (*GMT_FailCallback)();

typedef struct GMT_Setup {
  GMT_Mode mode;
  const char* test_path;  // Path to the test file to record or replay.
  GMT_LogCallback log_callback;          // NULL uses the default (stdout/stderr).
  GMT_AllocCallback alloc_callback;      // NULL uses malloc.
  GMT_FreeCallback free_callback;        // NULL uses free.
  GMT_ReallocCallback realloc_callback;  // NULL uses realloc.
  const char* work_dir;  // Optional working directory for the executable.
  GMT_DirectoryMapping* directory_mappings;  // May be NULL if directory_mapping_count is 0.
  size_t directory_mapping_count;
  GMT_SignalCallback* signal_callback;                    // NULL disables the user callback.
  GMT_FailCallback* fail_callback;                        // NULL uses the default (print + exit).
  GMT_AssertionTriggerCallback* assertion_trigger_callback;  // NULL disables the user callback.
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
// Advances the frame counter, captures input (RECORD) or injects it (REPLAY),
// and resets the per-frame sequential indices used by Pin and Track.
GMT_API void GMT_Update_(void);

// Discards the current recording/replay and starts fresh from the next frame.
// Also clears the failed-assertion list and resets Pin/Track sequential counters.
GMT_API void GMT_Reset_(void);

// Immediately fails the current test and invokes the fail callback.
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

// Parses --headless from args. Returns false if not found.
GMT_API bool GMT_ParseHeadlessMode(const char** args, size_t arg_count, bool* out_headless);

// Parses --work-dir=<path> from args. Returns false if not found.
GMT_API bool GMT_ParseWorkingDirectory(const char** args, size_t arg_count, char* out_work_dir, size_t out_work_dir_size);

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

// ===== Pin =====

// Pins a variable to its recorded value, making it consistent across record and replay runs.
//
// Record mode: reads the current value of *value and stores it in the test file. *value is unchanged.
// Replay mode: overwrites *value with the value that was stored during recording.
// Disabled mode: no-op.
//
// Calls with the same key are matched sequentially: the first call with key K is paired with the
// first recorded entry for K, the second call with the second entry, and so on. This means Pin can
// be used inside loops or at multiple call sites with the same key, as long as the number and order
// of calls is identical between record and replay. The sequential counter resets each frame (GMT_Update).
//
// Typical use: pinning a random seed so replay is deterministic.
//   unsigned int seed = (unsigned int)time(NULL);
//   GMT_PinUIntAuto(&seed);
//   srand(seed);

GMT_API void GMT_PinInt_(unsigned int key, int* value, GMT_CodeLocation loc);
GMT_API void GMT_PinUInt_(unsigned int key, unsigned int* value, GMT_CodeLocation loc);
GMT_API void GMT_PinFloat_(unsigned int key, float* value, GMT_CodeLocation loc);
GMT_API void GMT_PinDouble_(unsigned int key, double* value, GMT_CodeLocation loc);
GMT_API void GMT_PinBool_(unsigned int key, bool* value, GMT_CodeLocation loc);
GMT_API void GMT_PinBytes_(unsigned int key, void* data, size_t size, GMT_CodeLocation loc);

#ifndef GMT_DISABLE
#  define GMT_PinInt(key, value)              GMT_PinInt_(key, value, GMT_LOCATION())
#  define GMT_PinIntString(str, value)        GMT_PinInt_((unsigned int)GMT_HashString_(str), value, GMT_LOCATION())
#  define GMT_PinIntAuto(value)               GMT_PinInt_((unsigned int)GMT_HashCodeLocation_(GMT_LOCATION()), value, GMT_LOCATION())
#  define GMT_PinUInt(key, value)             GMT_PinUInt_(key, value, GMT_LOCATION())
#  define GMT_PinUIntString(str, value)       GMT_PinUInt_((unsigned int)GMT_HashString_(str), value, GMT_LOCATION())
#  define GMT_PinUIntAuto(value)              GMT_PinUInt_((unsigned int)GMT_HashCodeLocation_(GMT_LOCATION()), value, GMT_LOCATION())
#  define GMT_PinFloat(key, value)            GMT_PinFloat_(key, value, GMT_LOCATION())
#  define GMT_PinFloatString(str, value)      GMT_PinFloat_((unsigned int)GMT_HashString_(str), value, GMT_LOCATION())
#  define GMT_PinFloatAuto(value)             GMT_PinFloat_((unsigned int)GMT_HashCodeLocation_(GMT_LOCATION()), value, GMT_LOCATION())
#  define GMT_PinDouble(key, value)           GMT_PinDouble_(key, value, GMT_LOCATION())
#  define GMT_PinDoubleString(str, value)     GMT_PinDouble_((unsigned int)GMT_HashString_(str), value, GMT_LOCATION())
#  define GMT_PinDoubleAuto(value)            GMT_PinDouble_((unsigned int)GMT_HashCodeLocation_(GMT_LOCATION()), value, GMT_LOCATION())
#  define GMT_PinBool(key, value)             GMT_PinBool_(key, value, GMT_LOCATION())
#  define GMT_PinBoolString(str, value)       GMT_PinBool_((unsigned int)GMT_HashString_(str), value, GMT_LOCATION())
#  define GMT_PinBoolAuto(value)              GMT_PinBool_((unsigned int)GMT_HashCodeLocation_(GMT_LOCATION()), value, GMT_LOCATION())
#  define GMT_PinBytes(key, data, size)       GMT_PinBytes_(key, data, size, GMT_LOCATION())
#  define GMT_PinBytesString(str, data, size) GMT_PinBytes_((unsigned int)GMT_HashString_(str), data, size, GMT_LOCATION())
#  define GMT_PinBytesAuto(data, size)        GMT_PinBytes_((unsigned int)GMT_HashCodeLocation_(GMT_LOCATION()), data, size, GMT_LOCATION())
#else
#  define GMT_PinInt(key, value)              ((void)0)
#  define GMT_PinIntString(str, value)        ((void)0)
#  define GMT_PinIntAuto(value)               ((void)0)
#  define GMT_PinUInt(key, value)             ((void)0)
#  define GMT_PinUIntString(str, value)       ((void)0)
#  define GMT_PinUIntAuto(value)              ((void)0)
#  define GMT_PinFloat(key, value)            ((void)0)
#  define GMT_PinFloatString(str, value)      ((void)0)
#  define GMT_PinFloatAuto(value)             ((void)0)
#  define GMT_PinDouble(key, value)           ((void)0)
#  define GMT_PinDoubleString(str, value)     ((void)0)
#  define GMT_PinDoubleAuto(value)            ((void)0)
#  define GMT_PinBool(key, value)             ((void)0)
#  define GMT_PinBoolString(str, value)       ((void)0)
#  define GMT_PinBoolAuto(value)              ((void)0)
#  define GMT_PinBytes(key, data, size)       ((void)0)
#  define GMT_PinBytesString(str, data, size) ((void)0)
#  define GMT_PinBytesAuto(data, size)        ((void)0)
#endif

// ===== Track =====

// Tracks a variable and verifies it matches the recorded value during replay.
//
// Record mode: snapshots the current value and stores it in the test file.
// Replay mode: compares the current value against the stored snapshot; triggers an assertion
//              failure (same path as GMT_Assert) if the values do not match.
// Disabled mode: no-op.
//
// Calls with the same key are matched sequentially: the first call with key K is compared against
// the first recorded snapshot for K, the second call against the second snapshot, and so on. This
// allows Track to be called inside loops or at multiple call sites with the same key. The sequential
// counter resets each frame (GMT_Update). If replay reaches a key with no remaining recorded entry
// (i.e. more calls than were made during recording), the assertion fails immediately.
//
// Float/double comparisons use GMT_FLOAT_EPSILON / GMT_DOUBLE_EPSILON respectively.
// Bytes comparison uses memcmp.
//
// Typical use: verifying that a score or game state matches the recording after replay.
//   GMT_TrackIntAuto(G.length);

GMT_API void GMT_TrackInt_(unsigned int key, int value, GMT_CodeLocation loc);
GMT_API void GMT_TrackUInt_(unsigned int key, unsigned int value, GMT_CodeLocation loc);
GMT_API void GMT_TrackFloat_(unsigned int key, float value, GMT_CodeLocation loc);
GMT_API void GMT_TrackDouble_(unsigned int key, double value, GMT_CodeLocation loc);
GMT_API void GMT_TrackBool_(unsigned int key, bool value, GMT_CodeLocation loc);
GMT_API void GMT_TrackBytes_(unsigned int key, const void* data, size_t size, GMT_CodeLocation loc);

#ifndef GMT_DISABLE
#  define GMT_TrackInt(key, value)              GMT_TrackInt_(key, value, GMT_LOCATION())
#  define GMT_TrackIntString(str, value)        GMT_TrackInt_((unsigned int)GMT_HashString_(str), value, GMT_LOCATION())
#  define GMT_TrackIntAuto(value)               GMT_TrackInt_((unsigned int)GMT_HashCodeLocation_(GMT_LOCATION()), value, GMT_LOCATION())
#  define GMT_TrackUInt(key, value)             GMT_TrackUInt_(key, value, GMT_LOCATION())
#  define GMT_TrackUIntString(str, value)       GMT_TrackUInt_((unsigned int)GMT_HashString_(str), value, GMT_LOCATION())
#  define GMT_TrackUIntAuto(value)              GMT_TrackUInt_((unsigned int)GMT_HashCodeLocation_(GMT_LOCATION()), value, GMT_LOCATION())
#  define GMT_TrackFloat(key, value)            GMT_TrackFloat_(key, value, GMT_LOCATION())
#  define GMT_TrackFloatString(str, value)      GMT_TrackFloat_((unsigned int)GMT_HashString_(str), value, GMT_LOCATION())
#  define GMT_TrackFloatAuto(value)             GMT_TrackFloat_((unsigned int)GMT_HashCodeLocation_(GMT_LOCATION()), value, GMT_LOCATION())
#  define GMT_TrackDouble(key, value)           GMT_TrackDouble_(key, value, GMT_LOCATION())
#  define GMT_TrackDoubleString(str, value)     GMT_TrackDouble_((unsigned int)GMT_HashString_(str), value, GMT_LOCATION())
#  define GMT_TrackDoubleAuto(value)            GMT_TrackDouble_((unsigned int)GMT_HashCodeLocation_(GMT_LOCATION()), value, GMT_LOCATION())
#  define GMT_TrackBool(key, value)             GMT_TrackBool_(key, value, GMT_LOCATION())
#  define GMT_TrackBoolString(str, value)       GMT_TrackBool_((unsigned int)GMT_HashString_(str), value, GMT_LOCATION())
#  define GMT_TrackBoolAuto(value)              GMT_TrackBool_((unsigned int)GMT_HashCodeLocation_(GMT_LOCATION()), value, GMT_LOCATION())
#  define GMT_TrackBytes(key, data, size)       GMT_TrackBytes_(key, data, size, GMT_LOCATION())
#  define GMT_TrackBytesString(str, data, size) GMT_TrackBytes_((unsigned int)GMT_HashString_(str), data, size, GMT_LOCATION())
#  define GMT_TrackBytesAuto(data, size)        GMT_TrackBytes_((unsigned int)GMT_HashCodeLocation_(GMT_LOCATION()), data, size, GMT_LOCATION())
#else
#  define GMT_TrackInt(key, value)              ((void)0)
#  define GMT_TrackIntString(str, value)        ((void)0)
#  define GMT_TrackIntAuto(value)               ((void)0)
#  define GMT_TrackUInt(key, value)             ((void)0)
#  define GMT_TrackUIntString(str, value)       ((void)0)
#  define GMT_TrackUIntAuto(value)              ((void)0)
#  define GMT_TrackFloat(key, value)            ((void)0)
#  define GMT_TrackFloatString(str, value)      ((void)0)
#  define GMT_TrackFloatAuto(value)             ((void)0)
#  define GMT_TrackDouble(key, value)           ((void)0)
#  define GMT_TrackDoubleString(str, value)     ((void)0)
#  define GMT_TrackDoubleAuto(value)            ((void)0)
#  define GMT_TrackBool(key, value)             ((void)0)
#  define GMT_TrackBoolString(str, value)       ((void)0)
#  define GMT_TrackBoolAuto(value)              ((void)0)
#  define GMT_TrackBytes(key, data, size)       ((void)0)
#  define GMT_TrackBytesString(str, data, size) ((void)0)
#  define GMT_TrackBytesAuto(data, size)        ((void)0)
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

// ===== Input Consistency Guide =====
//
// This section describes how to structure your game loop so that input is
// recorded and replayed correctly, and documents the known limits and edge
// cases where input records may be deferred, dropped, or mismatched.
//
//
// ----- Required frame structure -----
//
// GMT_Update() MUST be called exactly once per frame, at the very top of the
// game loop, before your code reads any input:
//
//   while (running) {
//       GMT_Update();          // 1. advance the framework (captures / injects input)
//       PollInput();           // 2. read input — framework state is now current
//       UpdateGame(dt);        // 3. game logic
//       Render();              // 4. draw
//   }
//
// Calling GMT_Update() after your input poll means the framework state lags
// one frame behind, producing an off-by-one timing mismatch between recording
// and replay that grows over long tests.
//
//
// ----- What is captured -----
//
// Each GMT_Update() snapshots the full input state into one record:
//   - Keyboard  : pressed state + auto-repeat count for every key in GMT_Key.
//   - Mouse     : absolute screen position (pixels), wheel delta accumulated
//                 since the last frame (positive = right/up), button bitmask.
//   - Gamepads  : up to GMT_MAX_GAMEPADS (4) controllers, each with a buttons
//                 bitmask, analog triggers [0,255], and thumbstick axes
//                 [-32768, 32767].
//
// Records are written with a wall-clock timestamp (seconds since the start of
// the recording) and are delta-compressed: if the input state is identical to
// the previous frame, no record is written. This means held keys do not inflate
// the file — only transitions (press, release, position change, etc.) appear.
//
// Fast key taps that start and end between two GMT_Update() calls are caught
// by the platform layer, which writes an extra input record on each key
// transition, so they are not lost even at low frame rates.
//
//
// ----- Known limits -----
//
//   Injection batch cap (GMT__MAX_INJECT_BATCH = 64)
//     At most 64 input-state transitions are injected per GMT_Update() call.
//     Under normal usage this limit is never reached. It can be approached only
//     when many distinct state changes are due simultaneously — for example, if
//     GMT_Update() is called very infrequently while many recorded events pile
//     up. When the cap is hit, excess records are deferred to the next frame
//     and a warning is logged:
//       "batch limit (64) reached; input records deferred to next frame"
//     Remedy: call GMT_Update() every frame without skipping.
//
//   Pin / Track payload cap (GMT_MAX_DATA_RECORD_PAYLOAD = 256 bytes)
//     A single GMT_PinBytes / GMT_TrackBytes call may not exceed 256 bytes.
//     Calls that exceed this limit are silently skipped and an error is logged.
//     Remedy: split large structures into smaller pinned fields, or store a
//     hash / checksum and pin that instead.
//
//   Gamepad slot cap (GMT_MAX_GAMEPADS = 4)
//     Only the first 4 gamepad slots are captured. A 5th controller is never
//     recorded.
//
//   Mouse extra buttons (GMT_MouseButton_5/6/7)
//     Buttons 5, 6, and 7 are reserved in the format for future platforms but
//     may not be captured on all platforms. Do not rely on them in assertions.
//
//   Failed-assertion storage (GMT_MAX_FAILED_ASSERTIONS = 1024)
//     Only the first 1024 failed assertions per run are retained in memory.
//     Subsequent failures still trigger the fail callback and increment the
//     counter, but their details are not stored for GMT_GetFailedAssertions.
//
//
// ----- Cases where input may be missed or replayed incorrectly -----
//
//   1. GMT_Update() not called during a freeze or long stall
//      If the game stalls (e.g., a blocking load) without calling GMT_Update(),
//      recorded timestamps are relative to wall-clock time. On replay the
//      framework waits for those timestamps to elapse before injecting, which
//      reproduces the correct timing only if a GMT_SyncSignal() surrounds the
//      variable-time event. Without a sync signal, inputs after the stall may
//      inject too early or too late. Use GMT_SyncSignal() at every point where
//      wall-clock duration varies between runs (loading screens, network waits,
//      first-frame init).
//
//   2. Signal ID mismatch during replay
//      GMT_SyncSignal() advances the internal signal cursor only when the
//      emitted ID matches the next recorded ID in order. If the IDs differ
//      (e.g., the game skips a code path that emits a signal, or signals are
//      emitted in a different order), the call is ignored with a warning and
//      the cursor does not advance. Subsequent signals will also be out of
//      order, causing the replay to remain gated on the wrong signal and
//      effectively stalling input injection. Signals MUST always fire in the
//      same order and with the same IDs as during recording.
//
//   3. More GMT_SyncSignal() calls during replay than were recorded
//      Once all recorded signals are consumed, any extra GMT_SyncSignal() call
//      is ignored with a warning. This is safe as long as the extra signals
//      occur after all input has been injected. If extra signals appear before
//      injection is complete, input that depended on a later signal will be
//      injected without gating, potentially arriving too early.
//
//   4. Non-deterministic values not pinned
//      If your game reads wall-clock time, random numbers, or any other value
//      that differs between runs, and that value influences game state (and
//      therefore which inputs are valid), replay will diverge. Use GMT_PinXxx()
//      to capture those values during recording and restore them during replay.
//      Common candidates: random seeds, first-frame delta-time, physics step
//      counters, network-derived state.