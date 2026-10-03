#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fixture_root="${AETHERKIRI_RENPY_FIXTURE:-$repo_root/tests/fixtures/renpy_smoke}"
renpy_sdk="${RENPY_SDK:-}"
renpy_bin="${RENPY_BIN:-}"

usage() {
    cat <<'USAGE'
Usage: tools/run_renpy_smoke.sh [--help]

Compile and initialize the source-only Ren'Py smoke fixture with an official
Ren'Py SDK. Set RENPY_SDK to the extracted SDK directory, or RENPY_BIN to its
launcher executable (renpy.sh on Linux, renpy on macOS).

Environment:
  RENPY_SDK                 Official SDK directory containing renpy.sh/renpy
  RENPY_BIN                 SDK launcher executable (overrides RENPY_SDK)
  AETHERKIRI_RENPY_FIXTURE   Fixture directory (defaults to the repository one)
USAGE
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    usage
    exit 0
fi
if [[ $# -ne 0 ]]; then
    printf 'unexpected argument: %s\n' "$1" >&2
    usage >&2
    exit 2
fi

if [[ ! -d "$fixture_root/game" ]]; then
    printf "Ren'Py fixture is missing game/: %s\n" "$fixture_root" >&2
    exit 1
fi
for source_file in options.rpy script.rpy; do
    if [[ ! -f "$fixture_root/game/$source_file" ]]; then
        printf "Ren'Py fixture is missing game/%s\n" "$source_file" >&2
        exit 1
    fi
done

if [[ -z "$renpy_bin" && -n "$renpy_sdk" ]]; then
    if [[ -x "$renpy_sdk/renpy.sh" ]]; then
        renpy_bin="$renpy_sdk/renpy.sh"
    elif [[ -x "$renpy_sdk/renpy" ]]; then
        renpy_bin="$renpy_sdk/renpy"
    elif [[ -x "$renpy_sdk/renpy.exe" ]]; then
        renpy_bin="$renpy_sdk/renpy.exe"
    else
        printf "Ren'Py launcher not found under RENPY_SDK: %s\n" "$renpy_sdk" >&2
        exit 1
    fi
fi
if [[ -z "$renpy_bin" ]]; then
    if command -v renpy.sh >/dev/null 2>&1; then
        renpy_bin="$(command -v renpy.sh)"
    elif command -v renpy >/dev/null 2>&1; then
        renpy_bin="$(command -v renpy)"
    else
        printf "Set RENPY_SDK to an extracted official Ren'Py SDK or RENPY_BIN to its launcher.\n" >&2
        exit 1
    fi
fi
if [[ ! -x "$renpy_bin" ]]; then
    printf "Ren'Py launcher is not executable: %s\n" "$renpy_bin" >&2
    exit 1
fi

sdk_root="$(cd "$(dirname "$renpy_bin")" && pwd)"
renpy_bin="$sdk_root/$(basename "$renpy_bin")"
tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/aetherkiri-renpy-smoke.XXXXXX")"
cleanup() {
    rm -rf "$tmp_root"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
sandbox="$tmp_root/fixture"
mkdir -p "$sandbox"
cp -a "$fixture_root/." "$sandbox/"
mkdir -p "$tmp_root/home" "$tmp_root/config"

# --compile forces the SDK to parse the source even when stale bytecode exists;
# quit initializes the project without leaving a game window running.
(
    cd "$sdk_root"
    HOME="$tmp_root/home" XDG_CONFIG_HOME="$tmp_root/config" \
        "$renpy_bin" --compile --savedir "$tmp_root/saves" "$sandbox" quit
)

printf "Ren'Py smoke ok: fixture=%s sdk=%s\n" "$fixture_root" "$sdk_root"
