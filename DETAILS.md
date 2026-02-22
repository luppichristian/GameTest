# Details

---

## Hard limits

### Injection batch cap — 64

At most 64 input-state transitions are injected per `GMT_Update` call. Under normal usage this limit is never reached. It becomes relevant only if `GMT_Update` is called very infrequently while many recorded events accumulate. When the cap is hit, the excess records are deferred to the next frame and the following warning is logged:

```
batch limit (64) reached; input records deferred to next frame
```

Remedy: call `GMT_Update` every frame without skipping.

### Pin / Track payload cap — 256 bytes

A single `GMT_PinBytes` or `GMT_TrackBytes` call may not exceed 256 bytes. Calls that exceed this limit are silently skipped and an error is logged. To work around it, split the structure into smaller fields and pin each individually, or compute a hash or checksum of the structure and pin that instead.

### Gamepad slot cap — 4

Only the first four gamepad slots are captured. A fifth controller is never recorded.

### Mouse extra buttons

`GMT_MouseButton_5`, `GMT_MouseButton_6`, and `GMT_MouseButton_7` are reserved in the file format for future platforms but may not be captured on Win32. Do not use them in assertions.

### Failed-assertion storage — 1024

Only the first 1024 failed assertions per run are retained in memory and accessible via `GMT_GetFailedAssertions`. Subsequent failures still invoke the fail callback and increment the internal counter, but their details are not stored.

---

## Known replay edge cases

### Freeze or stall without a sync signal

If the game stalls — for example during a blocking asset load — without calling `GMT_Update`, timestamps from the recording are based on wall-clock time. On replay the framework waits for those timestamps before injecting, which reproduces correct timing only if a `GMT_SyncSignal` surrounds the variable-duration event. Without a sync signal, inputs after the stall may inject too early or too late.

Place `GMT_SyncSignal` at every point where wall-clock duration varies between runs: loading screens, network waits, first-frame initialization.

### Signal ID mismatch

`GMT_SyncSignal` advances the internal signal cursor only when the emitted ID matches the next recorded ID in sequence. If the game skips a code path that emits a signal, or signals fire in a different order, the call is ignored with a warning and the cursor does not advance. All subsequent signals will also be out of order, and the replay will stall waiting on the wrong signal indefinitely.

Signals must always fire in the same order and with the same IDs as during recording.

### More signals during replay than were recorded

Once all recorded signals are consumed, extra `GMT_SyncSignal` calls are ignored with a warning. This is harmless if the extra calls occur after all input has been injected. If they appear before injection is complete, inputs that depended on a later signal will be injected without gating and may arrive too early.

### Non-deterministic values not pinned

Any value that differs between runs — wall-clock time, random numbers, OS-queried state — and that influences game logic will cause replay to diverge. Use `GMT_PinXxx` to restore those values to their recorded state during replay.

Common candidates: random seeds, first-frame delta-time, physics sub-step counters, network-derived state.

---

## Floating-point comparison thresholds

`GMT_TrackFloat` and `GMT_AssertNearFloat` use `GMT_FLOAT_EPSILON` (default `0.00001f`).  
`GMT_TrackDouble` and `GMT_AssertNearDouble` use `GMT_DOUBLE_EPSILON` (default `0.00000000001`).

Both can be overridden by defining the macro before including `GameTest.h`:

```c
#define GMT_FLOAT_EPSILON 0.0001f
#include <GameTest.h>
```

---

## API reference

### Setup

```c
bool GMT_Init(const GMT_Setup* setup);
void GMT_Quit(void);
```

`GMT_Init` returns `false` if initialization fails (for example, the test file cannot be opened). `GMT_Quit` flushes and closes the file and frees all internal memory.

`GMT_Setup` fields:

| Field | Type | Description |
|---|---|---|
| `mode` | `GMT_Mode` | `DISABLED`, `RECORD`, or `REPLAY`. |
| `test_path` | `const char*` | Path to the `.gmt` file. |
| `log_callback` | `GMT_LogCallback` | Override log output. NULL uses stdout/stderr. |
| `alloc_callback` | `GMT_AllocCallback` | Override allocator. NULL uses `malloc`. |
| `free_callback` | `GMT_FreeCallback` | Override free. NULL uses `free`. |
| `realloc_callback` | `GMT_ReallocCallback` | Override realloc. NULL uses `realloc`. |
| `work_dir` | `const char*` | Working directory for the process. Optional. |
| `directory_mappings` | `GMT_DirectoryMapping*` | Path redirect table. May be NULL. |
| `directory_mapping_count` | `size_t` | Number of entries in the redirect table. |
| `signal_callback` | `GMT_SignalCallback*` | Called on every `GMT_SyncSignal` in all modes. NULL disables. |
| `fail_callback` | `GMT_FailCallback*` | Called when the test fails. NULL uses the default (print + `exit(1)`). |
| `assertion_trigger_callback` | `GMT_AssertionTriggerCallback*` | Called on every assertion failure. NULL disables. |
| `fail_assertion_trigger_count` | `int` | Number of failures before the test is failed. <= 1 means fail on first. |

