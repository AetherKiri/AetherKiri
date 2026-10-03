#!/usr/bin/env bash
# Validate the iOS Renios link/bundle boundary without requiring Xcode.  A
# downloaded/staged archive is optional; when provided this also checks the
# complete native closure and the dynamic framework input.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_script="$repo_root/scripts/build_ios.sh"
launcher_probe="$repo_root/tools/test_renios_ios_launcher.sh"
[[ -x "$build_script" ]] || { echo "iOS build script is not executable" >&2; exit 1; }
[[ -f "$launcher_probe" ]] || { echo "Renios launcher probe is missing" >&2; exit 1; }
bash -n "$build_script"
bash -n "$launcher_probe"

grep -Fq 'AETHERKIRI_RENPY_MOBILE_ROOT' "$build_script"
grep -Fq 'collect_renios_archives' "$build_script"
grep -Fq 'libaether_renpy_runtime.a' "$build_script"
grep -Fq 'stage_renios_ios_resources' "$build_script"
grep -Fq 'libSDL2main.a|libSDL2_test.a' "$build_script"
grep -Fq 'MetalANGLE.xcframework' "$build_script"
# The Godot iOS build must not grow a second UIKit/SDL application entrypoint.
if grep -Eq '^[[:space:]]*(SDL_UIKitRunApp|UIApplicationMain)[[:space:]]*\(' "$build_script"; then
    echo "build_ios.sh introduced a second iOS application entrypoint" >&2
    exit 1
fi

mobile_root="${RENPY_MOBILE_TEST_ROOT:-}"
if [[ -z "$mobile_root" ]]; then
    bash "$launcher_probe"
    echo "Renios iOS bundle boundary validation ok (archive inspection not requested)"
    exit 0
fi

bash "$launcher_probe" "$mobile_root" "${RENPY_MOBILE_TEST_CONFIG:-release}"

prototype="$mobile_root/renios/prototype"
prebuilt="$prototype/prebuilt/${RENPY_MOBILE_TEST_CONFIG:-release}"
[[ -d "$prebuilt" ]] || { echo "missing Renios prebuilt directory: $prebuilt" >&2; exit 1; }
[[ -f "$prototype/Frameworks/MetalANGLE.xcframework/Info.plist" ]] || {
    echo "missing official MetalANGLE framework metadata" >&2
    exit 1
}
for archive in librenpython.a librenpy.a libpython3.12.a libSDL2.a libSDL2_image.a; do
    [[ -f "$prebuilt/$archive" ]] || { echo "missing Renios runtime archive: $archive" >&2; exit 1; }
done
for archive in "$prebuilt"/lib*.a; do
    [[ -f "$archive" ]] || continue
    case "$(basename "$archive")" in
        libSDL2main.a|libSDL2_test.a) continue ;;
    esac
done
echo "Renios iOS native closure/framework validation ok"
