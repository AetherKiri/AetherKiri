#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
installer="$repo_root/tools/install_renpy_mobile_support.sh"
[[ -f "$installer" ]] || { echo "mobile installer is missing: $installer" >&2; exit 1; }
bash -n "$installer"

config="$(bash "$installer" --platform both --print-config)"
grep -Fx 'RENPY_VERSION=8.5.3' <<<"$config"
grep -Fx 'RENPY_RAPT_ARCHIVE=renpy-8.5.3-rapt.zip' <<<"$config"
grep -Fx 'RENPY_RAPT_SHA256=8a12be34a2f5238d125ff6dd76a56772fd8e44838af864f81bca82c1059b00e6' <<<"$config"
grep -Fx 'RENPY_RENIOS_ARCHIVE=renpy-8.5.3-renios.zip' <<<"$config"
grep -Fx 'RENPY_RENIOS_SHA256=c4fae153e8276ed0faed5e84ea3e0b7c4bf337f0e3208e9130c6a41748a83b2b' <<<"$config"

if bash "$installer" --platform windows --print-config >/dev/null 2>&1; then
    echo "installer accepted unsupported Windows platform" >&2
    exit 1
fi

# A real archive check is opt-in for CI/local runs that have downloaded the
# official packages. It avoids making a metadata-only unit test depend on a
# 200+ MiB network transfer.
cache_dir="${RENPY_MOBILE_TEST_CACHE_DIR:-}"
if [[ -n "$cache_dir" ]]; then
    destination="$(mktemp -d "${TMPDIR:-/tmp}/aetherkiri-renpy-mobile-test.XXXXXX")"
    trap 'rm -rf "$destination"' EXIT
    bash "$installer" --platform both --cache-dir "$cache_dir" --destination "$destination" >/dev/null
    [[ -s "$destination/rapt/.aetherkiri-sha256" ]]
    [[ -s "$destination/renios/.aetherkiri-sha256" ]]
    [[ -f "$destination/rapt/prototype/renpyandroid/src/main/jniLibs/arm64-v8a/librenpython.so" ]]
    [[ -f "$destination/renios/prototype/prebuilt/release/librenpython.a" ]]
    grep -F '8a12be34a2f5238d125ff6dd76a56772fd8e44838af864f81bca82c1059b00e6' \
        "$destination/rapt/.aetherkiri-sha256"
    grep -F 'c4fae153e8276ed0faed5e84ea3e0b7c4bf337f0e3208e9130c6a41748a83b2b' \
        "$destination/renios/.aetherkiri-sha256"
    echo "Ren'Py mobile archive staging validation ok"
else
    echo "Ren'Py mobile installer metadata validation ok (archive staging not requested)"
fi
