#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workspace_root="$(cd "$repo_root/.." && pwd)"
game_root="${AETHERKIRI_SMOKE_GAME:-$workspace_root/game}"
godot_bin="${GODOT_BIN:-$HOME/Projects/AetherKiri/.aetherkiri-cache/godot/Godot_v4.7-stable_linux.x86_64}"
build_dir="$repo_root/out/linux/debug"
cargo_target="$build_dir/rust/minori_runtime/cargo-target"
extension="$build_dir/bridge/godot_extension/libaether_kiri_godot.so"

mode="${1:-}"
if [[ "$mode" == "--probe" && ! -d "$game_root" ]]; then
    printf 'Minori game directory not found: %s\n' "$game_root" >&2
    printf 'Set AETHERKIRI_SMOKE_GAME to the original game directory.\n' >&2
    exit 1
fi
if [[ ! -x "$godot_bin" ]]; then
    printf 'Godot executable not found: %s\n' "$godot_bin" >&2
    printf 'Set GODOT_BIN to the Godot 4.7 executable.\n' >&2
    exit 1
fi
if [[ ! -f "$build_dir/build.ninja" ]]; then
    printf 'Linux debug build is not configured: %s\n' "$build_dir" >&2
    printf 'Run ./build.sh linux debug --jobs=2 once, then retry.\n' >&2
    exit 1
fi

CARGO_TARGET_DIR="$cargo_target" \
    cargo build --manifest-path "$repo_root/rust/minori_runtime/Cargo.toml" --lib
ninja -C "$build_dir" -j2 aether_kiri_godot

install -Dm755 "$extension" \
    "$repo_root/apps/godot_app/bin/linux/debug/libaether_kiri_godot.so"
install -Dm755 "$extension" \
    "$repo_root/out/godot/linux/debug/libaether_kiri_godot.so"

if [[ "$mode" == "--probe" ]]; then
    exec env \
        AETHERKIRI_SMOKE_GAME="$game_root" \
        AETHERKIRI_MANUAL_PROBE_WINDOW_W="${AETHERKIRI_MANUAL_PROBE_WINDOW_W:-1280}" \
        AETHERKIRI_MANUAL_PROBE_WINDOW_H="${AETHERKIRI_MANUAL_PROBE_WINDOW_H:-720}" \
        "$godot_bin" \
        --path "$repo_root/apps/godot_app" \
        --script res://scripts/manual_render_probe.gd
fi

exec "$godot_bin" --path "$repo_root/apps/godot_app"
