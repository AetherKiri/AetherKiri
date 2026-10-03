#!/usr/bin/env bash
# Inspect the official Renios launch contract without invoking Xcode or iOS.
# The probe deliberately reports whether the archive exposes only the
# blocking Python launcher; it never claims that this is a host-tick runtime.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mobile_root="${1:-${RENPY_MOBILE_TEST_ROOT:-${AETHERKIRI_RENPY_MOBILE_ROOT:-}}}"
configuration="${2:-${RENPY_MOBILE_TEST_CONFIG:-release}}"

if [[ -z "$mobile_root" ]]; then
    echo "Renios iOS launcher probe metadata validation ok (archive inspection not requested)"
    exit 0
fi

prototype="$mobile_root/renios/prototype"
prebuilt="$prototype/prebuilt/$configuration"
[[ -d "$prototype" ]] || { echo "missing Renios prototype: $prototype" >&2; exit 1; }
[[ -d "$prebuilt" ]] || { echo "missing Renios prebuilt directory: $prebuilt" >&2; exit 1; }

for archive in librenpython.a librenpy.a libpython3.12.a libSDL2.a libSDL2main.a; do
    [[ -f "$prebuilt/$archive" ]] || {
        echo "missing Renios launcher archive: $prebuilt/$archive" >&2
        exit 1
    }
done

has_symbol() {
    local archive="$1" symbol="$2"
    # Apple's strings only scans initialized data by default.  In the Renios
    # Mach-O archive that includes the archive symbol index (so
    # _launcher_main is found) but can skip the undefined _Py_RunMain entry in
    # the member object.  Prefer the symbol table, and retain a full-file
    # strings fallback for portable fixture archives and hosts without nm.
    if command -v nm >/dev/null 2>&1; then
        local symbols
        if symbols="$(nm "$archive" 2>/dev/null)"; then
            awk -v wanted="$symbol" '$NF == wanted { found = 1 } END { exit !found }' <<<"$symbols"
            return $?
        fi
    fi
    strings -a "$archive" | grep -Fqx "$symbol"
}

# librenpython.a exports the launcher, but its relocation set calls
# Py_RunMain. That is the blocking boundary we must split before host ticks.
has_symbol "$prebuilt/librenpython.a" _launcher_main || {
    echo "librenpython.a does not export _launcher_main" >&2
    exit 1
}
has_symbol "$prebuilt/librenpython.a" _Py_RunMain || {
    echo "librenpython.a does not expose the blocking _Py_RunMain dependency" >&2
    exit 1
}
# SDL_UIKitRunApp is implemented by SDL2 itself. The separate SDL2main
# archive is only the prototype's dummy-main shim and is intentionally
# excluded from the Godot extension archive. Keep both facts explicit.
has_symbol "$prebuilt/libSDL2.a" _SDL_UIKitRunApp || {
    echo "libSDL2.a does not contain the expected UIKit entrypoint" >&2
    exit 1
}
has_symbol "$prebuilt/libSDL2.a" _UIApplicationMain || {
    echo "libSDL2.a does not expose its UIKit UIApplicationMain dependency" >&2
    exit 1
}
if has_symbol "$prebuilt/libSDL2main.a" _SDL_UIKitRunApp; then
    echo "SDL2main unexpectedly owns the UIKit entrypoint" >&2
    exit 1
fi
grep -Fq 'SDL_UIKitRunApp' "$prototype/main.c" || {
    echo "Renios prototype main.c no longer documents its UIKit entrypoint" >&2
    exit 1
}

cat <<REPORT
Renios iOS launcher contract probe ok
  launcher_main: present in librenpython.a
  blocking dependency: Py_RunMain
  UIKit entrypoint: SDL2 export (prototype main.c intentionally omitted)
  UIKit launcher dependency: UIApplicationMain (non-reentrant)
  host-owned tick/frame/input API: unavailable
  required next step: rebuild/wrap librenpython with split init/tick/shutdown
REPORT
