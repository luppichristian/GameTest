@echo off
rem Usage: game-test.bat <mode> <test_name> <executable>
rem   mode        - record | replay | disabled
rem   test_name   - name of the test (stored as tests\<test_name>.gmt)
rem   executable  - path to the game executable
rem
rem Examples:
rem   game-test.bat record  my_test  build\GameTest-Game.exe
rem   game-test.bat replay  my_test  build\GameTest-Game.exe

set TESTS_DIR=tests

rem ── Argument validation ──────────────────────────────────────────────────────

if "%~3"=="" goto usage

set MODE=%~1
set TEST_NAME=%~2
set GAME_EXE=%~3

if /i "%MODE%"=="record"   goto mode_ok
if /i "%MODE%"=="replay"   goto mode_ok
if /i "%MODE%"=="disabled" goto mode_ok
echo Error: invalid mode '%MODE%'. Must be record, replay, or disabled.
goto usage

:mode_ok

rem ── Paths ────────────────────────────────────────────────────────────────────

set SCRIPT_DIR=%~dp0
rem Remove trailing backslash
if "%SCRIPT_DIR:~-1%"=="\" set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

rem Resolve repo root (one level up from scripts\)
pushd "%SCRIPT_DIR%\.."
set REPO_ROOT=%CD%
popd

set TEST_FILE=%REPO_ROOT%\%TESTS_DIR%\%TEST_NAME%.gmt
set EXE=%REPO_ROOT%\%GAME_EXE%

rem ── Pre-flight checks ────────────────────────────────────────────────────────

if not exist "%EXE%" (
  echo Error: game executable not found: %EXE%
  echo Build the project first ^(e.g. cmake --build build^).
  exit /b 1
)

if /i "%MODE%"=="replay" (
  if not exist "%TEST_FILE%" (
    echo Error: test file not found for replay: %TEST_FILE%
    echo Run in record mode first to create it.
    exit /b 1
  )
)

if /i "%MODE%"=="record" (
  if not exist "%REPO_ROOT%\%TESTS_DIR%" mkdir "%REPO_ROOT%\%TESTS_DIR%"
)

rem ── Run ──────────────────────────────────────────────────────────────────────

echo [%MODE%] %TEST_NAME%  ->  %TEST_FILE%
"%EXE%" "--test-mode=%MODE%" "--test=%TEST_FILE%"
set EXIT_CODE=%ERRORLEVEL%

if %EXIT_CODE% neq 0 (
  echo Test exited with code %EXIT_CODE%.
) else (
  echo Done.
)

exit /b %EXIT_CODE%

rem ── Usage ─────────────────────────────────────────────────────────────────────

:usage
echo Usage: %~nx0 ^<mode^> ^<test_name^> ^<executable^>
echo   mode        - record ^| replay ^| disabled
echo   test_name   - name of the test ^(stored as %TESTS_DIR%\^<test_name^>.gmt^)
echo   executable  - path to the game executable
exit /b 1
