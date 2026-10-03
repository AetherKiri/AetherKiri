#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
godot_bin="${GODOT_BIN:-}"
renpy_sdk="${RENPY_SDK:-${AETHERKIRI_RENPY_SDK_ROOT:-}}"
fixture_root="${AETHERKIRI_RENPY_FIXTURE:-$repo_root/tests/fixtures/renpy_smoke}"
timeout_seconds="${RENPY_GODOT_E2E_TIMEOUT:-90}"

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  echo "Run the built Godot host against the official Ren'Py SDK fixture."
  exit 0
fi
[[ -x "$godot_bin" ]] || { echo "GODOT_BIN must point to a built Godot executable." >&2; exit 1; }
[[ -d "$renpy_sdk" ]] || { echo "RENPY_SDK must point to an extracted SDK." >&2; exit 1; }
[[ -d "$fixture_root/game" ]] || { echo "Ren'Py fixture missing game/." >&2; exit 1; }

tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/aetherkiri-renpy-godot-e2e.XXXXXX")"
trap 'rm -rf "$tmp_root"' EXIT
fixture="$tmp_root/fixture"
cp -a "$fixture_root/." "$fixture"
rm -rf "$fixture/game/cache" "$fixture/game/saves" "$fixture/cache" "$fixture/saves"
find "$fixture/game" -type f \( -name '*.rpyc' -o -name '*.rpymc' \) -delete
rm -f "$fixture/game/aetherkiri-choice"
config="$tmp_root/config.json"
cat > "$config" <<JSON
{
  "game_path": "$fixture",
  "surface_size": [640, 360],
  "window_size": [1600, 900],
  "warmup_frames": 0,
  "measure_frames": 30,
  "capture_startup": false,
  "actions": [
    {"type":"wait_ms","duration_ms":10000,"label":"wait-first-frame","capture":false},
    {"type":"key","key_code":13,"unicode":13,"label":"open-choice","after_frames":120,"capture":true},
    {"type":"key","key_code":13,"unicode":13,"label":"choose-continue","after_frames":0,"capture":true}
  ]
}
JSON
log="$tmp_root/godot.log"
set +e
SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-dummy}" SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}" RENPY_RENDERER="${RENPY_RENDERER:-sw}" \
AETHERKIRI_RENPY_SDK_ROOT="$renpy_sdk" AETHERKIRI_CLI_PROBE_SCRIPT="res://scripts/step_render_probe.gd" \
AETHERKIRI_TEST_CONFIG="$config" timeout -k 5s "${timeout_seconds}s" \
"$godot_bin" --headless --path "$repo_root/apps/godot_app" --verbose >"$log" 2>&1
rc=$?
set -e
cat "$log"
[[ "$rc" -eq 0 ]] || { echo "Godot Ren'Py E2E exited with status $rc" >&2; exit "$rc"; }
grep -F 'step probe fps=' "$log" >/dev/null
grep -F 'AKRF1 RGBA bridge' "$log" >/dev/null
grep -F 'step 02 label=choose-continue' "$log" >/dev/null
marker="$fixture/game/aetherkiri-choice"
[[ -f "$marker" ]] || { echo "Ren'Py fixture did not record a menu choice." >&2; exit 1; }
choice="$(<"$marker")"
[[ "$choice" == "continue" ]] || { echo "Unexpected Ren'Py menu choice: $choice" >&2; exit 1; }
echo "Ren'Py Godot E2E ok: frame=640x360, display=1600x900, renderer=AKRF1 RGBA bridge, choice=$choice"
