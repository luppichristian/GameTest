@echo off
rem game-test-parallel.bat
rem Thin wrapper that invokes game-test-parallel.ps1 through PowerShell.
rem
rem Usage:
rem   game-test-parallel.bat <executable> [options] [test1.gmt test2.gmt ...]
rem
rem Options forwarded to the PowerShell script:
rem   -Isolated    Launch each test in its own Win32 window station (headless only).
rem   -Jobs N      Max concurrent tests (default: 0 = all).
rem
rem Examples:
rem   game-test-parallel.bat build\GameTest-Game.exe
rem   game-test-parallel.bat build\GameTest-Game.exe -Jobs 4
rem   game-test-parallel.bat build\GameTest-Game.exe example\tests\playing.gmt

setlocal

set SCRIPT_DIR=%~dp0
if "%SCRIPT_DIR:~-1%"=="\" set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

powershell.exe -NoProfile -ExecutionPolicy Bypass ^
  -File "%SCRIPT_DIR%\game-test-parallel.ps1" %*

exit /b %ERRORLEVEL%
