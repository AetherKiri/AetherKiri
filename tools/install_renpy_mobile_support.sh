#!/usr/bin/env bash
# Fetch and stage official Ren'Py mobile support archives. This prepares
# toolchain inputs only; it does not make the desktop provider mobile-capable.
set -euo pipefail

readonly RENPY_VERSION="8.5.3"
readonly RENPY_DOWNLOAD_ROOT="https://www.renpy.org/dl/$RENPY_VERSION"
readonly RENPY_RAPT_ARCHIVE="renpy-$RENPY_VERSION-rapt.zip"
readonly RENPY_RAPT_SHA256="8a12be34a2f5238d125ff6dd76a56772fd8e44838af864f81bca82c1059b00e6"
readonly RENPY_RENIOS_ARCHIVE="renpy-$RENPY_VERSION-renios.zip"
readonly RENPY_RENIOS_SHA256="c4fae153e8276ed0faed5e84ea3e0b7c4bf337f0e3208e9130c6a41748a83b2b"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
destination="${AETHERKIRI_RENPY_MOBILE_ROOT:-$repo_root/.aetherkiri-cache/renpy-mobile/$RENPY_VERSION}"
cache_dir="${AETHERKIRI_RENPY_MOBILE_CACHE_DIR:-$repo_root/.aetherkiri-cache/renpy-mobile/downloads}"
platform="both"
print_config=false

usage() {
    cat <<'USAGE'
Usage: tools/install_renpy_mobile_support.sh [options]

Download and stage the pinned official Ren'Py 8.5.3 mobile support packages.
The output contains rapt/ and/or renios/. It is not a playable mobile runtime.

Options:
  --platform PLATFORM   android, ios, or both (default: both)
  --destination PATH    support root to create
  --cache-dir PATH      archive download cache
  --print-config        print archive metadata and exit
  -h, --help            show this help
USAGE
}

while (($#)); do
    case "$1" in
        --platform) (($# >= 2)) || { echo "--platform requires a value" >&2; exit 2; }; platform="$2"; shift 2 ;;
        --destination) (($# >= 2)) || { echo "--destination requires a value" >&2; exit 2; }; destination="$2"; shift 2 ;;
        --cache-dir) (($# >= 2)) || { echo "--cache-dir requires a value" >&2; exit 2; }; cache_dir="$2"; shift 2 ;;
        --print-config) print_config=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

case "$platform" in
    android|ios|both) ;;
    *) echo "platform must be android, ios, or both (got '$platform')" >&2; exit 2 ;;
esac

if [[ "$print_config" == true ]]; then
    printf 'RENPY_VERSION=%s\n' "$RENPY_VERSION"
    printf 'RENPY_PLATFORM=%s\n' "$platform"
    printf 'RENPY_RAPT_ARCHIVE=%s\n' "$RENPY_RAPT_ARCHIVE"
    printf 'RENPY_RAPT_SHA256=%s\n' "$RENPY_RAPT_SHA256"
    printf 'RENPY_RAPT_URL=%s/%s\n' "$RENPY_DOWNLOAD_ROOT" "$RENPY_RAPT_ARCHIVE"
    printf 'RENPY_RENIOS_ARCHIVE=%s\n' "$RENPY_RENIOS_ARCHIVE"
    printf 'RENPY_RENIOS_SHA256=%s\n' "$RENPY_RENIOS_SHA256"
    printf 'RENPY_RENIOS_URL=%s/%s\n' "$RENPY_DOWNLOAD_ROOT" "$RENPY_RENIOS_ARCHIVE"
    printf 'RENPY_MOBILE_ROOT=%s\n' "$destination"
    exit 0
fi

for command in curl unzip; do
    command -v "$command" >/dev/null 2>&1 || { echo "required command not found: $command" >&2; exit 1; }
done
if ! command -v sha256sum >/dev/null 2>&1 && ! command -v shasum >/dev/null 2>&1; then
    echo "required command not found: sha256sum or shasum" >&2
    exit 1
fi
mkdir -p "$cache_dir" "$(dirname "$destination")"

verify_archive() {
    local archive_path="$1" expected="$2"
    [[ -f "$archive_path" ]] || return 1
    if command -v sha256sum >/dev/null 2>&1; then
        printf '%s  %s\n' "$expected" "$archive_path" | sha256sum --check --status -
    else
        [[ "$(shasum -a 256 "$archive_path" | awk '{ print $1 }')" == "$expected" ]]
    fi
}

download_archive() {
    local archive="$1" expected="$2" archive_path="$cache_dir/$1"
    if ! verify_archive "$archive_path" "$expected"; then
        rm -f "$archive_path" "$archive_path.part"
        echo "Downloading official Ren'Py $RENPY_VERSION support: $archive" >&2
        curl --fail --location --retry 5 --retry-delay 3 --retry-all-errors \
            --output "$archive_path.part" "$RENPY_DOWNLOAD_ROOT/$archive"
        mv -f "$archive_path.part" "$archive_path"
        verify_archive "$archive_path" "$expected" || { echo "SHA-256 verification failed for $archive_path" >&2; rm -f "$archive_path"; exit 1; }
    fi
    printf '%s\n' "$archive_path"
}

safe_zip_members() {
    local archive_path="$1"
    unzip -Z1 "$archive_path" | while IFS= read -r member; do
        case "$member" in
            /*|../*|*/../*|*/..|..|*\\*) echo "Refusing archive with unsafe path: $member" >&2; exit 1 ;;
        esac
    done
}

stage_archive() {
    local archive_path="$1" expected_root="$2"
    safe_zip_members "$archive_path"
    local temporary
    temporary="$(mktemp -d "${TMPDIR:-/tmp}/aetherkiri-renpy-mobile.XXXXXX")"
    unzip -q "$archive_path" -d "$temporary"
    [[ -d "$temporary/$expected_root" ]] || { echo "Ren'Py archive did not contain expected $expected_root/ directory" >&2; rm -rf "$temporary"; exit 1; }
    mkdir -p "$destination"
    rm -rf "$destination/$expected_root"
    mv "$temporary/$expected_root" "$destination/$expected_root"
    rm -rf "$temporary"
    case "$expected_root" in
        rapt) checksum="$RENPY_RAPT_SHA256" ;;
        renios) checksum="$RENPY_RENIOS_SHA256" ;;
    esac
    printf '%s  %s\n' "$checksum" "$(basename "$archive_path")" > "$destination/$expected_root/.aetherkiri-sha256"
}

case "$platform" in
    android) archive="$(download_archive "$RENPY_RAPT_ARCHIVE" "$RENPY_RAPT_SHA256")"; stage_archive "$archive" rapt ;;
    ios) archive="$(download_archive "$RENPY_RENIOS_ARCHIVE" "$RENPY_RENIOS_SHA256")"; stage_archive "$archive" renios ;;
    both)
        archive="$(download_archive "$RENPY_RAPT_ARCHIVE" "$RENPY_RAPT_SHA256")"; stage_archive "$archive" rapt
        archive="$(download_archive "$RENPY_RENIOS_ARCHIVE" "$RENPY_RENIOS_SHA256")"; stage_archive "$archive" renios
        ;;
esac

printf 'Ren\x27Py mobile support staged: %s\n' "$destination"
printf 'RENPY_MOBILE_ROOT=%s\n' "$destination"
printf 'RENPY_MOBILE_PLAYABLE=false\n'
