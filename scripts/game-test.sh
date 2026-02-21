#!/bin/bash
# Usage: ./game-test.sh <mode> <test_file> <executable>
#   mode        - record | replay | disabled
#   test_file   - test name (stored as tests/<name>.gmt) OR path to a .gmt file
#   executable  - path to the game executable
#
# Examples:
#   ./game-test.sh record  my_test                      build/GameTest-Game.exe
#   ./game-test.sh record  example/tests/playing.gmt   build/GameTest-Game.exe
#   ./game-test.sh replay  my_test                      build/GameTest-Game.exe

TESTS_DIR="tests"

# ── Argument validation ──────────────────────────────────────────────────────

usage() {
  echo "Usage: $0 <mode> <test_file> <executable>"
  echo "  mode        - record | replay | disabled"
  echo "  test_file   - test name (stored as $TESTS_DIR/<name>.gmt) OR path to a .gmt file"
  echo "  executable  - path to the game executable"
  exit 1
}

if [ $# -lt 3 ]; then
  usage
fi

MODE="$1"
TEST_ARG="$2"
GAME_EXE="$3"

case "$MODE" in
  record|replay|disabled) ;;
  *) echo "Error: invalid mode '$MODE'. Must be record, replay, or disabled."; usage ;;
esac

# ── Paths ────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Accept bare name or full .gmt path
if [[ "$TEST_ARG" == *.gmt ]]; then
  TEST_FILE="$REPO_ROOT/$TEST_ARG"
else
  TEST_FILE="$REPO_ROOT/$TESTS_DIR/${TEST_ARG}.gmt"
fi
EXE="$REPO_ROOT/$GAME_EXE"

# ── Pre-flight checks ────────────────────────────────────────────────────────

if [ ! -f "$EXE" ]; then
  echo "Error: game executable not found: $EXE"
  echo "Build the project first (e.g. cmake --build build)."
  exit 1
fi

if [ "$MODE" = "replay" ] && [ ! -f "$TEST_FILE" ]; then
  echo "Error: test file not found for replay: $TEST_FILE"
  echo "Run in record mode first to create it."
  exit 1
fi

if [ "$MODE" = "record" ]; then
  mkdir -p "$(dirname "$TEST_FILE")"
fi

# ── Run ──────────────────────────────────────────────────────────────────────

echo "[$MODE] $TEST_ARG  ->  $TEST_FILE"
"$EXE" "--test-mode=$MODE" "--test=$TEST_FILE"
EXIT_CODE=$?

if [ $EXIT_CODE -ne 0 ]; then
  echo "Test exited with code $EXIT_CODE."
fi

exit $EXIT_CODE