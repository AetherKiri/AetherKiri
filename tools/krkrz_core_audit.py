#!/usr/bin/env python3
"""Audit every checked-out krkrz_dev core file against Aether's policy.

This is intentionally a source-tree audit, not a build of the upstream core.
It verifies that every file in the checked-out core tree is seen, that every
manifest core path exists, that all nested submodules are clean, and that the
source bridges which are allowed to enter the Aether core ABI still point at
the pinned submodule.
"""

from __future__ import annotations

import argparse
import json
import subprocess
from collections import Counter
from pathlib import Path
from typing import Any

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - Python < 3.11
    tomllib = None  # type: ignore[assignment]


REPO_ROOT = Path(__file__).resolve().parents[1]
CORE_ROOT = REPO_ROOT / "third_party" / "krkrz_dev" / "src" / "core"
MANIFEST = REPO_ROOT / "runtime" / "kirikiri" / "manifests" / "plugins.toml"

BRIDGE_WRAPPERS = {
    "common/sound/MathAlgorithms.cpp": Path(
        "cpp/core/utils/MathAlgorithms_Default.cpp"
    ),
    "common/sound/RealFFT.cpp": Path("cpp/core/utils/upstream_bridge/RealFFT.cpp"),
    "common/sound/WaveSegmentQueue.cpp": Path(
        "cpp/core/sound/upstream_bridge/WaveSegmentQueue.cpp"
    ),
    "common/visual/gl/WeightFunctor.cpp": Path(
        "cpp/core/visual/upstream_bridge/WeightFunctor.cpp"
    ),
    "common/utils/Random.cpp": Path("cpp/core/utils/upstream_bridge/Random.cpp"),
    "common/utils/ClipboardIntf.cpp": Path(
        "cpp/core/utils/upstream_bridge/ClipboardIntf.cpp"
    ),
    "common/utils/MiscUtility.cpp": Path(
        "cpp/core/utils/upstream_bridge/MiscUtility.cpp"
    ),
    "common/utils/md5.c": Path("cpp/core/utils/upstream_bridge/md5.c"),
    "common/base/PluginIntf.cpp": Path(
        "cpp/core/plugin/upstream_bridge/PluginIntf.cpp"
    ),
    "common/tjs2/tjsException.cpp": Path(
        "cpp/core/tjs2/upstream_bridge/tjsException.cpp"
    ),
}

# These are the old Aether copies removed when a bridge became authoritative.
# Keeping the list here prevents a later merge from silently restoring two
# implementations of the same public symbols.
LEGACY_LOCAL_IMPLEMENTATIONS = {
    "common/sound/MathAlgorithms.cpp": Path("cpp/core/sound/MathAlgorithms.cpp"),
    "common/sound/RealFFT.cpp": Path("cpp/core/utils/RealFFT_Default.cpp"),
    "common/sound/WaveSegmentQueue.cpp": Path("cpp/core/sound/WaveSegmentQueue.cpp"),
    "common/visual/gl/WeightFunctor.cpp": Path("cpp/core/visual/gl/WeightFunctor.cpp"),
    "common/utils/Random.cpp": Path("cpp/core/utils/Random.cpp"),
    "common/utils/ClipboardIntf.cpp": Path("cpp/core/utils/ClipboardIntf.cpp"),
    "common/utils/MiscUtility.cpp": Path("cpp/core/utils/MiscUtility.cpp"),
    "common/utils/md5.c": Path("cpp/core/utils/md5.c"),
    "common/base/PluginIntf.cpp": Path("cpp/core/plugin/PluginIntf.cpp"),
    "common/tjs2/tjsException.cpp": Path("cpp/core/tjs2/tjsException.cpp"),
}

