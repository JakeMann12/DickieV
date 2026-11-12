# pio_wrapper.ps1: Invokes PlatformIO with the correct repo path and flags.
# Usage (from anywhere):
#   powershell -ExecutionPolicy Bypass -File .agent\pio_wrapper.ps1 run
#   powershell -ExecutionPolicy Bypass -File .agent\pio_wrapper.ps1 run -t clean -v
#
# This avoids the common `pio -C` mistake and ensures `pio run` always
# uses the DickieV project directory, even when invoked from other dirs.

# Find the DickieV repo root: walk up from this script until we find platformio.ini
$repoRoot = $PSScriptRoot
while ($repoRoot -and !(Test-Path (Join-Path $repoRoot 'platformio.ini'))) {
    $repoRoot = Split-Path -Parent $repoRoot
}
if (-not $repoRoot) {
    Write-Host "ERROR: Could not find DickieV repo root (no platformio.ini found)."
    exit 1
}

$pioExe   = 'c:\Users\jmann\.platformio\penv\Scripts\pio.exe'

$pioArgs = @()
foreach ($arg in $args) {
    if ($arg -eq '-C' -or $arg -eq '--project-dir') {
        # Skip next arg since we supply the project dir
        $null = $args | Select-Object -Skip 1
    } else {
        $pioArgs += $arg
    }
}

# The first non-option argument is the command. Everything else is
# passed to the command. We pass '-d projectDir' before the command.
$projectDir = $null
$remainingArgs = @()
$command = $null
foreach ($arg in $pioArgs) {
    if ($command -eq $null) {
        $command = $arg
    } else {
        $remainingArgs += $arg
    }
}

if ($projectDir -eq $null) {
    # Look for '-d' or '--project-dir' flag
    foreach ($arg in $pioArgs) {
        if ($arg -eq '-d' -or $arg -eq '--project-dir') {
            $projectDir = $args | Select-Object -Skip 1
        }
    }
}

if ($projectDir) {
    & $pioExe run -d $projectDir @remainingArgs
} else {
    & $pioExe run @remainingArgs
}

Write-Host 'Done.'
