#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_TYPE="debug"
SIMULATOR=false
SIMULATOR_ARCH="${IOS_SIMULATOR_ARCH:-x86_64}"
PACKAGE_IPA=false
for arg in "$@"; do
    case "$arg" in
        debug|release|Debug|Release) BUILD_TYPE="$arg" ;;
        --simulator) SIMULATOR=true ;;
        --simulator-arch=*) SIMULATOR_ARCH="${arg#*=}" ;;
        --package-ipa|--unsigned-ipa|--ipa) PACKAGE_IPA=true ;;
        *) echo "[WARN] Unknown iOS build argument ignored: $arg" ;;
    esac
done

BUILD_TYPE_LOWER="$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"
BUILD_TYPE_CAP="$(echo "${BUILD_TYPE_LOWER:0:1}" | tr '[:lower:]' '[:upper:]')${BUILD_TYPE_LOWER:1}"

if [[ "$SIMULATOR" == true ]]; then
    if [[ "$SIMULATOR_ARCH" == "x86_64" || "$SIMULATOR_ARCH" == "x64" ]]; then
        SIMULATOR_ARCH="x86_64"
        CMAKE_CONFIG_PRESET="iOS Simulator x64 Debug Config"
        CMAKE_BUILD_PRESET="iOS Simulator x64 Debug Build"
        CMAKE_BUILD_DIR="$PROJECT_ROOT/out/ios-simulator-x64/debug"
        GODOT_TRIPLET_DIR="ios-simulator-x64/debug"
        VCPKG_TRIPLET_DIR="x64-ios-simulator"
    elif [[ "$SIMULATOR_ARCH" == "arm64" ]]; then
        CMAKE_CONFIG_PRESET="iOS Simulator Debug Config"
        CMAKE_BUILD_PRESET="iOS Simulator Debug Build"
        CMAKE_BUILD_DIR="$PROJECT_ROOT/out/ios-simulator/debug"
        GODOT_TRIPLET_DIR="ios-simulator/debug"
        VCPKG_TRIPLET_DIR="arm64-ios-simulator"
    else
        echo "Error: Invalid simulator arch '$SIMULATOR_ARCH'. Use x86_64 or arm64." >&2
        exit 1
    fi
else
    CMAKE_CONFIG_PRESET="iOS ${BUILD_TYPE_CAP} Config"
    CMAKE_BUILD_PRESET="iOS ${BUILD_TYPE_CAP} Build"
    CMAKE_BUILD_DIR="$PROJECT_ROOT/out/ios/$BUILD_TYPE_LOWER"
    GODOT_TRIPLET_DIR="ios/$BUILD_TYPE_LOWER"
    VCPKG_TRIPLET_DIR="arm64-ios"
fi

GODOT_BIN="${GODOT_BIN:-/Applications/Godot.app/Contents/MacOS/Godot}"
GODOT_EXPORT_TEMPLATE="${GODOT_EXPORT_TEMPLATE:-$HOME/Library/Application Support/Godot/export_templates/4.7.stable/ios.zip}"
GODOT_APP_DIR="$PROJECT_ROOT/apps/godot_app"
GODOT_BIN_DIR="$GODOT_APP_DIR/bin/$GODOT_TRIPLET_DIR"
RUNTIME_CJK_FONT_SOURCE="$GODOT_APP_DIR/assets/fonts/aetherkiri-runtime-cjk.otf"
RUNTIME_SYMBOL_FONT_SOURCE="$GODOT_APP_DIR/assets/fonts/aetherkiri-runtime-symbols.ttf"
# The Renios archive is intentionally opt-in.  When present, the build folds
# the official static-library closure into the Godot extension archive and
# copies only its resources/framework into the exported app.  It must never
# copy or compile Renios' prototype main.c: Godot already owns the process and
# the UIKit/SDL application entrypoint.
RENPY_MOBILE_ROOT="${AETHERKIRI_RENPY_MOBILE_ROOT:-${RENPY_MOBILE_ROOT:-}}"
# Renios archives contain the native prototype/resources but intentionally do
# not contain a game-specific `base/` directory.  A caller may provide the
# generated Renios base explicitly for a bundle smoke; gameplay remains gated
# by the adapter contract regardless.
RENPY_RENIOS_BASE="${AETHERKIRI_RENPY_RENIOS_BASE:-}"
PARALLEL_JOBS="${JOBS:-8}"
FORCE_LOAD_PLUGIN_ARCHIVES=(
    "libSDL2.a"
    "libkrkr2plugin.a"
    "libextkagparser.a"
    "libkagparserex.a"
    "liblayerExDraw.a"
    "libmotionplayer.a"
    "libpsbfile.a"
    "libpsdfile.a"
    "libpsdparse.a"
)
FORCE_LOAD_PLUGIN_SOURCES=(
    "vcpkg_installed/$VCPKG_TRIPLET_DIR/lib/libSDL2.a"
    "packages/AetherKrkr/plugins/libkrkr2plugin.a"
    "packages/AetherKrkr/plugins/extkagparser/libextkagparser.a"
    "packages/AetherKrkr/plugins/kagparserex/libkagparserex.a"
    "packages/AetherKrkr/plugins/layerex_draw/liblayerExDraw.a"
    "packages/AetherKrkr/plugins/motionplayer/libmotionplayer.a"
    "packages/AetherKrkr/plugins/psbfile/libpsbfile.a"
    "packages/AetherKrkr/plugins/psdfile/libpsdfile.a"
    "packages/AetherKrkr/plugins/psdfile/psdparse/psdparse/libpsdparse.a"
)
PRIVATE_RUNTIME_ARCHIVE_LISTER="$PROJECT_ROOT/packages/AetherInternal/tools/list_ios_runtime_archives.sh"
if [[ -x "$PRIVATE_RUNTIME_ARCHIVE_LISTER" ]]; then
    private_runtime_target="device"
    if [[ "${SIMULATOR:-false}" == true ]]; then
        private_runtime_target="simulator-$SIMULATOR_ARCH"
    fi
    while IFS=$'\t' read -r archive source; do
        if [[ -z "$archive" || -z "$source" ]]; then
            continue
        fi
        FORCE_LOAD_PLUGIN_ARCHIVES+=("$archive")
        FORCE_LOAD_PLUGIN_SOURCES+=("$source")
    done < <("$PRIVATE_RUNTIME_ARCHIVE_LISTER" "$private_runtime_target")
fi
IOS_SDK_COMPAT_ARCHIVE="libios_sdk_compat_symbols.a"

