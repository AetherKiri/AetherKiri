#!/usr/bin/env python3
"""Validate and inspect the KiriKiri plugin capability manifest.

The manifest is deliberately independent from C++ registration discovery:
registration tells us what symbols are linked, while this contract records
whether a module is upstream, hybrid, Aether-owned, optional, or a stub.
Keeping both views makes accidental claims of feature parity visible.
"""

from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path
from typing import Any

try:
    import tomllib
except ModuleNotFoundError:  # pragma: no cover - Python < 3.11 fallback
    tomllib = None  # type: ignore[assignment]


VALID_STATUSES = {
    "upstream-adapted",
    "hybrid",
    "aether",
    "stub",
    "optional",
}
VALID_CORE_STATUSES = {"upstream-adapted", "hybrid", "aether", "optional"}
VALID_SCRIPT_STATUSES = {"reference", "fixture"}


def load_manifest(path: Path) -> dict[str, Any]:
    if tomllib is None:
        raise SystemExit("plugin_manifest_report.py requires Python 3.11+ (tomllib)")
    with path.open("rb") as stream:
        data = tomllib.load(stream)
    if not isinstance(data.get("plugins"), list):
        raise SystemExit("manifest must contain one or more [[plugins]] tables")
    return data


def upstream_revision(repo_root: Path) -> str | None:
    checkout = repo_root / "third_party" / "krkrz_dev"
    try:
        return subprocess.check_output(
            ["git", "-C", str(checkout), "rev-parse", "HEAD"],
            text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def paths(value: Any) -> list[str]:
    if value is None:
        return []
    if isinstance(value, str):
        return [value]
    if isinstance(value, list) and all(isinstance(item, str) for item in value):
        return value
    return ["<invalid path value>"]


def validate(data: dict[str, Any], repo_root: Path) -> list[str]:
    errors: list[str] = []
    if data.get("schema_version") != 1:
        errors.append(f"unsupported schema_version: {data.get('schema_version')!r}")
    if not isinstance(data.get("upstream_repository"), str) or not data.get(
        "upstream_repository"
    ):
        errors.append("upstream_repository must be a non-empty string")
    revision = data.get("upstream_revision")
    if not isinstance(revision, str) or len(revision) < 7:
        errors.append("upstream_revision must be a pinned commit SHA")

    seen: set[str] = set()
    for index, plugin in enumerate(data["plugins"]):
        if not isinstance(plugin, dict):
            errors.append(f"plugins[{index}] is not a table")
            continue
        name = plugin.get("name")
        if not isinstance(name, str) or not name:
            errors.append(f"plugins[{index}] has no name")
            name = f"plugins[{index}]"
        if name.lower() in seen:
            errors.append(f"duplicate plugin name: {name}")
        seen.add(name.lower())

        status = plugin.get("status")
        if status not in VALID_STATUSES:
            errors.append(f"{name}: invalid status {status!r}")

        adapter = plugin.get("adapter")
        if isinstance(adapter, str) and not (repo_root / adapter).exists():
            errors.append(f"{name}: adapter path does not exist: {adapter}")

        for source in paths(plugin.get("upstream_path")):
            if source.startswith("<"):
                errors.append(f"{name}: invalid upstream_path")
            elif not (repo_root / "third_party" / "krkrz_dev" / source).exists():
                errors.append(f"{name}: upstream path does not exist: {source}")

    scripts = data.get("script_components", [])
    if not isinstance(scripts, list):
        errors.append("script_components must be a list when present")
    else:
        script_seen: set[str] = set()
        for index, script in enumerate(scripts):
            if not isinstance(script, dict):
                errors.append(f"script_components[{index}] is not a table")
                continue
            name = script.get("name")
            if not isinstance(name, str) or not name:
                errors.append(f"script_components[{index}] has no name")
                name = f"script_components[{index}]"
            if name.lower() in script_seen:
                errors.append(f"duplicate script component name: {name}")
            script_seen.add(name.lower())

            status = script.get("status")
            if status not in VALID_SCRIPT_STATUSES:
                errors.append(f"{name}: invalid script status {status!r}")

            revision = script.get("revision")
            if not isinstance(revision, str) or len(revision) < 7:
                errors.append(f"{name}: revision must be a pinned commit SHA")

            for field in ("upstream_path", "entrypoint"):
                value = script.get(field)
                if not isinstance(value, str) or not value:
                    errors.append(f"{name}: {field} must be a non-empty path")
                    continue
                if not (repo_root / "third_party" / "krkrz_dev" / value).exists():
                    errors.append(f"{name}: {field} does not exist: {value}")

    components = data.get("core_components")
    if not isinstance(components, list) or not components:
        errors.append("manifest must contain one or more [[core_components]] tables")
    else:
        core_seen: set[str] = set()
        for index, component in enumerate(components):
            if not isinstance(component, dict):
                errors.append(f"core_components[{index}] is not a table")
                continue
            name = component.get("name")
            if not isinstance(name, str) or not name:
                errors.append(f"core_components[{index}] has no name")
                name = f"core_components[{index}]"
            if name.lower() in core_seen:
                errors.append(f"duplicate core component name: {name}")
            core_seen.add(name.lower())

            status = component.get("status")
            if status not in VALID_CORE_STATUSES:
                errors.append(f"{name}: invalid core status {status!r}")

            adapter = component.get("adapter")
            if isinstance(adapter, str) and not (repo_root / adapter).exists():
                errors.append(f"{name}: adapter path does not exist: {adapter}")

            for source in paths(component.get("upstream_path")):
                if source.startswith("<"):
                    errors.append(f"{name}: invalid upstream_path")
                elif not (
                    repo_root / "third_party" / "krkrz_dev" / source
                ).exists():
                    errors.append(f"{name}: upstream path does not exist: {source}")

            parity_test = component.get("parity_test")
            if isinstance(parity_test, str) and not (
                repo_root / "third_party" / "krkrz_dev" / parity_test
            ).exists():
                errors.append(f"{name}: parity_test does not exist: {parity_test}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("runtime/kirikiri/manifests/plugins.toml"),
    )
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="fail if the checked-out submodule revision differs from the pin",
    )
    args = parser.parse_args()

    manifest = args.manifest.expanduser().resolve()
    repo_root = manifest.parents[3]
    data = load_manifest(manifest)
    errors = validate(data, repo_root)
    actual_revision = upstream_revision(repo_root)
    expected_revision = data.get("upstream_revision")
    revision_matches = actual_revision == expected_revision
    if args.strict and actual_revision is not None and not revision_matches:
        errors.append(
            "checked-out krkrz_dev revision differs from manifest pin: "
            f"{actual_revision} != {expected_revision}"
        )

    plugins = data["plugins"]
    counts: dict[str, int] = {}
    for plugin in plugins:
        status = plugin.get("status", "invalid") if isinstance(plugin, dict) else "invalid"
        counts[status] = counts.get(status, 0) + 1

    core_components = data.get("core_components", [])
    core_counts: dict[str, int] = {}
    if isinstance(core_components, list):
        for component in core_components:
            status = (
                component.get("status", "invalid")
                if isinstance(component, dict)
                else "invalid"
            )
            core_counts[status] = core_counts.get(status, 0) + 1

    script_components = data.get("script_components", [])
    script_counts: dict[str, int] = {}
    if isinstance(script_components, list):
        for component in script_components:
            status = (
                component.get("status", "invalid")
                if isinstance(component, dict)
                else "invalid"
            )
            script_counts[status] = script_counts.get(status, 0) + 1

    report = {
        "manifest": str(manifest),
        "schema_version": data.get("schema_version"),
        "upstream_repository": data.get("upstream_repository"),
        "expected_revision": expected_revision,
        "actual_revision": actual_revision,
        "revision_matches": revision_matches,
        "plugin_count": len(plugins),
        "status_counts": counts,
        "core_component_count": len(core_components)
        if isinstance(core_components, list)
        else 0,
        "core_status_counts": core_counts,
        "script_component_count": len(script_components)
        if isinstance(script_components, list)
        else 0,
        "script_status_counts": script_counts,
        "errors": errors,
    }
    if args.json:
        print(json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True))
    else:
        print(f"manifest: {manifest}")
        print(f"upstream: {data.get('upstream_repository')} @ {expected_revision}")
        print(f"checkout: {actual_revision or 'unavailable'}")
        print(f"plugins: {len(plugins)}")
        print("status: " + ", ".join(f"{key}={counts[key]}" for key in sorted(counts)))
        print(f"core components: {len(core_components) if isinstance(core_components, list) else 0}")
        if core_counts:
            print(
                "core status: "
                + ", ".join(
                    f"{key}={core_counts[key]}" for key in sorted(core_counts)
                )
            )
        if script_counts:
            print(
                "script status: "
                + ", ".join(
                    f"{key}={script_counts[key]}" for key in sorted(script_counts)
                )
            )
        if errors:
            print("\nerrors")
            for error in errors:
                print(f"- {error}")
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
