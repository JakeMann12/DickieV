# Agent Instructions - DickieV

Read this whole file before you change anything. It is short on purpose.

## 1. What this project is

DickieV is firmware for an ESP8266 (ESP-12E) board with a 16x2 LCD.
It pulls Duke men's basketball games from the ESPN API over WiFi and shows:
- a live score during a game
- a ticking countdown (`T-HH:MM:SS`) before a game
- hype animations when there is no game today

It also updates itself over WiFi (OTA) from GitHub Releases.

## 2. The 5 rules (follow every time)

1. **Build after every code change:**
   `powershell -ExecutionPolicy Bypass -File .agent\build.ps1`
   You are done only when it prints `BUILD PASSED`. If it prints `BUILD FAILED`, fix the issue and build again.
2. **Never hard-code a user folder.** This repo is used on two PCs (`C:\Users\jmann` and `C:\Users\jake.mann`). Use paths relative to the repo, or `$env:USERPROFILE`.
3. **Never change `FIRMWARE_VERSION` by hand.** CI sets it (see section 7).
4. **Never commit secrets or build output.** Do not commit `.pio/`, `include/wifi_credentials.h`, or any WiFi passwords.
5. **Keep changes small.** Change only what the task needs. Do not reformat or reorder code you are not changing.

## 3. Shell rules (Windows PowerShell)

The terminal is **PowerShell**, not bash. Common mistakes:

| Do NOT write | Write instead |
|---|---|
| `cd x && pio run` | `cd x; pio run` |
| `cmd1 \|\| cmd2` | Run them as two separate commands |
| `pio run -C .` | `pio run --project-dir .` (or just use `.agent\build.ps1`) |
| `head -n 20 file` | `Get-Content file -TotalCount 20` |
| `tail -n 20 file` | `Get-Content file -Tail 20` |
| `grep text file` | `Select-String -Path file -Pattern text` |
| `ls -la` | `Get-ChildItem -Force` |
| `cat file` | `Get-Content file` |
| `git log` / `git diff` (can hang in a pager) | `git --no-pager log --oneline -5` / `git --no-pager diff` |
| `python -c "...complex quotes..."` | Write a `.py` file, then run `python file.py` |

`pio` may **not** be on PATH. `.agent\build.ps1` finds it for you. If you must call it directly, use:
`& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run --project-dir .`

## 4. Files

| Path | What it is | Edit? |
|---|---|---|
| `src/main.cpp` | All firmware logic (WiFi, ESPN, countdown, OTA, `setup()`, `loop()`) | Yes |
| `include/animations.h` | LCD custom characters and hype/celebration animations | Yes |
| `include/phrases.h` | Dickie V quote strings (`dickieV::pick(...)`) | Yes |
| `include/wifi_debug.h` | `WIFI_DEBUG_LCD` switch and WiFi debug LCD hooks (`wifiDebug::...`). Hooks are no-ops when the switch is `0`. | Yes |
| `platformio.ini` | Board and library settings | Only if the task needs it |
| `scripts/pre_build.py` | Adds WiFi credentials at build time | Rarely |
| `.github/workflows/buildversion.yml` | CI: builds and publishes a GitHub Release on push to `main` | Rarely |
| `check_espn.py` | Desktop script to inspect ESPN API responses (`python check_espn.py`) | Optional |
| `.agent/` | Agent docs and helper scripts. The firmware build never uses these files. | Only if asked |
| `.pio/` | Build output. Ignored by git. | Never |

Do not create new `.cpp` files in `src/` unless the task asks for it. All logic lives in `src/main.cpp`.

## 5. Map of `src/main.cpp`

Line numbers are approximate. Search for the name instead of trusting the number.

| Name | ~Line | Purpose |
|---|---|---|
| Settings block | 13-80 | `MY_TEAM`, ESPN `HOST`/`LEAGUE_PATH`, timing intervals, `FIRMWARE_VERSION`, `BUTTON_PIN`, GitHub owner/repo, `lcd` pins |
| `showOnLcd(l1, l2)` | 81 | Clears the LCD and prints two lines. Each line is cut to 16 chars. |
| `parseIsoUtc` / `formatCountdown` | 93 / 109 | ISO time to `time_t`; builds `T-HH:MM:SS` (`Tip-off soon` once start time passes) |
| `readBootCache` / `writeBootCache` | 162 / 169 | Saves the tracked game (event id, start, opponent) and last SSID to EEPROM so a reboot can show it before ESPN answers |
| `loadWifiNetworks` | 195 | Reads WiFi networks baked in at build time |
| `beginWifiConnect` / `pollWifiConnect` | 333 / 367 | Non-blocking WiFi connect that tries each network in turn |
| `refreshCountdownDisplay` / `tickCountdown` | 402 / 425 | Redraws the countdown only when the text changes (no flicker) |
| `fetchScore()` | 454 | Calls ESPN, shows score or countdown, updates `cachedGameStartIso` |
| `scanDukeGamesForDate` | 513 | Downloads and parses one day of ESPN games |
| `findNextDukeGame` | 639 | Looks up to 14 days ahead for the next game |
| `checkForUpdate()` | 725 | OTA: checks the latest GitHub release and flashes it if newer |
| `setup()` | 793 | LCD init, boot animations, WiFi, NTP time, first fetch |
| `loop()` | 886 | Button (D4) = check for update; ESPN refresh; countdown tick; hype animations; hourly OTA check |