ensure_vcpkg() {
    if [[ -f "$PROJECT_ROOT/.devtools/vcpkg/.vcpkg-root" ]]; then
        export VCPKG_ROOT="$PROJECT_ROOT/.devtools/vcpkg"
    elif [[ -n "${VCPKG_ROOT:-}" && -f "$VCPKG_ROOT/.vcpkg-root" ]]; then
        export VCPKG_ROOT
    else
        echo "[INFO] vcpkg not found. Automatically setting up vcpkg in .devtools/vcpkg..."
        mkdir -p "$PROJECT_ROOT/.devtools"
        rm -rf "$PROJECT_ROOT/.devtools/vcpkg"
        git clone https://github.com/microsoft/vcpkg.git "$PROJECT_ROOT/.devtools/vcpkg"
        (cd "$PROJECT_ROOT/.devtools/vcpkg" && ./bootstrap-vcpkg.sh -disableMetrics)
        export VCPKG_ROOT="$PROJECT_ROOT/.devtools/vcpkg"
    fi

    if [[ ! -x "$VCPKG_ROOT/vcpkg" ]]; then
        if [[ -x "$VCPKG_ROOT/bootstrap-vcpkg.sh" ]]; then
            echo "[INFO] vcpkg binary missing. Bootstrapping existing vcpkg tree..."
            (cd "$VCPKG_ROOT" && ./bootstrap-vcpkg.sh -disableMetrics)
        else
            echo "[INFO] vcpkg tree is incomplete. Recreating .devtools/vcpkg..."
            mkdir -p "$PROJECT_ROOT/.devtools"
            rm -rf "$PROJECT_ROOT/.devtools/vcpkg"
            git clone https://github.com/microsoft/vcpkg.git "$PROJECT_ROOT/.devtools/vcpkg"
            (cd "$PROJECT_ROOT/.devtools/vcpkg" && ./bootstrap-vcpkg.sh -disableMetrics)
            export VCPKG_ROOT="$PROJECT_ROOT/.devtools/vcpkg"
        fi
    fi
}

ensure_host_rust() {
    # Cargo resolves rustc by bare command name from PATH at build time, so a
    # Homebrew rust shadowing the rustup proxies links Minori against a
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

command -v cmake >/dev/null
NINJA_BIN="${CMAKE_MAKE_PROGRAM:-$(command -v ninja || command -v ninja-build || true)}"
if [[ -z "$NINJA_BIN" ]]; then
    echo "Error: Ninja build tool not found. Install ninja and ensure it is available in PATH." >&2
    exit 1
fi
export CMAKE_MAKE_PROGRAM="$NINJA_BIN"

preflight_simulator_template_arch() {
    local arch="$1"
    local template="$2"
    local tmpdir
    local libgodot
    local info

    if [[ ! -f "$template" ]]; then
        return
    fi

    tmpdir="$(mktemp -d /tmp/aetherkiri-ios-template.XXXXXX)"
    libgodot="$tmpdir/libgodot.ios.debug.xcframework/ios-arm64_x86_64-simulator/libgodot.a"
    unzip -q "$template" \
        'libgodot.ios.debug.xcframework/ios-arm64_x86_64-simulator/libgodot.a' \
        -d "$tmpdir"
    info="$(lipo -archs "$libgodot" 2>/dev/null || true)"
    rm -rf "$tmpdir"

    if [[ " $info " != *" $arch "* ]]; then
        echo "Error: Godot iOS simulator export template does not contain '$arch'." >&2
        echo "       $template" >&2
        echo "       architectures: ${info:-unknown}" >&2
        echo "       Install or build a Godot export template with an $arch simulator slice, or use --simulator-arch=x86_64." >&2
        exit 1
    fi
}

if [[ "$SIMULATOR" == true ]]; then
    preflight_simulator_template_arch "$SIMULATOR_ARCH" "$GODOT_EXPORT_TEMPLATE"
fi

resolve_ios_godot_cpp_lib() {
    local triplet_root="$1"
    local arch="$2"
    local build_type="$3"
    local config_name="release"
    local config_upper="RELEASE"
    if [[ "$build_type" == "debug" ]]; then
        config_name="debug"
        config_upper="DEBUG"
    fi

    local config_file="$triplet_root/share/unofficial-godot-cpp/unofficial-godot-cpp-config-$config_name.cmake"
    local location=""
    if [[ -f "$config_file" ]]; then
        location="$(sed -n "s|.*IMPORTED_LOCATION_${config_upper} \"\\(.*\\)\".*|\\1|p" "$config_file" | head -n 1)"
        location="${location//\$\{_IMPORT_PREFIX\}/$triplet_root}"
        if [[ -n "$location" && -f "$location" ]]; then
            printf '%s\n' "$location"
            return 0
        fi
    fi

    local search_dirs=()
    if [[ "$build_type" == "debug" ]]; then
        search_dirs+=("$triplet_root/debug/lib" "$triplet_root/lib")
    else
        search_dirs+=("$triplet_root/lib" "$triplet_root/debug/lib")
    fi

    local dir
    local found
    for dir in "${search_dirs[@]}"; do
        [[ -d "$dir" ]] || continue
        found="$(find "$dir" -maxdepth 1 -name "libgodot-cpp.ios.*.$arch.a" -print -quit 2>/dev/null || true)"
        if [[ -n "$found" ]]; then
            printf '%s\n' "$found"
            return 0
        fi
    done

    return 1
}

build_ios_sdk_compat_archive() {
    local output="$1"
    local triplet="$2"
    local arch="arm64"
    local sdk="iphoneos"
    local min_flag="-mios-version-min=${IOS_MIN_VERSION:-16.0}"
    local work_dir="$CMAKE_BUILD_DIR/ios_sdk_compat"
    local source="$work_dir/ios_sdk_compat_symbols.mm"
    local object="$work_dir/ios_sdk_compat_symbols.o"

    if [[ "$triplet" == "x64-ios-simulator" ]]; then
        arch="x86_64"
        sdk="iphonesimulator"
        min_flag="-mios-simulator-version-min=${IOS_MIN_VERSION:-16.0}"
    elif [[ "$triplet" == "arm64-ios-simulator" ]]; then
        sdk="iphonesimulator"
        min_flag="-mios-simulator-version-min=${IOS_MIN_VERSION:-16.0}"
    fi

    mkdir -p "$work_dir"
    cat > "$source" <<'EOF'
#import <Foundation/Foundation.h>
#include <stddef.h>

extern "C" {
struct hid_device_;
struct hid_device_info;

extern "C" __attribute__((weak, visibility("default"))) NSString * const CADynamicRangeAutomatic = @"CADynamicRangeAutomatic";
extern "C" __attribute__((weak, visibility("default"))) NSString * const CADynamicRangeConstrainedHigh = @"CADynamicRangeConstrainedHigh";
extern "C" __attribute__((weak, visibility("default"))) NSString * const CADynamicRangeHigh = @"CADynamicRangeHigh";
extern "C" __attribute__((weak, visibility("default"))) NSString * const CADynamicRangeStandard = @"CADynamicRangeStandard";
extern "C" __attribute__((weak, visibility("default"))) NSString * const MTLLogStateErrorDomain = @"MTLLogStateErrorDomain";
extern "C" __attribute__((weak, visibility("default"))) NSString * const MTLTensorDomain = @"MTLTensorDomain";
extern "C" __attribute__((weak, visibility("default"))) NSString * const NSDeviceCertificationiPhonePerformanceGaming = @"NSDeviceCertificationiPhonePerformanceGaming";
extern "C" __attribute__((weak, visibility("default"))) NSString * const NSProcessInfoPerformanceProfileDidChangeNotification = @"NSProcessInfoPerformanceProfileDidChangeNotification";
extern "C" __attribute__((weak, visibility("default"))) NSString * const NSProcessPerformanceProfileDefault = @"NSProcessPerformanceProfileDefault";
extern "C" __attribute__((weak, visibility("default"))) NSString * const NSProcessPerformanceProfileSustained = @"NSProcessPerformanceProfileSustained";

// Godot 4.7's SDL HID wrapper references APIs newer than the iOS SDL backend
// bundled by this branch. Keep the unsupported queries as weak fallbacks so a
// future SDL implementation can replace them without creating duplicate symbols.
extern "C" __attribute__((weak, visibility("default"))) struct hid_device_info *PLATFORM_hid_get_device_info(struct hid_device_ *) {
    return nullptr;
}
extern "C" __attribute__((weak, visibility("default"))) int PLATFORM_hid_get_input_report(struct hid_device_ *, unsigned char *, size_t) {
    return -1;
}
extern "C" __attribute__((weak, visibility("default"))) int PLATFORM_hid_get_report_descriptor(struct hid_device_ *, unsigned char *, size_t) {
    return -1;
}
}
EOF

    xcrun --sdk "$sdk" clang++ -arch "$arch" "$min_flag" -fobjc-arc -c "$source" -o "$object"
    libtool -static -o "$output" "$object"
}

