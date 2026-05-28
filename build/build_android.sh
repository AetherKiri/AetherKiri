#!/usr/bin/env bash
set -euo pipefail

echo "Android Godot export is not wired in this migration pass." >&2
echo "Use './build.sh macos debug' or './build.sh ios debug --simulator'." >&2
exit 2