### Key settings (top of `src/main.cpp`)

| Constant | Current value | Meaning |
|---|---|---|
| `MY_TEAM` | `{"150", "DUKE"}` | ESPN numeric team id (from the espn.com team URL) and the LCD label (<= 5 chars) |
| `CLOSE_GAME_CHECK_INTERVAL_MS` | `10UL * 1000UL` (10 s) | Poll rate when `isCloseLateGame()`: period >= 2, clock <= `CLOSE_GAME_CLOCK_SECS` (5 min), margin <= `CLOSE_GAME_MARGIN` (15) |
| `NEXT_GAME_RECHECK_INTERVAL_MS` | `60UL * 60UL * 1000UL` | How often an upcoming game is re-validated via `teams/{id}` |
| `NO_GAME_RECHECK_INTERVAL_MS` | `6UL * 60UL * 60UL * 1000UL` | Re-check interval when no games remain |
| `GAME_START_CHECK_INTERVAL_MS` | `30UL * 1000UL` (30 s) | How often `fetchScore()` runs (it only downloads when the game phase needs it) |
| `COUNTDOWN_TICK_MS` | `200UL` | How often the countdown text is checked. The LCD changes once per second. |
| `NO_GAME_HYPE_INTERVAL_MS` | `120UL * 1000UL` | Hype animation cycle when there is no game |
| `UPDATE_CHECK_INTERVAL_MS` | `60UL * 60UL * 1000UL` | OTA check, once an hour |
| `WIFI_DEBUG_LCD` | `0` | Lives in `include/wifi_debug.h`. Set to `1` (or add `build_flags = -DWIFI_DEBUG_LCD=1`) to show WiFi debug info on the LCD. Set it back to `0` before committing. |
| `BUTTON_PIN` | `D4` | Button to GND, `INPUT_PULLUP`, LOW = pressed |

If you change a value here, update this table too.

## 6. Hardware and runtime facts

- LCD: 16 columns x 2 rows, `hd44780_pinIO lcd(D1, D2, D5, D6, D7, D0)`. **Any text longer than 16 chars is cut off.** Check string lengths.
- The ESP8266 has very little RAM (~40 KB free). Avoid large `String` concatenations, big buffers, and large `JsonDocument`s. Use ArduinoJson filters, as the existing code does.
- Never block `loop()` for a long time. Use `millis()` timers like the existing code. Do not add long `delay()` calls.
- Times from ESPN are UTC. They are shown in Pacific time (see `makePstDateKeyFromUtc`).
- Libraries: ArduinoJson **v7** (use `JsonDocument`, not `StaticJsonDocument`/`DynamicJsonDocument`), hd44780.

## 7. Versions and releases (do not do this by hand)

- On every push to `main`, the GitHub Action `.github/workflows/buildversion.yml`:
  1. sets `FIRMWARE_VERSION` in `src/main.cpp` to the run number (for example `"24"`)
  2. builds the firmware
  3. creates the GitHub release `v24` with `firmware.bin` attached
- Devices compare the release tag against their own `FIRMWARE_VERSION` and update when the tag is higher.
- In the repo, `FIRMWARE_VERSION` stays `"0"`. **Do not edit it, and do not create git tags.**

## 8. WiFi credentials (local builds)

`scripts/pre_build.py` adds WiFi networks to the firmware at build time:
1. from the `WIFI_SECRETS` environment variable (`SSID|PASSWORD`, one per line, max 8), or
2. from `include/wifi_credentials.h`. It is ignored by git and is **never** committed. Format: `{"SSID", "PASSWORD"},`

If neither exists, the build still passes and prints `WARNING: WIFI_SECRETS is empty`. That is normal for test builds. The board just won't connect to WiFi.

## 9. Standard workflow

1. `git status` and `git --no-pager log --oneline -5` to see the current state.
2. Read the parts of `src/main.cpp` you need. Use section 5 to find them.
3. Make the change.
4. `powershell -ExecutionPolicy Bypass -File .agent\build.ps1` and repeat until `BUILD PASSED`.
5. `git --no-pager diff` and check that only the intended lines changed.
6. Commit only if the user asks you to.

## 10. Helper scripts in `.agent/`

| Script | Purpose |
|---|---|
| `build.ps1` | Builds the firmware and prints `BUILD PASSED` or `BUILD FAILED`. Use `-Clean` for a full rebuild. The full log is in `.pio\agent_build.log`. |
| `setup_env.ps1` | Checks that PlatformIO and the ESP8266 toolchain are installed. |
| `instructions.ps1` | Prints this file. |

Uploading to the board (`pio run -t upload`) needs the board connected over USB. Do it only when the user asks.