renios_enabled() {
    case "${AETHERKIRI_ENABLE_RENPY:-OFF}" in
        ON|TRUE|YES|1|on|true|yes) [[ -n "$RENPY_MOBILE_ROOT" ]] ;;
        *) return 1 ;;
    esac
}

renios_link_enabled() {
    case "${AETHERKIRI_RENPY_RENIOS_LINK:-OFF}" in
        ON|TRUE|YES|1|on|true|yes) return 0 ;;
        *) return 1 ;;
    esac
}

renios_prototype_root() {
    [[ -n "$RENPY_MOBILE_ROOT" ]] || return 1
    local root="$RENPY_MOBILE_ROOT/renios/prototype"
    [[ -d "$root" ]] || return 1
    [[ -d "$root/prebuilt" ]] || return 1
    printf '%s\n' "$root"
}

renios_configuration() {
    if [[ "${SIMULATOR:-false}" == true ]]; then
        printf 'debug\n'
    else
        printf '%s\n' "$BUILD_TYPE_LOWER"
    fi
}

renios_prebuilt_root() {
    local prototype
    prototype="$(renios_prototype_root 2>/dev/null || true)"
    [[ -n "$prototype" ]] || return 1
    # Renios release archives are device arm64 only.  The debug archive is a
    # universal simulator/device slice, so use it for every simulator export
    # even when the surrounding Godot export is a release configuration.
    local configuration
    configuration="$(renios_configuration)"
    local prebuilt="$prototype/prebuilt/$configuration"
    if [[ ! -d "$prebuilt" ]]; then
        echo "Error: Renios prebuilt directory is missing: $prebuilt" >&2
        return 1
    fi
    printf '%s\n' "$prebuilt"
}

renios_archive_is_excluded() {
    case "$(basename "$1")" in
        # SDL2main owns the UIKit/SDL application entrypoint in the official
        # prototype.  Godot's application must supply the only entrypoint.
        libSDL2main.a|libSDL2_test.a) return 0 ;;
        *) return 1 ;;
    esac
}

collect_renios_archives() {
    local prebuilt="$1"
    local archive
    [[ -d "$prebuilt" ]] || return 1
    # Keep the runtime roots first for deterministic archive inspection, then
    # append every transitive archive shipped by Renios.  The latter is the
    # complete native dependency closure for the selected SDK/configuration.
    local root_name
    for root_name in librenpython.a librenpy.a libpython3.12.a libSDL2.a libSDL2_image.a; do
        archive="$prebuilt/$root_name"
        [[ -f "$archive" ]] || {
            echo "Error: Renios runtime archive is missing: $archive" >&2
            return 1
        }
        renios_archive_is_excluded "$archive" || printf '%s\n' "$archive"
    done
    while IFS= read -r archive; do
        [[ -f "$archive" ]] || continue
        renios_archive_is_excluded "$archive" && continue
        case "$(basename "$archive")" in
            librenpython.a|librenpy.a|libpython3.12.a|libSDL2.a|libSDL2_image.a) ;;
            *) printf '%s\n' "$archive" ;;
        esac
    done < <(find "$prebuilt" -maxdepth 1 -type f -name 'lib*.a' | sort)
}

renios_archive_names() {
    local prebuilt="$1"
    collect_renios_archives "$prebuilt" | xargs -n1 basename | paste -sd, -
}

renios_launcher_probe() {
    renios_enabled || return 0
    bash "$PROJECT_ROOT/tools/test_renios_ios_launcher.sh" \
        "$RENPY_MOBILE_ROOT" "$(renios_configuration)"
}

stage_renios_ios_resources() {
    local export_root="$1"
    if ! renios_enabled; then
        return 0
    fi
    local prototype
    prototype="$(renios_prototype_root 2>/dev/null || true)"
    if [[ -z "$prototype" ]]; then
        echo "Warning: AETHERKIRI_ENABLE_RENPY=ON but Renios support root is not staged; keeping iOS Ren'Py NOT_SUPPORTED" >&2
        return 0
    fi

    local app_root="$export_root/Aether"
    local resource_root="$app_root/renios"
    local resource_source="$prototype"
    mkdir -p "$resource_root"
    # These are the official Renios app resources.  Do not copy main.c,
    # prototype.xcodeproj, or any source file: this export remains a Godot
    # application and has one host-owned lifecycle.
    local item
    for item in "Launch Screen.storyboard" LaunchImage-background.png LaunchImage-foreground.png Info.plist Media.xcassets; do
        if [[ -e "$resource_source/$item" ]]; then
            rm -rf "$resource_root/$item"
            cp -R "$resource_source/$item" "$resource_root/"
        fi
    done
    if [[ -d "$RENPY_RENIOS_BASE" ]]; then
        rm -rf "$resource_root/base"
        cp -R "$RENPY_RENIOS_BASE" "$resource_root/base"
    elif [[ -d "$resource_source/base" ]]; then
        rm -rf "$resource_root/base"
        cp -R "$resource_source/base" "$resource_root/base"
    fi

    # MetalANGLE is the only dynamic dependency in Renios' prototype.  Keep
    # it in the app's Frameworks directory; the PBX patch below links and
    # embeds it.  Static Renios libraries are folded into the extension
    # archive, so they are not copied into the app bundle.
    if [[ -d "$resource_source/Frameworks/MetalANGLE.xcframework" ]]; then
        mkdir -p "$app_root/Frameworks"
        rm -rf "$app_root/Frameworks/MetalANGLE.xcframework"
        cp -R "$resource_source/Frameworks/MetalANGLE.xcframework" "$app_root/Frameworks/"
    fi

    local prebuilt
    prebuilt="$(renios_prebuilt_root 2>/dev/null || true)"
    if [[ -z "$prebuilt" ]]; then
        echo "Warning: Renios prebuilt closure is unavailable; resources staged but iOS Ren'Py remains NOT_SUPPORTED" >&2
        return 0
    fi
    {
        printf 'Renios archive: %s\n' "$RENPY_MOBILE_ROOT"
        if [[ -f "$RENPY_MOBILE_ROOT/renios/.aetherkiri-sha256" ]]; then
            printf 'Renios archive SHA-256: %s\n' "$(cat "$RENPY_MOBILE_ROOT/renios/.aetherkiri-sha256")"
        fi
        printf 'Configuration: %s\n' "$(renios_configuration)"
        printf 'Static closure: %s\n' "$(renios_archive_names "$prebuilt")"
        printf 'Excluded archives: libSDL2main.a,libSDL2_test.a (host entrypoint/test)\n'
        if [[ -d "$resource_root/base" ]]; then
            printf 'Base resources: bundled\n'
        else
            printf 'Base resources: none (Renios archive is game-agnostic)\n'
        fi
        printf 'Host entrypoint: Godot (Renios main.c intentionally omitted)\n'
        printf 'Runtime status: NOT_SUPPORTED until lifecycle/render/input adapter is linked\n'
    } > "$resource_root/renios-manifest.txt"
}

