# setup_env.ps1 — Run once per machine.
# Verifies PlatformIO and the ESP8266 toolchain for the DickieV repo.
# No side effects unless explicitly invoked (only reads paths).
param(
    [string]$PythonExe = "c:\Users\jmann\.platformio\penv\Scripts\python.exe",
    [string]$PioExe    = "c:\Users\jmann\.platformio\penv\Scripts\pio.exe"
)

Write-Host "=== DickieV Agent Setup ==="

# Verify PlatformIO executable
if (Test-Path $PioExe) {
    $ver = & $PioExe --version 2>$null
    Write-Host "PlatformIO: $($ver)"
} else {
    Write-Host "MISSING: $PioExe"
}

# Verify ESP8266 toolchain (xtensa-lx106-elf-gcc)
$toolchain = "c:\Users\jmann\.platformio\packages\toolchain-xtensa\bin\xtensa-lx106-elf-gcc.exe"
if (Test-Path $toolchain) {
    Write-Host "Toolchain: $toolchain"
} else {
    Write-Host "MISSING: $toolchain"
}

# Verify PlatformIO Python environment
if (Test-Path $PythonExe) {
    Write-Host "Python: $PythonExe"
} else {
    Write-Host "MISSING: $PythonExe"
}

Write-Host "Setup complete."
