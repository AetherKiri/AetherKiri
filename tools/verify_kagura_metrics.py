#!/usr/bin/env python3
"""Verify that Kagura changed real protected IR CFG metrics."""

from __future__ import annotations

import re
import sys
from pathlib import Path


METRICS_RE = re.compile(r"BB count\s*:\s*(\d+)\D+(\d+)")
CYCLO_RE = re.compile(r"Cyclomatic:\s*(\d+)\D+(\d+)")


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("usage: verify_kagura_metrics.py <ios-release-build.log>")

    text = Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace")
    if "kagura obfuscation metrics report" not in text:
        raise SystemExit("Kagura metrics report is missing from the Release build log")

    bb_changes = [(int(before), int(after)) for before, after in METRICS_RE.findall(text)]
    cyclo_changes = [(int(before), int(after)) for before, after in CYCLO_RE.findall(text)]
    if not bb_changes:
        raise SystemExit("Kagura metrics report contains no function records")

    increased_bb = sum(after > before for before, after in bb_changes)
    increased_cyclo = sum(after > before for before, after in cyclo_changes)
    if increased_bb == 0 and increased_cyclo == 0:
        raise SystemExit("Kagura metrics show no real CFG increase")

    print(
        f"verified {len(bb_changes)} metric records: "
        f"{increased_bb} increased BB count, {increased_cyclo} increased cyclomatic complexity"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
