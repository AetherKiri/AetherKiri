#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
# shellcheck source=tools/linux_env.sh
source "$PROJECT_ROOT/tools/linux_env.sh"

BUILD_TYPE="${1:-debug}"
BUILD_TYPE_LOWER="$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"
BUILD_TYPE_CAP="$(tr '[:lower:]' '[:upper:]' <<< "${BUILD_TYPE_LOWER:0:1}")${BUILD_TYPE_LOWER:1}"
if [[ "$BUILD_TYPE_LOWER" != "debug" && "$BUILD_TYPE_LOWER" != "release" ]]; then
    echo "Error: invalid build type '$BUILD_TYPE'. Use 'debug' or 'release'." >&2
    exit 1
fi

GODOT_APP_DIR="$PROJECT_ROOT/apps/godot_app"
GODOT_TEMPLATE_VERSION="${GODOT_TEMPLATE_VERSION:-4.7.stable}"
GODOT_EXPORT_TEMPLATE="${GODOT_EXPORT_TEMPLATE:-$GODOT_TEMPLATE_DIR/$GODOT_TEMPLATE_VERSION/linux_${BUILD_TYPE_LOWER}.x86_64}"
CMAKE_CONFIG_PRESET="Linux ${BUILD_TYPE_CAP} Config"
CMAKE_BUILD_PRESET="Linux ${BUILD_TYPE_CAP} Build"
CMAKE_BUILD_DIR="$PROJECT_ROOT/out/linux/$BUILD_TYPE_LOWER"
GODOT_BIN_DIR="$GODOT_APP_DIR/bin/linux/$BUILD_TYPE_LOWER"
GODOT_EXPORT_PRESET="Linux ${BUILD_TYPE_CAP}"
GODOT_EXPORT_MODE="--export-debug"
PARALLEL_JOBS="${JOBS:-8}"
if [[ "$BUILD_TYPE_LOWER" == "release" ]]; then
    GODOT_EXPORT_MODE="--export-release"
fi

ensure_vcpkg() {
    if [[ ! -f "$VCPKG_ROOT/.vcpkg-root" ]]; then
        echo "vcpkg is missing. Run tools/setup_linux.sh first." >&2
        exit 1
    fi
    if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
        (cd "$VCPKG_ROOT" && ./bootstrap-vcpkg.sh -disableMetrics)
    fi
}

ensure_host_rust() {
    # Cargo resolves rustc by bare command name from PATH at build time, so a
    # distro rust shadowing the rustup proxies links Minori against a
    # different std than Siglus (which pins its own toolchain) and the final
    # extension link fails with duplicate _rust_eh_personality symbols. Pin
    # the whole build to one toolchain by prepending the rustup-resolved bin
    # directory (same normalization build_android.sh applies).
    if [[ -n "${CARGO:-}" ]]; then
        local cargo_dir
        cargo_dir="$(dirname "$CARGO")"
        case ":$PATH:" in
            *":$cargo_dir:"*) ;;
            *) export PATH="$cargo_dir:$PATH" ;;
        esac
        return 0
    fi
    command -v rustup >/dev/null || return 0
    local rustc_bin
    rustc_bin="$(rustup which rustc 2>/dev/null || true)"
    [[ -n "$rustc_bin" && -x "$rustc_bin" ]] || return 0
    local toolchain_bin
    toolchain_bin="$(dirname "$rustc_bin")"
    [[ -x "$toolchain_bin/cargo" ]] || return 0
    case ":$PATH:" in
        *":$toolchain_bin:"*) ;;
        *) export PATH="$toolchain_bin:$PATH" ;;
    esac
}

ensure_vcpkg
ensure_host_rust

ensure_godot_project_cache() {
    local project_metadata_dir="$GODOT_APP_DIR/.godot"
    local cache_metadata_dir="$AETHERKIRI_CACHE_DIR/godot-project"

    if [[ -L "$project_metadata_dir" ]]; then
        mkdir -p "$cache_metadata_dir"
        return
    fi
    if [[ -e "$project_metadata_dir" && ! -L "$project_metadata_dir" ]]; then
        echo "Using existing Godot project metadata at $project_metadata_dir"
        return
    fi
    mkdir -p "$cache_metadata_dir"
    ln -s "$cache_metadata_dir" "$project_metadata_dir"
}

ensure_godot_project_cache
command -v cmake >/dev/null
NINJA_BIN="${CMAKE_MAKE_PROGRAM:-$(command -v ninja || command -v ninja-build || true)}"
if [[ -z "$NINJA_BIN" ]]; then
    echo "Error: Ninja is required for the Linux build." >&2
    exit 1
