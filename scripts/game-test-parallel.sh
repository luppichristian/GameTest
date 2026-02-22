#!/bin/bash
# game-test-parallel.sh
# Runs multiple GameTest replay tests in parallel, one game process per test.
#
# Usage:
#   ./game-test-parallel.sh <executable> [--jobs N] [test1.gmt test2.gmt ...]
#
#   executable   Path to the game binary (relative to repo root or absolute).
#   --jobs N     Maximum concurrent tests (default: 0 = all at once).
#   test*.gmt    Test files to replay.  If none are given, discovers tests/*.gmt.
#
# Each test is launched as a separate OS process, so all framework globals
# (replayed input state, IAT hooks, signal cursors) are fully isolated.
#
# Note on cursor racing: on Linux/macOS the cursor is per-display-server session,
# not per-process.  For headless CI runs (Xvfb / offscreen) each test should use
# its own display (:1, :2, ...) to avoid X cursor interference.  Pass the display
# via the environment, e.g.:
#   DISPLAY=:1 ./game-test-parallel.sh build/GameTest-Game ...
#
# Examples:
#   ./game-test-parallel.sh build/GameTest-Game
#   ./game-test-parallel.sh build/GameTest-Game --jobs 4
#   ./game-test-parallel.sh build/GameTest-Game example/tests/playing.gmt

set -euo pipefail

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------

usage() {
  echo "Usage: $0 <executable> [--jobs N] [test1.gmt test2.gmt ...]"
  exit 1
}

if [ $# -lt 1 ]; then usage; fi

GAME_EXE="$1"; shift
MAX_JOBS=0
TESTS=()

while [ $# -gt 0 ]; do
  case "$1" in
    --jobs)
      MAX_JOBS="$2"; shift 2 ;;
    *.gmt)
      TESTS+=("$1"); shift ;;
    -h|--help)
      usage ;;
    *)
      echo "Unknown argument: $1"; usage ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ---------------------------------------------------------------------------
# Resolve executable
# ---------------------------------------------------------------------------
if [[ "$GAME_EXE" == /* ]]; then
  EXE_PATH="$GAME_EXE"
else
  EXE_PATH="$REPO_ROOT/$GAME_EXE"
fi

if [ ! -f "$EXE_PATH" ]; then
  echo "Error: executable not found: $EXE_PATH"
  echo "Build the project first."
  exit 1
fi

# ---------------------------------------------------------------------------
# Resolve test files
# ---------------------------------------------------------------------------
if [ ${#TESTS[@]} -eq 0 ]; then
  TESTS_DIR="$REPO_ROOT/tests"
  if [ ! -d "$TESTS_DIR" ]; then
    echo "Error: no tests specified and $TESTS_DIR not found."
    exit 1
  fi
  while IFS= read -r -d '' f; do
    TESTS+=("$f")
  done < <(find "$TESTS_DIR" -name '*.gmt' -print0 | sort -z)
  if [ ${#TESTS[@]} -eq 0 ]; then
    echo "Error: no .gmt files found in $TESTS_DIR"
    exit 1
  fi
fi

# Build array of absolute paths.
RESOLVED=()
for t in "${TESTS[@]}"; do
  if [[ "$t" == /* ]]; then
    RESOLVED+=("$t")
  else
    p="$REPO_ROOT/$t"
    if [ ! -f "$p" ]; then
      echo "Warning: test file not found, skipping: $p"
    else
      RESOLVED+=("$p")
    fi
  fi
done

TOTAL=${#RESOLVED[@]}
if [ $TOTAL -eq 0 ]; then
  echo "Error: no valid test files to run."
  exit 1
fi

LIMIT=$MAX_JOBS
if [ "$LIMIT" -le 0 ]; then LIMIT=$TOTAL; fi

echo "Running $TOTAL test(s) with up to $LIMIT parallel process(es)..."
echo ""

# ---------------------------------------------------------------------------
# Temp directory for per-test exit codes
# ---------------------------------------------------------------------------
TMPDIR="$(mktemp -d)"
# shellcheck disable=SC2064
trap "rm -rf '$TMPDIR'" EXIT

# ---------------------------------------------------------------------------
# Launch a single test in the background.
# Writes the exit code to $TMPDIR/<name>.exit on completion.
# Prints the child process PID to stdout (captured by caller).
# ---------------------------------------------------------------------------
launch_test() {
  local test_file="$1"
  local name
  name="$(basename "$test_file" .gmt)"
  local exit_file="$TMPDIR/$name.exit"

  (
    "$EXE_PATH" "--test-mode=replay" "--test=$test_file" \
      >"$TMPDIR/$name.log" 2>&1
    echo $? >"$exit_file"
  ) &
  echo $!
}

# ---------------------------------------------------------------------------
# Parallel dispatch with a rolling window of $LIMIT slots
# ---------------------------------------------------------------------------
q_idx=0
active_pids=()
active_names=()

# Seed initial batch.
while [ $q_idx -lt $TOTAL ] && [ ${#active_pids[@]} -lt $LIMIT ]; do
  tf="${RESOLVED[$q_idx]}"
  name="$(basename "$tf" .gmt)"
  pid=$(launch_test "$tf")
  active_pids+=("$pid")
  active_names+=("$name")
  echo "  Started  [$name]  (pid $pid)"
  q_idx=$((q_idx + 1))
done

# Drain: whenever a slot frees, start the next pending test.
while [ ${#active_pids[@]} -gt 0 ]; do
  sleep 0.2
  new_pids=()
  new_names=()
  for i in "${!active_pids[@]}"; do
    pid="${active_pids[$i]}"
    name="${active_names[$i]}"
    if ! kill -0 "$pid" 2>/dev/null; then
      wait "$pid" 2>/dev/null || true   # reap zombie
      # Start next pending test, if any.
      if [ $q_idx -lt $TOTAL ]; then
        tf="${RESOLVED[$q_idx]}"
        nname="$(basename "$tf" .gmt)"
        npid=$(launch_test "$tf")
        new_pids+=("$npid")
        new_names+=("$nname")
        echo "  Started  [$nname]  (pid $npid)"
        q_idx=$((q_idx + 1))
      fi
    else
      new_pids+=("$pid")
      new_names+=("$name")
    fi
  done
  active_pids=("${new_pids[@]+"${new_pids[@]}"}")
  active_names=("${new_names[@]+"${new_names[@]}"}")
done

wait  # Reap any lingering zombies.

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
PASSED=0
FAILED=0

echo ""
printf '%s\n' '──────────────────────────────────────────────────'
printf '%s\n' '  Results'
printf '%s\n' '──────────────────────────────────────────────────'

for tf in "${RESOLVED[@]}"; do
  name="$(basename "$tf" .gmt)"
  exit_file="$TMPDIR/$name.exit"
  code=1
  if [ -f "$exit_file" ]; then
    code="$(cat "$exit_file")"
  fi
  if [ "$code" -eq 0 ]; then
    printf '  [ PASS ]  %s\n' "$name"
    PASSED=$((PASSED + 1))
  else
    printf '  [ FAIL ]  %s  (exit %s)\n' "$name" "$code"
    log_file="$TMPDIR/$name.log"
    if [ -f "$log_file" ] && [ -s "$log_file" ]; then
      head -20 "$log_file" | sed 's/^/            /'
    fi
    FAILED=$((FAILED + 1))
  fi
done

printf '%s\n' '──────────────────────────────────────────────────'
printf '  Passed: %d   Failed: %d   Total: %d\n' "$PASSED" "$FAILED" "$((PASSED + FAILED))"
printf '%s\n' '──────────────────────────────────────────────────'

[ $FAILED -eq 0 ]
