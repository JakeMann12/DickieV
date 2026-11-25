# build.ps1 - Build the DickieV firmware and print a clear PASS/FAIL verdict.
# Works from any directory and on any user account (no hard-coded paths).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .agent\build.ps1          # normal build
#   powershell -ExecutionPolicy Bypass -File .agent\build.ps1 -Clean   # clean, then build

param([switch]$Clean)

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $repoRoot 'platformio.ini'))) {
    Write-Host "BUILD FAILED: platformio.ini not found in $repoRoot"
    exit 1
}

# Find pio: PATH first, then the default PlatformIO install for the current user.
$pio = (Get-Command pio -ErrorAction SilentlyContinue).Source
if (-not $pio) {
    $candidate = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\pio.exe'
    if (Test-Path $candidate) { $pio = $candidate }
}
if (-not $pio) {
    Write-Host "BUILD FAILED: pio.exe not found on PATH or in $env:USERPROFILE\.platformio\penv\Scripts"
    exit 1
}

$logFile = Join-Path $repoRoot '.pio\agent_build.log'
New-Item -ItemType Directory -Force -Path (Split-Path $logFile) | Out-Null

if ($Clean) {
    & $pio run -t clean --project-dir $repoRoot | Out-Null
}

& $pio run --project-dir $repoRoot *>&1 | ForEach-Object { "$_" } | Set-Content $logFile
$exitCode = $LASTEXITCODE

# Only our own code matters (src/ and include/); library/framework noise is ignored.
$problems = Select-String -Path $logFile -Pattern '(src|include)[\\/][^:]+:\d+:\d+: (warning|error)'

Write-Host "Full log: $logFile"
if ($problems) {
    Write-Host "Problems in src/ or include/:"
    $problems | ForEach-Object { Write-Host "  $($_.Line)" }
}

if ($exitCode -eq 0 -and -not $problems) {
    Write-Host "BUILD PASSED (0 errors, 0 warnings in src/ and include/)"
    exit 0
} elseif ($exitCode -eq 0) {
    Write-Host "BUILD FAILED: compiled, but there are warnings above. Fix them."
    exit 1
} else {
    Write-Host "BUILD FAILED: compiler errors. Last 30 lines of log:"
    Get-Content $logFile -Tail 30 | ForEach-Object { Write-Host "  $_" }
    exit 1
}
