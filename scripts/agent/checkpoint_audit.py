#!/usr/bin/env python3
"""Validate UGA ownership plus index/lifecycle integrity for shared checkpoints."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "docs/status/uga_backlog.json"
BASE = ROOT / ".agent/shared/checkpoints"
LIFECYCLES = ("active", "blocked", "retained", "closed")
ID_SPEC_RE = re.compile(r"UGA-(\d{3})(?:\.\.UGA-(\d{3}))?")
OWNERSHIP_FILENAME_RE = re.compile(r"(?<![A-Za-z0-9])(UGA-\d{3})(?![A-Za-z0-9])")
INDEX_PATH_RE = re.compile(r"`((?:active|blocked|retained|closed)/[^`]+\.md)`")
DECLARED_LIFECYCLE_RE = re.compile(
    r"^Lifecycle:\s*(ACTIVE|BLOCKED|RETAINED|CLOSED)\b", re.IGNORECASE | re.MULTILINE
)


def expand_id_spec(spec: str) -> list[str]:
    match = ID_SPEC_RE.fullmatch(spec)
    if match is None:
        raise ValueError(f"invalid UGA ID specification: {spec}")
    first, last = int(match.group(1)), int(match.group(2) or match.group(1))
    if last < first:
        raise ValueError(f"descending UGA ID specification: {spec}")
    return [f"UGA-{number:03d}" for number in range(first, last + 1)]


def assignment_ids(value: object) -> list[str]:
    specs = [value] if isinstance(value, str) else value
    if not isinstance(specs, list) or not all(isinstance(spec, str) for spec in specs):
        raise ValueError("manifest assignment IDs must be a string or list of strings")
    return [stable_id for spec in specs for stable_id in expand_id_spec(spec)]


def load_manifest(path: Path = MANIFEST) -> dict[str, dict[str, str]]:
    """Expand the compact public manifest into status/validation records."""
    if not path.exists():
        return {}
    data = json.loads(path.read_text(encoding="utf-8"))
    baseline = data.get("baseline", {})
    item_count = baseline.get("item_count")
    if not isinstance(item_count, int) or item_count <= 0:
        raise ValueError("manifest baseline.item_count must be a positive integer")

    findings: dict[str, dict[str, str]] = {}
    for assignment in data.get("status_assignments", []):
        if not isinstance(assignment, dict) or not isinstance(assignment.get("status"), str):
            raise ValueError("manifest status assignment is invalid")
        for stable_id in assignment_ids(assignment.get("ids")):
            if stable_id in findings:
                raise ValueError(f"manifest assigns {stable_id} more than once")
            findings[stable_id] = {"status": assignment["status"], "validation": ""}

    expected = {f"UGA-{number:03d}" for number in range(1, item_count + 1)}
    if set(findings) != expected:
        missing = ", ".join(sorted(expected - set(findings)))
        extra = ", ".join(sorted(set(findings) - expected))
        raise ValueError(f"manifest status assignments do not cover baseline (missing={missing}; extra={extra})")

    for assignment in data.get("validation", []):
        if not isinstance(assignment, dict):
            raise ValueError("manifest validation assignment is invalid")
        stable_id, value = assignment.get("id"), assignment.get("value")
        if stable_id not in findings or not isinstance(value, str):
            raise ValueError("manifest validation assignment is invalid")
        if findings[stable_id]["validation"]:
            raise ValueError(f"manifest assigns validation for {stable_id} more than once")
        findings[stable_id]["validation"] = value
    return findings


def shared_entries(base: Path = BASE):
    """Return explicitly owned checkpoints and duplicate ownership declarations.

    A UGA ID in checkpoint prose is evidence, not ownership. Ownership is
    declared by a stable UGA ID in the checkpoint filename so mixed-plan and
    evidence checkpoints cannot accidentally claim the first finding mentioned.
    """
    entries: dict[str, tuple[str, Path]] = {}
    duplicates: list[tuple[str, Path, Path]] = []
    for lifecycle in LIFECYCLES:
        directory = base / lifecycle
        if not directory.exists():
            continue
        for path in sorted(directory.glob("*.md")):
            if path.name == "README.md":
                continue
            match = OWNERSHIP_FILENAME_RE.search(path.name)
            if match is None:
                continue
            stable_id = match.group(1)
            if stable_id in entries:
                duplicates.append((stable_id, entries[stable_id][1], path))
            else:
                entries[stable_id] = (lifecycle, path)
    return entries, duplicates


def all_shared_entries(base: Path = BASE) -> dict[str, Path]:
    entries: dict[str, Path] = {}
    for lifecycle in LIFECYCLES:
        directory = base / lifecycle
        if not directory.exists():
            continue
        for path in sorted(directory.glob("*.md")):
            if path.name == "README.md":
                continue
            entries[path.relative_to(base).as_posix()] = path
    return entries


def audit_shared_index(base: Path = BASE) -> tuple[dict[str, Path], list[str], list[str]]:
    entries = all_shared_entries(base)
    problems: list[str] = []
    warnings: list[str] = []
    index_path = base / "INDEX.md"
    if not index_path.exists():
        return entries, [f"shared checkpoint index is missing: {index_path}"], warnings

    references = INDEX_PATH_RE.findall(index_path.read_text(encoding="utf-8"))
    reference_counts = {reference: references.count(reference) for reference in set(references)}
    for reference, count in sorted(reference_counts.items()):
        if count != 1:
            problems.append(f"shared checkpoint index references {reference} {count} times")
        if reference not in entries:
            problems.append(f"shared checkpoint index references missing file: {reference}")
    for relative_path in sorted(set(entries) - set(references)):
        problems.append(f"shared checkpoint is not indexed: {relative_path}")

    basenames: dict[str, str] = {}
    for relative_path, path in entries.items():
        if path.name in basenames:
            problems.append(
                f"shared checkpoint filename exists in multiple lifecycles: "
                f"{basenames[path.name]} and {relative_path}"
            )
        else:
            basenames[path.name] = relative_path

        declared = DECLARED_LIFECYCLE_RE.search(path.read_text(encoding="utf-8"))
        if declared is not None:
            actual = relative_path.split("/", 1)[0].upper()
            if declared.group(1).upper() != actual:
                problems.append(
                    f"{relative_path}: declares lifecycle {declared.group(1).upper()} "
                    f"but is stored in {actual}"
                )
    return entries, problems, warnings


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


def audit(manifest_path: Path = MANIFEST, checkpoint_base: Path = BASE):
    manifest = load_manifest(manifest_path)
    entries, duplicates = shared_entries(checkpoint_base)
    problems, warnings = [], []

    for stable_id, first, second in duplicates:
        problems.append(f"{stable_id}: duplicated shared checkpoint: {first} and {second}")

    for stable_id, (lifecycle, path) in sorted(entries.items()):
        item = manifest.get(stable_id)
        if item is None:
            warnings.append(f"{stable_id}: checkpoint exists but finding is absent from manifest: {path}")
            continue
        allowed = allowed_lifecycle(item["status"], item["validation"])
        if allowed and lifecycle not in allowed:
            problems.append(
                f"{stable_id}: status={item['status']} validation={item['validation']} "
                f"but checkpoint is in {lifecycle}/; expected one of {sorted(allowed)}"
            )
    return manifest, entries, problems, warnings


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    manifest, entries, problems, warnings = audit()
    all_entries, shared_problems, shared_warnings = audit_shared_index()
    problems.extend(shared_problems)
    warnings.extend(shared_warnings)
    print("Shared checkpoint audit")
    print(f"  manifest findings: {len(manifest)}")
    print(f"  UGA-owned checkpoints: {len(entries)}")
    print(f"  all indexed shared checkpoints: {len(all_entries)}")
    print(f"  problems: {len(problems)}")
    print(f"  warnings: {len(warnings)}")
    for problem in problems:
        print(f"ERROR: {problem}")
    for warning in warnings:
        print(f"WARN: {warning}")
    return 1 if args.check and problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
