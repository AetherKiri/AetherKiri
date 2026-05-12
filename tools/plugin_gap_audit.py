#!/usr/bin/env python3
"""Compare KiriKiri2 reference plugin folders with AetherKiri registrations."""

from __future__ import annotations

import argparse
import os
import re
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = Path(__file__).with_name("plugin_gap_reference_plugins.txt")

ALIASES = {
    "json": "json",
    "layerExDraw": "layerExDraw",
    "layerExPerspective": "perspective",
    "libpsd": "psd",
    "psdfile": "psd",
    "scriptsEx": "ScriptsEx",
    "steam": "krkrsteam",
}

IGNORED_REFERENCE_DIRS = {
    "00_simplebinder",
    "basetest",
    "exceptiontest",
    "nativeclasstest",
    "ncbind",
    "parserskelton",
    "libjpeg",
    "zlib",
}


def registered_modules(plugin_root: Path) -> set[str]:
    names: set[str] = set()
    patterns = [
        re.compile(r'NCB_MODULE_NAME\s+TJS_W\("([^"]+)"\)'),
        re.compile(r'ncbCallbackAutoRegister\s+\w+\s*\(\s*TJS_W\("([^"]+)"\)'),
    ]
    for path in list(plugin_root.rglob("*.cpp")) + list(plugin_root.rglob("*.h")):
        text = path.read_text(errors="ignore")
        for pattern in patterns:
            for match in pattern.finditer(text):
                name = match.group(1)
                if name.lower().endswith(".dll"):
                    name = name[:-4]
                names.add(name)
    return names


def reference_plugins(reference_root: Path) -> set[str]:
    return {
        path.name
        for path in reference_root.iterdir()
        if path.is_dir() and path.name not in IGNORED_REFERENCE_DIRS
    }


def manifest_plugins(manifest: Path) -> set[str]:
    names: set[str] = set()
    for line in manifest.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            names.add(line)
    return names


def resolve_reference_root(explicit: Path | None) -> Path | None:
    candidates: list[Path] = []
    if explicit is not None:
        candidates.append(explicit)

    env_reference = os.environ.get("KIRIKIRI2_PLUGIN_DIR")
    if env_reference:
        candidates.append(Path(env_reference))

    if not candidates:
        return None

    for candidate in candidates:
        resolved = candidate.expanduser().resolve()
        if resolved.is_dir():
            return resolved

    searched = "\n  ".join(str(path.expanduser()) for path in candidates)
    raise SystemExit(
        "KiriKiri2 plugin reference directory was not found.\n"
        f"Searched:\n  {searched}"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--reference",
        type=Path,
        default=None,
        help=(
            "KiriKiri2 win32 plugin directory. Overrides the bundled manifest."
        ),
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST,
        help="Bundled reference plugin manifest used when no reference checkout is configured.",
    )
    parser.add_argument(
        "--plugins",
        type=Path,
        default=Path("cpp/plugins"),
        help="AetherKiri plugin source directory",
    )
    args = parser.parse_args()

    plugin_root = args.plugins.expanduser().resolve()
    reference_root = resolve_reference_root(args.reference)

    if reference_root is not None:
        reference = reference_plugins(reference_root)
    else:
        reference = manifest_plugins(args.manifest.expanduser().resolve())
    registered = registered_modules(plugin_root)

    covered: set[str] = set()
    missing: list[str] = []
    for name in sorted(reference):
        module = ALIASES.get(name, name)
        if module in registered:
            covered.add(name)
        else:
            missing.append(name)

    print(f"reference: {len(reference)}")
    print(f"registered modules: {len(registered)}")
    print(f"covered reference plugins: {len(covered)}")
    print(f"missing reference plugins: {len(missing)}")
    if missing:
        print()
        for name in missing:
            print(name)

    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