BRIDGE_CMAKE_REFERENCES = {
    "common/sound/MathAlgorithms.cpp": (
        Path("cpp/core/utils/CMakeLists.txt"),
        "MathAlgorithms_Default.cpp",
    ),
    "common/sound/RealFFT.cpp": (
        Path("cpp/core/utils/CMakeLists.txt"),
        "AETHER_REAL_FFT_SOURCE",
    ),
    "common/sound/WaveSegmentQueue.cpp": (
        Path("cpp/core/sound/CMakeLists.txt"),
        "AETHER_WAVE_SEGMENT_QUEUE_SOURCE",
    ),
    "common/visual/gl/WeightFunctor.cpp": (
        Path("cpp/core/visual/CMakeLists.txt"),
        "AETHER_WEIGHT_FUNCTOR_SOURCE",
    ),
    "common/utils/Random.cpp": (
        Path("cpp/core/utils/CMakeLists.txt"),
        "AETHER_RANDOM_SOURCE",
    ),
    "common/utils/ClipboardIntf.cpp": (
        Path("cpp/core/utils/CMakeLists.txt"),
        "AETHER_CLIPBOARD_INTF_SOURCE",
    ),
    "common/utils/MiscUtility.cpp": (
        Path("cpp/core/utils/CMakeLists.txt"),
        "AETHER_MISC_UTILITY_SOURCE",
    ),
    "common/utils/md5.c": (
        Path("cpp/core/utils/CMakeLists.txt"),
        "AETHER_MD5_SOURCE",
    ),
    "common/base/PluginIntf.cpp": (
        Path("cpp/core/plugin/CMakeLists.txt"),
        "AETHER_PLUGIN_INTF_SOURCE",
    ),
    "common/tjs2/tjsException.cpp": (
        Path("cpp/core/tjs2/CMakeLists.txt"),
        "AETHER_TJS_EXCEPTION_SOURCE",
    ),
}

BRIDGED_SOURCES = set(BRIDGE_WRAPPERS)

COMMON_POLICIES = {
    "base": "aether-owner-or-method-parity",
    "environ": "host-reference",
    "extension": "aether-registry-with-leaf-bridge",
    "glad": "platform-reference",
    "msg": "aether-message-owner",
    "sound": "leaf-bridge-or-parity",
    "tjs2": "aether-vm-owner",
    "utils": "leaf-bridge-or-parity",
    "visual": "renderer-owner-or-leaf-bridge",
}

PLATFORM_DIRS = {"generic", "sdl3", "win32"}
KNOWN_CORE_SCOPES = {
    "common",
    "generic",
    "sdl3",
    "win32",
    "external",
    "data",
    "resource",
    "testdata_viewport",
    "tests",
    "cmake",
    "doc",
    "licenses",
    "tp_stub",
}


