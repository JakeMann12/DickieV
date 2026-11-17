"""PlatformIO pre-build script: inject WiFi credentials.

PlatformIO extra_scripts run in SCons context; use `Import('env')` to get the
build environment and `env.subst("$PROJECT_DIR")` for the project path.
"""
import os
import re
from pathlib import Path

Import("env")

PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))


# --- WiFi credentials -------------------------------------------------------
# Read from the WIFI_SECRETS environment variable (the GitHub Actions repo
# secret in CI; set it manually for local builds). Format: one network per
# line, "SSID|PASSWORD". Passed to the compiler as a comma-separated byte list
# (WIFI_SECRETS_BYTES) so no header file is written and no shell/SCons escaping
# of special characters in passwords is needed.
MAX_WIFI_NETWORKS = 8

WIFI_CREDENTIALS_HEADER = PROJECT_DIR / "include" / "wifi_credentials.h"


def get_wifi_credentials_from_header() -> str:
    """Parse SSID|PASSWORD pairs from include/wifi_credentials.h.

    Expected entry format (one per line):  {"SSID", "PASSWORD"},
    Returns the same "SSID|PASSWORD\\nSSID|PASSWORD" string format used by
    WIFI_SECRETS, or an empty string if the file is missing or has no entries.
    """
    if not WIFI_CREDENTIALS_HEADER.is_file():
        return ""
    entries = []
    text = WIFI_CREDENTIALS_HEADER.read_text(encoding="utf-8")
    pattern = re.compile(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"\s*\}')
    for match in pattern.finditer(text):
        ssid = match.group(1).strip()
        password = match.group(2).strip()
        if ssid and password:
            entries.append(f"{ssid}|{password}")
    if len(entries) > MAX_WIFI_NETWORKS:
        raise SystemExit(
            f"ERROR: wifi_credentials.h has more than {MAX_WIFI_NETWORKS} networks."
        )
    return "\n".join(entries)


def get_wifi_secrets() -> str:
    data = os.environ.get("WIFI_SECRETS", "").strip()
    if not data:
        header_data = get_wifi_credentials_from_header()
        if header_data:
            count = len(header_data.splitlines())
            print(
                f"INFO: WIFI_SECRETS not set; falling back to "
                f"include/wifi_credentials.h ({count} network(s))"
            )
            return header_data
        msg = "WIFI_SECRETS is empty or unset; firmware will have no WiFi networks."
        if os.environ.get("GITHUB_ACTIONS") == "true":
            raise SystemExit(f"ERROR: {msg} Add the secret in the repository settings.")
        print(
            f"WARNING: {msg} Set $env:WIFI_SECRETS = 'SSID|PASSWORD' for local builds, "
            f"or populate include/wifi_credentials.h."
        )
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