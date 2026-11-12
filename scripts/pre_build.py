"""PlatformIO pre-build script: inject BUILD_NUMBER and WiFi credentials.

Runs `git rev-list --count HEAD` to get the total commit count and writes it
to src/build_version.h as #define BUILD_NUMBER <n>. This value is compiled
into the firmware and used on the LCD splash to show v#.#.# b##.

PlatformIO extra_scripts run in SCons context; use `Import('env')` to get the
build environment and `env.subst("$PROJECT_DIR")` for the project path.
"""
import os
import subprocess
from pathlib import Path

Import("env")

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
SRC_DIR = PROJECT_DIR / "src"
HEADER_PATH = SRC_DIR / "build_version.h"


def get_build_number() -> int:
    """Return total commit count from git, or 0 if git is unavailable."""
    try:
        result = subprocess.run(
            ["git", "rev-list", "--count", "HEAD"],
            cwd=PROJECT_DIR,
            capture_output=True,
            text=True,
            check=True,
        )
        return int(result.stdout.strip())
    except (subprocess.CalledProcessError, FileNotFoundError, ValueError):
        print("WARNING: Could not determine git build number, defaulting to 0")
        return 0


build_number = get_build_number()
HEADER_PATH.parent.mkdir(parents=True, exist_ok=True)
HEADER_PATH.write_text(
    "#ifndef BUILD_VERSION_H\n"
    "#define BUILD_VERSION_H\n"
    "\n"
    f"#define BUILD_NUMBER {build_number}\n"
    "\n"
    "#endif // BUILD_VERSION_H\n"
)
print(f"Build number injected: BUILD_NUMBER={build_number}")


# --- WiFi credentials -------------------------------------------------------
# Read from the WIFI_SECRETS environment variable (the GitHub Actions repo
# secret in CI; set it manually for local builds). Format: one network per
# line, "SSID|PASSWORD". Passed to the compiler as a comma-separated byte list
# (WIFI_SECRETS_BYTES) so no header file is written and no shell/SCons escaping
# of special characters in passwords is needed.
MAX_WIFI_NETWORKS = 8


def get_wifi_secrets() -> str:
    data = os.environ.get("WIFI_SECRETS", "").strip()
    if not data:
        msg = "WIFI_SECRETS is empty or unset; firmware will have no WiFi networks."
        if os.environ.get("GITHUB_ACTIONS") == "true":
            raise SystemExit(f"ERROR: {msg} Add the secret in the repository settings.")
        print(f"WARNING: {msg} Set $env:WIFI_SECRETS = 'SSID|PASSWORD' for local builds.")
        return ""

    entries = []
    for line in data.splitlines():
        line = line.strip()
        if not line:
            continue
        if "|" not in line:
            raise SystemExit("ERROR: Bad WIFI_SECRETS entry format. Expected SSID|PASSWORD per line.")
        ssid, password = (part.strip() for part in line.split("|", 1))
        if not ssid or not password:
            raise SystemExit("ERROR: Invalid WIFI_SECRETS entry (empty SSID or password).")
        entries.append(f"{ssid}|{password}")

    if len(entries) > MAX_WIFI_NETWORKS:
        raise SystemExit(f"ERROR: WIFI_SECRETS has more than {MAX_WIFI_NETWORKS} networks.")
    return "\n".join(entries)


wifi_secrets = get_wifi_secrets()
if wifi_secrets:
    byte_list = ",".join(str(b) for b in wifi_secrets.encode("utf-8"))
    env.Append(CPPDEFINES=[("WIFI_SECRETS_BYTES", byte_list)])
    print(f"WiFi credentials injected: {len(wifi_secrets.splitlines())} network(s)")