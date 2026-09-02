#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "docs/status/uga_backlog.json"
CP_BASE = ROOT / ".agent/shared/checkpoints"
ID_RE = re.compile(r"UGA-(\d+)")


def walk(obj):
    if isinstance(obj, dict):
        yield obj
        for v in obj.values():
            yield from walk(v)
    elif isinstance(obj, list):
        for v in obj:
            yield from walk(v)


def load_findings():
    data = json.loads(MANIFEST.read_text())
    out = {}
    for obj in walk(data):
        if not isinstance(obj, dict) or "status" not in obj:
            continue
        fid = obj.get("id") or obj.get("finding_id") or obj.get("uga_id")
        if isinstance(fid, str) and ID_RE.fullmatch(fid):
            out[fid] = obj
    return out


def checkpoint_for(fid):
    for lifecycle in ("active", "blocked", "retained", "closed"):
        d = CP_BASE / lifecycle
        if not d.exists():
            continue
        matches = sorted(d.glob(f"{fid}*.md"))
        if matches:
            return lifecycle, matches[0]
    return None, None


def n(fid):
    m = ID_RE.fullmatch(fid)
    return int(m.group(1)) if m else 10**9


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--status", action="append", choices=["PARTIAL", "OPEN", "BLOCKED"])
    args = ap.parse_args()

    priority = args.status or ["PARTIAL", "OPEN", "BLOCKED"]
    data = load_findings()

    chosen = None
    for wanted in priority:
        cands = sorted(
            [fid for fid, obj in data.items() if str(obj.get("status", "")).upper() == wanted],
            key=n,
        )
        if cands:
            chosen = cands[0]
            break

    if not chosen:
        print("NEXT_FINDING=")
        print("STATUS=NONE")
        return

    obj = data[chosen]
    lifecycle, path = checkpoint_for(chosen)
    print(f"NEXT_FINDING={chosen}")
    print(f"STATUS={str(obj.get('status', '')).upper()}")
    if obj.get("scope") is not None:
        print(f"SCOPE={obj.get('scope')}")
    if obj.get("validation") is not None:
        print(f"VALIDATION={obj.get('validation')}")
    print(f"CHECKPOINT_LIFECYCLE={lifecycle or 'NONE'}")
    print(f"CHECKPOINT={path.relative_to(ROOT) if path else ''}")


if __name__ == "__main__":
    main()
