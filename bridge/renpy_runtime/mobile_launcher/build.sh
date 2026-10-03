#!/usr/bin/env bash
# Check or install a host-owned Ren'Py mobile launcher fork.
#
# This script never transforms the official blocking launcher by itself.  It
# checks the upstream source locations, prints the exact renpy-build hooks, and
# optionally installs already-built artifacts only when they export the
# versioned host lifecycle ABI from include/renpy_mobile_launcher.h.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
contract_header="$repo_root/bridge/renpy_runtime/mobile_launcher/include/renpy_mobile_launcher.h"

usage() {
    cat <<'USAGE'
Usage:
  build.sh --check --renpy-build PATH
  build.sh --compile-contract --renpy-build PATH [--output-dir PATH]
  build.sh --check-python-patch --renpy-src PATH
  build.sh --apply-python-patch --renpy-src PATH
  build.sh --print-plan --renpy-build PATH
  build.sh --install --renpy-build PATH --stage PATH \
      [--android-so-arm64 PATH] [--android-so-armv7 PATH]
      [--android-so-x86_64 PATH] [--ios-debug-a PATH] [--ios-release-a PATH]

--check validates the official source layout and confirms that the current
launcher is still the blocking SDL_main/launcher_main implementation.
--source-build-plan prints the official renpy-build source-build commands,
required Ubuntu/toolchain inputs, and the exact RAPT/Renios artifact paths.
--check-renpy-build performs a strict, read-only preflight for those inputs; it
checks the Ubuntu/disk/toolchain prerequisites, source checkout, SDK archives,
and cooperative-loop patch applicability. It never runs the heavy build.
--compile-contract compiles the Android/iOS templates as contract-only objects;
those objects deliberately return NOT_IMPLEMENTED and are never packaged.
--check-python-patch validates the Ren'Py Python cooperative-loop patch against
an official Ren'Py source checkout. --apply-python-patch applies it in place
only after `git apply --check`; it refuses a dirty checkout. The patch is an
opt-in skeleton and does not make the runtime playable by itself.
--print-plan prints the upstream renpy-build commands and replacement paths.
--install copies caller-supplied, already-built lifecycle artifacts into the
staged RAPT/Renios tree after checking the required exported symbol names.
This script does not build Ren'Py and does not claim the resulting artifacts
are playable; the fork implementation and device/simulator tests remain
required.
USAGE
}

