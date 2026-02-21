#!/bin/bash
# Usage: ./gametest.sh <mode> <test_name> <executable>
#   mode        - record | replay | disabled
#   test_name   - name of the test (stored as tests/<test_name>.gmt)
#   executable  - path to the game executable
#
# Examples:
#   ./gametest.sh record  my_test  build/GameTest-Game.exe
#   ./gametest.sh replay  my_test  build/GameTest-Game.exe

TESTS_DIR="tests"

# ── Argument validation ──────────────────────────────────────────────────────

usage() {
  echo "Usage: $0 <mode> <test_name> <executable>"
  echo "  mode        - record | replay | disabled"
  echo "  test_name   - name of the test (stored as $TESTS_DIR/<test_name>.gmt)"
  echo "  executable  - path to the game executable"
  exit 1
}

if [ $# -lt 3 ]; then
  usage
fi

MODE="$1"
TEST_NAME="$2"
GAME_EXE="$3"

case "$MODE" in
  record|replay|disabled) ;;
  *) echo "Error: invalid mode '$MODE'. Must be record, replay, or disabled."; usage ;;
esac

# ── Paths ────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TEST_FILE="$REPO_ROOT/$TESTS_DIR/${TEST_NAME}.gmt"
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
  mkdir -p "$REPO_ROOT/$TESTS_DIR"
fi

# ── Run ──────────────────────────────────────────────────────────────────────

echo "[$MODE] $TEST_NAME  ->  $TEST_FILE"
"$EXE" "--test-mode=$MODE" "--test=$TEST_FILE"
EXIT_CODE=$?

if [ $EXIT_CODE -ne 0 ]; then
  echo "Test exited with code $EXIT_CODE."
else
  echo "Done."
fi

exit $EXIT_CODE