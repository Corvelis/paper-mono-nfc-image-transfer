#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
version="${1:-$(tr -d '[:space:]' < "$repo_root/VERSION")}"
version="${version#v}"
commit="${2:-$(git -C "$repo_root" rev-parse HEAD)}"
release_dir="${3:-$repo_root/dist/v$version}"

bash "$script_dir/check_version.sh" "$version" >/dev/null
mkdir -p "$release_dir"

required_assets=(
  "$release_dir/paper-mono-nfc-image-transfer-v$version-full.bin"
  "$release_dir/paper-mono-nfc-image-transfer-v$version-app.bin"
  "$release_dir/paper-mono-nfc-image-transfer-v$version-firmware-components.zip"
  "$release_dir/paper-mono-image-sender-v$version-android.apk"
  "$release_dir/paper-mono-image-sender-v$version-android.aab"
  "$release_dir/paper-mono-image-sender-v$version-android-signing-certificate.txt"
)
for required_asset in "${required_assets[@]}"; do
  if [[ ! -s "$required_asset" ]]; then
    echo "Release asset is missing or empty: $required_asset" >&2
    exit 1
  fi
done

staging_dir="$(mktemp -d)"
trap 'rm -rf "$staging_dir"' EXIT
notices_dir="$staging_dir/notices"
mkdir -p "$notices_dir/firmware/assets" \
  "$notices_dir/firmware-dependency-licenses" "$notices_dir/docs" \
  "$notices_dir/protocol"
cp "$repo_root/LICENSE" "$notices_dir/LICENSE"
cp "$repo_root/THIRD_PARTY_NOTICES.md" "$notices_dir/THIRD_PARTY_NOTICES.md"
cp "$repo_root/CONTRIBUTING.md" "$notices_dir/CONTRIBUTING.md"
cp "$repo_root/README.md" "$notices_dir/README.md"
cp "$repo_root/README.en.md" "$notices_dir/README.en.md"
cp "$repo_root/firmware/assets/LICENSE.md" "$notices_dir/firmware/assets/LICENSE.md"
cp "$repo_root"/docs/*.md "$notices_dir/docs/"
cp "$repo_root/protocol/protocol_v1.md" "$notices_dir/protocol/protocol_v1.md"
cp "$repo_root/protocol/test_vectors.json" "$notices_dir/protocol/test_vectors.json"

dependency_root="$repo_root/firmware/.pio/libdeps/paper-mono"
if [[ ! -d "$dependency_root" ]]; then
  echo "PlatformIO dependency directory is missing: $dependency_root" >&2
  exit 1
fi
while IFS= read -r -d '' license_file; do
  relative_path="${license_file#"$dependency_root/"}"
  destination="$notices_dir/firmware-dependency-licenses/$relative_path"
  mkdir -p "$(dirname "$destination")"
  cp "$license_file" "$destination"
done < <(
  find "$dependency_root" -type f \
    \( -iname 'LICENSE*' -o -iname 'NOTICE*' -o -iname 'COPYING*' \) -print0
)

notices_zip="$release_dir/paper-mono-nfc-image-transfer-v$version-notices.zip"
rm -f "$notices_zip"
(
  cd "$staging_dir"
  zip -q -r "$notices_zip" notices
)

generated_at="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
python3 - "$release_dir/release-manifest.json" "$version" "$commit" "$generated_at" <<'PY'
import json
import sys

path, version, commit, generated_at = sys.argv[1:]
manifest = {
    "schema": 1,
    "product": "Paper Mono NFC Image Transfer",
    "version": version,
    "gitCommit": commit,
    "generatedAt": generated_at,
    "firmwareTarget": "M5Stack Paper Mono C153 (ESP32-S3, 16 MB flash)",
    "firmwareImages": {
        "full": {"offset": "0x0000", "purpose": "first or clean installation"},
        "app": {"offset": "0x10000", "purpose": "data-preserving compatible update"},
    },
    "mobile": {
        "androidApplicationId": "io.github.corvelis.paper_mono_image_sender",
        "minimumAndroidApi": 24,
        "signingCertificate": "paper-mono-image-sender-v%s-android-signing-certificate.txt" % version,
        "iosDistribution": "TestFlight or App Store; no unsigned IPA is published",
    },
    "protocolVersion": 1,
}
with open(path, "w", encoding="utf-8") as output:
    json.dump(manifest, output, ensure_ascii=False, indent=2)
    output.write("\n")
PY

checksums="$release_dir/SHA256SUMS"
: > "$checksums"
while IFS= read -r asset; do
  name="$(basename "$asset")"
  if command -v sha256sum >/dev/null 2>&1; then
    digest="$(sha256sum "$asset" | awk '{print $1}')"
  else
    digest="$(shasum -a 256 "$asset" | awk '{print $1}')"
  fi
  printf '%s  %s\n' "$digest" "$name" >> "$checksums"
done < <(find "$release_dir" -maxdepth 1 -type f ! -name SHA256SUMS | LC_ALL=C sort)

printf 'Finalized release assets in %s\n' "$release_dir"
