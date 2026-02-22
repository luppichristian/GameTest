# GameTest-Tool

`GameTest-Tool` is a command-line runner that launches your game executable with the correct arguments to record or replay tests. It handles process spawning, parallel execution, auto-discovery of test files, and result reporting.

**Platform:** Win32 only. Built when `GMT_BUILD_TOOL` is ON (the default).

---

## Synopsis

```
GameTest-Tool <mode> <executable> [tests] [options] [-- arg ...]
```

| Argument | Description |
|---|---|
| `mode` | `record`, `replay`, or `disabled` |
| `executable` | Path to the game executable. Relative paths are resolved from the current working directory. |
| `tests` | One or more `.gmt` file paths or bare test names (see below). Optional for `replay`. |
| `-- arg ...` | Any arguments after `--` are forwarded verbatim to every launched game process. |

---

## Modes

### record

Records a single test. Exactly one test name or path must be specified.

```
GameTest-Tool record MyGame.exe intro_sequence
```

The tool creates any missing parent directories for the test file, then launches the game with:

```
MyGame.exe --test-mode=record --test=tests\intro_sequence.gmt
```

The game runs interactively. When it exits the `.gmt` file contains the captured input and pinned values.

### replay

Replays one or more tests. If no tests are specified, the tool recursively discovers every `.gmt` file under `tests\` in the current directory.

```
GameTest-Tool replay MyGame.exe
GameTest-Tool replay MyGame.exe intro_sequence combat_round1
```

Each test is launched as a separate process. Results are printed as each process exits:

```
Running 3 test(s) [replay] with up to 3 parallel process(es)...
  Started [intro_sequence] (pid 14820)
  Started [combat_round1] (pid 14824)
  Started [ui_navigation] (pid 14828)
  [PASS] intro_sequence
  [FAIL] combat_round1 (exit 1)
  [PASS] ui_navigation

Finished. Passed: 2  Failed: 1  Total: 3
```

The tool exits with code `0` if all tests pass, `1` otherwise.

### disabled

Runs the game with `--test-mode=disabled`. The framework inside the game is fully inert. Useful for smoke-testing the executable through the tool's process-management path.

---

## Test path resolution

A bare name (no `.gmt` extension) is expanded to `tests\<name>.gmt` relative to the current working directory. A path ending in `.gmt` is used as-is if absolute, or resolved relative to the current working directory if not.

---

## Options

### `--jobs N`

Maximum number of test processes to run in parallel during replay. Defaults to running all tests at once. Use `--jobs 1` for sequential execution.

```
GameTest-Tool replay MyGame.exe --jobs 4
```

### `--headless`

Appends `--headless` to every game process's argument list. The game is responsible for implementing headless behavior (no window, no rendering) when this flag is present. Strongly recommended for CI.

```
GameTest-Tool replay MyGame.exe --headless
```

### `--isolated`

Launches each child process in its own Win32 window station and desktop. Processes in isolated window stations cannot interact with each other's input queues, which prevents one test's synthetic input from interfering with another running concurrently. Only meaningful with `--headless`.

```
GameTest-Tool replay MyGame.exe --headless --isolated --jobs 8
```

### `-- arg ...`

Arguments after a bare `--` separator are appended to every child process's command line after the tool's own arguments.

```
GameTest-Tool replay MyGame.exe -- --log-level=verbose
```

---

## Typical CI workflow

```sh
# Record (done locally, committed to source control):
GameTest-Tool record MyGame.exe new_feature

# Replay all tests in CI:
GameTest-Tool replay MyGame.exe --headless --isolated --jobs 0
```

`--jobs 0` is equivalent to running all discovered tests at once.

Tests can run concurrently as long as each test's `.gmt` file is distinct and the game processes do not write to the same files on disk.
