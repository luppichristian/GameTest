<#
.SYNOPSIS
    Runs multiple GameTest replay tests in parallel, one game process per test.

.DESCRIPTION
    Launches every .gmt test file as an independent game process.  Because each
    process has its own address space all framework globals (g_gmt, g_replayed_input,
    IAT hooks, LL hooks) are fully isolated.

    The only shared OS resource that can still race is the system cursor position,
    because SendInput with MOUSEEVENTF_ABSOLUTE writes to a single desktop-wide
    cursor.  Pass -Isolated to launch each process in its own Win32 window station
    (which owns a private cursor), but note that non-interactive stations cannot
    host rendered windows — only use -Isolated with headless / off-screen builds.

.PARAMETER Exe
    Path to the game executable.  May be absolute or relative to the repo root.

.PARAMETER Tests
    One or more .gmt file paths to replay.  May be absolute or relative to the
    repo root.  If omitted, all *.gmt files under tests\ are discovered automatically.

.PARAMETER Isolated
    Launch each process through gmt-launch-isolated.exe so every run gets its own
    Win32 window station and therefore its own cursor position.
    Requires gmt-launch-isolated.exe to be built first (cmake --build build).
    NOT suitable for games that open a visible rendered window.

.PARAMETER Jobs
    Maximum number of tests to run simultaneously.  0 (default) means no limit —
    all tests start at once.

.EXAMPLE
    .\game-test-parallel.ps1 -Exe build\GameTest-Game.exe
    # Auto-discovers and replays every tests\*.gmt file in parallel.

.EXAMPLE
    .\game-test-parallel.ps1 -Exe build\GameTest-Game.exe example\tests\playing.gmt
    # Replays a single specific test (useful for smoke-testing the runner).

.EXAMPLE
    .\game-test-parallel.ps1 -Exe build\GameTest-Game.exe -Jobs 4
    # Runs at most 4 tests concurrently (useful on machines with few cores).

.EXAMPLE
    .\game-test-parallel.ps1 -Exe build\GameTest-Game.exe -Isolated
    # Headless build: full cursor isolation via separate window stations.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $Exe,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Tests,

    [switch] $Isolated,

    [ValidateRange(0, 256)]
    [int] $Jobs = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

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
# Resolve the isolated launcher (optional)
# ---------------------------------------------------------------------------
$LauncherPath = $null
if ($Isolated) {
    $LauncherPath = Join-Path $RepoRoot 'build\gmt-launch-isolated.exe'
    if (-not (Test-Path $LauncherPath)) {
        Write-Error "gmt-launch-isolated.exe not found at $LauncherPath`nBuild the project first."
        exit 1
    }
}

# ---------------------------------------------------------------------------
# Resolve test files
# ---------------------------------------------------------------------------
[string[]] $ResolvedTests = @()

if (-not $Tests -or $Tests.Count -eq 0) {
    $TestsDir = Join-Path $RepoRoot 'tests'
    if (-not (Test-Path $TestsDir)) {
        Write-Error "No -Tests specified and tests\ directory not found at $TestsDir"
        exit 1
    }
    $ResolvedTests = Get-ChildItem -Path $TestsDir -Filter '*.gmt' -Recurse |
                     ForEach-Object { $_.FullName }
    if ($ResolvedTests.Count -eq 0) {
        Write-Error "No .gmt files found in $TestsDir"
        exit 1
    }
} else {
    foreach ($t in $Tests) {
        $p = if ([IO.Path]::IsPathRooted($t)) { $t } else { Join-Path $RepoRoot $t }
        if (-not (Test-Path $p)) {
            Write-Warning "Test file not found, skipping: $p"
        } else {
            $ResolvedTests += $p
        }
    }
    if ($ResolvedTests.Count -eq 0) {
        Write-Error 'No valid test files to run.'
        exit 1
    }
}

$Total    = $ResolvedTests.Count
$MaxJobs  = if ($Jobs -gt 0) { $Jobs } else { $Total }

