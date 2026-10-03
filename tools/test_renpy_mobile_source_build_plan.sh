#!/usr/bin/env bash
# Validate the explicit Ren'Py native source-build plan without downloading
# toolchains or pretending that the current upstream launcher is playable.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
launcher="$repo_root/bridge/renpy_runtime/mobile_launcher/build.sh"
plan="$(mktemp)"
trap 'rm -f "$plan"' EXIT

bash "$launcher" --source-build-plan --renpy-build "$repo_root" >"$plan"
grep -F "./build.sh --platform android rebuild rapt rapt-sdl2" "$plan"
grep -F "./build.sh --platform ios rebuild renios" "$plan"
grep -F "librenpython.so" "$plan"
grep -F "librenpython.a" "$plan"
grep -F "does not assert mobile playability" "$plan"

# The repository checkout is intentionally not a renpy-build checkout. The
# strict preflight must refuse it instead of silently running a partial build.
if bash "$launcher" --check-renpy-build --renpy-build "$repo_root" >"$plan" 2>&1; then
    echo "strict preflight unexpectedly accepted the AetherKiri checkout" >&2
    exit 1
fi
grep -F "Ren'Py source-build preflight failed" "$plan"

echo "Ren'Py mobile source-build plan checks passed"