combine_ios_static_extension() {
    local output="$1"
    local triplet="$2"
    # Verify that the staged closure still has the known blocking launcher
    # contract before merging it into the host archive. This probe never
    # invokes launcher_main and cannot turn the provider on by itself.
    renios_launcher_probe
    local vcpkg_triplet_root="$CMAKE_BUILD_DIR/vcpkg_installed/$triplet"
    local vcpkg_lib_dir="$vcpkg_triplet_root/lib"
    local cubism_package_root="${AETHERKIRI_INTERNAL_DIR:-$PROJECT_ROOT/packages/AetherInternal}"
    local cubism_core_lib="$cubism_package_root/third_party/cubism/Core/lib/ios/Release-iphoneos/libLive2DCubismCore.a"
    local godot_cpp_arch="arm64"
    local godot_cpp_lib=""
    local rfvp_rust_target="aarch64-apple-ios"
    local libs=(
        "$CMAKE_BUILD_DIR/bridge/godot_extension/libaether_kiri_godot.a"
        "$CMAKE_BUILD_DIR/bridge/onscripter_runtime/libaether_onscripter_runtime.a"
        "$CMAKE_BUILD_DIR/abi/libengine_api.a"
        "$CMAKE_BUILD_DIR/bridge/krkr2_runtime/libaether_krkr2_runtime.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/core/base/libcore_base_module.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/core/environ/libcore_environ_module.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/core/extension/libcore_extension_module.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/core/movie/libcore_movie_module.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/core/plugin/libcore_plugin_module.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/core/sound/libcore_sound_module.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/core/tjs2/libtjs2.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/core/utils/libcore_utils_module.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/core/visual/libcore_visual_module.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/core/visual/simd/libtvpgl_simd.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/plugins/libkrkr2plugin.a"
        # krkr2plugin links the Hxv4 provider as a static dependency, but
        # static archives do not contain their dependent archive members.
        # Merge it explicitly so pluginAnchors.cpp's registration symbol is
        # present in the archive consumed by Godot's iOS exporter.
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/plugins/Crypt/hxv4/libhxv4_decoder.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/plugins/extkagparser/libextkagparser.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/plugins/kagparserex/libkagparserex.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/plugins/layerex_draw/liblayerExDraw.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/plugins/motionplayer/libmotionplayer.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/plugins/psbfile/libpsbfile.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/plugins/psdfile/libpsdfile.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/plugins/psdfile/psdparse/psdparse/libpsdparse.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/plugins/libCubismFramework.a"
        "$CMAKE_BUILD_DIR/packages/AetherKrkr/external/libbpg/liblibbpg.a"
    )

    local siglus_runtime_lib="$CMAKE_BUILD_DIR/bridge/siglus_runtime/libaether_siglus_runtime.a"
    if [[ -f "$siglus_runtime_lib" ]]; then
        libs+=("$siglus_runtime_lib")
    fi
    local renpy_runtime_lib="$CMAKE_BUILD_DIR/bridge/renpy_runtime/libaether_renpy_runtime.a"
    if [[ -f "$renpy_runtime_lib" ]]; then
        # CMake's private archive dependency is not propagated by Godot's
        # static iOS exporter; merge the provider registration object here so
        # the guarded adapter boundary is present in the final app.
        libs+=("$renpy_runtime_lib")
    fi
    while IFS= read -r siglus_vm_lib; do
        if [[ -n "$siglus_vm_lib" && -f "$siglus_vm_lib" ]]; then
            libs+=("$siglus_vm_lib")
        fi
    done < <(find "$CMAKE_BUILD_DIR/siglus-rs-target" -name 'libsiglus_scene_vm.a' 2>/dev/null || true)

    if [[ "$triplet" == "x64-ios-simulator" ]]; then
        godot_cpp_arch="x86_64"
        rfvp_rust_target="x86_64-apple-ios"
        cubism_core_lib="$cubism_package_root/third_party/cubism/Core/lib/ios/Release-iphonesimulator-x86_64/libLive2DCubismCore.a"
    elif [[ "$triplet" == "arm64-ios-simulator" ]]; then
        rfvp_rust_target="aarch64-apple-ios-sim"
        cubism_core_lib="$cubism_package_root/third_party/cubism/Core/lib/ios/Release-iphonesimulator-arm64/libLive2DCubismCore.a"
    fi
    godot_cpp_lib="$(resolve_ios_godot_cpp_lib "$vcpkg_triplet_root" "$godot_cpp_arch" "$BUILD_TYPE_LOWER" || true)"
    if [[ ! -f "$godot_cpp_lib" ]]; then
        echo "Error: missing Godot C++ iOS archive for $BUILD_TYPE_LOWER build ($godot_cpp_arch)." >&2
        echo "       Expected an archive matching: $vcpkg_triplet_root/{lib,debug/lib}/libgodot-cpp.ios.*.$godot_cpp_arch.a" >&2
        exit 1
    fi
    libs=("$godot_cpp_lib" "${libs[@]}")
    libs+=("$cubism_core_lib")
    if [[ -f "$CMAKE_BUILD_DIR/bridge/rfvp_runtime/libaether_rfvp_runtime.a" ]]; then
        libs+=(
            "$CMAKE_BUILD_DIR/bridge/rfvp_runtime/libaether_rfvp_runtime.a"
            "$CMAKE_BUILD_DIR/bridge/rfvp_runtime/prepared/target/$rfvp_rust_target/$BUILD_TYPE_LOWER/librfvp.a"
        )
    fi

    # Renios ships a self-contained iOS Python/SDL/FFmpeg closure.  Godot's
    # exporter links only the extension archive and does not propagate its
    # CMake dependencies, so merge the official closure into that archive.
    # SDL2main and SDL2_test are deliberately excluded: they contain the
    # prototype's application entrypoint and test harness,
    # which must never be introduced into the Godot application.
    local renios_prebuilt=""
    if renios_enabled && renios_link_enabled; then
        renios_prebuilt="$(renios_prebuilt_root 2>/dev/null || true)"
        if [[ -n "$renios_prebuilt" ]]; then
            local renios_archive
            while IFS= read -r renios_archive; do
                [[ -f "$renios_archive" ]] || continue
                libs+=("$renios_archive")
            done < <(collect_renios_archives "$renios_prebuilt")
        else
            echo "Warning: Renios support requested but no native closure was found; iOS Ren'Py remains NOT_SUPPORTED" >&2
        fi
    fi

    while IFS= read -r lib; do
        # If Renios is enabled, prefer its iOS SDL/image/runtime archives over
        # identically named vcpkg archives.  Other vcpkg libraries remain in
        # the closure for Godot and the existing runtimes.
        if [[ -n "$renios_prebuilt" ]] && [[ -f "$renios_prebuilt/$(basename "$lib")" ]]; then
            continue
        fi
        libs+=("$lib")
    done < <(find "$vcpkg_lib_dir" -maxdepth 1 -name 'lib*.a' \
        ! -name 'libgodot-cpp*.a' \
        ! -name 'libSDL2main.a' | sort)

    # Some vcpkg ports (e.g. tiff on iOS) install static libraries as
    # <name>.framework bundles instead of lib<name>.a. Merge their archive
    # binaries too, otherwise consumers of those ports fail to link.
    while IFS= read -r framework; do
        fw_name="$(basename "$framework" .framework)"
        fw_binary="$framework/Versions/Current/$fw_name"
        if [[ ! -e "$fw_binary" ]]; then
            fw_binary="$framework/$fw_name"
        fi
        if [[ -f "$fw_binary" && "$(head -c 8 "$fw_binary" 2>/dev/null)" == '!<arch>'* ]]; then
            libs+=("$fw_binary")
        else
            echo "warning: skipping non-static framework: $framework" >&2
        fi
    done < <(find "$vcpkg_lib_dir" -maxdepth 1 -name '*.framework' | sort)

    local existing_libs=()
    local lib
    for lib in "${libs[@]}"; do
        if [[ -f "$lib" ]]; then
            existing_libs+=("$lib")
        else
            echo "warning: skipping missing optional static library: $lib" >&2
        fi
    done

    local tmp
    tmp="$(mktemp /tmp/aetherkiri-ios-static.XXXXXX).a"
    libtool -static -o "$tmp" "${existing_libs[@]}"
    mv "$tmp" "$output"
}

