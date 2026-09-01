#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
version="${1:-$(tr -d '[:space:]' < "$repo_root/VERSION")}"
version="${version#v}"
release_dir="${2:-$repo_root/dist/v$version}"
pio_cmd="${PLATFORMIO_CMD:-pio}"

bash "$script_dir/check_version.sh" "$version" >/dev/null
mkdir -p "$release_dir"

"$pio_cmd" run -e paper-mono --project-dir "$repo_root/firmware"

pio_info="$("$pio_cmd" system info --json-output)"
core_dir="$(printf '%s' "$pio_info" | python3 -c 'import json,sys; print(json.load(sys.stdin)["core_dir"]["value"])')"
pio_python="$(printf '%s' "$pio_info" | python3 -c 'import json,sys; print(json.load(sys.stdin)["python_exe"]["value"])')"

build_dir="$repo_root/firmware/.pio/build/paper-mono"
framework_dir="$core_dir/packages/framework-arduinoespressif32"
esptool_py="$core_dir/packages/tool-esptoolpy/esptool.py"
full_bin="$release_dir/paper-mono-nfc-image-transfer-v$version-full.bin"
app_bin="$release_dir/paper-mono-nfc-image-transfer-v$version-app.bin"
components_zip="$release_dir/paper-mono-nfc-image-transfer-v$version-firmware-components.zip"

required_files=(
  "$build_dir/bootloader.bin"
  "$build_dir/partitions.bin"
  "$framework_dir/tools/partitions/boot_app0.bin"
  "$build_dir/firmware.bin"
  "$esptool_py"
)
for required_file in "${required_files[@]}"; do
  if [[ ! -f "$required_file" ]]; then
    echo "Required build file not found: $required_file" >&2
    exit 1
  fi
done

"$pio_python" "$esptool_py" --chip esp32s3 merge_bin \
  --output "$full_bin" \
  --flash_mode qio \
  --flash_freq 80m \
  --flash_size 16MB \
  0x0000 "$build_dir/bootloader.bin" \
  0x8000 "$build_dir/partitions.bin" \
  0xe000 "$framework_dir/tools/partitions/boot_app0.bin" \
  0x10000 "$build_dir/firmware.bin"

verify_segment() {
  local offset="$1"
  local source_file="$2"
  local size
  size="$(wc -c < "$source_file" | tr -d '[:space:]')"
  dd if="$full_bin" bs=1 skip="$((offset))" count="$size" 2>/dev/null | cmp - "$source_file"
}

# merge_bin rewrites the bootloader flash-parameter bytes and its trailing SHA.
# Compare the immutable payload between them, then let image_info validate the
# rewritten checksum and hash.
bootloader_size="$(wc -c < "$build_dir/bootloader.bin" | tr -d '[:space:]')"
if (( bootloader_size <= 36 )); then
  echo "Bootloader is unexpectedly small: $bootloader_size bytes" >&2
  exit 1
fi
dd if="$full_bin" bs=1 skip=4 count="$((bootloader_size - 36))" 2>/dev/null \
  | cmp - <(dd if="$build_dir/bootloader.bin" bs=1 skip=4 count="$((bootloader_size - 36))" 2>/dev/null)
verify_segment 0x8000 "$build_dir/partitions.bin"
verify_segment 0xe000 "$framework_dir/tools/partitions/boot_app0.bin"
verify_segment 0x10000 "$build_dir/firmware.bin"
"$pio_python" "$esptool_py" --chip esp32s3 image_info "$full_bin" >/dev/null
cp "$build_dir/firmware.bin" "$app_bin"
cmp "$app_bin" "$build_dir/firmware.bin"

staging_dir="$(mktemp -d)"
trap 'rm -rf "$staging_dir"' EXIT
mkdir -p "$staging_dir/firmware-components"
cp "$build_dir/bootloader.bin" "$staging_dir/firmware-components/bootloader.bin"
cp "$build_dir/partitions.bin" "$staging_dir/firmware-components/partitions.bin"
cp "$framework_dir/tools/partitions/boot_app0.bin" "$staging_dir/firmware-components/boot_app0.bin"
cp "$build_dir/firmware.bin" "$staging_dir/firmware-components/firmware.bin"
cp "$repo_root/firmware/partitions_16mb_ota_4m_littlefs_7m.csv" \
  "$staging_dir/firmware-components/partitions_16mb_ota_4m_littlefs_7m.csv"

cat > "$staging_dir/firmware-components/flash-args.txt" <<EOF
Chip: ESP32-S3
Flash mode: qio
Flash frequency: 80m
Flash size: 16MB

0x0000  bootloader.bin
0x8000  partitions.bin
0xe000  boot_app0.bin
0x10000 firmware.bin
EOF

rm -f "$components_zip"
(
  cd "$staging_dir"
  zip -q -r "$components_zip" firmware-components
)

printf 'Created %s\n' "$full_bin"
printf 'Created %s\n' "$app_bin"
printf 'Created %s\n' "$components_zip"
