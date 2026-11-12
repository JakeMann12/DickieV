"""PlatformIO pre-build script: inject BUILD_NUMBER from git into firmware.

Runs `git rev-list --count HEAD` to get the total commit count and writes it
to src/build_version.h as #define BUILD_NUMBER <n>. This value is compiled
into the firmware and used on the LCD splash to show v#.#.# b##.

PlatformIO extra_scripts run in SCons context; use `Import('env')` to get the
build environment and `env.subst("$PROJECT_DIR")` for the project path.
"""
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