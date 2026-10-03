#!/usr/bin/env bash
# Metadata/source-only test for the host-owned Ren'Py launcher scaffold.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scaffold="$repo_root/bridge/renpy_runtime/mobile_launcher"
script="$scaffold/build.sh"
header="$scaffold/include/renpy_mobile_launcher.h"
loader="$scaffold/src/renpy_mobile_loader.cpp"
renpy_build="${RENPY_BUILD_TEST_ROOT:-/tmp/renpy-build-src}"
renpy_source="${RENPY_SOURCE_TEST_ROOT:-/tmp/renpy-src}"

[[ -x "$script" ]] || { echo "missing launcher scaffold script" >&2; exit 1; }
[[ -f "$header" ]] || { echo "missing launcher lifecycle header" >&2; exit 1; }
[[ -f "$loader" ]] || { echo "missing host lifecycle loader" >&2; exit 1; }
bash -n "$script"

grep -Fq 'librenpython.so' "$loader"
grep -Fq 'weak_import' "$loader"
for symbol in \
    renpy_mobile_init renpy_mobile_tick renpy_mobile_frame renpy_mobile_input \
    renpy_mobile_pause renpy_mobile_resume renpy_mobile_shutdown; do
    grep -Fq "$symbol" "$loader"
done

for symbol in \
    renpy_mobile_init renpy_mobile_tick renpy_mobile_frame renpy_mobile_input \
    renpy_mobile_pause renpy_mobile_resume renpy_mobile_shutdown; do
    grep -Fq "$symbol" "$header"
done

grep -Fq 'Py_RunMain' "$scaffold/README.md"
grep -Fq 'SDL_main' "$scaffold/README.md"
grep -Fq 'renpy-build' "$scaffold/patches/README.md"
grep -Fq 'cooperative-loop-skeleton.patch' "$scaffold/patches/python/README.md"
if [[ -d "$renpy_source" ]]; then
    patch_output="$($script --check-python-patch --renpy-src "$renpy_source")"
    grep -Fq 'applies cleanly' <<<"$patch_output"
fi
for template in \
    "$scaffold/patches/android/librenpython_android_host.c.template" \
    "$scaffold/patches/ios/librenpython_ios_host.c.template"; do
    [[ -f "$template" ]] || { echo "missing native template: $template" >&2; exit 1; }
    for symbol in \
        renpy_mobile_init renpy_mobile_tick renpy_mobile_frame renpy_mobile_input \
        renpy_mobile_pause renpy_mobile_resume renpy_mobile_shutdown; do
        grep -Fq "$symbol" "$template"
    done
    # Templates must not accidentally reintroduce process entrypoints.
    ! grep -Eq '(^|[^[:alnum:]_])(Py_RunMain|SDL_main|SDL_RunApp|SDL_UIKitRunApp|UIApplicationMain)[[:space:]]*\(' "$template"
    grep -Fq 'AETHERKIRI_RENPY_LIFECYCLE_STUB' "$template"
done

if [[ ! -d "$renpy_build" ]]; then
    echo "Ren'Py build checkout not present; metadata-only scaffold test ok"
    exit 0
fi

output="$($script --check --renpy-build "$renpy_build")"
grep -Fq 'source check ok' <<<"$output"
grep -Fq 'no artifact was built or installed' <<<"$output"

contract_dir="$(mktemp -d "${TMPDIR:-/tmp}/renpy-mobile-contract.XXXXXX")"
trap 'rm -rf "$contract_dir"' EXIT
$script --compile-contract --renpy-build "$renpy_build" --output-dir "$contract_dir" >/dev/null
for object in "$contract_dir"/*.o; do
    for symbol in \
        renpy_mobile_init renpy_mobile_tick renpy_mobile_frame renpy_mobile_input \
        renpy_mobile_pause renpy_mobile_resume renpy_mobile_shutdown; do
        nm -g --defined-only "$object" | awk '{print $3}' | grep -Fxq "$symbol"
    done
done
if "$script" --install --renpy-build "$renpy_build" --stage "$contract_dir/stage" \
        --android-so-arm64 "$contract_dir/librenpython_android_host.o" \
        >"$contract_dir/install.stdout" 2>"$contract_dir/install.stderr"; then
    echo "contract-only lifecycle object was accepted as a runtime artifact" >&2
    exit 1
fi
grep -Fq 'contract-only lifecycle stub' "$contract_dir/install.stderr"

if "$script" --check --renpy-build "$renpy_build" | grep -Fq 'SDL_main'; then
    :
else
    echo "source check did not report the blocking Android launcher" >&2
    exit 1
fi

echo "Ren'Py mobile launcher scaffold/source smoke ok"
bash "$repo_root/tools/test_renpy_mobile_launcher_loader.sh"