def checkout_revision() -> str | None:
    checkout = REPO_ROOT / "third_party" / "krkrz_dev"
    try:
        return subprocess.check_output(
            ["git", "-C", str(checkout), "rev-parse", "HEAD"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def nested_submodule_errors() -> tuple[int, list[str]]:
    """Verify that every krkrz_dev child gitlink is checked out cleanly."""
    checkout = REPO_ROOT / "third_party" / "krkrz_dev"
    try:
        output = subprocess.check_output(
            ["git", "-C", str(checkout), "submodule", "status", "--recursive"],
            text=True,
            stderr=subprocess.STDOUT,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        detail = getattr(exc, "output", "") or str(exc)
        return 0, [
            "cannot inspect krkrz_dev nested submodules: " + detail.strip()
        ]

    count = 0
    errors: list[str] = []
    for raw_line in output.splitlines():
        if not raw_line.strip():
            continue
        count += 1
        marker = raw_line[0] if raw_line[0] in "-+U" else " "
        fields = (
            raw_line[1:].strip().split(maxsplit=2)
            if marker != " "
            else raw_line.strip().split(maxsplit=2)
        )
        path = fields[1] if len(fields) >= 2 else raw_line.strip()
        revision = fields[0] if fields else "?"
        if marker == "-":
            errors.append(f"krkrz nested submodule is not initialized: {path}")
        elif marker == "+":
            errors.append(
                "krkrz nested submodule revision differs from parent gitlink: "
                f"{path} ({revision})"
            )
        elif marker == "U":
            errors.append(f"krkrz nested submodule has merge conflicts: {path}")
        elif len(fields) < 2:
            errors.append(f"unparseable krkrz submodule status: {raw_line}")

    # A matching gitlink does not guarantee a clean child worktree.  Check the
    # krkrz checkout and every initialized descendant so the audit cannot pass
    # while a local patch is being compiled through a source bridge.
    try:
        root_status = subprocess.check_output(
            [
                "git",
                "-C",
                str(checkout),
                "status",
                "--porcelain",
                "--untracked-files=all",
                "--ignore-submodules=all",
            ],
            text=True,
            stderr=subprocess.STDOUT,
        ).strip()
        if root_status:
            sample = "; ".join(root_status.splitlines()[:3])
            errors.append(f"krkrz_dev checkout has local changes: {sample}")

        initialized = subprocess.check_output(
            [
                "git",
                "-C",
                str(checkout),
                "submodule",
                "foreach",
                "--recursive",
                "--quiet",
                "printf '%s\\n' \"$displaypath\"",
            ],
            text=True,
            stderr=subprocess.STDOUT,
        )
        for relative in initialized.splitlines():
            relative = relative.strip()
            if not relative:
                continue
            child_status = subprocess.check_output(
                [
                    "git",
                    "-C",
                    str(checkout / relative),
                    "status",
                    "--porcelain",
                    "--untracked-files=all",
                ],
                text=True,
                stderr=subprocess.STDOUT,
            ).strip()
            if child_status:
                sample = "; ".join(child_status.splitlines()[:3])
                errors.append(
                    f"krkrz nested submodule has local changes: {relative}: {sample}"
                )
    except (OSError, subprocess.CalledProcessError) as exc:
        detail = getattr(exc, "output", "") or str(exc)
        errors.append("cannot inspect krkrz checkout worktrees: " + detail.strip())
    return count, errors


def load_manifest() -> dict[str, Any]:
    if tomllib is None:
        raise SystemExit("krkrz_core_audit.py requires Python 3.11+ (tomllib)")
    with MANIFEST.open("rb") as stream:
        value = tomllib.load(stream)
    if not isinstance(value, dict):
        raise SystemExit("manifest root must be a table")
    return value


def manifest_core_paths(data: dict[str, Any]) -> tuple[list[str], list[str]]:
    paths: list[str] = []
    errors: list[str] = []
    components = data.get("core_components", [])
    if not isinstance(components, list):
        return [], ["manifest core_components must be a list"]
    for index, component in enumerate(components):
        if not isinstance(component, dict):
            errors.append(f"core_components[{index}] is not a table")
            continue
        value = component.get("upstream_path", [])
        values = [value] if isinstance(value, str) else value
        if not isinstance(values, list) or not all(
            isinstance(item, str) for item in values
        ):
            errors.append(f"core_components[{index}] has invalid upstream_path")
            continue
        for item in values:
            if not item.startswith("src/core/"):
                errors.append(
                    f"{component.get('name', index)} path is outside src/core: {item}"
                )
                continue
            relative = item.removeprefix("src/core/")
            if relative in paths:
                errors.append(f"duplicate manifest core path: {relative}")
            paths.append(relative)
            if not (CORE_ROOT / relative).is_file():
                errors.append(f"missing manifest core path: {item}")
    return paths, errors


def audited_files() -> list[str]:
    files: list[str] = []
    files.extend(
        str(path.relative_to(CORE_ROOT))
        for path in CORE_ROOT.rglob("*")
        if path.is_file() and ".git" not in path.parts
    )
    return sorted(files)


def policy_for(relative: str) -> str:
    first, _, rest = relative.partition("/")
    if first == "common":
        component = rest.split("/", 1)[0]
        return COMMON_POLICIES.get(component, "reference")
    if first in PLATFORM_DIRS:
        return "platform-reference"
    if first == "external":
        return "external-reference"
    if first == "tests":
        return "test-input"
    if first in {"data", "resource", "testdata_viewport"}:
        return "asset-reference"
    if first in {"cmake", "doc", "licenses"} or not rest:
        return "metadata-reference"
    if first == "tp_stub":
        return "abi-reference"
    return "reference"


def bridge_paths_present() -> set[str]:
    return {
        source
        for source, wrapper in BRIDGE_WRAPPERS.items()
        if (REPO_ROOT / wrapper).is_file()
    }


def build_report() -> dict[str, Any]:
    data = load_manifest()
    files = audited_files()
    manifest_paths, errors = manifest_core_paths(data)
    actual = checkout_revision()
    nested_count, nested_errors = nested_submodule_errors()
    errors.extend(nested_errors)
    expected = data.get("upstream_revision")
    if actual != expected:
        errors.append(
            f"checked-out revision differs from manifest pin: {actual} != {expected}"
        )

    bridged_in_scope = sorted(BRIDGED_SOURCES.intersection(files))
    missing_bridges = sorted(BRIDGED_SOURCES.difference(files))
    absent_adapters = sorted(BRIDGED_SOURCES.difference(bridge_paths_present()))
    errors.extend(
        f"bridge source is missing from checkout: {path}" for path in missing_bridges
    )
    errors.extend(
        f"bridge wrapper is missing: {path}" for path in absent_adapters
    )
    for source, wrapper in BRIDGE_WRAPPERS.items():
        wrapper_path = REPO_ROOT / wrapper
        if not wrapper_path.is_file():
            continue
        wrapper_text = wrapper_path.read_text(encoding="utf-8", errors="ignore")
        if source not in wrapper_text:
            errors.append(f"bridge wrapper does not name upstream source: {wrapper}")
        legacy = REPO_ROOT / LEGACY_LOCAL_IMPLEMENTATIONS[source]
        if legacy.is_file():
            errors.append(f"legacy duplicate implementation still exists: {legacy}")
        cmake_path, token = BRIDGE_CMAKE_REFERENCES[source]
        cmake_text = (REPO_ROOT / cmake_path).read_text(
            encoding="utf-8", errors="ignore"
        ) if (REPO_ROOT / cmake_path).is_file() else ""
        if token not in cmake_text:
            errors.append(
                f"bridge wrapper is not referenced by its CMake target: {cmake_path}"
            )

    counts = Counter(policy_for(path) for path in files)
    scopes = Counter(path.split("/", 1)[0] for path in files)
    # Top-level files (README, Makefile, git metadata, etc.) are intentional
    # metadata inputs; a new top-level directory, including an empty one, must
    # be reviewed explicitly instead of silently inheriting the generic
    # reference policy.
    unknown_directories = sorted(
        path.name
        for path in CORE_ROOT.iterdir()
        if path.is_dir()
        and path.name != ".git"
        and path.name not in KNOWN_CORE_SCOPES
    )
    errors.extend(
        f"unreviewed core top-level directory: {scope}"
        for scope in unknown_directories
    )
    common_root = CORE_ROOT / "common"
    common_components = {
        path.name for path in common_root.iterdir() if path.is_dir()
    } if common_root.is_dir() else set()
    unknown_common = sorted(common_components - set(COMMON_POLICIES))
    errors.extend(
        f"unreviewed krkrz common component: common/{component}"
        for component in unknown_common
    )
    return {
        "core_revision": actual,
        "manifest_revision": expected,
        "revision_matches": actual == expected,
        "nested_submodule_count": nested_count,
        "nested_submodules_clean": not nested_errors,
        "audited_file_count": len(files),
        "scope_counts": dict(sorted(scopes.items())),
        "common_components": sorted(common_components),
        "unknown_common_components": unknown_common,
        "unknown_top_level_directories": unknown_directories,
        "policy_counts": dict(sorted(counts.items())),
        "manifest_core_path_count": len(manifest_paths),
        "bridged_source_count": len(bridged_in_scope),
        "bridged_sources": bridged_in_scope,
        "errors": errors,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="emit JSON")
    args = parser.parse_args()
    report = build_report()
    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    else:
        print(f"core revision: {report['core_revision']}")
        print(
            "nested submodules: "
            f"{report['nested_submodule_count']} "
            f"({'clean' if report['nested_submodules_clean'] else 'problems'})"
        )
        print(f"audited files: {report['audited_file_count']}")
        print(
            "scopes: "
            + ", ".join(
                f"{key}={value}" for key, value in report["scope_counts"].items()
            )
        )
        print(
            "policy: "
            + ", ".join(
                f"{key}={value}"
                for key, value in report["policy_counts"].items()
            )
        )
        print(f"manifest core paths: {report['manifest_core_path_count']}")
        print(f"source bridges: {report['bridged_source_count']}")
        if report["errors"]:
            print("\\nerrors")
            for error in report["errors"]:
                print(f"- {error}")
    return 1 if report["errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
