#!/usr/bin/env bash
# Download and unpack the pinned official Ren'Py desktop SDK for CI/development.
# The archive is never checked into this repository. Every cached archive is
# verified before extraction, including archives restored by CI cache actions.
set -euo pipefail

readonly RENPY_VERSION="8.5.3"
readonly RENPY_DOWNLOAD_ROOT="https://www.renpy.org/dl/${RENPY_VERSION}"
readonly RENPY_LINUX_ARCHIVE="renpy-${RENPY_VERSION}-sdk.tar.bz2"
readonly RENPY_LINUX_SHA256="eb0a9be7f0fb13632fe25ceade9a8bed5a1b4d6b6e83bd19eeeb29e1a1bb4a45"
readonly RENPY_MACOS_ARM_ARCHIVE="renpy-${RENPY_VERSION}-sdkarm.tar.bz2"
readonly RENPY_MACOS_ARM_SHA256="0579782517f203ba3535dcc2dab54e34bfc318f2f2a7510a5130b6f809f901f6"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
platform="$(uname -s | tr '[:upper:]' '[:lower:]')"
arch="$(uname -m)"
destination="${AETHERKIRI_RENPY_SDK_ROOT:-${repo_root}/.aetherkiri-cache/renpy-sdk/${RENPY_VERSION}}"
cache_dir="${AETHERKIRI_RENPY_SDK_CACHE_DIR:-${repo_root}/.aetherkiri-cache/renpy-sdk/downloads}"
print_config=false

usage() {
    cat <<'USAGE'
Usage: tools/install_renpy_sdk.sh [options]

Download and extract the pinned official Ren'Py 8.5.3 desktop SDK. The
archive is SHA-256 verified before it is used. Existing verified SDK roots are
reused without downloading or extracting again.

Options:
  --platform PLATFORM   linux or darwin (default: host platform)
  --arch ARCH           x86_64 or arm64 (default: host architecture)
  --destination PATH    SDK extraction directory
  --cache-dir PATH      Download cache directory
  --print-config        Print the selected archive metadata and exit
  -h, --help            Show this help

Environment:
  AETHERKIRI_RENPY_SDK_ROOT       Default extraction directory
  AETHERKIRI_RENPY_SDK_CACHE_DIR  Default download cache directory
USAGE
}

while (($#)); do
    case "$1" in
        --platform)
            (($# >= 2)) || { echo "--platform requires a value" >&2; exit 2; }
            platform="$2"; shift 2 ;;
        --arch)
            (($# >= 2)) || { echo "--arch requires a value" >&2; exit 2; }
            arch="$2"; shift 2 ;;
        --destination)
            (($# >= 2)) || { echo "--destination requires a value" >&2; exit 2; }
            destination="$2"; shift 2 ;;
        --cache-dir)
            (($# >= 2)) || { echo "--cache-dir requires a value" >&2; exit 2; }
            cache_dir="$2"; shift 2 ;;
        --print-config) print_config=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

case "$platform" in
    linux)
        # Ren'Py's sdk archive contains the x86_64 desktop runtime used by the
        # Linux product job. ARM Linux builds are intentionally not enabled.
        [[ "$arch" == "x86_64" || "$arch" == "amd64" ]] || {
            echo "Ren'Py SDK is only enabled for Linux x86_64 (requested $arch)" >&2
            exit 1
        }
        archive="$RENPY_LINUX_ARCHIVE"
        sha256="$RENPY_LINUX_SHA256"
        ;;
    darwin|macos)
        [[ "$arch" == "arm64" || "$arch" == "aarch64" || "$arch" == "x86_64" || "$arch" == "amd64" ]] || {
            echo "Ren'Py SDK is only enabled for macOS arm64/x86_64 (requested $arch)" >&2
            exit 1
        }
        platform="darwin"
        if [[ "$arch" == "arm64" || "$arch" == "aarch64" ]]; then
            # sdkarm is the official Apple Silicon archive. It includes the
            # universal macOS launcher and arm64 support files.
            archive="$RENPY_MACOS_ARM_ARCHIVE"
            sha256="$RENPY_MACOS_ARM_SHA256"
        else
            # The regular SDK archive contains the universal macOS launcher
            # used by Intel hosts (and the Linux x86_64 runtime).
            archive="$RENPY_LINUX_ARCHIVE"
            sha256="$RENPY_LINUX_SHA256"
        fi
        ;;
    *)
        echo "Ren'Py SDK is not available for platform '$platform' (desktop Linux/macOS only)" >&2
        exit 1
        ;;
esac

archive_url="${RENPY_DOWNLOAD_ROOT}/${archive}"
archive_path="${cache_dir}/${archive}"

if [[ "$print_config" == true ]]; then
    printf 'RENPY_VERSION=%s\nRENPY_PLATFORM=%s\nRENPY_ARCH=%s\nRENPY_ARCHIVE=%s\nRENPY_SHA256=%s\nRENPY_URL=%s\nRENPY_SDK_ROOT=%s\n' \
        "$RENPY_VERSION" "$platform" "$arch" "$archive" "$sha256" "$archive_url" "$destination"
    exit 0
fi

require_command() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "required command not found: $1" >&2
        exit 1
    }
}
require_command curl
require_command tar
if ! command -v sha256sum >/dev/null 2>&1 &&
   ! command -v shasum >/dev/null 2>&1; then
    echo "required command not found: sha256sum or shasum" >&2
    exit 1
