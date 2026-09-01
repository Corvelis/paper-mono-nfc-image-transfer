#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
version="${1:-$(tr -d '[:space:]' < "$repo_root/VERSION")}"
version="${version#v}"
build_number="${2:-1}"
release_dir="${3:-$repo_root/dist/v$version}"
flutter_cmd="${FLUTTER_CMD:-flutter}"

bash "$script_dir/check_version.sh" "$version" >/dev/null
if [[ ! "$build_number" =~ ^[1-9][0-9]*$ ]]; then
  echo "Android build number must be a positive integer; got: $build_number" >&2
  exit 1
fi

required_env=(
  ANDROID_KEYSTORE_PATH
  ANDROID_KEYSTORE_PASSWORD
  ANDROID_KEY_ALIAS
  ANDROID_KEY_PASSWORD
)
for variable in "${required_env[@]}"; do
  if [[ -z "${!variable:-}" ]]; then
    echo "Required Android signing variable is missing: $variable" >&2
    exit 1
  fi
done
if [[ ! -f "$ANDROID_KEYSTORE_PATH" ]]; then
  echo "Android keystore not found: $ANDROID_KEYSTORE_PATH" >&2
  exit 1
fi

mkdir -p "$release_dir"
(
  cd "$repo_root/mobile"
  "$flutter_cmd" pub get
  "$flutter_cmd" build apk --release \
    --build-name "$version" --build-number "$build_number"
  "$flutter_cmd" build appbundle --release \
    --build-name "$version" --build-number "$build_number"
)

apk_source="$repo_root/mobile/build/app/outputs/flutter-apk/app-release.apk"
aab_source="$repo_root/mobile/build/app/outputs/bundle/release/app-release.aab"
if [[ ! -f "$apk_source" || ! -f "$aab_source" ]]; then
  echo "Flutter did not produce the expected signed Android outputs" >&2
  exit 1
fi

apk_output="$release_dir/paper-mono-image-sender-v$version-android.apk"
aab_output="$release_dir/paper-mono-image-sender-v$version-android.aab"
certificate_output="$release_dir/paper-mono-image-sender-v$version-android-signing-certificate.txt"
cp "$apk_source" "$apk_output"
cp "$aab_source" "$aab_output"

android_sdk_root="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
if [[ -z "$android_sdk_root" && -f "$repo_root/mobile/android/local.properties" ]]; then
  android_sdk_root="$(sed -n 's/^sdk.dir=//p' "$repo_root/mobile/android/local.properties" | head -1)"
fi
if [[ -z "$android_sdk_root" || ! -d "$android_sdk_root/build-tools" ]]; then
  echo "Android SDK build-tools could not be located for signature verification" >&2
  exit 1
fi
apksigner_path="$(find "$android_sdk_root/build-tools" -type f -name apksigner | LC_ALL=C sort | tail -1)"
if [[ -z "$apksigner_path" ]]; then
  echo "apksigner was not found below $android_sdk_root/build-tools" >&2
  exit 1
fi

"$apksigner_path" verify --verbose --print-certs "$apk_output" \
  | tee "$certificate_output"
jarsigner -verify "$aab_output" >/dev/null

printf 'Created signed Android APK and AAB in %s\n' "$release_dir"
