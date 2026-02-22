# Example — Snake

The `example/` directory contains a complete Snake game that demonstrates how to integrate GameTest into a real game loop. The game uses GLFW for windowing and input and OpenGL for rendering. It is built as the `GameTest-Game` target alongside the library.

---

## Building

```sh
cmake -B build -DGMT_BUILD_EXAMPLE=ON
cmake --build build
```

GLFW 3.4 is fetched automatically via CMake's `FetchContent`.

---

## Running

**Without the framework** — launch the executable directly. Both test arguments are absent so the mode defaults to `DISABLED` and every GameTest call is a no-op.

```sh
build\GameTest-Game.exe
```

**Record a test** — play the game normally; input and pinned values are written to `example\tests\playing.gmt`.

```sh
example\record-game-test.bat
```

**Replay a test** — the recorded input is injected back; the final score is compared against the stored snapshot.

```sh
example\replay-game-test.bat
```

Both batch files are thin wrappers around `GameTest-Tool`:

```bat
build\GameTest-Tool.exe record build\GameTest-Game.exe example\tests\playing.gmt
build\GameTest-Tool.exe replay build\GameTest-Game.exe example\tests\playing.gmt
```

---

## How the example is structured

### Initialization

At startup the game parses `--test-mode` and `--test` from the command line and passes the results to `GMT_Init`. When launched directly (without those flags) the mode is `DISABLED` and the framework is entirely inert.

```c
char test_name[256] = {0};
GMT_Mode test_mode = GMT_Mode_DISABLED;
GMT_ParseTestMode((const char**)argv, argc, &test_mode);
GMT_ParseTestFilePath((const char**)argv, argc, test_name, sizeof(test_name));

GMT_Setup setup = {
    .mode      = test_mode,
    .test_path = test_name,
    .fail_assertion_trigger_count = 1,  // fail on the first assertion failure
};
GMT_Init(&setup);
```

### Pinning the random seed

Food is placed using `rand()`. Without intervention this produces different positions on each run, which causes replay to diverge. The seed is pinned immediately after `GMT_Init`:

```c
unsigned int seed = (unsigned int)time(NULL);
GMT_PinUIntString("seed", &seed);
srand(seed);
```

In RECORD mode the current value of `seed` is written to the test file. In REPLAY mode it is overwritten with the stored value before `srand` is called, making food placement identical to the original recording.

### Sync signal

After initialization but before the main loop, the game emits a sync signal:

```c
GMT_SyncSignalString("Init");
```

This anchors the replay clock to the moment the game finishes starting up. Any variation in startup time between the recording machine and the replay machine is absorbed here, so recorded input timestamps remain correct relative to the game's actual start.

### Game loop

`GMT_Update` is called once per frame at the very top of the loop, before `glfwPollEvents`. This ensures that injected input is visible to GLFW on the same frame it is due.

```c
while (!glfwWindowShouldClose(win)) {
    GMT_Update();          // advance the framework first
    glfwPollEvents();      // then read input — injected keys are now present
    // ... game logic and rendering ...
}
```

### Inline assertions

Assertions are placed directly in the game logic rather than in a separate test file. They check invariants that must hold regardless of mode, and they fire during both recording and replay. Examples:

- Food must land inside the grid and not on a snake segment (`place_food`).
- The input queue count must stay in [0, 2] (`queue_dir`).
- After each step: length bounds, head position inside grid, food inside grid.
- After init: starting length == 3, direction == RIGHT, `game_over` == false.

These fire during recording and catch bugs immediately. During replay they fire again with the replayed input, catching any divergence in game logic.

### Final score verification

At the end of the run `GMT_TrackIntString` verifies that the final snake length matches the recorded value:

```c
GMT_TrackIntString("score", G.length);
```

In RECORD mode the length is snapshotted and stored. In REPLAY mode it is compared against the snapshot; a mismatch triggers an assertion failure, which causes the process to exit with a non-zero code and the tool to report `[FAIL]`.

### Shutdown

```c
GMT_Quit();
```

Flushes the recording to disk in RECORD mode, frees replay data in REPLAY mode, prints a summary report, and tears down all framework state.

### Compile-time removal

Defining `GMT_DISABLE` before including the header strips every GameTest call at compile time. The commented-out line at the top of `Game.c` shows the intended pattern for production builds:

```c
// #define GMT_DISABLE
#include <GameTest.h>
```
