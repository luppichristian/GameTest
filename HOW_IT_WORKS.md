# How GameTest Works

GameTest operates in three modes selected at startup via `GMT_Setup.mode`:

- **RECORD** — captures input and pinned values to a `.gmt` file each frame.
- **REPLAY** — loads the `.gmt` file and injects the captured state each frame, then verifies tracked values.
- **DISABLED** — every macro becomes a no-op; the framework has no runtime presence.

---

## Frame structure

`GMT_Update` must be called exactly once per frame, before the game reads any input:

```c
while (running) {
    GMT_Update();    // 1. advance the framework
    PollInput();     // 2. read input — injected state is now current
    UpdateGame(dt);  // 3. game logic
    Render();        // 4. draw
}
```

Calling `GMT_Update` after input polling causes a one-frame lag between what was recorded and what is replayed. Over a long test this drift accumulates and replay diverges.

---

## Input capture

On each `GMT_Update` call the platform layer snapshots the complete input state:

- **Keyboard** — pressed state and auto-repeat count for every key in `GMT_Key`.
- **Mouse** — absolute screen position in pixels, accumulated wheel delta since the last frame (positive = right/up), and a button bitmask.
- **Gamepads** — up to four controllers, each with a button bitmask, analog triggers in [0, 255], and thumbstick axes in [−32768, 32767].

Records are written with a wall-clock timestamp (seconds since the start of the recording). **Delta compression** is applied: if the full input state is identical to the previous frame, no record is written. Only transitions — key press, release, mouse move, button change — appear in the file, so held keys do not inflate it.

Short key taps that begin and end between two `GMT_Update` calls are caught by the platform layer's raw-input hook, which writes an intermediate record for each transition. No events are lost at low frame rates.

### Replay injection

During replay, `GMT_Update` reads the pending records from the file and uses the platform layer to synthesize the corresponding input events (`SendInput` on Win32). At most 64 state transitions are injected per `GMT_Update` call. If more are due simultaneously the excess are deferred to the next frame and a warning is logged.

---

## Pin

`GMT_PinXxx` stabilizes a non-deterministic value so that replay is deterministic.

- **Record** — reads the current value and stores it in the file. The value in memory is unchanged.
- **Replay** — overwrites the value in memory with what was stored during recording.

Multiple calls with the same key are matched sequentially within a frame. The sequential counter resets each frame (`GMT_Update`). This means Pin can appear inside loops or at multiple call sites sharing a key, as long as the number and order of calls is the same between record and replay.

**Typical use:** random seeds, first-frame delta-time, or any value read from the OS that differs between runs.

```c
unsigned int seed = (unsigned int)time(NULL);
GMT_PinUIntAuto(&seed);   // recorded: saves the seed; replayed: restores it
srand(seed);
```

### Key variants

Each `GMT_PinXxx` family has three key-selection suffixes:

| Suffix | Key source |
|---|---|
| *(none)* `GMT_PinInt(key, value)` | explicit `unsigned int` key |
| `String` `GMT_PinIntString(str, value)` | string hashed to `unsigned int` |
| `Auto` `GMT_PinIntAuto(value)` | call-site location hashed to `unsigned int` |

The same three suffixes apply to `GMT_TrackXxx` and `GMT_SyncSignal`.

---

## Track

`GMT_TrackXxx` verifies that a value matches what was recorded.

- **Record** — snapshots the value and stores it in the file.
- **Replay** — compares the current value against the stored snapshot. A mismatch triggers an assertion failure through the same path as `GMT_Assert`.

Matching is sequential per key and per frame, exactly as with Pin.

Float and double comparisons use `GMT_FLOAT_EPSILON` and `GMT_DOUBLE_EPSILON` respectively (overridable by defining those macros before including the header). All other types use `memcmp`. On a bytes mismatch a hex dump of up to 32 bytes is logged before the assertion fires.

**Typical use:** verifying score, entity count, game state flags, or any computed value that should be reproducible.

```c
GMT_TrackIntAuto(game.score);
GMT_TrackBoolAuto(game.player_alive);
```

---

## SyncSignal

`GMT_SyncSignal` marks a point where wall-clock duration varies between runs, such as a loading screen or a network wait.

- **Record** — writes the signal ID and current timestamp into the file.
- **Replay** — pauses input injection when the internal clock reaches the recorded timestamp for that signal. Injection resumes when the game calls `GMT_SyncSignal` with the matching ID, at which point the framework shifts its internal clock forward so subsequent inputs are replayed at the correct relative timing regardless of how long the load took.

Signals must fire in the same order and with the same IDs as during recording. An out-of-order or missing signal causes the replay to stall on the wrong signal and injection to stop advancing.

```c
// After a loading screen completes:
GMT_SyncSignalAuto();
```

---

## Directory mappings

`GMT_Setup` accepts an array of `GMT_DirectoryMapping` entries. Each entry pairs a path prefix that the game uses with a redirected path that the framework will read from or write to instead. This allows test files to be stored outside the game's normal data directories without modifying the game's file-loading code.

---

## Headless mode

For CI and fast automated testing, the game should support a `--headless` flag that runs the game loop and processes input normally but skips rendering and window creation. The framework parses `--headless` via `GMT_ParseHeadlessMode`. In headless mode tests run faster, require no display, and are less likely to be disrupted by focus changes or OS-level input grabs.

Running multiple tests concurrently is safe as long as each test writes to its own `.gmt` file and the game processes do not share mutable state on disk.

---

## Assertions and failure

`GMT_Assert` and its variants record a failure into an internal list and invoke the assertion trigger callback if one is set. The test fails (the fail callback is called) once the number of accumulated failures reaches `GMT_Setup.fail_assertion_trigger_count`. The default fail callback prints the assertion report and calls `exit(1)`.

The failed-assertion list can be retrieved with `GMT_GetFailedAssertions` and cleared with `GMT_ClearFailedAssertions`. `GMT_Reset` clears it automatically.

`GMT_Fail` fails the test immediately without an assertion.