fi
export CMAKE_MAKE_PROGRAM="$NINJA_BIN"

stage_vcpkg_runtime_libraries() {
    local source_library="$1"
    local runtime_dir="$CMAKE_BUILD_DIR/vcpkg_installed/x64-linux/lib"
    local resolved_library

    if [[ ! -d "$runtime_dir" ]]; then
        echo "Error: vcpkg runtime library directory is missing: $runtime_dir" >&2
        exit 1
    fi

    while IFS= read -r resolved_library; do
        [[ -n "$resolved_library" ]] || continue
        cp -Lf "$resolved_library" "$GODOT_BIN_DIR/"
    done < <(
        LD_LIBRARY_PATH="$runtime_dir${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
            ldd "$source_library" | awk -v runtime_dir="$runtime_dir/" '
                $2 == "=>" && index($3, runtime_dir) == 1 { print $3 }
            ' | sort -u
    )
}

stage_all_vcpkg_runtime_libraries() {
    local previous_count=-1
    local current_count
    local library

    while true; do
        current_count="$(find "$GODOT_BIN_DIR" -maxdepth 1 -type f -name 'lib*.so*' | wc -l)"
        if [[ "$current_count" == "$previous_count" ]]; then
            break
        fi
        previous_count="$current_count"
        while IFS= read -r -d '' library; do
            stage_vcpkg_runtime_libraries "$library"
        done < <(find "$GODOT_BIN_DIR" -maxdepth 1 -type f -name 'lib*.so*' -print0)
    done
}

verify_linux_libraries() {
    local library
    local missing=0

    while IFS= read -r -d '' library; do
        if ldd "$library" | grep -q 'not found'; then
            ldd "$library" >&2
            missing=1
        fi
    done < <(find "$1" -maxdepth 1 -type f -name '*.so*' -print0)

    if ((missing)); then
        echo "Error: Linux runtime bundle has unresolved shared-library dependencies." >&2
        exit 1
    fi
}

strip_linux_runtime_symbols() {
    local binaries=()
    local binary

    while IFS= read -r -d '' binary; do
        binaries+=("$binary")
    done < <(find "$@" -maxdepth 1 -type f \
        \( -name 'AetherKiri.x86_64' -o -name '*.so' -o -name '*.so.*' \) \
        -print0)
    if ((${#binaries[@]})); then
        "$PROJECT_ROOT/tools/strip_runtime_symbols.sh" elf "${binaries[@]}"
    fi
}

stage_export_runtime_libraries() {
    find "$GODOT_BIN_DIR" -maxdepth 1 -type f -name 'lib*.so*' \
        -exec cp -Lf {} "$GODOT_EXPORT_DIR/" \;
}

stage_renpy_overlay() {
    [[ "${renpy_enabled_normalized:-OFF}" == "ON" ]] || return 0
    local destination="$1"
    local source="$PROJECT_ROOT/bridge/renpy_runtime/python/aether_renpy_overlay.py"
    [[ -f "$source" ]] || {
        echo "Error: Ren'Py overlay source is missing: $source" >&2
        exit 1
    }
    cp -f "$source" "$destination/aetherkiri-renpy-overlay.py"
}

stage_renpy_sdk() {
    [[ "${renpy_enabled_normalized:-OFF}" == "ON" ]] || return 0
    local destination="$1/renpy-sdk"
    [[ -d "${renpy_sdk_root:-}" ]] || {
        echo "Error: Ren'Py SDK root is missing: ${renpy_sdk_root:-}" >&2
        exit 1
    }
    rm -rf "$destination"
    mkdir -p "$(dirname "$destination")"
    cp -a "$renpy_sdk_root" "$destination"
}

stage_release_extension_for_editor_scan() {
    if [[ "$BUILD_TYPE_LOWER" != "release" ]]; then
        return
    fi

    # Godot opens the host debug GDExtension while importing the project,
    # including before a release export. Use the ABI-compatible release
    # runtime for that scan when this job has not built debug yet.
    local debug_dir="$GODOT_APP_DIR/bin/linux/debug"
    mkdir -p "$debug_dir"
    find "$GODOT_BIN_DIR" -maxdepth 1 -type f -name 'lib*.so*' \
        -exec cp -Lf {} "$debug_dir/" \;
}

renpy_cmake_args=()
renpy_enabled="${AETHERKIRI_ENABLE_RENPY:-OFF}"
renpy_enabled_normalized="$(printf '%s' "$renpy_enabled" | tr '[:lower:]' '[:upper:]')"
case "$renpy_enabled_normalized" in
    ON|TRUE|YES|1)
        renpy_enabled_normalized="ON"
        renpy_sdk_root="${AETHERKIRI_RENPY_SDK_ROOT:-}"
        if [[ -z "$renpy_sdk_root" || ! -d "$renpy_sdk_root" ]]; then
            echo "Error: AETHERKIRI_ENABLE_RENPY requires AETHERKIRI_RENPY_SDK_ROOT" >&2
            exit 1
        fi
        renpy_cmake_args+=(
            -D "AETHERKIRI_ENABLE_RENPY=ON"
            -D "AETHERKIRI_RENPY_SDK_ROOT=$renpy_sdk_root"
        )
        ;;
    OFF|FALSE|NO|0|'')
        renpy_cmake_args+=( -D "AETHERKIRI_ENABLE_RENPY=OFF" )
        ;;
    *)
        echo "Error: AETHERKIRI_ENABLE_RENPY must be ON/OFF or true/false" >&2
        exit 1
        ;;
