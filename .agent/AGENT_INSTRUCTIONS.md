# Agent Instructions — DickieV ESP8266 Project

## LOCATION & SCOPE
- **This file:** `.agent/AGENT_INSTRUCTIONS.md` — **read-only for agents.**
- **Never** include `.agent/` files in firmware builds. `.agent/` is **not** part of the ESP8266 source tree and must **never** be added to `src/`, `include/`, or any compiled output.
- This file is the **single source of truth** for agent behavior. If this file and a shell session conflict, this file wins.

---

## MANDATORY FIRST STEPS (do these before ANY work, every session)

1. **Read this file in full.** Do not skip sections.
2. **Read `src/main.cpp` header block (lines 1–60)** to confirm `TEAM`, `COUNTDOWN_TICK_MS`, `GAME_START_CHECK_INTERVAL_MS`, `FIRMWARE_VERSION`, and pin mappings match the project context below.
3. **Verify the build toolchain:** run `.agent\setup_env.ps1` or confirm `pio.exe` is on PATH.
4. **Check git status:** `git status` and `git log --oneline -5` to know what changed before you.
5. **After any edit to `src/`:** rebuild with `pio run --project-dir .` and confirm **zero warnings and zero errors** before declaring done.

If you cannot confirm items 1–4, stop and re-read this file. Do not guess.

---

## PROJECT CONTEXT (so you never re-read the whole codebase)

### What this is
DickieV is an **ESP8266 (ESP-12E) Arduino firmware** project that displays a Duke basketball countdown on a character LCD. It polls the ESPN scoreboard API over WiFi, caches the next Duke game start time in EEPROM, and shows a ticking `T-HH:MM:SS` countdown until game time.

