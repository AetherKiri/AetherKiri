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
PLUGIN_CATALOG_CATEGORIES = {
    "direct",
    "hybrid",
    "aether_owner",
    "host_compat",
    "optional",
    "infrastructure",
}

# Historical DLL names do not always match the upstream plugin directory.
# This validation-only alias keeps ``krkrsteam.dll`` as the public module
# while checking it against the upstream ``steam`` catalog entry. It never
# creates a second registration or source owner.
MANIFEST_PLUGIN_ALIASES = {
    "krkrsteam": "steam",
}


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


def upstream_nested_submodules(repo_root: Path) -> tuple[list[str], list[str]]:
    """Return recursive krkrz submodule entries and checkout problems.

    The parent gitlink pins the complete krkrz tree, including every plugin,
    core and script child. ``git submodule status --recursive`` detects a
    child that is uninitialised (``-``), checked out at a different revision
    (``+``), or in a merge conflict (``U``).
    """
    checkout = repo_root / "third_party" / "krkrz_dev"
    try:
        output = subprocess.check_output(
            ["git", "-C", str(checkout), "submodule", "status", "--recursive"],
            text=True,
            stderr=subprocess.STDOUT,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        detail = getattr(exc, "output", "") or str(exc)
        return [], [
            "cannot inspect krkrz_dev nested submodules: " + detail.strip()
        ]

    entries: list[str] = []
    errors: list[str] = []
    for raw_line in output.splitlines():
        if not raw_line.strip():
            continue
        marker = raw_line[0] if raw_line[0] in "-+U" else " "
        fields = (
            raw_line[1:].strip().split(maxsplit=2)
            if marker != " "
            else raw_line.strip().split(maxsplit=2)
        )
        if len(fields) < 2:
            errors.append(f"unparseable krkrz submodule status: {raw_line}")
            continue
        revision, path = fields[0], fields[1]
        entries.append(f"{revision} {path}")
        if marker == "-":
            errors.append(f"krkrz nested submodule is not initialized: {path}")
        elif marker == "+":
            errors.append(
                "krkrz nested submodule revision differs from parent gitlink: "
                f"{path} ({revision})"
            )
        elif marker == "U":
            errors.append(f"krkrz nested submodule has merge conflicts: {path}")

    # `git submodule status` validates the gitlink revision, but it does not
    # reliably expose modified or untracked files inside an otherwise
    # correctly checked-out child.  Inspect the krkrz superproject and every
    # initialized descendant as well; source-level reuse must never consume a
    # locally patched checkout by accident.
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
            child = checkout / relative
            child_status = subprocess.check_output(
                [
                    "git",
                    "-C",
                    str(child),
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
    return entries, errors


def paths(value: Any) -> list[str]:
    if value is None:
        return []
    if isinstance(value, str):
        return [value]
    if isinstance(value, list) and all(isinstance(item, str) for item in value):
        return value
    return ["<invalid path value>"]


def normalize_plugin_name(name: str) -> str:
    value = name.strip().lower()
    for suffix in (".dll", ".tpm"):
        if value.endswith(suffix):
            value = value[: -len(suffix)]
    return value


def validate_plugin_catalog(
    data: dict[str, Any], repo_root: Path, errors: list[str]
) -> tuple[int, dict[str, int]]:
    """Check that the complete catalog covers every upstream plugin directory."""
    catalog = data.get("plugin_catalog")
    if not isinstance(catalog, dict):
        errors.append("manifest must contain a [plugin_catalog] table")
        return 0, {}

    catalog_names: dict[str, str] = {}
    category_counts: dict[str, int] = {}
    for category, value in catalog.items():
        if category not in PLUGIN_CATALOG_CATEGORIES:
            errors.append(f"plugin_catalog has unknown category: {category}")
            continue
        if not isinstance(value, list) or not all(isinstance(item, str) for item in value):
            errors.append(f"plugin_catalog.{category} must be a list of strings")
            continue
        category_counts[category] = len(value)
        for item in value:
            normalized = normalize_plugin_name(item)
            if not normalized:
                errors.append(f"plugin_catalog.{category} contains an empty name")
                continue
            if normalized in catalog_names:
                errors.append(
                    "plugin_catalog duplicate plugin: "
                    f"{item} (already in {catalog_names[normalized]})"
                )
            else:
                catalog_names[normalized] = category

    plugin_root = repo_root / "third_party" / "krkrz_dev" / "src" / "plugins"
    if not plugin_root.is_dir():
        errors.append(f"upstream plugin directory does not exist: {plugin_root}")
        return len(catalog_names), category_counts
    actual_names = {
        normalize_plugin_name(path.name)
        for path in plugin_root.iterdir()
        if path.is_dir() and not path.name.startswith(".")
    }
    missing = sorted(actual_names - set(catalog_names))
    extra = sorted(set(catalog_names) - actual_names)
    errors.extend(f"plugin_catalog missing upstream plugin: {name}" for name in missing)
    errors.extend(f"plugin_catalog names unknown upstream plugin: {name}" for name in extra)

    # Every executable manifest record must also have a complete-catalog owner.
    for plugin in data.get("plugins", []):
        if isinstance(plugin, dict) and isinstance(plugin.get("name"), str):
            # Aether-only implementations and compatibility aliases do not
            # correspond to an upstream directory and are intentionally not
            # required to appear in the upstream catalog.
            if "upstream_path" not in plugin:
                continue
        name = normalize_plugin_name(plugin["name"])
        catalog_name = MANIFEST_PLUGIN_ALIASES.get(name, name)
        if catalog_name not in catalog_names:
                errors.append(
                    f"{plugin['name']}: executable record is absent from plugin_catalog"
                )
    return len(catalog_names), category_counts


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

    validate_plugin_catalog(data, repo_root, errors)
    _, nested_errors = upstream_nested_submodules(repo_root)
    errors.extend(nested_errors)

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
    nested_submodules, nested_errors = upstream_nested_submodules(repo_root)
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

    plugin_catalog = data.get("plugin_catalog", {})
    catalog_counts = {
        key: len(value)
        for key, value in plugin_catalog.items()
        if key in PLUGIN_CATALOG_CATEGORIES and isinstance(value, list)
    } if isinstance(plugin_catalog, dict) else {}
    catalog_count = sum(catalog_counts.values())

    report = {
        "manifest": str(manifest),
        "schema_version": data.get("schema_version"),
        "upstream_repository": data.get("upstream_repository"),
        "expected_revision": expected_revision,
        "actual_revision": actual_revision,
        "revision_matches": revision_matches,
        "nested_submodule_count": len(nested_submodules),
        "nested_submodules_clean": not nested_errors,
        "nested_submodule_errors": nested_errors,
        "plugin_count": len(plugins),
        "status_counts": counts,
        "plugin_catalog_count": catalog_count,
        "plugin_catalog_counts": catalog_counts,
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
        print(
            "nested submodules: "
            f"{len(nested_submodules)} ({'clean' if not nested_errors else 'problems'})"
        )
        print(f"plugins: {len(plugins)}")
        print(
            "plugin catalog: "
            f"{catalog_count} ("
            + ", ".join(
                f"{key}={catalog_counts[key]}" for key in sorted(catalog_counts)
            )
            + ")"
        )
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