esac

echo "==> Building Linux engine and Godot extension"
cmake --preset "$CMAKE_CONFIG_PRESET" \
    -D "CMAKE_MAKE_PROGRAM=$CMAKE_MAKE_PROGRAM" \
    -D "AETHERKIRI_ENABLE_INTERNAL=${AETHERKIRI_ENABLE_INTERNAL:-ON}" \
    "${renpy_cmake_args[@]}"
cmake --build --preset "$CMAKE_BUILD_PRESET" -- -j"$PARALLEL_JOBS"

mkdir -p "$GODOT_BIN_DIR"
cp -f "$CMAKE_BUILD_DIR/abi/libengine_api.so" "$GODOT_BIN_DIR/"
cp -f "$CMAKE_BUILD_DIR/bridge/godot_extension/libaether_kiri_godot.so" "$GODOT_BIN_DIR/"
stage_renpy_overlay "$GODOT_BIN_DIR"
stage_all_vcpkg_runtime_libraries
if [[ "$BUILD_TYPE_LOWER" == "release" ]]; then
    echo "==> Removing non-runtime symbols from staged Linux Release libraries"
    strip_linux_runtime_symbols "$GODOT_BIN_DIR"
fi

if readelf -d "$GODOT_BIN_DIR/libengine_api.so" | grep -Fq "$CMAKE_BUILD_DIR"; then
    echo "Error: engine API retains a build-directory runtime path." >&2
    exit 1
fi
verify_linux_libraries "$GODOT_BIN_DIR"
stage_release_extension_for_editor_scan

if [[ ! -x "$GODOT_BIN" ]]; then
    echo "Error: Godot not found at $GODOT_BIN. Run tools/setup_linux.sh first." >&2
    exit 1
fi
if [[ ! -f "$GODOT_EXPORT_TEMPLATE" ]]; then
    echo "Error: Linux export template not found at $GODOT_EXPORT_TEMPLATE. Run tools/setup_linux.sh first." >&2
    exit 1
fi

echo "==> Validating GDExtension in headless Godot"
"$GODOT_BIN" --headless --path "$GODOT_APP_DIR" --editor --quit

GODOT_EXPORT_DIR="$PROJECT_ROOT/out/godot/linux/$BUILD_TYPE_LOWER"
GODOT_EXPORT_APP="$GODOT_EXPORT_DIR/AetherKiri.x86_64"
mkdir -p "$GODOT_EXPORT_DIR"
echo "==> Exporting Linux Godot application"
"$GODOT_BIN" --headless --path "$GODOT_APP_DIR" \
    "$GODOT_EXPORT_MODE" "$GODOT_EXPORT_PRESET" "$GODOT_EXPORT_APP"

stage_export_runtime_libraries
stage_renpy_overlay "$GODOT_EXPORT_DIR"
stage_renpy_sdk "$GODOT_EXPORT_DIR"
if [[ "$BUILD_TYPE_LOWER" == "release" ]]; then
    echo "==> Removing non-runtime symbols from exported Linux Release application"
    strip_linux_runtime_symbols "$GODOT_EXPORT_DIR"
fi
verify_linux_libraries "$GODOT_EXPORT_DIR"
echo "Linux build output: $GODOT_EXPORT_APP"
