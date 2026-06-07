#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${VCPKG_BUNDLE_ASSET:-}" ]]; then
    echo "VCPKG_BUNDLE_ASSET is required" >&2
    exit 1
fi
if [[ -z "${VCPKG_TRIPLET:-}" ]]; then
    echo "VCPKG_TRIPLET is required" >&2
    exit 1
fi
if [[ -z "${VCPKG_ROOT:-}" ]]; then
    echo "VCPKG_ROOT is required" >&2
    exit 1
fi
if [[ ! -d "$VCPKG_ROOT/installed/$VCPKG_TRIPLET" ]]; then
    echo "Missing installed vcpkg triplet: $VCPKG_ROOT/installed/$VCPKG_TRIPLET" >&2
    exit 1
fi
if [[ "${VCPKG_BUNDLE_REQUIRE_DEBUG:-1}" != "0" && ! -d "$VCPKG_ROOT/installed/$VCPKG_TRIPLET/debug" ]]; then
    echo "Missing debug vcpkg dependencies for triplet: $VCPKG_ROOT/installed/$VCPKG_TRIPLET/debug" >&2
    echo "The bundle must contain both release dependencies and debug dependencies." >&2
    exit 1
fi

payload_dir="${RUNNER_TEMP:-/tmp}/vcpkg-bundle-payload"
bundle_root="$payload_dir/workspace/.devtools/vcpkg"
rm -rf "$payload_dir"
mkdir -p "$(dirname "$bundle_root")"

cp -a "$VCPKG_ROOT" "$bundle_root"
rm -rf \
    "$bundle_root/buildtrees" \
    "$bundle_root/downloads" \
    "$bundle_root/packages"

if [[ -d "$bundle_root/.git" ]]; then
    git -C "$bundle_root" gc --prune=now >/dev/null 2>&1 || true
fi

installed_tmp="${RUNNER_TEMP:-/tmp}/vcpkg-installed-selected"
rm -rf "$installed_tmp"
mkdir -p "$installed_tmp"
cp -a "$VCPKG_ROOT/installed/$VCPKG_TRIPLET" "$installed_tmp/"
if [[ -d "$VCPKG_ROOT/installed/vcpkg" ]]; then
    cp -a "$VCPKG_ROOT/installed/vcpkg" "$installed_tmp/"
fi
rm -rf "$bundle_root/installed"
mv "$installed_tmp" "$bundle_root/installed"

bundle_path="${RUNNER_TEMP:-/tmp}/$VCPKG_BUNDLE_ASSET"
tar --zstd -cf "$bundle_path" -C "$payload_dir" workspace

if stat -c%s "$bundle_path" >/dev/null 2>&1; then
    bundle_size_bytes="$(stat -c%s "$bundle_path")"
else
    bundle_size_bytes="$(stat -f%z "$bundle_path")"
fi
bundle_size_mb="$((bundle_size_bytes / 1024 / 1024))"
echo "Bundle size: ${bundle_size_mb} MB"
if (( bundle_size_bytes >= 2147483648 )); then
    echo "Bundled asset exceeds GitHub's 2 GB release upload limit." >&2
    exit 1
fi

echo "VCPKG_BUNDLE_PATH=$bundle_path" >> "$GITHUB_ENV"
echo "Packed vcpkg release bundle: $bundle_path"