stage_force_load_plugin_archives() {
    local destination="$1"
    local source
    local resolved
    mkdir -p "$destination"
    for source in "${FORCE_LOAD_PLUGIN_SOURCES[@]}"; do
        # Renios supplies the iOS SDL archive in its merged native closure.
        # Do not copy/force-load the vcpkg SDL archive as well, otherwise the
        # final Xcode link sees duplicate SDL symbols.
        if renios_enabled && renios_link_enabled && [[ "$(basename "$source")" == "libSDL2.a" ]]; then
            continue
        fi
        resolved="$source"
        if [[ ! -f "$resolved" ]]; then
            resolved="$CMAKE_BUILD_DIR/$source"
        fi
        if [[ ! -f "$resolved" ]]; then
            resolved="$PROJECT_ROOT/$source"
        fi
        cp -f "$resolved" "$destination/" 2>/dev/null || true
    done
}

verify_exported_simulator_template_arch() {
    local export_root="$1"
    local arch="$2"
    local libgodot="$export_root/Aether.xcframework/ios-arm64_x86_64-simulator/libgodot.a"
    local info

    if [[ ! -f "$libgodot" ]]; then
        echo "Error: exported Godot simulator template is missing: $libgodot" >&2
        exit 1
    fi

    info="$(lipo -archs "$libgodot" 2>/dev/null || true)"
    if [[ " $info " != *" $arch "* ]]; then
        echo "Error: Godot iOS simulator export template does not contain '$arch'." >&2
        echo "       $libgodot" >&2
        echo "       architectures: ${info:-unknown}" >&2
        echo "       Install or build a Godot export template with an $arch simulator slice, or use --simulator-arch=x86_64." >&2
        exit 1
    fi
}

stage_ios_runtime_fonts() {
    local export_root="$1"
    local app_source_dir="$export_root/Aether"
    local font_dir="$app_source_dir/fonts"

    mkdir -p "$font_dir"
    if [[ -f "$RUNTIME_CJK_FONT_SOURCE" ]]; then
        cp -f "$RUNTIME_CJK_FONT_SOURCE" "$app_source_dir/default.otf"
        cp -f "$RUNTIME_CJK_FONT_SOURCE" "$font_dir/default.otf"
    else
        echo "Warning: runtime CJK font missing: $RUNTIME_CJK_FONT_SOURCE" >&2
    fi
    if [[ -f "$RUNTIME_SYMBOL_FONT_SOURCE" ]]; then
        cp -f "$RUNTIME_SYMBOL_FONT_SOURCE" "$font_dir/symbols.ttf"
    else
        echo "Warning: runtime symbol font missing: $RUNTIME_SYMBOL_FONT_SOURCE" >&2
    fi
}

patch_ios_runtime_font_resources() {
    local project_file="$1"
    if [[ ! -f "$project_file" ]]; then
        return
    fi
    if grep -Fq 'A3F001000000000000000002 /* default.otf */' "$project_file"; then
        return
    fi

    perl -0pi -e 's@(/\* Begin PBXBuildFile section \*/\n)@$1\t\tA3F001000000000000000001 /* default.otf in Resources */ = {isa = PBXBuildFile; fileRef = A3F001000000000000000002 /* default.otf */; };\n\t\tA3F001000000000000000003 /* fonts in Resources */ = {isa = PBXBuildFile; fileRef = A3F001000000000000000004 /* fonts */; };\n@' "$project_file"
    perl -0pi -e 's@(/\* Begin PBXFileReference section \*/\n)@$1\t\tA3F001000000000000000002 /* default.otf */ = {isa = PBXFileReference; lastKnownFileType = file; path = default.otf; sourceTree = "<group>"; };\n\t\tA3F001000000000000000004 /* fonts */ = {isa = PBXFileReference; lastKnownFileType = folder; path = fonts; sourceTree = "<group>"; };\n@' "$project_file"
    perl -0pi -e 's@(\t\tD0BCFE4118AEBDA2004A7AAE /\* Aether \*/ = \{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = \(\n)@$1\t\t\t\tA3F001000000000000000002 /* default.otf */,\n\t\t\t\tA3F001000000000000000004 /* fonts */,\n@' "$project_file"
    perl -0pi -e 's@(\t\tD0BCFE3218AEBDA2004A7AAE /\* Resources \*/ = \{\n\t\t\tisa = PBXResourcesBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = \(\n)@$1\t\t\t\tA3F001000000000000000001 /* default.otf in Resources */,\n\t\t\t\tA3F001000000000000000003 /* fonts in Resources */,\n@' "$project_file"
}

