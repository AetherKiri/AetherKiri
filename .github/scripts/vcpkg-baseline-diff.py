import argparse
import json


MARKER = "<!-- aetherkiri:vcpkg-baseline-diff -->"
MAX_ROWS_DEFAULT = 60

LABELS = {
    "major": "更新（major ⚠️）",
    "port-version": "port-version 更新",
    "update": "版本更新",
    "added": "新增",
    "removed": "移除",
}


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


def format_row(port, old, new, kind):
    return f"| {port} | {old or '-'} | {new or '-'} | {LABELS[kind]} |"


def build_report(old_entries, new_entries, old_sha, new_sha, max_rows):
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

    shown = rows[:max_rows]
    lines = [
        MARKER,
        "## vcpkg baseline 变更明细",
        "",
        f"Baseline `{old_sha[:7]}` → `{new_sha[:7]}` 共影响 **{len(rows)}** 个 port："
        f"{len(changed)} 个版本变更、{len(added)} 个新增、{len(removed)} 个移除。",
        "",
        "| Port | 原版本 | 新版本 | 变更类型 |",
        "| --- | --- | --- | --- |",
    ]
    lines.extend(format_row(*item) for item in shown)
    compare_url = f"https://github.com/microsoft/vcpkg/compare/{old_sha}...{new_sha}"
    if len(rows) > len(shown):
        lines.append("")
        lines.append(
            f"> 还有 {len(rows) - len(shown)} 个 port 未列出，完整列表见 [compare 视图]({compare_url})。"
        )
    else:
        lines.append("")
        lines.append(f"> 完整对比见 [microsoft/vcpkg compare]({compare_url})。")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description="Diff two vcpkg baseline.json snapshots.")
    parser.add_argument("old")
    parser.add_argument("new")
    parser.add_argument("old_sha")
    parser.add_argument("new_sha")
    parser.add_argument("--output", required=True)
    parser.add_argument("--max-rows", type=int, default=MAX_ROWS_DEFAULT)
    args = parser.parse_args()

    report = build_report(
        load_entries(args.old),
        load_entries(args.new),
        args.old_sha,
        args.new_sha,
        args.max_rows,
    )
    with open(args.output, "w", encoding="utf-8") as fh:
        fh.write(report)


if __name__ == "__main__":
    main()
