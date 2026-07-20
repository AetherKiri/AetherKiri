#!/usr/bin/env bash
# Source this file before invoking project build commands on Linux.

if [[ -z "${BASH_SOURCE[0]:-}" ]]; then
    echo "linux_env.sh must be sourced by bash" >&2
    return 1 2>/dev/null || exit 1
fi

_aetherkiri_tools_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export AETHERKIRI_PROJECT_ROOT="$(cd "${_aetherkiri_tools_dir}/.." && pwd)"
export AETHERKIRI_CACHE_DIR="${AETHERKIRI_CACHE_DIR:-${AETHERKIRI_PROJECT_ROOT}/.aetherkiri-cache}"
export AETHERKIRI_SYSTEM_TOOLS_DIR="${AETHERKIRI_SYSTEM_TOOLS_DIR:-${AETHERKIRI_CACHE_DIR}/system-tools}"

if [[ -d "${AETHERKIRI_SYSTEM_TOOLS_DIR}/bin" ]]; then
    export PATH="${AETHERKIRI_SYSTEM_TOOLS_DIR}/bin:${PATH}"
fi

export CCACHE_DIR="${CCACHE_DIR:-${AETHERKIRI_CACHE_DIR}/ccache}"
export VCPKG_ROOT="${VCPKG_ROOT:-${AETHERKIRI_CACHE_DIR}/vcpkg}"
export VCPKG_DOWNLOADS="${VCPKG_DOWNLOADS:-${AETHERKIRI_CACHE_DIR}/vcpkg-downloads}"
export VCPKG_DEFAULT_BINARY_CACHE="${VCPKG_DEFAULT_BINARY_CACHE:-${AETHERKIRI_CACHE_DIR}/vcpkg-binaries}"
export npm_config_cache="${npm_config_cache:-${AETHERKIRI_CACHE_DIR}/npm}"
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-${AETHERKIRI_CACHE_DIR}/xdg/cache}"
export XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-${AETHERKIRI_CACHE_DIR}/xdg/config}"
export XDG_DATA_HOME="${XDG_DATA_HOME:-${AETHERKIRI_CACHE_DIR}/xdg/data}"
export GODOT_TEMPLATE_DIR="${GODOT_TEMPLATE_DIR:-${XDG_DATA_HOME}/godot/export_templates}"
export GODOT_BIN="${GODOT_BIN:-${AETHERKIRI_CACHE_DIR}/godot/Godot_v4.7-stable_linux.x86_64}"

mkdir -p "$AETHERKIRI_SYSTEM_TOOLS_DIR" "$CCACHE_DIR" "$VCPKG_DOWNLOADS" "$VCPKG_DEFAULT_BINARY_CACHE" \
    "$npm_config_cache" "$XDG_CACHE_HOME" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