patch_ios_renios_resources() {
    local project_file="$1"
    local export_root="$2"
    local resource_dir="$export_root/Aether/renios"
    local framework="$export_root/Aether/Frameworks/MetalANGLE.xcframework"
    [[ -f "$project_file" ]] || return 0
    [[ -d "$resource_dir" ]] || return 0

    # Add the Renios resources as a folder reference.  This keeps the
    # original names/relative paths intact while avoiding the prototype's
    # application-entrypoint source files.
    if ! grep -Fq 'A3F002000000000000000003 /* renios in Resources */' "$project_file"; then
        perl -0pi -e 's@(/\* Begin PBXBuildFile section \*/\n)@$1\t\tA3F002000000000000000003 /* renios in Resources */ = {isa = PBXBuildFile; fileRef = A3F002000000000000000004 /* renios */; };\n@' "$project_file"
        perl -0pi -e 's@(/\* Begin PBXFileReference section \*/\n)@$1\t\tA3F002000000000000000004 /* renios */ = {isa = PBXFileReference; lastKnownFileType = folder; path = renios; sourceTree = "<group>"; };\n@' "$project_file"
        perl -0pi -e 's@(\t\tD0BCFE4118AEBDA2004A7AAE /\* Aether \*/ = \{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = \(\n)@$1\t\t\t\tA3F002000000000000000004 /* renios */,\n@' "$project_file"
        perl -0pi -e 's@(\t\tD0BCFE3218AEBDA2004A7AAE /\* Resources \*/ = \{\n\t\t\tisa = PBXResourcesBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = \(\n)@$1\t\t\t\tA3F002000000000000000003 /* renios in Resources */,\n@' "$project_file"
        if ! grep -Fq 'A3F002000000000000000004 /* renios */,' "$project_file"; then
            # Godot's object IDs are stable in current exports, but retain a
            # semantic fallback for a future exporter that renumbers them.
            perl -0pi -e 's@(\n\t\t[0-9A-F]+ /\* [^*]+ \*/ = \{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = \(\n)@$1\t\t\t\tA3F002000000000000000004 /* renios */,\n@'s "$project_file"
        fi
        if ! grep -Fq 'A3F002000000000000000003 /* renios in Resources */,' "$project_file"; then
            perl -0pi -e 's@(\n\t\t[0-9A-F]+ /\* [^*]+ \*/ = \{\n\t\t\tisa = PBXResourcesBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = \(\n)@$1\t\t\t\tA3F002000000000000000003 /* renios in Resources */,\n@'s "$project_file"
        fi
    fi

    # MetalANGLE is a dynamic framework in the official Renios package.  It
    # must be linked and embedded, rather than copied as an opaque resource.
    # The static Renios closure is already folded into libaether_kiri_godot.a.
    if [[ -d "$framework" ]] && ! grep -Fq 'A3F003000000000000000003 /* MetalANGLE.xcframework in Frameworks */' "$project_file"; then
        perl -0pi -e 's@(/\* Begin PBXBuildFile section \*/\n)@$1\t\tA3F003000000000000000003 /* MetalANGLE.xcframework in Frameworks */ = {isa = PBXBuildFile; fileRef = A3F003000000000000000004 /* MetalANGLE.xcframework */; };\n\t\tA3F003000000000000000005 /* MetalANGLE.xcframework in Embed Frameworks */ = {isa = PBXBuildFile; fileRef = A3F003000000000000000004 /* MetalANGLE.xcframework */; settings = {ATTRIBUTES = (CodeSignOnCopy, RemoveHeadersOnCopy, ); }; };\n@' "$project_file"
        perl -0pi -e 's@(/\* Begin PBXFileReference section \*/\n)@$1\t\tA3F003000000000000000004 /* MetalANGLE.xcframework */ = {isa = PBXFileReference; lastKnownFileType = wrapper.xcframework; path = Frameworks/MetalANGLE.xcframework; sourceTree = "<group>"; };\n@' "$project_file"
        # Godot's export project has an Aether group, a framework phase, and
        # an Embed Frameworks phase.  If a future exporter changes those IDs,
        # leave the resource bundle in place and retain NOT_SUPPORTED rather
        # than guessing at an unsafe Xcode project mutation.
        perl -0pi -e 's@(\t\tD0BCFE4118AEBDA2004A7AAE /\* Aether \*/ = \{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = \(\n)@$1\t\t\t\tA3F003000000000000000004 /* MetalANGLE.xcframework */,\n@' "$project_file"
        # Add the framework to the first framework phase and existing embed
        # phase by matching their semantic comments, not generated IDs.
        perl -0pi -e 's@(\/\* Begin PBXFrameworksBuildPhase section \*\/\n.*?files = \(\n)@$1\t\t\t\tA3F003000000000000000003 /* MetalANGLE.xcframework in Frameworks */,\n@'s "$project_file"
        perl -0pi -e 's@(\/\* Begin PBXCopyFilesBuildPhase section \*\/\n.*?files = \(\n)@$1\t\t\t\tA3F003000000000000000005 /* MetalANGLE.xcframework in Embed Frameworks */,\n@'s "$project_file"
        if ! grep -Fq 'A3F003000000000000000004 /* MetalANGLE.xcframework */,' "$project_file"; then
            perl -0pi -e 's@(\n\t\t[0-9A-F]+ /\* [^*]+ \*/ = \{\n\t\t\tisa = PBXGroup;\n\t\t\tchildren = \(\n)@$1\t\t\t\tA3F003000000000000000004 /* MetalANGLE.xcframework */,\n@'s "$project_file"
        fi
        if ! grep -Fq 'A3F003000000000000000003 /* MetalANGLE.xcframework in Frameworks */,' "$project_file"; then
            perl -0pi -e 's@(\n\t\t[0-9A-F]+ /\* [^*]+ \*/ = \{\n\t\t\tisa = PBXFrameworksBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tfiles = \(\n)@$1\t\t\t\tA3F003000000000000000003 /* MetalANGLE.xcframework in Frameworks */,\n@'s "$project_file"
        fi
        if ! grep -Fq 'A3F003000000000000000005 /* MetalANGLE.xcframework in Embed Frameworks */,' "$project_file"; then
            perl -0pi -e 's@(\n\t\t[0-9A-F]+ /\* [^*]+ \*/ = \{\n\t\t\tisa = PBXCopyFilesBuildPhase;\n\t\t\tbuildActionMask = 2147483647;\n\t\t\tdstPath = "";\n\t\t\tdstSubfolderSpec = 10;\n\t\t\tfiles = \(\n)@$1\t\t\t\tA3F003000000000000000005 /* MetalANGLE.xcframework in Embed Frameworks */,\n@'s "$project_file"
        fi
    fi
}

