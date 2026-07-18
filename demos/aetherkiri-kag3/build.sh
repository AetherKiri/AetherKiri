#!/usr/bin/env bash
set -euo pipefail

demo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repository_dir="$(cd -- "${demo_dir}/../.." && pwd)"
krkrrel_bin="${KRKRREL_BIN:-krkrrel}"
output_path="${repository_dir}/apps/godot_app/builtin_demos/aetherkiri-kag3/data.xp3"

"${krkrrel_bin}" "${demo_dir}/data" -out "${output_path}"
shasum -a 256 "${output_path}"
