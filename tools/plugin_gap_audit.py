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
    "layerexdraw": "layerexdraw",
    "layerexperspective": "perspective",
    "libpsd": "psd",
    "psdfile": "psd",
    "scriptsex": "scriptsex",
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


def normalize_plugin_name(name: str) -> str:
    name = name.strip().lower()
    for suffix in (".dll", ".tpm"):
        if name.endswith(suffix):
            name = name[: -len(suffix)]
    return name


def split_module_blocks(text: str) -> list[tuple[str, str]]:
    pattern = re.compile(
        r'(?:NCB_MODULE_NAME\s+TJS_W\("([^"]+)"\)|'
        r'ncbCallbackAutoRegister\s+\w+\s*\(\s*TJS_W\("([^"]+)"\))'
    )
    matches = list(pattern.finditer(text))
    blocks: list[tuple[str, str]] = []
    for index, match in enumerate(matches):
        module = match.group(1) or match.group(2)
        end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
        blocks.append((normalize_plugin_name(module), text[match.start() : end]))
    return blocks


def classify_module_block(block: str, path: Path) -> str:
    lower = block.lower()
    if re.search(r"(?m)^\s*register_empty_plugin\s*\(", lower):
        return "empty_stub"
    if re.search(r"static\s+void\s+\w*stub\w*\s*\(\s*\)\s*\{\s*\}", block):
        return "empty_stub"
    if path.name in {
        "compatLegacyPlugins.cpp",
        "compatSystemPlugins.cpp",
        "compatMediaLayerPlugins.cpp",
    }:
        return "compat_stub"
    if "aetherkiri_compat_stub" in lower:
        return "compat_stub"
    if path.name == "dummy_plugin_stubs.cpp" and path.parent.name == "stubs":
        return "compat_stub"

    real_markers = (
        "ncb_register_class",
        "ncb_attach_class",
        "ncb_attach_class_with_hook",
        "ncb_attach_function",
        "ncb_register_function",
        "ncb_method(",
        "ncb_method_raw_callback",
        "ncb_method_detail",
        "ncb_property(",
        "ncb_property_ro",
        "ncb_property_proxy",
        "ncb_constructor",
        "tvpregisterstoragemedia",
        "tvpaddtranshandlerprovider",
        "sqlite3_vfs_register",
        "tvpcreatestream",
        "tvpcreatenativeclass",
        "tjscreatenativeclassmethod",
        "tjscreatearrayobject",
        "tjscreatedictionaryobject",
        "registerwavetranshandlerprovider",
        "registermosaictranshandlerprovider",
        "registerturntranshandlerprovider",
        "registerrotatetranshandlerprovider",
        "registerrippletranshandlerprovider",
        "ncb_pre_regist_callback(initextrans",
        "ncb_pre_regist_callback(linkkagparsercompatibility",
        "ncb_post_unregist_callback(unlinkkagparsercompatibility",
        "&linkkagparsercompatibility",
        "&unlinkkagparsercompatibility",
    )
    if any(marker in lower for marker in real_markers):
        return "real"

    compat_markers = (
        "compat",
        "stub",
        "dummy",
        "noop",
        "no-op",
        "placeholder",
        "fallback",
    )
    if any(marker in lower for marker in compat_markers):
        return "compat_stub"

    return "compat_stub"


def merge_classification(old: str | None, new: str) -> str:
    rank = {"empty_stub": 0, "compat_stub": 1, "real": 2}
    if old is None or rank[new] > rank[old]:
        return new
    return old


def registered_modules(plugin_root: Path) -> dict[str, str]:
    names: dict[str, str] = {}
    patterns = [
        re.compile(r'NCB_MODULE_NAME\s+TJS_W\("([^"]+)"\)'),
        re.compile(r'ncbCallbackAutoRegister\s+\w+\s*\(\s*TJS_W\("([^"]+)"\)'),
    ]
    for path in list(plugin_root.rglob("*.cpp")) + list(plugin_root.rglob("*.h")):
        text = path.read_text(errors="ignore")
        if not any(pattern.search(text) for pattern in patterns):
            continue
        for name, block in split_module_blocks(text):
            names[name] = merge_classification(
                names.get(name), classify_module_block(block, path)
            )
    return names


def reference_plugins(reference_root: Path) -> set[str]:
    return {
        normalize_plugin_name(path.name)
        for path in reference_root.iterdir()
        if path.is_dir() and path.name not in IGNORED_REFERENCE_DIRS
    }


def manifest_plugins(manifest: Path) -> set[str]:
    names: set[str] = set()
    for line in manifest.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            names.add(normalize_plugin_name(line))
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
    compat: list[str] = []
    empty: list[str] = []
    missing: list[str] = []
    for name in sorted(reference):
        module = ALIASES.get(name, name)
        state = registered.get(module)
        if state == "real":
            covered.add(name)
        elif state == "compat_stub":
            covered.add(name)
            compat.append(name)
        elif state == "empty_stub":
            empty.append(name)
        else:
            missing.append(name)

    print(f"reference: {len(reference)}")
    print(f"registered modules: {len(registered)}")
    print(f"covered reference plugins: {len(covered)}")
    print(f"compat stub coverage: {len(compat)}")
    print(f"empty stubs: {len(empty)}")
    print(f"missing reference plugins: {len(missing)}")
    print(
        "registered by class: "
        + ", ".join(
            f"{state}={sum(1 for value in registered.values() if value == state)}"
            for state in ("real", "compat_stub", "empty_stub")
        )
    )
    if compat:
        print("\ncompat_stub")
        for name in compat:
            print(name)
    if empty:
        print("\nempty_stub")
        for name in empty:
            print(name)
    if missing:
        print("\nmissing")
        for name in missing:
            print(name)

    return 1 if missing or empty else 0


if __name__ == "__main__":
    raise SystemExit(main())
