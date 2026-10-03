#!/usr/bin/env bash
# Stage the official Ren'Py RAPT Android inputs into a Godot Android export.
#
# RAPT is an SDLActivity application template. It must not be merged as the
# exported app's manifest or launched as a second Activity: Godot owns the
# process, window, and SDL lifecycle. This script therefore packages the
# official Java/resources as inspectable assets, stages only the arm64 native
# payload in jniLibs, and installs a tiny host-owned JNI bridge. The provider
# remains NOT_SUPPORTED until the lifecycle, surface, input, and renderer
# handoff is implemented.
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage: tools/stage_renpy_android_support.sh --mobile-root PATH --godot-build PATH

Stage the pinned official RAPT Android support package into an existing Godot
Android build template. The RAPT PythonSDLActivity is stored under APK assets
and is never added to the manifest, so this command cannot create a second
Activity. An optional game-specific private/assets directory can be supplied
with --private-assets.

Options:
  --mobile-root PATH       Root containing rapt/prototype/renpyandroid
  --godot-build PATH       Extracted Godot Android build template directory
  --private-assets PATH    Optional directory or private.mp3 file to copy into
                           the RAPT private asset staging area (default: none)
  -h, --help               Show this help
USAGE
}

mobile_root=""
godot_build=""
private_assets=""