mode=""
renpy_build=""
renpy_src=""
stage=""
android_so_arm64=""
android_so_armv7=""
android_so_x86_64=""
ios_debug_a=""
ios_release_a=""
output_dir=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --check|--source-build-plan|--check-renpy-build|--compile-contract|--check-python-patch|--apply-python-patch|--print-plan|--install)
            [[ -z "$mode" ]] || { echo "choose one mode" >&2; exit 2; }
            mode="${1#--}"
            ;;
        --renpy-build)
            [[ $# -ge 2 ]] || { echo "--renpy-build requires a path" >&2; exit 2; }
            renpy_build="$2"; shift
            ;;
        --renpy-src)
            [[ $# -ge 2 ]] || { echo "--renpy-src requires a path" >&2; exit 2; }
            renpy_src="$2"; shift
            ;;
        --stage)
            [[ $# -ge 2 ]] || { echo "--stage requires a path" >&2; exit 2; }
            stage="$2"; shift
            ;;
        --output-dir)
            [[ $# -ge 2 ]] || { echo "--output-dir requires a path" >&2; exit 2; }
            output_dir="$2"; shift
            ;;
        --android-so|--android-so-arm64)
            [[ $# -ge 2 ]] || { echo "--android-so-arm64 requires a path" >&2; exit 2; }
            android_so_arm64="$2"; shift
            ;;
        --android-so-armv7)
            [[ $# -ge 2 ]] || { echo "--android-so-armv7 requires a path" >&2; exit 2; }
            android_so_armv7="$2"; shift
            ;;
        --android-so-x86_64)
            [[ $# -ge 2 ]] || { echo "--android-so-x86_64 requires a path" >&2; exit 2; }
            android_so_x86_64="$2"; shift
            ;;
        --ios-debug-a)
            [[ $# -ge 2 ]] || { echo "--ios-debug-a requires a path" >&2; exit 2; }
            ios_debug_a="$2"; shift
            ;;
        --ios-release-a)
            [[ $# -ge 2 ]] || { echo "--ios-release-a requires a path" >&2; exit 2; }
            ios_release_a="$2"; shift
            ;;
        -h|--help)
            usage; exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2; usage >&2; exit 2
            ;;
    esac
    shift
done

[[ -n "$mode" ]] || { usage >&2; exit 2; }
[[ -f "$contract_header" ]] || { echo "missing lifecycle contract: $contract_header" >&2; exit 1; }

# The source-build modes are deliberately separate from --install. They make
# the official renpy-build workflow reproducible and auditable without hiding a
# many-hour native rebuild inside the regular AetherKiri CI jobs.
renpy_build_source_preflight() {
    local strict="$1"
    local failures=0
    local required
    for required in \
        "$renpy_build/build.sh" \
        "$renpy_build/prepare.sh" \
        "$renpy_build/tasks/renpython.py" \
        "$renpy_build/tasks/rapt.py" \
        "$renpy_build/tasks/renios.py" \
        "$renpy_build/runtime/librenpython_android.c" \
        "$renpy_build/runtime/librenpython.c"; do
        if [[ ! -f "$required" ]]; then
            echo "missing renpy-build input: $required" >&2
            failures=$((failures + 1))
        fi
    done

    local source_root="${renpy_src:-$renpy_build/renpy}"
    if [[ ! -d "$source_root/.git" ]]; then
        echo "Ren'Py source checkout missing: $source_root" >&2
        echo "  run: (cd $renpy_build && ./prepare.sh)" >&2
        failures=$((failures + 1))
    else
        if [[ ! -f "$source_root/renpy/main.py" || ! -f "$source_root/renpy/execution.py" || ! -f "$source_root/renpy/display/core.py" ]]; then
            echo "Ren'Py source checkout is incomplete: $source_root" >&2
            failures=$((failures + 1))
        elif [[ -f "$python_patch" ]]; then
            if ! git -C "$source_root" apply --check "$python_patch" >/dev/null 2>&1; then
                echo "cooperative-loop patch does not apply to $source_root" >&2
                failures=$((failures + 1))
            else
                echo "cooperative-loop patch: applies cleanly"
            fi
        fi
    fi

    # renpy-build's current toolchain.py/run.py expect these exact archives.
    # They are intentionally checked rather than downloaded implicitly: iOS
    # SDK tarballs are licensed inputs and must be supplied by the builder.
    local archive
    local ndk_version="android-ndk-r29"
    if [[ -f "$renpy_build/renpybuild/run.py" ]]; then
        ndk_version="$(sed -n 's/.*c.var("ndk_version", "\([^"]*\)").*/\1/p' "$renpy_build/renpybuild/run.py" | head -1)"
        [[ -n "$ndk_version" ]] || ndk_version="android-ndk-r29"
    fi
    for archive in \
        "$renpy_build/tars/${ndk_version}-linux.zip" \
        "$renpy_build/tars/iPhoneOS.sdk.tar.gz" \
        "$renpy_build/tars/iPhoneSimulator.sdk.tar.gz"; do
        if [[ ! -f "$archive" ]]; then
            echo "missing licensed/toolchain archive: $archive" >&2
            failures=$((failures + 1))
        else
            echo "toolchain archive: $archive"
        fi
    done

    local command
    for command in bash git python3 curl tar unzip make cmake ninja pkg-config clang clang++; do
        if command -v "$command" >/dev/null 2>&1; then
            printf 'tool: %-12s %s\n' "$command" "$(command -v "$command")"
        else
            echo "missing host tool: $command" >&2
            failures=$((failures + 1))
        fi
    done
    if command -v uv >/dev/null 2>&1; then
        echo "tool: uv            $(command -v uv)"
    else
        echo "missing host tool: uv (install from https://astral.sh/uv)" >&2
        failures=$((failures + 1))
    fi

    local system_name
    system_name="$(uname -s)"
    if [[ "$system_name" != "Linux" ]]; then
        echo "renpy-build requires Ubuntu Linux; detected $system_name" >&2
        failures=$((failures + 1))
    else
        local ubuntu_id="" ubuntu_version=""
        if [[ -r /etc/os-release ]]; then
            # shellcheck disable=SC1091
            . /etc/os-release
            ubuntu_id="${ID:-}"
            ubuntu_version="${VERSION_ID:-}"
        fi
        if [[ "$ubuntu_id" == "ubuntu" && "$ubuntu_version" == "24.04" ]]; then
            echo "host: Ubuntu 24.04"
        else
            echo "renpy-build officially targets Ubuntu 24.04 (detected ${ubuntu_id:-unknown} ${ubuntu_version:-unknown})" >&2
            failures=$((failures + 1))
        fi
    fi

    local available_kib=0
    available_kib="$(df -Pk "$renpy_build" 2>/dev/null | awk 'NR==2 {print $4}')"
    if [[ "$available_kib" =~ ^[0-9]+$ ]]; then
        printf 'disk: %.1f GiB available (official minimum: 64 GiB)\n' "$(awk -v kib="$available_kib" 'BEGIN {print kib / 1024 / 1024}')"
        if (( available_kib < 64 * 1024 * 1024 )); then
            echo "insufficient free disk for renpy-build (need at least 64 GiB)" >&2
            failures=$((failures + 1))
        fi
    else
        echo "unable to determine free disk for $renpy_build" >&2
        failures=$((failures + 1))
    fi

    if [[ "$strict" == "1" && "$failures" -ne 0 ]]; then
        echo "Ren'Py source-build preflight failed with $failures issue(s)" >&2
        return 1
    fi
    return 0
}

renpy_build_source_plan() {
    local source_root="${renpy_src:-$renpy_build/renpy}"
    local artifact_root="$renpy_build/renpy"
    cat <<PLAN
Ren'Py native mobile source-build plan
  Build repository: $renpy_build
  Ren'Py source:   $source_root
  Source setup:    (cd $renpy_build && ./prepare.sh)
  Python patch:    $repo_root/bridge/renpy_runtime/mobile_launcher/patches/python/0001-cooperative-loop-skeleton.patch
  Patch check:     (cd $source_root && git apply --check $repo_root/bridge/renpy_runtime/mobile_launcher/patches/python/0001-cooperative-loop-skeleton.patch)
  Patch apply:     (cd $source_root && git apply $repo_root/bridge/renpy_runtime/mobile_launcher/patches/python/0001-cooperative-loop-skeleton.patch)

Official build commands (Ubuntu 24.04; heavy, opt-in)
  Android: (cd $renpy_build && ./build.sh --platform android rebuild rapt rapt-sdl2)
  iOS:     (cd $renpy_build && ./build.sh --platform ios rebuild renios)

Canonical artifacts produced by renpy-build
  Android arm64-v8a: $artifact_root/rapt/prototype/renpyandroid/src/main/jniLibs/arm64-v8a/librenpython.so
  Android armeabi-v7a: $artifact_root/rapt/prototype/renpyandroid/src/main/jniLibs/armeabi-v7a/librenpython.so
  Android x86_64:     $artifact_root/rapt/prototype/renpyandroid/src/main/jniLibs/x86_64/librenpython.so
  iOS release:        $artifact_root/renios/prototype/prebuilt/release/librenpython.a
  iOS debug:          $artifact_root/renios/prototype/prebuilt/debug/librenpython.a

AetherKiri staging command (only after a real lifecycle fork exports all seven
renpy_mobile_* symbols; the current upstream launcher remains blocking)
  $repo_root/bridge/renpy_runtime/mobile_launcher/build.sh --install --renpy-build $renpy_build --stage <staged-mobile-root> --android-so-arm64 <arm64-v8a/librenpython.so> --android-so-armv7 <armeabi-v7a/librenpython.so> --android-so-x86_64 <x86_64/librenpython.so> --ios-debug-a <debug/librenpython.a> --ios-release-a <release/librenpython.a>

Important: this plan does not assert mobile playability. A native fork must
export the host lifecycle ABI, then pass device/simulator gameplay E2E.
PLAN
}


python_patch="$repo_root/bridge/renpy_runtime/mobile_launcher/patches/python/0001-cooperative-loop-skeleton.patch"
if [[ "$mode" == "source-build-plan" || "$mode" == "check-renpy-build" ]]; then
    [[ -n "$renpy_build" ]] || { echo "--renpy-build is required" >&2; exit 2; }
    [[ -d "$renpy_build" ]] || { echo "renpy-build directory not found: $renpy_build" >&2; exit 1; }
    renpy_build_source_plan
    if [[ "$mode" == "check-renpy-build" ]]; then
        renpy_build_source_preflight 1
    fi
    exit 0
fi

if [[ "$mode" == "check-python-patch" || "$mode" == "apply-python-patch" ]]; then
    [[ -n "$renpy_src" ]] || { echo "--renpy-src is required" >&2; exit 2; }
    [[ -d "$renpy_src" ]] || { echo "Ren'Py source checkout not found: $renpy_src" >&2; exit 1; }
    [[ -f "$python_patch" ]] || { echo "missing cooperative-loop patch: $python_patch" >&2; exit 1; }
    git -C "$renpy_src" rev-parse --is-inside-work-tree >/dev/null 2>&1 || {
        echo "--renpy-src must be a git checkout" >&2; exit 1;
    }
    git -C "$renpy_src" apply --check "$python_patch"
    if [[ "$mode" == "check-python-patch" ]]; then
        echo "Ren'Py Python cooperative-loop patch applies cleanly"
        echo "  skeleton only: Python/SDL lifecycle integration remains required"
        exit 0
    fi
    if [[ -n "$(git -C "$renpy_src" status --porcelain --untracked-files=no)" ]]; then
        echo "refusing to apply cooperative-loop patch to a dirty Ren'Py checkout" >&2
        exit 1
    fi
    git -C "$renpy_src" apply "$python_patch"
    python3 -m py_compile "$renpy_src/renpy/main.py" \
        "$renpy_src/renpy/execution.py" "$renpy_src/renpy/display/core.py"
    echo "applied and syntax-checked Ren'Py Python cooperative-loop skeleton"
    echo "  this does not make the mobile runtime playable"
    exit 0
fi

[[ -n "$renpy_build" ]] || { echo "--renpy-build is required" >&2; exit 2; }
[[ -d "$renpy_build" ]] || { echo "renpy-build directory not found: $renpy_build" >&2; exit 1; }

runtime="$renpy_build/runtime"
tasks="$renpy_build/tasks/renpython.py"
renios_main="$renpy_build/renios/prototype/main.c"
for required in \
    "$runtime/librenpython_android.c" \
    "$runtime/librenpython.c" \
    "$runtime/jniwrapperstuff.h" \
    "$tasks" \
    "$renios_main"; do
    [[ -f "$required" ]] || { echo "missing official Ren'Py build input: $required" >&2; exit 1; }
done

# Keep this check source-based: it works on a checkout without requiring a
# host compiler or an Apple/Android SDK.
grep -Eq 'int[[:space:]]+SDL_main[[:space:]]*\(' "$runtime/librenpython_android.c" || {
    echo "official Android launcher no longer contains SDL_main; inspect before using this scaffold" >&2
    exit 1
}
grep -Eq 'Py_RunMain[[:space:]]*\(' "$runtime/librenpython_android.c" || {
    echo "official Android launcher no longer contains Py_RunMain; inspect before using this scaffold" >&2
    exit 1
}
grep -Eq 'launcher_main[[:space:]]*\(' "$runtime/librenpython.c" || {
    echo "official desktop/iOS launcher no longer contains launcher_main; inspect before using this scaffold" >&2
    exit 1
}
grep -Eq 'Py_RunMain[[:space:]]*\(' "$runtime/librenpython.c" || {
    echo "official desktop/iOS launcher no longer contains Py_RunMain; inspect before using this scaffold" >&2
    exit 1
}
grep -Eq 'SDL_RunApp|SDL_UIKitRunApp' "$renios_main" || {
    echo "Renios prototype entrypoint changed; inspect before using this scaffold" >&2
    exit 1
}

print_plan() {
    cat <<PLAN
Official inputs
  Android source: $runtime/librenpython_android.c
  iOS/desktop source: $runtime/librenpython.c
  JNI helper: $runtime/jniwrapperstuff.h
  Ren'Py build task: $tasks
  Renios prototype entrypoint: $renios_main

Required fork work (outside this scaffold)
  Add the symbols in $contract_header to the launcher source.
  Move Python/SDL setup out of SDL_main/launcher_main into init.
  Make tick/frame/input/pause/resume/shutdown return promptly; do not call
  Py_RunMain, SDL_main, SDL_RunApp, SDL_UIKitRunApp, or UIApplicationMain.

Official renpy-build hooks
  Android: (cd $renpy_build && ./build.sh --platform android rebuild renpython rapt rapt-sdl2)
  iOS:     (cd $renpy_build && ./build.sh --platform ios rebuild renpython renios)
  Android output (per ABI): tmp/install.android-*/lib/librenpython.so
  iOS output (per ABI):     tmp/install.ios-*/lib/librenpython.a

Replacement destinations
  Android arm64-v8a: $stage/rapt/prototype/renpyandroid/src/main/jniLibs/arm64-v8a/librenpython.so
  Android armeabi-v7a: $stage/rapt/prototype/renpyandroid/src/main/jniLibs/armeabi-v7a/librenpython.so
  Android x86_64:      $stage/rapt/prototype/renpyandroid/src/main/jniLibs/x86_64/librenpython.so
  iOS release:         $stage/renios/prototype/prebuilt/release/librenpython.a
  iOS debug:           $stage/renios/prototype/prebuilt/debug/librenpython.a
PLAN
}

if [[ "$mode" == "check" ]]; then
    echo "Ren'Py mobile launcher scaffold source check ok"
    echo "  official Android launcher remains blocking (SDL_main -> Py_RunMain)"
    echo "  official iOS/desktop launcher remains blocking (launcher_main -> Py_RunMain)"
    echo "  no artifact was built or installed"
    exit 0
fi

if [[ "$mode" == "compile-contract" ]]; then
    compiler="${CC:-cc}"
    output_dir="${output_dir:-$repo_root/out/renpy-mobile-contract}"
    mkdir -p "$output_dir"
    common=(-std=c11 -Wall -Wextra -Werror -fPIC -I"$repo_root/bridge/renpy_runtime/mobile_launcher/include" -DAETHERKIRI_RENPY_LIFECYCLE_CONTRACT_ONLY=1)
    "$compiler" "${common[@]}" -x c -c \
        "$repo_root/bridge/renpy_runtime/mobile_launcher/patches/android/librenpython_android_host.c.template" \
        -o "$output_dir/librenpython_android_host.o"
    "$compiler" "${common[@]}" -x c -c \
        "$repo_root/bridge/renpy_runtime/mobile_launcher/patches/ios/librenpython_ios_host.c.template" \
        -o "$output_dir/librenpython_ios_host.o"
    echo "compiled contract-only launcher objects in $output_dir"
    echo "these objects intentionally return RENPY_MOBILE_NOT_IMPLEMENTED and are not runtime artifacts"
    exit 0
fi

print_plan
[[ "$mode" == "print-plan" ]] && exit 0

[[ -n "$stage" ]] || { echo "--stage is required for --install" >&2; exit 2; }
[[ -n "$android_so_arm64" || -n "$android_so_armv7" || -n "$android_so_x86_64" ||
   -n "$ios_debug_a" || -n "$ios_release_a" ]] || {
    echo "--install requires at least one artifact path" >&2
    exit 2
}

required_symbols=(
    renpy_mobile_init renpy_mobile_tick renpy_mobile_frame renpy_mobile_input
    renpy_mobile_pause renpy_mobile_resume renpy_mobile_shutdown
)
check_symbols() {
    local artifact="$1"
    [[ -f "$artifact" ]] || { echo "artifact not found: $artifact" >&2; exit 1; }
    if strings "$artifact" | grep -Fq 'AETHERKIRI_RENPY_LIFECYCLE_STUB'; then
        echo "refusing contract-only lifecycle stub as runtime artifact: $artifact" >&2
        exit 1
    fi
    local format
    format="$(file -b "$artifact" 2>/dev/null || true)"
    for symbol in "${required_symbols[@]}"; do
        if [[ "$format" == *ELF* ]] && command -v nm >/dev/null 2>&1; then
            if ! nm -D --defined-only "$artifact" 2>/dev/null | awk '{print $3}' | grep -Fxq "$symbol"; then
                echo "artifact lacks required lifecycle export $symbol: $artifact" >&2
                exit 1
            fi
        elif ! strings "$artifact" | grep -Fqx "$symbol"; then
            # Linux nm cannot inspect the Mach-O universal archives emitted by
            # the iOS hook; strings is the portable fallback for those files.
            echo "artifact lacks required lifecycle export $symbol: $artifact" >&2
            exit 1
        fi
    done
}

install_one() {
    local source="$1" destination="$2"
    check_symbols "$source"
    mkdir -p "$(dirname "$destination")"
    install -m 0755 "$source" "$destination"
    echo "installed host lifecycle artifact: $destination"
}

# Each Android ABI is explicit so an arm64 binary cannot silently be copied
# into an armeabi-v7a or x86_64 slot.
if [[ -n "$android_so_arm64" ]]; then
    install_one "$android_so_arm64" "$stage/rapt/prototype/renpyandroid/src/main/jniLibs/arm64-v8a/librenpython.so"
fi
if [[ -n "$android_so_armv7" ]]; then
    install_one "$android_so_armv7" "$stage/rapt/prototype/renpyandroid/src/main/jniLibs/armeabi-v7a/librenpython.so"
fi
if [[ -n "$android_so_x86_64" ]]; then
    install_one "$android_so_x86_64" "$stage/rapt/prototype/renpyandroid/src/main/jniLibs/x86_64/librenpython.so"
fi
if [[ -n "$ios_release_a" ]]; then
    install_one "$ios_release_a" "$stage/renios/prototype/prebuilt/release/librenpython.a"
fi
if [[ -n "$ios_debug_a" ]]; then
    install_one "$ios_debug_a" "$stage/renios/prototype/prebuilt/debug/librenpython.a"
fi

echo "Artifacts installed, but playable mobile support is not asserted by this scaffold"