### Runtime

```c
void GMT_Update(void);  // call once per frame, before input polling
void GMT_Reset(void);   // restart recording/replay; clears failed assertions
void GMT_Fail(void);    // fail the test immediately
```

### Assertions

All assertions have a default-message form and a custom-message form (suffix `Msg`).

| Macro | Condition checked |
|---|---|
| `GMT_Assert(cond)` | `cond` is true |
| `GMT_AssertTrue(cond)` | `cond` is true |
| `GMT_AssertFalse(cond)` | `cond` is false |
| `GMT_AssertEqual(a, b)` | `a == b` |
| `GMT_AssertNotEqual(a, b)` | `a != b` |
| `GMT_AssertZero(v)` | `v == 0` |
| `GMT_AssertNonZero(v)` | `v != 0` |
| `GMT_AssertNearFloat(a, b)` | `fabsf(a - b) < GMT_FLOAT_EPSILON` |
| `GMT_AssertNearDouble(a, b)` | `fabs(a - b) < GMT_DOUBLE_EPSILON` |

```c
bool GMT_GetFailedAssertions(GMT_Assertion* out, size_t max, size_t* out_count);
void GMT_ClearFailedAssertions(void);
```

### Signals

```c
GMT_SyncSignal(id)            // integer ID
GMT_SyncSignalString(string)  // string hashed to integer ID
GMT_SyncSignalAuto()          // call-site location hashed to integer ID
```

### Pin

Supported types: `Int`, `UInt`, `Float`, `Double`, `Bool`, `Bytes`.  
Key variants per type: `GMT_PinInt(key, &v)`, `GMT_PinIntString(str, &v)`, `GMT_PinIntAuto(&v)`.

```c
GMT_PinBytes(key, ptr, size)
GMT_PinBytesString(str, ptr, size)
GMT_PinBytesAuto(ptr, size)
```

### Track

Supported types: `Int`, `UInt`, `Float`, `Double`, `Bool`, `Bytes`.  
Key variants per type: `GMT_TrackInt(key, v)`, `GMT_TrackIntString(str, v)`, `GMT_TrackIntAuto(v)`.

```c
GMT_TrackBytes(key, ptr, size)
GMT_TrackBytesString(str, ptr, size)
GMT_TrackBytesAuto(ptr, size)
```

### Utilities

```c
bool GMT_ParseTestFilePath(const char** args, size_t count, char* out, size_t out_size);
bool GMT_ParseTestMode(const char** args, size_t count, GMT_Mode* out_mode);
bool GMT_ParseHeadlessMode(const char** args, size_t count, bool* out_headless);
bool GMT_ParseWorkingDirectory(const char** args, size_t count, char* out, size_t out_size);
void GMT_PrintReport(void);
```

Each parser returns `false` if the corresponding flag was not found in `args`.

### Memory and logging

All internal allocations go through the callbacks set in `GMT_Setup`. The `GMT_CodeLocation` passed to each callback identifies the call site within the framework, not within user code.

```c
void* GMT_Alloc(size_t size);
void  GMT_Free(void* ptr);
void* GMT_Realloc(void* ptr, size_t new_size);

void GMT_LogInfo(const char* fmt, ...);
void GMT_LogWarning(const char* fmt, ...);
void GMT_LogError(const char* fmt, ...);
```

---

## Thread safety

The framework is thread-safe. Internal state is protected by a mutex. `GMT_Update` is intended to be called from the main thread; `GMT_PinXxx`, `GMT_TrackXxx`, `GMT_Assert`, and `GMT_SyncSignal` may be called from any thread.

---

## Macro vs. internal functions

Functions whose names end with `_` (e.g. `GMT_Update_`) are internal entry points. Always call the corresponding macro instead. The macros attach source location metadata, respect `GMT_DISABLE`, and constitute the stable public API. The underscore functions are not part of the public interface and may change without notice.