### Platform / Build
- Platform: `espressif8266`, board: `esp12e`, framework: `arduino`, CPU: 160MHz
- Build system: **PlatformIO** (`pio.exe` / `platformio.exe` on PATH)
- Project root: `c:\Users\jmann\Documents\DickieV\DickieV`
- `platformio.ini` lives at project root; `lib_deps`: ArduinoJson ^7, hd44780 ^1.3.2
- Pre-build script: `scripts/pre_build.py` injects WiFi secrets from `WIFI_SECRETS_BYTES` env var, falling back to `include/wifi_credentials.h`
- Build artifacts go to `.pio\build\esp12e\` — **never** commit `.pio/`

### Key source file
- **`src/main.cpp`** — the only firmware source file. Everything lives here.
- Include order (do not reorder): `Arduino.h`, `ESP8266WiFi.h`, `WiFiClientSecure.h`, `ESP8266HTTPClient.h`, `ESP8266httpUpdate.h`, `EEPROM.h`, `ArduinoJson.h`, `hd44780.h`, `hd44780ioClass/hd44780_pinIO.h`, `time.h`, `phrases.h`, `animations.h`

### Key constants (defined at top of `src/main.cpp`)
| Constant | Value | Meaning |
|---|---|---|
| `TEAM` | `"DUKE"` | Team abbreviation filtered from ESPN API |
| `COUNTDOWN_TICK_MS` | `1000UL` | LCD countdown refresh interval (1 second) |
| `GAME_START_CHECK_INTERVAL_MS` | `15UL * 1000UL` | Under-the-hood ESPN re-check interval (15 s) |
| `FIRMWARE_VERSION` | `"0"` | Displayed on LCD splash |
| `BUTTON_PIN` | `D4` | Button to GND, INPUT_PULLUP, pressed = LOW |
| `NO_GAME_HYPE_INTERVAL_MS` | `120UL * 1000UL` | No-game hype animation cycle |
| `UPDATE_CHECK_INTERVAL_MS` | `60 * 60 * 1000UL` | OTA hourly check |

### Key functions in `src/main.cpp`
| Function | Line (approx) | Purpose |
|---|---|---|
| `showOnLcd(l1, l2)` | ~63 | Clears LCD, prints line1/line2 (each truncated to 16 chars) |
| `parseIsoUtc(iso)` | ~73 | Parses ISO-8601 to `time_t` UTC |
| `formatPstTime(iso)` | ~89 | `HH:MM PST` from ISO |
| `formatCountdown(iso)` | ~100 | `T-HH:MM:SS` ticking countdown, `LIVE` if past, `T- --:--` on parse fail |
| `refreshCountdownDisplay()` | ~305 | Builds `DUKE @ <time>` + `T-HH:MM:SS`, dedupes against last render |
| `fetchScore()` | ~322 | Polls ESPN, updates `cachedGameStartIso`, shows score or countdown |
| `loop()` | ~713 | Button debounce, 15s ESPN re-check, 1s countdown tick, hype animations, OTA check |

### Runtime behavior (under the hood vs. on screen)
- **Under the hood:** `fetchScore()` runs every `GAME_START_CHECK_INTERVAL_MS` (15s) over WiFi to verify/correct the true game start time and update `cachedGameStartIso`.
- **On screen:** `loop()` ticks `refreshCountdownDisplay()` every `COUNTDOWN_TICK_MS` (1s) off `cachedGameStartIso` + `time(nullptr)`. The clock runs continuously between ESPN re-checks; it does **not** wait for the 15s check to update the display.
- Countdown format: always `T-HH:MM:SS`. Never `"in X days"` (that static behavior was removed).

### Hardware pins (hd44780_pinIO)
`lcd(D1, D2, D5, D6, D7, D0)` — RS=D1, RW=D2, E=D5, D4=D6, D5=D7, D6=D0. LCD is 16×2.

---

## AGGRESSIVELY ENFORCED RULES (violations = redo the work)

### Rule 1: `&&` is NOT a shell separator in PowerShell

### Rule 1: `&&` is NOT a shell separator in PowerShell
PowerShell treats `&&` as a variable-name continuation, not a command separator. **Always use `;`** between commands. Never write `cd ... && pio run`. A command like:





### Rule 2: `-C` is NOT a PlatformIO option
`pio.exe` does **not** accept `-C`. Use `--project-dir` or `cd` first:
pio run --project-dir c:\Users\jmann\Documents\DickieV\DickieV


### Rule 3: No Unix utilities in PowerShell
No `head`, `tail`, `ls -la`, `grep`. Use `Get-Content -TotalCount` / `-Tail`, `dir` / `Get-ChildItem -Force`, `Select-String`.

### Rule 4: Build verification is mandatory
After **every** edit to `src/`:
1. `pio run --project-dir .`
2. Confirm **zero warnings and zero errors** in output.
3. If `Wunused-function`, `format-truncation`, or any new warning appears, fix it before finishing.

### Rule 5: `.agent/` is never compiled
Never add `.agent/` to `src/`, `include/`, or any build path. Never reference `.agent/` paths in firmware code or OTA artifacts.

### Rule 6: Commit discipline
Only source changes in `src/` (and intentionally `include/`) belong in commits. Never commit `.pio/`, build logs, or `.agent/` working files.

### Rule 7: PowerShell quoting
Avoid nested quotes in `-c` Python arguments; write a `.py` file and run it. Avoid `||` chains in PowerShell; use `;`.


---

## BUILD VERIFICATION WORKFLOW (copy-paste this)

```powershell
cd c:\Users\jmann\Documents\DickieV\DickieV
pio run --project-dir .
```

Expected success marker: `======================== [SUCCESS]` with no `warning:` lines from `src/main.cpp`.

If you see `[-Wunused-function]` or similar, remove the unused function — do not leave warnings.

---

## SESSION HANDOFF TEMPLATE
When ending a session, leave a note for the next agent (append to `.agent/SESSION_NOTES.md` or this file):

```
## Session Handoff — <date>
### What was done:
- <change 1>
- <change 2>
### Files modified:
- src/main.cpp (lines X–Y)
### Build status:
- [SUCCESS] / [FAILED] — pio run output notes
### Next steps / TODOs:
- <todo 1>
- <todo 2>
### Known open issues:
- <issue>
```

Do not start a new session without reading the most recent handoff note first.

---

## FILES THAT MUST NOT BE COMPILED
| Path | Purpose | Compiled? |
|---|---|---|
| `src/main.cpp` | Firmware entry | ✅ Yes |
| `src/*.h` | Firmware headers | ✅ Yes |
| `.agent/` | Agent instructions | ❌ Never |
| `lib/`, `include/` | Libraries | ✅ Yes (headers only) |
| `.pio/`, `.vscode/` | Build config | ❌ Never |






git show c88895e -- include/animations.h \| 


# CORRECT:
git show c88895e -- include/animations.h \| Select-Object -First 100


### Rule 9: `pio` / `platformio` CLI IS installed
The `pio.exe` executable **is** on this machine's PATH at `C:\Users\jmann\.platformio\penv\Scripts`. Running `pio run` or `platformio run` works directly.

You may run `pio` or `platformio` directly (e.g. `pio run --project-dir .`). Prefer `--project-dir` over `-C` (see Rule 2) and use `;` as the shell separator in PowerShell (see Rule 1).

### Rule 10: `cd ... && ...` chains fail even with single quotes
Even wrapping paths in single quotes does not make `&&` valid in PowerShell. Always split into separate commands with `;`:

```powershell
# WRONG:
cd 'C:\path' && dir

# CORRECT:
cd 'C:\path'; dir
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

---

## 6. Version Bumping Rules

### 6.1. Version Format

`FIRMWARE_VERSION` in `src/main.cpp` follows semantic versioning: `MAJOR.MINOR.PATCH` (e.g. `1.0.0`).

The LCD splash displays versions as `vMAJOR.MINOR.PATCH` (the firmware release tag from GitHub).

### 6.2. Bumping the Version

The agent decides which component to bump:

| Bump type | When to use | Effect |
|-----------|-------------|--------|
| `major`   | Breaking changes, new hardware, incompatible API | `1.0.0` → `2.0.0` |
| `minor`   | New features, backward-compatible changes | `1.0.0` → `1.1.0` |
| `patch`   | Bug fixes, small tweaks | `1.0.0` → `1.0.1` |

Use `.agent/bump_version.py`:

```powershell
python .agent/bump_version.py patch   # 1.0.0 → 1.0.1
python .agent/bump_version.py minor   # 1.0.0 → 1.1.0
python .agent/bump_version.py major   # 1.0.0 → 2.0.0
python .agent/bump_version.py --new 1.2.3  # explicit version
```

### 6.3. After Bumping

1. Commit the `src/main.cpp` change.
2. Create a git tag: `git tag v<major>.<minor>.<patch>` (e.g. `git tag v1.0.1`).
3. Push tag: `git push origin v1.0.1`.
4. Rebuild firmware: `pio run --project-dir .`.
5. The GitHub Actions workflow (`buildversion.yml`) will also sync `FIRMWARE_VERSION` to `github.run_number` on CI builds.

### 6.4. LCD Version Display

The version splash now shows: `v<major>.<minor>.<patch>`

Example: `v1.0.0`

The version comes from `FIRMWARE_VERSION` in `src/main.cpp`, which is synced to the GitHub
release tag on CI builds. No build/commit number is displayed.
