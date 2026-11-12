# Agent Instructions — DickieV ESP8266 Project

## Location
- **This file:** `.agent/AGENT_INSTRUCTIONS.md`
- **Read-only for agents** — never include these files in firmware builds.
- The `.agent/` directory is **not** part of the ESP8266 source tree and must **never** be added to `src/`, `include/`, or any compiled output.

---

## 1. PowerShell / CMD Compatibility Rules

These are the same mistakes repeated across sessions. Adopt them as habits.

### Rule 1: `&&` is NOT a shell separator in PowerShell
PowerShell treats `&&` as a variable-name continuation, not a command separator. A command like:

```powershell
cd C:\path && pio run
```

becomes:
```powershell
cd C:\path
pio run
```
...but the parser may break on the `&&`. **Always use `;` as the separator.**

### Rule 2: `-C` is NOT a PlatformIO option
PlatformIO's `pio.exe` does **not** accept `-C`. The correct flag is `--project-dir`:

```powershell
pio.exe run --project-dir c:\Users\jmann\Documents\DickieV\DickieV
```

or use `cd` first:

```powershell
cd c:\Users\jmann\Documents\DickieV\DickieV
pio.exe run
```

### Rule 3: `where` in PowerShell is a alias that does NOT support `||`
```powershell
where cmd1 || where cmd2   # WRONG — breaks parsing
```

Use semicolons or `;` instead:

```powershell
where cmd1; where cmd2
```

### Rule 4: `Get-ChildItem -Filter` takes a single string, not an array
```powershell
Get-ChildItem -Filter *.ino, *.json   # WRONG
Get-ChildItem -Filter "*.ino"         # CORRECT
```

### Rule 5: `Where-Object` boolean operators need parentheses
```powershell
Where-Object { $_.Name -eq 'a' -and $_.Name -eq 'b' }  # WRONG
Where-Object { ($_.Name -eq 'a') -and ($_.Name -eq 'b') }  # CORRECT
```

### Rule 6: PowerShell's default output encoding can corrupt tool output
When running long tool commands, pipe to `Out-File` and then read with `Get-Content`:

```powershell
cmd 2>&1 | Out-File -FilePath c:\tmp\build.log
Get-Content c:\tmp\build.log
```

### Rule 7: Avoid nested quotes in `-c` Python arguments
PowerShell's quote-escaping mangles `-c` strings. Write a `.py` file and run it:

```powershell
python c:\tmp\inspect.py
```

---

## 2. Reusable Setup Helper

`setup_env.ps1` — installs/verifies the PlatformIO and ESP8266 toolchain for this repo.

```powershell
# setup_env.ps1 — Run once per machine.
# Reads environment state; no side effects unless explicitly invoked.
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
```

---

## 3. Build Verification Workflow

When making a change to `src/`:

1. **Clean:** `pio run -t clean --project-dir .`
2. **Build:** `pio run --project-dir .` (or `-v` for verbose)
3. **Check for warnings** — specifically:
   - `format-truncation`
   - `Wunused-function`
   - any new error in `src/main.cpp`
4. **Confirm no new files** are added to `src/`, `include/`, `lib/`, or `test/`.
5. **Review the git diff** before committing — only source changes in `src/` should be committed to the build artifact.

---

## 4. Files That Must NOT Be Compiled

| Path | Purpose | Compiled? |
|------|---------|-----------|
| `src/main.cpp` | Firmware entry | ✅ Yes |
| `src/*.h` | Firmware headers | ✅ Yes (via includes) |
| `.agent/` | Agent instructions | ❌ Never |
| `lib/`, `include/` | Libraries | ✅ Yes (library headers) |
| `.pio/`, `.vscode/` | Build config | ❌ Never |

---

## 5. OTA Update Pipeline Safety

When working on `checkForUpdate()` or any OTA-related code:

- **Never** create a firmware binary that contains files from `.agent/`.
- The OTA update loads only `src/main.cpp` and its dependencies.
- If adding agent tooling, put it in `.agent/` or root-level scripts that are excluded from `src/`.
- Always verify the build artifact (`.pio/build/esp12e/*.elf`) does not reference `.agent/` paths.
