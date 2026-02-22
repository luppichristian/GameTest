<#
.SYNOPSIS
    Run a single GameTest recording or replay session.

.DESCRIPTION
    Wraps the game executable with the --test-mode and --test arguments used by
    the GameTest framework.  The test file can be specified as a bare name
    (stored under tests\<name>.gmt relative to the repo root) or as an explicit
    path ending in .gmt.

.PARAMETER Mode
    The test mode: record, replay, or disabled.

.PARAMETER TestArg
    Test name (stored as tests\<name>.gmt) OR a path to a .gmt file.

.PARAMETER Exe
    Path to the game executable.  May be absolute or relative to the repo root.

.EXAMPLE
    .\game-test.ps1 record  my_test  build\GameTest-Game.exe
    # Records input to tests\my_test.gmt.

.EXAMPLE
    .\game-test.ps1 replay  my_test  build\GameTest-Game.exe
    # Replays input from tests\my_test.gmt.

.EXAMPLE
    .\game-test.ps1 record  example\tests\playing.gmt  build\GameTest-Game.exe
    # Records input to an explicit .gmt path.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('record', 'replay', 'disabled', IgnoreCase = $true)]
    [string] $Mode,

    [Parameter(Mandatory = $true, Position = 1)]
    [string] $TestArg,

    [Parameter(Mandatory = $true, Position = 2)]
    [string] $Exe
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$TestsDir  = 'tests'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path -Parent $ScriptDir

# ---------------------------------------------------------------------------
# Resolve the game executable
# ---------------------------------------------------------------------------
$ExePath = if ([IO.Path]::IsPathRooted($Exe)) { $Exe } else { Join-Path $RepoRoot $Exe }
if (-not (Test-Path $ExePath)) {
    Write-Error "Game executable not found: $ExePath`nBuild the project first (e.g. cmake --build build)."
    exit 1
}

# ---------------------------------------------------------------------------
# Resolve the test file path
# ---------------------------------------------------------------------------
$TestFile = if ($TestArg -imatch '\.gmt$') {
    if ([IO.Path]::IsPathRooted($TestArg)) { $TestArg } else { Join-Path $RepoRoot $TestArg }
} else {
    Join-Path $RepoRoot $TestsDir "$TestArg.gmt"
}

# ---------------------------------------------------------------------------
# Pre-flight checks
# ---------------------------------------------------------------------------
if ($Mode -ieq 'replay' -and -not (Test-Path $TestFile)) {
    Write-Error "Test file not found for replay: $TestFile`nRun in record mode first to create it."
    exit 1
}

if ($Mode -ieq 'record') {
    $TestDir = Split-Path -Parent $TestFile
    if (-not (Test-Path $TestDir)) {
        New-Item -ItemType Directory -Path $TestDir | Out-Null
    }
}

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------
Write-Host "[$Mode] $TestArg  ->  $TestFile"
& $ExePath "--test-mode=$Mode" "--test=$TestFile"
$exitCode = $LASTEXITCODE

if ($exitCode -ne 0) {
    Write-Host "Test exited with code $exitCode."
}

exit $exitCode
