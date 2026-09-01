#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
version="$(tr -d '[:space:]' < "$repo_root/VERSION")"

if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "VERSION must contain x.y.z; got: $version" >&2
  exit 1
fi

mobile_version="$(awk '$1 == "version:" { print $2; exit }' "$repo_root/mobile/pubspec.yaml")"
mobile_name="${mobile_version%%+*}"
if [[ "$mobile_name" != "$version" ]]; then
  echo "VERSION ($version) and mobile/pubspec.yaml ($mobile_name) do not match" >&2
  exit 1
fi

if [[ $# -gt 0 ]]; then
  requested="${1#v}"
  if [[ "$requested" != "$version" ]]; then
    echo "Requested version ($requested) does not match VERSION ($version)" >&2
    exit 1
  fi
fi

printf '%s\n' "$version"