Write-Host "Running $Total test(s) with up to $MaxJobs parallel process(es)..."
if ($Isolated) { Write-Host '(window-station isolation enabled — headless mode)' }
Write-Host ''

# ---------------------------------------------------------------------------
# Process tracking helpers
# ---------------------------------------------------------------------------

# Each entry: @{ Proc; Name; Stdout; Stderr } where Stdout/Stderr are Tasks.
$Running  = [System.Collections.Generic.List[hashtable]]::new()
$Results  = [System.Collections.Generic.List[hashtable]]::new()
$Queue    = [System.Collections.Generic.Queue[string]]::new($ResolvedTests)

function Start-NextTest {
    if ($Queue.Count -eq 0) { return }
    $testFile = $Queue.Dequeue()
    $name     = [IO.Path]::GetFileNameWithoutExtension($testFile)

    if ($Isolated) {
        $startExe  = $LauncherPath
        $arguments = "`"$ExePath`" `"--test-mode=replay`" `"--test=$testFile`""
    } else {
        $startExe  = $ExePath
        $arguments = "`"--test-mode=replay`" `"--test=$testFile`""
    }

    $psi = [System.Diagnostics.ProcessStartInfo]::new()
    $psi.FileName               = $startExe
    $psi.Arguments              = $arguments
    $psi.UseShellExecute        = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    $psi.CreateNoWindow         = $true

    $proc = [System.Diagnostics.Process]::new()
    $proc.StartInfo = $psi
    [void]$proc.Start()

    # Read stdout/stderr asynchronously so buffers never fill and deadlock.
    $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
    $stderrTask = $proc.StandardError.ReadToEndAsync()

    Write-Host "  Started  [$name]  (pid $($proc.Id))"

    $script:Running.Add(@{
        Proc       = $proc
        Name       = $name
        StdoutTask = $stdoutTask
        StderrTask = $stderrTask
    })
}

# ---------------------------------------------------------------------------
# Seed the initial batch
# ---------------------------------------------------------------------------
for ($i = 0; $i -lt $MaxJobs -and $Queue.Count -gt 0; $i++) { Start-NextTest }

# ---------------------------------------------------------------------------
# Drain loop — poll until every process has exited
# ---------------------------------------------------------------------------
while ($Running.Count -gt 0) {
    Start-Sleep -Milliseconds 150

    $stillRunning = [System.Collections.Generic.List[hashtable]]::new()
    foreach ($item in $Running) {
        if ($item.Proc.HasExited) {
            $stdout = $item.StdoutTask.GetAwaiter().GetResult()
            $stderr = $item.StderrTask.GetAwaiter().GetResult()
            $passed = $item.Proc.ExitCode -eq 0
            $Results.Add(@{
                Name     = $item.Name
                Passed   = $passed
                ExitCode = $item.Proc.ExitCode
                Output   = ($stdout + $stderr).Trim()
            })
            $item.Proc.Dispose()
            # Fill the freed slot with the next pending test.
            Start-NextTest
        } else {
            $stillRunning.Add($item)
        }
    }
    $Running = $stillRunning
}

# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
$passed = 0
$failed = 0

Write-Host ''
Write-Host ('─' * 50)
Write-Host '  Results'
Write-Host ('─' * 50)

foreach ($r in $Results | Sort-Object Name) {
    if ($r.Passed) {
        Write-Host "  [ PASS ]  $($r.Name)"
        $passed++
    } else {
        Write-Host "  [ FAIL ]  $($r.Name)  (exit $($r.ExitCode))"
        if ($r.Output) {
            foreach ($line in ($r.Output -split "`n" | Select-Object -First 20)) {
                Write-Host "            $line"
            }
        }
        $failed++
    }
}

Write-Host ('─' * 50)
Write-Host "  Passed: $passed   Failed: $failed   Total: $($passed + $failed)"
Write-Host ('─' * 50)

exit $(if ($failed -gt 0) { 1 } else { 0 })
