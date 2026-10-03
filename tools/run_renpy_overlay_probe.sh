#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
renpy_sdk="${RENPY_SDK:-}"
renpy_bin="${RENPY_BIN:-}"
renderer="${RENPY_RENDERER:-gl2}"

usage() {
    cat <<'USAGE'
Usage: tools/run_renpy_overlay_probe.sh

Run a real Ren'Py SDK frame/input bridge probe. Set RENPY_SDK to an extracted
official SDK directory, or RENPY_BIN to its launcher executable. The probe
uses SDL's dummy video/audio drivers, so it does not need a desktop display.
USAGE
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    usage
    exit 0
fi
if [[ $# -ne 0 ]]; then
    printf 'unexpected argument: %s\n' "$1" >&2
    usage >&2
    exit 2
fi

if [[ -z "$renpy_bin" && -n "$renpy_sdk" ]]; then
    if [[ -x "$renpy_sdk/renpy.sh" ]]; then
        renpy_bin="$renpy_sdk/renpy.sh"
    elif [[ -x "$renpy_sdk/renpy" ]]; then
        renpy_bin="$renpy_sdk/renpy"
    else
        printf "Ren'Py launcher not found under RENPY_SDK: %s\n" "$renpy_sdk" >&2
        exit 1
    fi
fi
if [[ -z "$renpy_bin" ]]; then
    if command -v renpy.sh >/dev/null 2>&1; then
        renpy_bin="$(command -v renpy.sh)"
    elif command -v renpy >/dev/null 2>&1; then
        renpy_bin="$(command -v renpy)"
    else
        printf "Set RENPY_SDK to an extracted official Ren'Py SDK or RENPY_BIN to its launcher.\n" >&2
        exit 1
    fi
fi
if [[ ! -x "$renpy_bin" ]]; then
    printf "Ren'Py launcher is not executable: %s\n" "$renpy_bin" >&2
    exit 1
fi

tmp_root="$(mktemp -d "${TMPDIR:-/tmp}/aetherkiri-renpy-overlay.XXXXXX")"
cleanup() { rm -rf "$tmp_root"; }
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

project="$tmp_root/project"
mkdir -p "$project/game" "$tmp_root/home" "$tmp_root/config"
cp "$repo_root/bridge/renpy_runtime/python/aether_renpy_overlay.py" "$project/game/"
cat > "$project/game/options.rpy" <<'R'
define config.name = "AetherKiri bridge probe"
define config.version = "1.0"
define config.window_title = "AetherKiri bridge probe"
define config.screen_width = 640
define config.screen_height = 360
define config.developer = True
define config.has_sound = False
define config.has_music = False
define config.has_voice = False
define config.gl_enable = False
define config.performance_test = False

init python:
    import aether_renpy_overlay
    aether_renpy_overlay.install()
R
cat > "$project/game/script.rpy" <<'R'
label main_menu:
    jump start

label start:
    scene Solid("#194a72")
    with None
    menu:
        "Input bridge continue":
            $ open(renpy.config.gamedir + "/input-ok", "w").write("continue\n")
            $ renpy.quit()
        "Input bridge finish":
            $ open(renpy.config.gamedir + "/input-ok", "w").write("finish\n")
            $ renpy.quit()
R

frame="$tmp_root/frame.bin"
input="$tmp_root/input.json"
errors="$tmp_root/bridge-errors.log"
: > "$input"
sdk_root="$(cd "$(dirname "$renpy_bin")" && pwd)"
renpy_bin="$sdk_root/$(basename "$renpy_bin")"

(
    cd "$sdk_root"
    HOME="$tmp_root/home" XDG_CONFIG_HOME="$tmp_root/config" \
        RENPY_RENDERER="$renderer" AETHERKIRI_RENPY_OVERLAY=1 \
        AETHERKIRI_RENPY_FRAME="$frame" AETHERKIRI_RENPY_INPUT="$input" \
        AETHERKIRI_RENPY_ERROR="$errors" \
        "$renpy_bin" --compile "$project" quit >/dev/null
)

set +e
(
    cd "$sdk_root"
    HOME="$tmp_root/home" XDG_CONFIG_HOME="$tmp_root/config" \
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy RENPY_RENDERER="$renderer" \
        AETHERKIRI_RENPY_OVERLAY=1 AETHERKIRI_RENPY_FRAME="$frame" \
        AETHERKIRI_RENPY_INPUT="$input" AETHERKIRI_RENPY_ERROR="$errors" \
        timeout -k 1s "${RENPY_PROBE_TIMEOUT:-20}s" "$renpy_bin" "$project"
) >"$tmp_root/game.out" 2>&1 &
pid=$!
ready=0
for _ in $(seq 1 200); do
    if [[ -s "$frame" ]]; then ready=1; break; fi
    sleep 0.1
done
if [[ "$ready" -ne 1 ]]; then
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    printf 'overlay probe did not publish a frame\n' >&2
    cat "$tmp_root/game.out" >&2
    exit 1
fi

# The overlay fills Ren'Py 8.5.x KEYDOWN defaults when constructing an Event.
printf '%s\n%s\n' \
    '{"type":768,"attributes":{"key":1073741905}}' \
    '{"type":768,"attributes":{"key":13}}' \
    >> "$input"
wait "$pid"
rc=$?
set -e
if [[ "$rc" -ne 0 ]]; then
    printf "overlay probe Ren'Py process exited with status %s\n" "$rc" >&2
    cat "$tmp_root/game.out" >&2
    exit 1
fi

python3 - "$frame" "$project/game/input-ok" "$errors" <<'PY'
import pathlib
import struct
import sys

frame = pathlib.Path(sys.argv[1]).read_bytes()
marker = pathlib.Path(sys.argv[2]).read_text()
errors = pathlib.Path(sys.argv[3])
if frame[:8] != b"AKRF1\0\0\0":
    raise SystemExit("unexpected frame magic")
width, height, serial, payload_len = struct.unpack("<IIQI", frame[8:28])
payload = frame[28:]
if width != 640 or height != 360 or serial < 1:
    raise SystemExit(f"unexpected frame metadata: {(width, height, serial)}")
if payload_len != len(payload) or payload_len != width * height * 4:
    raise SystemExit(f"unexpected frame payload length: {payload_len}")
if not any(payload):
    raise SystemExit("frame payload is blank")
if marker not in ("continue\n", "finish\n"):
    raise SystemExit(f"input marker did not change: {marker!r}")
if errors.exists() and errors.read_text():
    raise SystemExit(f"overlay reported errors:\n{errors.read_text()}")
nonzero = sum(1 for byte in payload if byte)
print(f"Ren'Py overlay probe ok: {width}x{height}, serial={serial}, nonzero_bytes={nonzero}, input={marker.strip()}")
PY
