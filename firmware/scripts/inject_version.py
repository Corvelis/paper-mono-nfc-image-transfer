"""Expose the repository release version to the firmware build."""

import re
from pathlib import Path

Import("env")


project_dir = Path(env.subst("$PROJECT_DIR"))
version_file = project_dir.parent / "VERSION"
version = version_file.read_text(encoding="utf-8").strip()

if not re.fullmatch(r"\d+\.\d+\.\d+", version):
    raise RuntimeError(f"VERSION must contain x.y.z, got {version!r}")

# The escaped quotes must survive SCons and the compiler command line so the
# macro expands to a C string literal.
env.Append(CPPDEFINES=[("PAPER_MONO_RELEASE_VERSION", f'\\"{version}\\"')])
print(f"[version] Paper Mono NFC Image Transfer {version}")
