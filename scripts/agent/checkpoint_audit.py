#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "docs/status/uga_backlog.json"
BASE = ROOT / ".agent/shared/checkpoints"
LIFECYCLES = ("active", "blocked", "retained", "closed")
ID_RE = re.compile(r"\bUGA-\d+\b")


def walk(obj):
    if isinstance(obj, dict):
        yield obj
        for v in obj.values():
            yield from walk(v)
    elif isinstance(obj, list):
        for v in obj:
            yield from walk(v)


def load_manifest():
    if not MANIFEST.exists():
        return {}
    data = json.loads(MANIFEST.read_text())
    out = {}
    for obj in walk(data):
        if not isinstance(obj, dict) or "status" not in obj:
            continue
        fid = obj.get("id") or obj.get("finding_id") or obj.get("uga_id")
        if isinstance(fid, str) and ID_RE.fullmatch(fid):
            out[fid] = obj
    return out


def shared_entries():
    entries = {}
    duplicates = []
    for lifecycle in LIFECYCLES:
        d = BASE / lifecycle
        if not d.exists():
            continue
        for path in sorted(d.glob("*.md")):
            if path.name == "README.md":
                continue
            m = ID_RE.search(path.name)
            if not m:
                m = ID_RE.search(path.read_text(errors="replace"))
            if not m:
                continue
            fid = m.group(0)
            if fid in entries:
                duplicates.append((fid, entries[fid][1], path))
            else:
                entries[fid] = (lifecycle, path)
    return entries, duplicates


def allowed_lifecycle(status, validation):
    status = str(status).upper()
    validation = str(validation or "").upper()
    if status == "OPEN":
        return {"active"}
    if status == "PARTIAL":
        return {"active", "blocked"} if validation == "HARDWARE_REQUIRED" else {"active"}
    if status == "BLOCKED":
        return {"blocked"}
    if status == "IMPLEMENTED":
        return {"retained", "closed"}
    return set()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    manifest = load_manifest()
    entries, duplicates = shared_entries()
    problems, warnings = [], []

    for fid, a, b in duplicates:
        problems.append(f"{fid}: duplicated shared checkpoint: {a} and {b}")

    for fid, (lifecycle, path) in sorted(entries.items()):
        item = manifest.get(fid)
        if item is None:
            warnings.append(f"{fid}: checkpoint exists but finding is absent from manifest: {path}")
            continue
        allowed = allowed_lifecycle(item.get("status"), item.get("validation"))
        if allowed and lifecycle not in allowed:
            problems.append(
                f"{fid}: status={item.get('status')} validation={item.get('validation')} "
                f"but checkpoint is in {lifecycle}/; expected one of {sorted(allowed)}"
            )

    for fid, item in sorted(manifest.items()):
        status = str(item.get("status", "")).upper()
        if status in {"OPEN", "PARTIAL", "BLOCKED"} and fid not in entries:
            warnings.append(f"{fid}: live finding has no shared checkpoint (manual review only)")

    print("Shared checkpoint audit")
    print(f"  manifest findings: {len(manifest)}")
    print(f"  shared checkpoints: {len(entries)}")
    print(f"  problems: {len(problems)}")
    print(f"  warnings: {len(warnings)}")

    for p in problems:
        print(f"ERROR: {p}")
    for w in warnings:
        print(f"WARN: {w}")

    return 1 if args.check and problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