patch_ios_export_project() {
    local project_file="$1/project.pbxproj"
    local export_root
    export_root="$(dirname "$1")"
    local dummy_cpp="$export_root/Aether/dummy.cpp"
    local info_plist="$export_root/Aether/Aether-Info.plist"
    local arch="$2"
    local export_build_type="$3"
    local flags
    flags='$(LD_CLASSIC_$(XCODE_VERSION_ACTUAL)) -Wl,-U,_aether_kiri_library_init'
    flags+=" -Wl,-force_load,Aether/bin/ios/$export_build_type/$IOS_SDK_COMPAT_ARCHIVE"
    local archive
    for archive in "${FORCE_LOAD_PLUGIN_ARCHIVES[@]}"; do
        if renios_enabled && renios_link_enabled && [[ "$archive" == "libSDL2.a" ]]; then
            continue
        fi
        flags+=" -Wl,-force_load,Aether/bin/ios/$export_build_type/$archive"
    done
    flags+=' -liconv -framework Accelerate -framework AudioToolbox -framework AVFoundation -framework CoreAudio -framework CoreBluetooth -framework CoreHaptics -framework CoreMedia -framework CoreMotion -framework CoreVideo -framework CoreServices -framework GameController -framework ImageIO -framework VideoToolbox -framework CoreGraphics -framework QuartzCore -framework Metal -framework MetalKit -framework OpenGLES -framework Security -framework StoreKit -framework SystemConfiguration -framework MobileCoreServices'
    if renios_enabled; then
        # The resource staging step places this official dynamic framework in
        # Aether/Frameworks and the PBX patch embeds it in the app bundle.
        flags+=' -F Aether/Frameworks -framework MetalANGLE'
    fi

    if [[ -f "$project_file" ]]; then
        FLAGS="$flags" perl -0pi -e 's/OTHER_LDFLAGS = "[^"]*";/"OTHER_LDFLAGS = \"" . $ENV{FLAGS} . "\";"/eg' "$project_file"
        # Automatic signing archives with a development identity first, then
        # Xcode re-signs the exported archive for App Store distribution.
        # Godot's explicit Apple Distribution value conflicts with Automatic.
        perl -0pi -e 's/CODE_SIGN_IDENTITY = "Apple Distribution";/CODE_SIGN_IDENTITY = "Apple Development";/g' "$project_file"
        if [[ "$arch" == "x86_64" ]]; then
            perl -0pi -e 's/ARCHS = "arm64";/ARCHS = "x86_64";/g' "$project_file"
            perl -0pi -e 's/VALID_ARCHS = "arm64 x86_64";/VALID_ARCHS = "x86_64";/g' "$project_file"
        else
            perl -0pi -e 's/ARCHS = "x86_64";/ARCHS = "arm64";/g' "$project_file"
            perl -0pi -e 's/VALID_ARCHS = "x86_64";/VALID_ARCHS = "arm64";/g' "$project_file"
        fi
        stage_ios_runtime_fonts "$export_root"
        patch_ios_runtime_font_resources "$project_file"
        patch_ios_renios_resources "$project_file" "$export_root"
    fi
    if [[ -f "$dummy_cpp" ]] && ! grep -Fq '__swift_FORCE_LOAD_$_swift_Builtin_float' "$dummy_cpp"; then
        cat >> "$dummy_cpp" <<'EOF'

extern "C" void aether_kiri_swift_builtin_float_force_load(void) __asm("__swift_FORCE_LOAD_$_swift_Builtin_float");
extern "C" void aether_kiri_swift_builtin_float_force_load(void) {}
EOF
    fi
    if [[ -f "$info_plist" ]]; then
        local bluetooth_purpose
        bluetooth_purpose="Aether uses Bluetooth to connect game controllers that you choose to use."
        /usr/libexec/PlistBuddy -c 'Set :UIFileSharingEnabled true' "$info_plist" 2>/dev/null || \
            /usr/libexec/PlistBuddy -c 'Add :UIFileSharingEnabled bool true' "$info_plist"
        /usr/libexec/PlistBuddy -c 'Set :LSSupportsOpeningDocumentsInPlace true' "$info_plist" 2>/dev/null || \
            /usr/libexec/PlistBuddy -c 'Add :LSSupportsOpeningDocumentsInPlace bool true' "$info_plist"
        /usr/libexec/PlistBuddy -c 'Set :UIStatusBarHidden false' "$info_plist" 2>/dev/null || \
            /usr/libexec/PlistBuddy -c 'Add :UIStatusBarHidden bool false' "$info_plist"
        /usr/libexec/PlistBuddy -c 'Set :UIViewControllerBasedStatusBarAppearance false' "$info_plist" 2>/dev/null || \
            /usr/libexec/PlistBuddy -c 'Add :UIViewControllerBasedStatusBarAppearance bool false' "$info_plist"
        /usr/libexec/PlistBuddy -c 'Set :SKIncludeConsumableInAppPurchaseHistory true' "$info_plist" 2>/dev/null || \
            /usr/libexec/PlistBuddy -c 'Add :SKIncludeConsumableInAppPurchaseHistory bool true' "$info_plist"
        /usr/libexec/PlistBuddy \
            -c "Set :NSBluetoothAlwaysUsageDescription $bluetooth_purpose" \
            "$info_plist" 2>/dev/null || \
            /usr/libexec/PlistBuddy \
                -c "Add :NSBluetoothAlwaysUsageDescription string $bluetooth_purpose" \
                "$info_plist"
    fi
}

package_ios_unsigned_ipa() {
    local export_dir="$1"
    local build_type_lower="$2"
    local config_cap="Release"
    if [[ "$build_type_lower" == "debug" ]]; then
        config_cap="Debug"
    fi
    local xcodeproj="$export_dir/Aether.xcodeproj"
    if [[ ! -d "$xcodeproj" ]]; then
        echo "Error: Xcode project not found at $xcodeproj" >&2
        return 1
    fi
    if ! command -v xcodebuild >/dev/null 2>&1; then
        echo "Error: xcodebuild not found. Cannot package unsigned .ipa." >&2
        return 1
    fi
    echo "==> Building unsigned iOS App ($config_cap) for Sideloading..."
    mkdir -p "$export_dir/build"
    local xcodebuild_args=(
        -project "$xcodeproj" \
        -scheme Aether \
        -configuration "$config_cap" \
        -sdk iphoneos \
        CODE_SIGN_IDENTITY="" \
        CODE_SIGNING_REQUIRED=NO \
        CODE_SIGNING_ALLOWED=NO \
        CONFIGURATION_BUILD_DIR="$export_dir/build"
    )
    if [[ "$build_type_lower" == "release" ]]; then
        xcodebuild_args+=(
            DEPLOYMENT_POSTPROCESSING=YES
            STRIP_INSTALLED_PRODUCT=YES
            STRIP_STYLE=all
            COPY_PHASE_STRIP=YES
            GCC_GENERATE_DEBUGGING_SYMBOLS=NO
            STRIP_SWIFT_SYMBOLS=YES
            DEAD_CODE_STRIPPING=YES
            GCC_SYMBOLS_PRIVATE_EXTERN=YES
            UNEXPORTED_SYMBOLS_FILE="$PROJECT_ROOT/cmake/ios_unexported_symbols.txt"
            'OTHER_LDFLAGS=$(inherited) -Wl,-dead_strip'
        )
    else
        xcodebuild_args+=(
            DEPLOYMENT_POSTPROCESSING=NO
            STRIP_INSTALLED_PRODUCT=NO
            COPY_PHASE_STRIP=NO
            GCC_GENERATE_DEBUGGING_SYMBOLS=YES
            DEBUG_INFORMATION_FORMAT=dwarf
        )
    fi
    xcodebuild build "${xcodebuild_args[@]}"

    local app_binary="$export_dir/build/Aether.app/Aether"
    if [[ ! -f "$app_binary" ]]; then
        echo "Error: iOS app executable not found: $app_binary" >&2
        return 1
    fi
    if [[ "$build_type_lower" == "release" ]]; then
        echo "==> Removing non-runtime symbols from iOS Release executable..."
        "$PROJECT_ROOT/tools/strip_runtime_symbols.sh" macho-executable "$app_binary"
    fi

    echo "==> Packaging into unsigned .ipa..."
    mkdir -p "$export_dir/Payload"
    rm -rf "$export_dir/Payload/Aether.app" "$export_dir/Aether-${config_cap}-Unsigned.ipa"
    cp -R "$export_dir/build/Aether.app" "$export_dir/Payload/"
    (cd "$export_dir" && zip -qry "Aether-${config_cap}-Unsigned.ipa" Payload)
    rm -rf "$export_dir/Payload" "$export_dir/build"
    echo "Unsigned IPA created: $export_dir/Aether-${config_cap}-Unsigned.ipa"
}

with_ios_only_gdextension() {
    local gdextension_file="$GODOT_APP_DIR/aether_kiri.gdextension"
    local backup_file
    backup_file="$(mktemp /tmp/aetherkiri-gdextension.XXXXXX)"

    cp "$gdextension_file" "$backup_file"
    restore_gdextension() {
        trap - RETURN
        cp "$backup_file" "$gdextension_file"
        rm -f "$backup_file"
    }
    trap restore_gdextension RETURN

    awk '
        BEGIN { skip = 0 }
        /^\[dependencies\]/ { skip = 1 }
        /^\[/ && $0 != "[dependencies]" { skip = 0 }
        skip && /^macos\./ { while (getline line && line !~ /^}/) {} ; next }
        !skip || !/^macos\./ { print }
    ' "$backup_file" | grep -v '^macos\.' > "$gdextension_file"

    "$GODOT_BIN" --headless --path "$GODOT_APP_DIR" \
        "$EXPORT_MODE" "$EXPORT_PRESET" "$PROJECT_ROOT/out/godot/ios/$BUILD_TYPE_LOWER/Aether.xcodeproj"
}

