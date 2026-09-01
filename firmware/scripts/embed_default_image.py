"""Validate and embed assets/default.jpg into the firmware build directory."""

from pathlib import Path

Import("env")


MAX_BYTES = 262_144
EXPECTED_WIDTH = 386
EXPECTED_HEIGHT = 386


def inspect_baseline_jpeg(data: bytes) -> tuple[int, int, int]:
    if len(data) < 4 or data[:2] != b"\xff\xd8" or data[-2:] != b"\xff\xd9":
        raise RuntimeError("default.jpg is not a complete JPEG")

    offset = 2
    while offset + 1 < len(data):
        if data[offset] != 0xFF:
            raise RuntimeError("default.jpg contains an invalid JPEG marker")
        while offset < len(data) and data[offset] == 0xFF:
            offset += 1
        if offset >= len(data):
            break
        marker = data[offset]
        offset += 1
        if marker == 0xD9:
            break
        if marker == 0xDA:
            break
        if marker == 0x01 or 0xD0 <= marker <= 0xD7:
            continue
        if offset + 2 > len(data):
            raise RuntimeError("default.jpg has a truncated JPEG segment")
        segment_length = int.from_bytes(data[offset : offset + 2], "big")
        if segment_length < 2 or offset + segment_length > len(data):
            raise RuntimeError("default.jpg has an invalid JPEG segment length")
        if marker == 0xC0:
            if segment_length < 8 or data[offset + 2] != 8:
                raise RuntimeError("default.jpg must use 8-bit baseline JPEG")
            height = int.from_bytes(data[offset + 3 : offset + 5], "big")
            width = int.from_bytes(data[offset + 5 : offset + 7], "big")
            components = data[offset + 7]
            return width, height, components
        if 0xC1 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
            raise RuntimeError("default.jpg must not use progressive/non-baseline JPEG")
        offset += segment_length
    raise RuntimeError("default.jpg does not contain a baseline SOF0 marker")


project_dir = Path(env.subst("$PROJECT_DIR"))
asset_path = project_dir / "assets" / "default.jpg"
data = asset_path.read_bytes()
if not 0 < len(data) <= MAX_BYTES:
    raise RuntimeError(f"default.jpg must be 1..{MAX_BYTES} bytes")

width, height, components = inspect_baseline_jpeg(data)
if (width, height) != (EXPECTED_WIDTH, EXPECTED_HEIGHT):
    raise RuntimeError(
        f"default.jpg must be {EXPECTED_WIDTH}x{EXPECTED_HEIGHT}, got {width}x{height}"
    )
if components != 3:
    raise RuntimeError(f"default.jpg must have 3 components, got {components}")

generated_dir = Path(env.subst("$BUILD_DIR")) / "generated"
generated_dir.mkdir(parents=True, exist_ok=True)
header_path = generated_dir / "DefaultImageData.h"

rows = []
for offset in range(0, len(data), 16):
    chunk = data[offset : offset + 16]
    rows.append("  " + ", ".join(f"0x{byte:02X}" for byte in chunk) + ",")

content = "\n".join(
    [
        "#pragma once",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "inline constexpr uint8_t kPaperMonoDefaultImage[] = {",
        *rows,
        "};",
        "inline constexpr size_t kPaperMonoDefaultImageSize =",
        "  sizeof(kPaperMonoDefaultImage);",
        "",
    ]
)
if not header_path.exists() or header_path.read_text(encoding="utf-8") != content:
    header_path.write_text(content, encoding="utf-8")

env.Append(CPPPATH=[str(generated_dir)])
print(
    f"[default-image] embedded {asset_path.name}: "
    f"{width}x{height}, {len(data)} bytes, {components} components"
)
