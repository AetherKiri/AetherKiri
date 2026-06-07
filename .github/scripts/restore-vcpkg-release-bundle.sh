#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${VCPKG_BUNDLE_TAG:-}" ]]; then
    echo "VCPKG_BUNDLE_TAG is required" >&2
    exit 1
fi
if [[ -z "${VCPKG_BUNDLE_ASSET:-}" ]]; then
    echo "VCPKG_BUNDLE_ASSET is required" >&2
    exit 1
fi
if [[ -z "${GITHUB_REPOSITORY:-}" ]]; then
    echo "GITHUB_REPOSITORY is required" >&2
    exit 1
fi

download_dir="${RUNNER_TEMP:-/tmp}/vcpkg-release-bundle"
extract_dir="${download_dir}/extracted"
rm -rf "$download_dir"
mkdir -p "$download_dir" "$extract_dir" "$HOME/.cache/vcpkg"

if gh release download "$VCPKG_BUNDLE_TAG" \
    --repo "$GITHUB_REPOSITORY" \
    --pattern "$VCPKG_BUNDLE_ASSET" \
    --dir "$download_dir"; then
    tar --zstd -xf "$download_dir/$VCPKG_BUNDLE_ASSET" -C "$extract_dir"
    cp -a "$extract_dir/workspace/." "$GITHUB_WORKSPACE/"
    if [[ -d "$extract_dir/home-cache" ]]; then
        cp -a "$extract_dir/home-cache/." "$HOME/.cache/"
    fi
    {
        echo "VCPKG_BUNDLE_RESTORED=1"
        echo "VCPKG_ROOT=$GITHUB_WORKSPACE/.devtools/vcpkg"
        echo "SKIP_VCPKG_INSTALL=1"
        echo "SKIP_ANDROID_VCPKG_INSTALL=1"
    } >> "$GITHUB_ENV"
    if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
        echo "restored=1" >> "$GITHUB_OUTPUT"
    fi
    echo "Restored vcpkg release bundle: $VCPKG_BUNDLE_ASSET"
else
    echo "VCPKG_BUNDLE_RESTORED=0" >> "$GITHUB_ENV"
    if [[ -n "${GITHUB_OUTPUT:-}" ]]; then
        echo "restored=0" >> "$GITHUB_OUTPUT"
    fi
    echo "No vcpkg release bundle found for $VCPKG_BUNDLE_TAG/$VCPKG_BUNDLE_ASSET"
fi
