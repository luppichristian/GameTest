@echo off
setlocal enabledelayedexpansion
rem Usage: game-test.bat <mode> <test_file> <executable>
rem   mode        - record | replay | disabled
rem   test_file   - test name (stored as tests\<name>.gmt) OR path to a .gmt file
rem   executable  - path to the game executable
rem
rem Examples:
rem   game-test.bat record  my_test                        build\GameTest-Game.exe
rem   game-test.bat record  example\tests\playing.gmt      build\GameTest-Game.exe
rem   game-test.bat replay  my_test                        build\GameTest-Game.exe

set TESTS_DIR=tests

rem ── Argument validation ──────────────────────────────────────────────────────

if "%~3"=="" goto usage

set MODE=%~1
set TEST_ARG=%~2
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

rem If the argument already ends with .gmt treat it as a path, else build tests\<name>.gmt
set _ARG_END=%TEST_ARG:~-4%
if /i "%_ARG_END%"==".gmt" (
  set TEST_FILE=%REPO_ROOT%\%TEST_ARG%
) else (
  set TEST_FILE=%REPO_ROOT%\%TESTS_DIR%\%TEST_ARG%.gmt
)
set TEST_FILE=%TEST_FILE:/=\%
set EXE=%REPO_ROOT%\%GAME_EXE%
set EXE=%EXE:/=\%

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
  for %%F in ("%TEST_FILE%") do set TEST_DIR=%%~dpF
  if not exist "!TEST_DIR!" mkdir "!TEST_DIR!"
)

rem ── Run ──────────────────────────────────────────────────────────────────────

echo [%MODE%] %TEST_ARG%  -^>  %TEST_FILE%
"%EXE%" "--test-mode=%MODE%" "--test=%TEST_FILE%"
set EXIT_CODE=%ERRORLEVEL%

if %EXIT_CODE% neq 0 (
  echo Test exited with code %EXIT_CODE%.
)

exit /b %EXIT_CODE%

rem ── Usage ─────────────────────────────────────────────────────────────────────

:usage
echo Usage: %~nx0 ^<mode^> ^<test_name^> ^<executable^>
echo   mode        - record ^| replay ^| disabled
echo   test_file   - test name ^(stored as %TESTS_DIR%\^<name^>.gmt^) OR path to a .gmt file
echo   executable  - path to the game executable
exit /b 1
