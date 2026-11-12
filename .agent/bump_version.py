"""Agent tool to bump FIRMWARE_VERSION in src/main.cpp.

Usage:
    python .agent/bump_version.py <major|minor|patch> [--new <x.y.z>]

Examples:
    python .agent/bump_version.py patch   # 1.0.0 -> 1.0.1
    python .agent/bump_version.py minor   # 1.0.0 -> 1.1.0
    python .agent/bump_version.py major   # 1.0.0 -> 2.0.0
    python .agent/bump_version.py --new 1.2.3   # explicit version
"""
import re
import sys
from pathlib import Path

SRC_MAIN = Path(__file__).resolve().parents[1] / "src" / "main.cpp"

VERSION_RE = re.compile(r'const char\* FIRMWARE_VERSION = "(\d+)\.(\d+)\.(\d+)";')


def read_version() -> tuple[int, int, int]:
    text = SRC_MAIN.read_text()
    m = VERSION_RE.search(text)
    if not m:
        print("ERROR: Could not find FIRMWARE_VERSION in src/main.cpp")
        sys.exit(1)
    return int(m.group(1)), int(m.group(2)), int(m.group(3))


def write_version(major: int, minor: int, patch: int):
    text = SRC_MAIN.read_text()
    new_line = f'const char* FIRMWARE_VERSION = "{major}.{minor}.{patch}";'
    text = VERSION_RE.sub(new_line, text)
    SRC_MAIN.write_text(text)
    print(f"FIRMWARE_VERSION updated: {major}.{minor}.{patch}")


def main():
    if len(sys.argv) < 2:
        print("Usage: python bump_version.py <major|minor|patch> [--new <x.y.z>]")
        sys.exit(1)

    mode = sys.argv[1]
    major, minor, patch = read_version()
    print(f"Current version: {major}.{minor}.{patch}")

    if mode == "--new" and len(sys.argv) >= 3:
        new_ver = sys.argv[2]
        m = re.match(r"^(\d+)\.(\d+)\.(\d+)$", new_ver)
        if not m:
            print(f"ERROR: Invalid version format '{new_ver}', expected x.y.z")
            sys.exit(1)
        major, minor, patch = int(m.group(1)), int(m.group(2)), int(m.group(3))
    elif mode == "major":
        major += 1
        minor = 0
        patch = 0
    elif mode == "minor":
        minor += 1
        patch = 0
    elif mode == "patch":
        patch += 1
    else:
        print(f"ERROR: Unknown mode '{mode}'. Use major, minor, patch, or --new x.y.z")
        sys.exit(1)

    write_version(major, minor, patch)
    print(f"New version: {major}.{minor}.{patch}")
    print("Next: commit the change, create a git tag (e.g. git tag v1.0.1), and rebuild.")


if __name__ == "__main__":
    main()