echo "==> Building native engine and Godot extension"
cmake_config_args=(
    -D "CMAKE_MAKE_PROGRAM=$CMAKE_MAKE_PROGRAM"
    -D "AETHERKIRI_ENABLE_INTERNAL=${AETHERKIRI_ENABLE_INTERNAL:-ON}"
    -D "AETHERKIRI_ENABLE_CODE_OBFUSCATION=${AETHERKIRI_ENABLE_CODE_OBFUSCATION:-OFF}"
    -D "AETHERKIRI_OBFUSCATOR_PLUGIN=${AETHERKIRI_OBFUSCATOR_PLUGIN:-}"
    -D "AETHERKIRI_OBFUSCATION_BUILD_ID=${AETHERKIRI_OBFUSCATION_BUILD_ID:-local}"
    -D "AETHERKIRI_ENABLE_RFVP=${AETHERKIRI_ENABLE_RFVP:-OFF}"
    # iOS links the Ren'Py mobile registration stub.  It deliberately returns
    # NOT_SUPPORTED until the Renios/Xcode bootstrap is wired.
    -D "AETHERKIRI_ENABLE_RENPY=${AETHERKIRI_ENABLE_RENPY:-OFF}"
)
if [[ -n "${RFVP_CARGO:-}" ]]; then
    cmake_config_args+=(-D "RFVP_CARGO=$RFVP_CARGO")
fi
if [[ -n "${RFVP_RUSTC:-}" ]]; then
    cmake_config_args+=(-D "RFVP_RUSTC=$RFVP_RUSTC")
fi
if [[ "${SKIP_VCPKG_INSTALL:-}" == "1" ]]; then
    if [[ ! -d "$VCPKG_ROOT/installed/$VCPKG_TRIPLET_DIR" ]]; then
        echo "Error: SKIP_VCPKG_INSTALL=1 but prebuilt vcpkg triplet is missing: $VCPKG_ROOT/installed/$VCPKG_TRIPLET_DIR" >&2
        exit 1
    fi
    mkdir -p "$CMAKE_BUILD_DIR"
    rm -rf "$CMAKE_BUILD_DIR/vcpkg_installed"
    ln -s "$VCPKG_ROOT/installed" "$CMAKE_BUILD_DIR/vcpkg_installed"
    cmake_config_args+=(
        -D "VCPKG_MANIFEST_INSTALL=OFF"
        -D "VCPKG_INSTALLED_DIR=$CMAKE_BUILD_DIR/vcpkg_installed"
    )
fi

cmake --preset "$CMAKE_CONFIG_PRESET" --fresh "${cmake_config_args[@]}"
cmake --build --preset "$CMAKE_BUILD_PRESET" -- -j"$PARALLEL_JOBS"

mkdir -p "$GODOT_BIN_DIR"
cp -f "$CMAKE_BUILD_DIR/abi/libengine_api.a" "$GODOT_BIN_DIR/" 2>/dev/null || true
cp -f "$CMAKE_BUILD_DIR/bridge/godot_extension/libaether_kiri_godot.a" "$GODOT_BIN_DIR/" 2>/dev/null || true
build_ios_sdk_compat_archive "$GODOT_BIN_DIR/$IOS_SDK_COMPAT_ARCHIVE" "$VCPKG_TRIPLET_DIR"
stage_force_load_plugin_archives "$GODOT_BIN_DIR"
if [[ -f "$CMAKE_BUILD_DIR/bridge/godot_extension/libaether_kiri_godot.a" ]]; then
    combine_ios_static_extension "$GODOT_BIN_DIR/libaether_kiri_godot.a" "$VCPKG_TRIPLET_DIR"
fi
if [[ "$SIMULATOR" == true ]]; then
    GODOT_EXPORT_BIN_DIR="$GODOT_APP_DIR/bin/ios/$BUILD_TYPE_LOWER"
    mkdir -p "$GODOT_EXPORT_BIN_DIR"
    cp -f "$CMAKE_BUILD_DIR/abi/libengine_api.a" "$GODOT_EXPORT_BIN_DIR/" 2>/dev/null || true
    cp -f "$GODOT_BIN_DIR/libaether_kiri_godot.a" "$GODOT_EXPORT_BIN_DIR/" 2>/dev/null || true
    cp -f "$GODOT_BIN_DIR/$IOS_SDK_COMPAT_ARCHIVE" "$GODOT_EXPORT_BIN_DIR/" 2>/dev/null || true
    stage_force_load_plugin_archives "$GODOT_EXPORT_BIN_DIR"
fi

if [[ ! -x "$GODOT_BIN" ]]; then
    echo "Warning: Godot not found at $GODOT_BIN; native libraries were staged only." >&2
elif [[ ! -f "$GODOT_EXPORT_TEMPLATE" ]]; then
    echo "Warning: Godot iOS export template missing at $GODOT_EXPORT_TEMPLATE; native libraries were staged only." >&2
else
    echo "==> Exporting Godot iOS project"
    mkdir -p "$PROJECT_ROOT/out/godot/ios/$BUILD_TYPE_LOWER"
    EXPORT_PRESET="iOS Debug"
    EXPORT_MODE="--export-debug"
    if [[ "$BUILD_TYPE_LOWER" == "release" ]]; then
        EXPORT_PRESET="iOS Release"
        EXPORT_MODE="--export-release"
    fi
    with_ios_only_gdextension
    if [[ "$SIMULATOR" == true ]]; then
        verify_exported_simulator_template_arch "$PROJECT_ROOT/out/godot/ios/$BUILD_TYPE_LOWER" "$SIMULATOR_ARCH"
    fi
    stage_renios_ios_resources "$PROJECT_ROOT/out/godot/ios/$BUILD_TYPE_LOWER"
    stage_force_load_plugin_archives "$PROJECT_ROOT/out/godot/ios/$BUILD_TYPE_LOWER/Aether/bin/ios/$BUILD_TYPE_LOWER"
    cp -f "$GODOT_BIN_DIR/$IOS_SDK_COMPAT_ARCHIVE" "$PROJECT_ROOT/out/godot/ios/$BUILD_TYPE_LOWER/Aether/bin/ios/$BUILD_TYPE_LOWER/" 2>/dev/null || true
    PATCH_ARCH="arm64"
    if [[ "$SIMULATOR" == true ]]; then
        PATCH_ARCH="$SIMULATOR_ARCH"
    fi
    patch_ios_export_project "$PROJECT_ROOT/out/godot/ios/$BUILD_TYPE_LOWER/Aether.xcodeproj" "$PATCH_ARCH" "$BUILD_TYPE_LOWER"
    if [[ "$PACKAGE_IPA" == true && "$SIMULATOR" == false ]]; then
        package_ios_unsigned_ipa "$PROJECT_ROOT/out/godot/ios/$BUILD_TYPE_LOWER" "$BUILD_TYPE_LOWER"
    elif [[ "$PACKAGE_IPA" == true && "$SIMULATOR" == true ]]; then
        echo "[WARN] --package-ipa is ignored when building for simulator."
    fi
fi

echo "iOS build output: $PROJECT_ROOT/out/godot/ios/$BUILD_TYPE_LOWER"
