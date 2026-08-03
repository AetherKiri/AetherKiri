#!/usr/bin/env bash
set -euo pipefail

vcpkg_root="${1:?Usage: prepare-vcpkg-cache.sh <vcpkg-root>}"
binary_cache="${VCPKG_DEFAULT_BINARY_CACHE:-${HOME}/.cache/vcpkg/archives}"
max_cache_bytes="${VCPKG_CACHE_MAX_BYTES:-2000000000}"

if [[ ! -f "$vcpkg_root/.vcpkg-root" ]]; then
    echo "Error: vcpkg root is invalid: $vcpkg_root" >&2
    exit 1
fi

echo "vcpkg checkout before cleanup:"
du -sh "$vcpkg_root"

# Binary packages are persisted separately in ~/.cache/vcpkg/archives.
# These directories only contain rebuildable sources and intermediate files.
rm -rf \
    "$vcpkg_root/buildtrees" \
    "$vcpkg_root/downloads" \
    "$vcpkg_root/packages"

echo "vcpkg checkout after cleanup:"
du -sh "$vcpkg_root"

cache_size_kib="$({ du -sk "$vcpkg_root"; [[ ! -d "$binary_cache" ]] || du -sk "$binary_cache"; } | awk '{ total += $1 } END { print total }')"
cache_size_bytes=$((cache_size_kib * 1024))
echo "vcpkg cache input size: $cache_size_bytes bytes"
if ((cache_size_bytes > max_cache_bytes)); then
    echo "Error: vcpkg cache input exceeds the 2 GB limit." >&2
    exit 1
fi