fi

mkdir -p "$cache_dir" "$(dirname "$destination")"
verify_archive() {
    [[ -f "$archive_path" ]] || return 1
    if command -v sha256sum >/dev/null 2>&1; then
        printf '%s  %s\n' "$sha256" "$archive_path" | sha256sum --check --status -
        return
    fi
    local actual
    actual="$(shasum -a 256 "$archive_path" | awk '{ print $1 }')"
    [[ "$actual" == "$sha256" ]]
}
if ! verify_archive; then
    rm -f "$archive_path" "$archive_path.part"
    echo "Downloading official Ren'Py ${RENPY_VERSION} SDK (${platform}/${arch})"
    curl --fail --location --retry 5 --retry-delay 3 --retry-all-errors \
        --output "$archive_path.part" "$archive_url"
    mv -f "$archive_path.part" "$archive_path"
    if ! verify_archive; then
        echo "SHA-256 verification failed for $archive_path" >&2
        rm -f "$archive_path"
        exit 1
    fi
fi

# A previous interrupted extraction must never be mistaken for a valid SDK.
launcher="$destination/renpy.sh"
if [[ "$platform" == "darwin" ]]; then
    launcher="$destination/renpy.sh"
fi
if [[ -x "$launcher" ]]; then
    printf 'Ren\x27Py SDK already installed: %s\n' "$destination"
    printf 'RENPY_SDK_ROOT=%s\n' "$destination"
    exit 0
fi

extract_root="$(mktemp -d "${TMPDIR:-/tmp}/aetherkiri-renpy-sdk.XXXXXX")"
cleanup() { rm -rf "$extract_root"; }
trap cleanup EXIT
# Ensure tar cannot write outside the temporary extraction root. The official
# archive has one relative top-level directory; reject malformed archives.
if ! tar -tjf "$archive_path" | while IFS= read -r member; do
    case "$member" in
        /*|../*|*/../*|*/..|..|*\\*)
            echo "Refusing archive with an unsafe path: $member" >&2
            exit 1
            ;;
    esac
done; then
    exit 1
fi
tar -xjf "$archive_path" -C "$extract_root"
extracted="$(find "$extract_root" -mindepth 1 -maxdepth 1 -type d -print -quit)"
if [[ -z "$extracted" || ! -x "$extracted/renpy.sh" ]]; then
    echo "Official Ren'Py archive did not contain renpy.sh" >&2
    exit 1
fi
rm -rf "$destination"
mv "$extracted" "$destination"
chmod +x "$destination/renpy.sh"
printf 'Ren\x27Py SDK installed: %s\n' "$destination"
printf 'RENPY_SDK_ROOT=%s\n' "$destination"
