# setup_env.ps1 - Check that the PlatformIO toolchain is installed. Read-only.
# Works on any user account (uses $env:USERPROFILE, no hard-coded usernames).
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File .agent\setup_env.ps1

$pioHome = Join-Path $env:USERPROFILE '.platformio'
$checks = [ordered]@{
    'pio.exe'   = Join-Path $pioHome 'penv\Scripts\pio.exe'
    'Toolchain' = Join-Path $pioHome 'packages\toolchain-xtensa\bin\xtensa-lx106-elf-gcc.exe'
}

$ok = $true
foreach ($name in $checks.Keys) {
    if (Test-Path $checks[$name]) {
        Write-Host "OK      $name : $($checks[$name])"
    } else {
        Write-Host "MISSING $name : $($checks[$name])"
        $ok = $false
    }
}

if (Get-Command pio -ErrorAction SilentlyContinue) {
    Write-Host "OK      pio is on PATH"
} else {
    Write-Host "NOTE    pio is not on PATH. Use .agent\build.ps1, which finds it for you."
}

if ($ok) { Write-Host "SETUP OK" } else { Write-Host "SETUP INCOMPLETE (toolchain downloads on first build)" }
