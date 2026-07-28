#!/usr/bin/env bash
set -euo pipefail

TOOLS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=tools/linux_env.sh
source "$TOOLS_DIR/linux_env.sh"

GODOT_RELEASE="${GODOT_RELEASE:-4.7-stable}"
GODOT_TEMPLATE_VERSION="${GODOT_TEMPLATE_VERSION:-4.7.stable}"
GODOT_ARCHIVE="$AETHERKIRI_CACHE_DIR/downloads/Godot_v${GODOT_RELEASE}_linux.x86_64.zip"
GODOT_TEMPLATE_ARCHIVE="$AETHERKIRI_CACHE_DIR/downloads/Godot_v${GODOT_RELEASE}_export_templates.tpz"
GODOT_DIR="$AETHERKIRI_CACHE_DIR/godot"
GODOT_TEMPLATE_DEST="$GODOT_TEMPLATE_DIR/$GODOT_TEMPLATE_VERSION"

install_arch_tool() {
    local package_name="$1"
    local executable_name="$2"
    local package_url

    if command -v "$executable_name" >/dev/null 2>&1; then
        return
    fi
    if ! command -v pacman >/dev/null 2>&1 || ! command -v bsdtar >/dev/null 2>&1; then
        return
    fi

    package_url="$(pacman -Sw --print-format '%l' "$package_name" | tail -n 1)"
    if [[ -z "$package_url" || "$package_url" != http* ]]; then
        echo "Could not resolve the Arch package URL for $package_name." >&2
        return
    fi
    local package_file="$AETHERKIRI_CACHE_DIR/downloads/${package_url##*/}"
    if [[ ! -f "$package_file" ]]; then
        curl --fail --location --retry 5 --retry-delay 3 --retry-all-errors \
            -o "$package_file" "$package_url"
    fi
    bsdtar -xf "$package_file" -C "$AETHERKIRI_SYSTEM_TOOLS_DIR"
    mkdir -p "$AETHERKIRI_SYSTEM_TOOLS_DIR/bin"
    ln -sf "../usr/bin/$executable_name" "$AETHERKIRI_SYSTEM_TOOLS_DIR/bin/$executable_name"
    export PATH="$AETHERKIRI_SYSTEM_TOOLS_DIR/bin:$PATH"
}

# This session may not have sudo. Keep tool-only Arch packages in the cache
# root so the repository can still bootstrap without changing the host.
install_arch_tool zip zip
install_arch_tool nasm nasm
install_arch_tool yasm yasm

require_commands=(cmake ninja c++ git curl unzip zip pkg-config)
missing_commands=()
for command_name in "${require_commands[@]}"; do
    command -v "$command_name" >/dev/null 2>&1 || missing_commands+=("$command_name")
done
if ((${#missing_commands[@]})); then
    printf 'Missing required commands: %s\n' "${missing_commands[*]}" >&2
    echo "Arch Linux: sudo pacman -S --needed base-devel cmake ninja pkgconf git curl unzip zip ccache nasm yasm" >&2
    exit 1
fi

mkdir -p "$AETHERKIRI_CACHE_DIR/downloads" "$GODOT_DIR" "$GODOT_TEMPLATE_DEST"

if [[ ! -f "$VCPKG_ROOT/.vcpkg-root" ]]; then
    rm -rf "$VCPKG_ROOT"
    git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
fi
if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
    (cd "$VCPKG_ROOT" && ./bootstrap-vcpkg.sh -disableMetrics)
fi

if [[ ! -x "$GODOT_BIN" ]]; then
    if [[ -f "$GODOT_ARCHIVE" ]] && ! unzip -tq "$GODOT_ARCHIVE" >/dev/null; then
        rm -f "$GODOT_ARCHIVE"
    fi
    if [[ ! -f "$GODOT_ARCHIVE" ]]; then
        curl --fail --location --retry 5 --retry-delay 3 --retry-all-errors \
            -o "$GODOT_ARCHIVE" \
            "https://github.com/godotengine/godot-builds/releases/download/${GODOT_RELEASE}/Godot_v${GODOT_RELEASE}_linux.x86_64.zip"
    fi
    unzip -tq "$GODOT_ARCHIVE"
    unzip -oq "$GODOT_ARCHIVE" -d "$GODOT_DIR"
    chmod +x "$GODOT_BIN"
fi

if [[ ! -f "$GODOT_TEMPLATE_DEST/linux_debug.x86_64" ]]; then
    if [[ -f "$GODOT_TEMPLATE_ARCHIVE" ]] && ! unzip -tq "$GODOT_TEMPLATE_ARCHIVE" >/dev/null; then
        rm -f "$GODOT_TEMPLATE_ARCHIVE"
    fi
    if [[ ! -f "$GODOT_TEMPLATE_ARCHIVE" ]]; then
        curl --fail --location --retry 5 --retry-delay 3 --retry-all-errors \
            -o "$GODOT_TEMPLATE_ARCHIVE" \
            "https://github.com/godotengine/godot-builds/releases/download/${GODOT_RELEASE}/Godot_v${GODOT_RELEASE}_export_templates.tpz"
    fi
    unzip -tq "$GODOT_TEMPLATE_ARCHIVE"
    temp_dir="$(mktemp -d)"
    trap 'rm -rf "$temp_dir"' EXIT
    unzip -oq "$GODOT_TEMPLATE_ARCHIVE" -d "$temp_dir"
    cp -a "$temp_dir/templates/." "$GODOT_TEMPLATE_DEST/"
fi

echo "Linux development environment ready."
echo "Cache root: $AETHERKIRI_CACHE_DIR"
echo "Godot: $GODOT_BIN"
echo "vcpkg: $VCPKG_ROOT/vcpkg"
echo "Optional compiler caching: install host ccache with: sudo pacman -S --needed ccache"
echo "Missing system-only packages can be installed with: sudo pacman -S --needed base-devel cmake ninja pkgconf git curl unzip zip nasm yasm"
