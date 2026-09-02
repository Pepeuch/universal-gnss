#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / ".agent/shared/checkpoints"

README = """# Shared checkpoints

This directory contains intentionally versioned resumable agent memory.

- `active/`: OPEN/PARTIAL work worth resuming.
- `blocked/`: real blocker with exact unblock condition.
- `retained/`: IMPLEMENTED detail expected to be reused.
- `closed/`: compact IMPLEMENTED anti-rediscovery records.

The canonical backlog source remains TODO/manifest/ledger.
Local working checkpoints remain under ignored `.agent/checkpoints/`.
"""

INDEX = """# Shared checkpoint index

This file is navigation only, not the backlog source of truth.

## Active

## Blocked

## Retained

## Closed
"""


def main():
    for name in ("active", "blocked", "retained", "closed"):
        d = BASE / name
        d.mkdir(parents=True, exist_ok=True)
        keep = d / ".gitkeep"
        if not keep.exists():
            keep.write_text("")
    readme = BASE / "README.md"
    if not readme.exists():
        readme.write_text(README)
    index = BASE / "INDEX.md"
    if not index.exists():
        index.write_text(INDEX)
    print(BASE.relative_to(ROOT))


if __name__ == "__main__":
    main()
