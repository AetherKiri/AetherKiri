import argparse
import json
from pathlib import Path


MARKER = "<!-- aetherkiri:vcpkg-baseline-diff -->"
MAX_BYTES_DEFAULT = 45000

LABELS = {
    "major": "更新（major ⚠️）",
    "port-version": "port-version 更新",
    "update": "版本更新",
    "added": "新增",
    "removed": "移除",
}

TABLE_HEADER = [
    "| Port | 原版本 | 新版本 | 变更类型 |",
    "| --- | --- | --- | --- |",
]


def load_entries(path):
    with open(path, encoding="utf-8") as fh:
        data = json.load(fh)
    return data.get("default", data)


def entry_version(entry):
    if isinstance(entry, str):
        return entry
    if not isinstance(entry, dict):
        return str(entry)
    baseline = str(entry.get("baseline", ""))
    port_version = entry.get("port-version", 0)
    if port_version and "#" not in baseline:
        return f"{baseline}#{port_version}"
    return baseline


def major_of(version):
    for token in version.replace("#", ".").split("."):
        if token.isdigit():
            return int(token)
        if token:
            break
    return None


def classify(old, new):
    old_major = major_of(old)
    new_major = major_of(new)
    if old_major is not None and new_major is not None and old_major != new_major:
        return "major"
    if old.partition("#")[0] == new.partition("#")[0]:
        return "port-version"
    return "update"


def collect_rows(old_entries, new_entries):
    changed = []
    for port in sorted(set(old_entries) & set(new_entries)):
        old_v = entry_version(old_entries[port])
        new_v = entry_version(new_entries[port])
        if old_v != new_v:
            changed.append((port, old_v, new_v))
    removed = [(p, entry_version(old_entries[p]), "") for p in sorted(set(old_entries) - set(new_entries))]
    added = [(p, "", entry_version(new_entries[p])) for p in sorted(set(new_entries) - set(old_entries))]

    rows = (
        [(p, o, n, classify(o, n)) for p, o, n in changed]
        + [(p, o, n, "removed") for p, o, n in removed]
        + [(p, o, n, "added") for p, o, n in added]
    )
    rows.sort(key=lambda item: (item[3] != "major", item[0]))
    return rows


def format_row(item):
    port, old, new, kind = item
    return f"| {port} | {old or '-'} | {new or '-'} | {LABELS[kind]} |"


def split_rows(rows, max_bytes, part_zero_overhead):
    budget = max_bytes - 512
    chunks = []
    current = []
    current_size = part_zero_overhead

    for row in rows:
        size = len(row.encode("utf-8")) + 1
        if current and current_size + size > budget:
            chunks.append(current)
            current = []
            current_size = 512
        current.append(row)
        current_size += size
    chunks.append(current)
    return chunks


def render_part(index, total, chunk, summary_line):
    lines = [MARKER]
    if index == 0:
        lines += ["## vcpkg baseline 变更明细", "", summary_line, ""]
    else:
        lines += [f"## vcpkg baseline 变更明细（续 {index + 1}/{total}）", ""]
    lines += TABLE_HEADER
    lines += chunk
    return "\n".join(lines) + "\n"


def load_manifest_deps(path):
    with open(path, encoding="utf-8") as fh:
        data = json.load(fh)
    names = set()
    for dep in data.get("dependencies", []):
        if isinstance(dep, str):
            names.add(dep)
        elif isinstance(dep, dict) and dep.get("name"):
            names.add(dep["name"])
    for override in data.get("overrides", []):
        names.discard(override.get("name"))
    return names


def load_overlay_ports(config_path):
    config_dir = Path(config_path).resolve().parent
    with open(config_path, encoding="utf-8") as fh:
        cfg = json.load(fh)
    names = set()
    for entry in cfg.get("overlay-ports", []):
        path = Path(entry)
        if not path.is_absolute():
            path = config_dir / path
        if not path.is_dir():
            continue
        for child in sorted(path.iterdir()):
            if child.is_dir() and (child / "vcpkg.json").exists():
                names.add(child.name)
    return names


def write_report(old_entries, new_entries, old_sha, new_sha, out_dir, basename, max_bytes, manifest=None, config=None):
    structured = collect_rows(old_entries, new_entries)
    if manifest is not None:
        wanted = load_manifest_deps(manifest)
        structured = [item for item in structured if item[0] in wanted]
    if config is not None:
        overlaid = load_overlay_ports(config)
        structured = [item for item in structured if item[0] not in overlaid]

    changed = sum(1 for item in structured if item[3] not in ("added", "removed"))
    added = sum(1 for item in structured if item[3] == "added")
    removed = sum(1 for item in structured if item[3] == "removed")
    rows = [format_row(item) for item in structured]

    summary_line = (
        f"Baseline `{old_sha[:7]}` → `{new_sha[:7]}` 影响本项目 **{len(rows)}** 个直接依赖："
        f"{changed} 个版本变更、{added} 个新增、{removed} 个移除。"
    )
    header_probe = len(render_part(0, 1, [], summary_line).encode("utf-8"))
    chunks = split_rows(rows, max_bytes, header_probe)

    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    for stale in out_dir.glob(f"{basename}-*.md"):
        stale.unlink()
    total = len(chunks)
    paths = []
    for index, chunk in enumerate(chunks):
        content = render_part(index, total, chunk, summary_line)
        path = out_dir / f"{basename}-{index + 1:03d}.md"
        path.write_text(content, encoding="utf-8")
        if len(content.encode("utf-8")) > 65000:
            raise SystemExit(f"part {path.name} exceeds GitHub comment limit")
        paths.append(path)
    print(f"generated {total} part(s) covering {len(rows)} ports under {out_dir}")


def main():
    parser = argparse.ArgumentParser(description="Diff two vcpkg baseline.json snapshots.")
    parser.add_argument("old")
    parser.add_argument("new")
    parser.add_argument("old_sha")
    parser.add_argument("new_sha")
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--basename", default="vcpkg-baseline-report")
    parser.add_argument("--max-bytes", type=int, default=MAX_BYTES_DEFAULT)
    parser.add_argument("--manifest", default=None)
    parser.add_argument("--config", default=None)
    args = parser.parse_args()

    write_report(
        load_entries(args.old),
        load_entries(args.new),
        args.old_sha,
        args.new_sha,
        args.output_dir,
        args.basename,
        args.max_bytes,
        args.manifest,
        args.config,
    )


if __name__ == "__main__":
    main()
