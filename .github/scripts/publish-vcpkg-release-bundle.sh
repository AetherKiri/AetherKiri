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
if [[ -z "${VCPKG_BUNDLE_PATH:-}" ]]; then
    echo "VCPKG_BUNDLE_PATH is required" >&2
    exit 1
fi
if [[ -z "${GITHUB_REPOSITORY:-}" ]]; then
    echo "GITHUB_REPOSITORY is required" >&2
    exit 1
fi

gh release view "$VCPKG_BUNDLE_TAG" --repo "$GITHUB_REPOSITORY" >/dev/null 2>&1 || \
gh release create "$VCPKG_BUNDLE_TAG" \
    --repo "$GITHUB_REPOSITORY" \
    --title "$VCPKG_BUNDLE_TAG" \
    --notes "Shared vcpkg release bundle" \
    --prerelease

gh release upload "$VCPKG_BUNDLE_TAG" \
    "$VCPKG_BUNDLE_PATH#$VCPKG_BUNDLE_ASSET" \
    --repo "$GITHUB_REPOSITORY" \
    --clobber