while (($#)); do
    case "$1" in
        --mobile-root)
            (($# >= 2)) || { echo "--mobile-root requires a value" >&2; exit 2; }
            mobile_root="$2"
            shift 2
            ;;
        --godot-build)
            (($# >= 2)) || { echo "--godot-build requires a value" >&2; exit 2; }
            godot_build="$2"
            shift 2
            ;;
        --private-assets)
            (($# >= 2)) || { echo "--private-assets requires a value" >&2; exit 2; }
            private_assets="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ -z "$mobile_root" || -z "$godot_build" ]]; then
    echo "--mobile-root and --godot-build are required" >&2
    usage >&2
    exit 2
fi

rapt_root="$mobile_root/rapt"
rapt_main="$rapt_root/prototype/renpyandroid/src/main"
rapt_native="$rapt_main/jniLibs/arm64-v8a/librenpython.so"

[[ -d "$rapt_main" ]] || {
    echo "RAPT Android module is missing: $rapt_main" >&2
    exit 1
}
[[ -f "$rapt_native" ]] || {
    echo "RAPT arm64 native library is missing: $rapt_native" >&2
    exit 1
}
[[ -d "$godot_build" ]] || {
    echo "Godot Android build template is missing: $godot_build" >&2
    exit 1
}
if [[ -n "$private_assets" && ! -e "$private_assets" ]]; then
    echo "private assets path is missing: $private_assets" >&2
    exit 1
fi

is_elf_file() {
    local path="$1"
    [[ -f "$path" ]] || return 1
    [[ "$(dd if="$path" bs=4 count=1 2>/dev/null | LC_ALL=C od -An -tx1 | tr -d ' \\n')" == "7f454c46" ]]
}

is_elf_file "$rapt_native" || {
    echo "RAPT arm64 native library is not an ELF file: $rapt_native" >&2
    exit 1
}

main_src="$godot_build/src/main"
asset_root="$main_src/assets/renpy_mobile"
asset_rapt="$asset_root/rapt"
asset_private="$asset_root/private"
private_archive="$asset_rapt/private.mp3"
jni_root="$main_src/jniLibs/arm64-v8a"
java_root="$main_src/java/org/github/krkr2/aetherkiri"
java_sdl_root="$main_src/java/org/libsdl/app"
java_renpy_root="$main_src/java/org/renpy/android"

mkdir -p "$asset_rapt" "$asset_private" "$jni_root" "$java_root"

# This source is part of AetherKiri, not an official RAPT Activity. It binds
# the existing Godot Activity to engine_api without creating a second owner.
cp -f "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/bridge/renpy_runtime/android/RenPyMobileBridge.java" \
    "$java_root/RenPyMobileBridge.java"

# Compile only the host-owned callback signatures. The official RAPT Java
# sources remain assets and its PythonSDLActivity manifest is never merged;
# compiling the full Activity would create a second SDL singleton. Refuse to
# overwrite an unrelated class if a Godot template starts shipping one.
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
install_host_shim() {
    local source="$1" destination="$2" marker="$3"
    mkdir -p "$(dirname "$destination")"
    if [[ -f "$destination" ]] && ! grep -Fq "$marker" "$destination"; then
        echo "Refusing to overwrite unrelated Android Java class: $destination" >&2
        exit 1
    fi
    cp -f "$source" "$destination"
}
install_host_shim \
    "$repo_root/bridge/renpy_runtime/android/org/libsdl/app/SDLActivity.java" \
    "$java_sdl_root/SDLActivity.java" \
    'renpy-sdl-host-shim-v1'
install_host_shim \
    "$repo_root/bridge/renpy_runtime/android/org/renpy/android/PythonSDLActivity.java" \
    "$java_renpy_root/PythonSDLActivity.java" \
    'renpy-python-host-shim-v1'

# The source tree is deliberately copied below assets rather than src/main/java
# or src/main/res. RAPT's manifest names PythonSDLActivity as its launcher;
# merging it into Godot would create a second Activity and two SDL owners.
rm -rf "$asset_rapt/java" "$asset_rapt/res" "$asset_rapt/templates"
mkdir -p "$asset_rapt/java" "$asset_rapt/res"
cp -a "$rapt_main/java/." "$asset_rapt/java/"
if [[ -d "$rapt_main/res" ]]; then
    cp -a "$rapt_main/res/." "$asset_rapt/res/"
fi
# Keep Kotlin templates too if a future official RAPT package adds them. The
# current 8.5.3 package is Java-only, but the asset boundary is language
# neutral and does not risk compiling a second Activity.
if find "$rapt_main" -type f -name '*.kt' -print -quit | grep -q .; then
    mkdir -p "$asset_rapt/kotlin"
    while IFS= read -r -d '' kotlin_file; do
        relative="${kotlin_file#"$rapt_main/"}"
        mkdir -p "$asset_rapt/kotlin/$(dirname "$relative")"
        cp -f "$kotlin_file" "$asset_rapt/kotlin/$relative"
    done < <(find "$rapt_main" -type f -name '*.kt' -print0)
fi
if [[ -f "$rapt_main/AndroidManifest.xml" ]]; then
    cp -f "$rapt_main/AndroidManifest.xml" "$asset_rapt/AndroidManifest.xml"
fi
if [[ -f "$rapt_root/prototype/renpyandroid/build.gradle" ]]; then
    cp -f "$rapt_root/prototype/renpyandroid/build.gradle" "$asset_rapt/build.gradle"
fi
if [[ -d "$rapt_root/templates" ]]; then
    cp -a "$rapt_root/templates" "$asset_rapt/templates"
fi

# Keep any official RAPT app assets and any optional game-specific private
# payload visible to the eventual extractor. RAPT does not ship a game
# private.mp3 in its support archive, so emit a deterministic marker instead
# of pretending that the provider can launch a game without one.
if [[ -d "$rapt_root/prototype/app/src/main/assets" ]]; then
    rm -rf "$asset_rapt/app-assets"
    mkdir -p "$asset_rapt/app-assets"
    cp -a "$rapt_root/prototype/app/src/main/assets/." "$asset_rapt/app-assets/"
fi
find "$asset_private" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
rm -f "$private_archive"
if [[ -d "$rapt_main/private" ]]; then
    cp -a "$rapt_main/private/." "$asset_private/"
fi
if [[ -d "$private_assets" ]]; then
    cp -a "$private_assets/." "$asset_private/"
elif [[ -n "$private_assets" ]]; then
    cp -f "$private_assets" "$asset_private/$(basename "$private_assets")"
fi
if [[ -f "$asset_private/private.mp3" ]]; then
    # PythonSDLActivity's ResourceManager looks up private.mp3 at the asset
    # root. Keep the auditable private/ copy too, but expose this canonical
    # RAPT filename for the future host-owned extractor.
    cp -f "$asset_private/private.mp3" "$private_archive"
fi
if [[ -z "$(find "$asset_private" -mindepth 1 -print -quit)" ]]; then
    cat > "$asset_private/README.txt" <<'PRIVATE_EOF'
This directory is reserved for the Ren'Py game's RAPT private payload.
The official RAPT support archive does not contain game-specific private.mp3
data. Supply --private-assets when staging a built Ren'Py game. AetherKiri
keeps runtime=renpy NOT_SUPPORTED until extraction, lifecycle, input, and
rendering are wired to the existing Godot Activity.
PRIVATE_EOF
fi

cp -f "$rapt_native" "$jni_root/librenpython.so"
chmod 755 "$jni_root/librenpython.so"

# Record the exact staged paths and the archive metadata when available. This
# is consumed by package smoke tests and makes an APK inspection auditable.
rapt_checksum=""
if [[ -f "$rapt_root/.aetherkiri-sha256" ]]; then
    rapt_checksum="$(awk 'NR == 1 { print $1; exit }' "$rapt_root/.aetherkiri-sha256")"
elif [[ -f "$rapt_root/hash.txt" ]]; then
    rapt_checksum="$(tr -d '[:space:]' < "$rapt_root/hash.txt")"
fi
{
    printf 'renpy_version=8.5.3\n'
    printf 'rapt_checksum=%s\n' "$rapt_checksum"
    printf 'playable=false\n'
    printf 'activity_template=assets/renpy_mobile/rapt/java/org/renpy/android/PythonSDLActivity.java\n'
    printf 'gradle_template=assets/renpy_mobile/rapt/build.gradle\n'
    printf 'native_library=lib/arm64-v8a/librenpython.so\n'
    printf 'private_assets=assets/renpy_mobile/private\n'
    printf 'private_archive=%s\n' "$( [[ -f "$private_archive" ]] && printf 'assets/renpy_mobile/rapt/private.mp3' || true )"
    printf 'manifest_merged=false\n'
    printf 'java_host_shims=org/libsdl/app/SDLActivity.java,org/renpy/android/PythonSDLActivity.java\n'
} > "$asset_root/manifest.properties"

echo "Ren'Py Android support staged into $godot_build"
echo "  official Java/resources: $asset_rapt"
echo "  private assets:          $asset_private"
echo "  arm64 native library:    $jni_root/librenpython.so"
echo "  playable:                false"
