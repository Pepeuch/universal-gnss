#!/usr/bin/env python3
"""Validate the public UGA manifest and generate the README status dashboard."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter
from dataclasses import dataclass
from html import escape
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "docs/status/uga_backlog.json"
README_PATH = ROOT / "README.md"
SVG_PATH = ROOT / "docs/status/uga_backlog.svg"
BEGIN_MARKER = "<!-- UGA_STATUS_BEGIN -->"
END_MARKER = "<!-- UGA_STATUS_END -->"
STATUS_ORDER = ("IMPLEMENTED", "PARTIAL", "OPEN", "BLOCKED", "DUPLICATE", "SUPERSEDED", "OBSOLETE")
STATUS_COLORS = {
    "IMPLEMENTED": "#1f8a70",
    "PARTIAL": "#d97706",
    "OPEN": "#3b82f6",
    "BLOCKED": "#c2410c",
    "DUPLICATE": "#6b7280",
    "SUPERSEDED": "#7c3aed",
    "OBSOLETE": "#64748b",
}
TODO_PATTERN = re.compile(r"^\s*- \[([ xX])\]")
ID_PATTERN = re.compile(r"UGA-(\d{3})")
PLAN_STATUS_PATTERN = re.compile(r"^\| `UG-PLAN-\d{3}` \| (IMPLEMENTED|PARTIAL|OPEN|BLOCKED) \|")
V07_START = "### v0.7.0 — Production containerization / deployment architecture"
V08_START = "### v0.8.0 — BlueOS extension"
NON_RELEASE_SECTION = "Non-ROS control/status surface:"
NON_RELEASE_TASKS = (
    "non-ROS status/configuration API contract for platform integrations",
    "authentication/bind-address policy for any exposed HTTP/WebSocket API",
)


class ManifestError(ValueError):
    """Raised when durable backlog evidence violates an invariant."""


@dataclass(frozen=True)
class BacklogData:
    records: dict[str, dict[str, str]]
    baseline_count: int

    @property
    def status_counts(self) -> Counter[str]:
        return Counter(record["status"] for record in self.records.values())

    @property
    def validation_counts(self) -> Counter[str]:
        return Counter(record["validation"] for record in self.records.values() if record["validation"])

    @property
    def todo_counts(self) -> Counter[str]:
        return Counter(record["todo_state"] for record in self.records.values())


def expand_id_spec(spec: str) -> list[str]:
    match = re.fullmatch(r"UGA-(\d{3})(?:\.\.UGA-(\d{3}))?", spec)
    if match is None:
        raise ManifestError(f"invalid stable-ID specification: {spec}")
    start = int(match.group(1))
    end = int(match.group(2) or match.group(1))
    if end < start:
        raise ManifestError(f"descending stable-ID range: {spec}")
    return [f"UGA-{number:03d}" for number in range(start, end + 1)]


def assignment_ids(value: Any) -> Iterable[str]:
    specs = [value] if isinstance(value, str) else value
    if not isinstance(specs, list):
        raise ManifestError("assignment ids must be a string or list of strings")
    for spec in specs:
        if not isinstance(spec, str):
            raise ManifestError("assignment id specification must be a string")
        yield from expand_id_spec(spec)


def apply_assignments(manifest: dict[str, Any], key: str, property_name: str, expected_ids: set[str]) -> dict[str, str]:
    values: dict[str, str] = {}
    for assignment in manifest.get(key, []):
        value = assignment.get(property_name)
        if not isinstance(value, str) or not value:
            raise ManifestError(f"{key} entry has no {property_name}")
        for stable_id in assignment_ids(assignment.get("ids")):
            if stable_id not in expected_ids:
                raise ManifestError(f"{key} references out-of-baseline ID {stable_id}")
            if stable_id in values:
                raise ManifestError(f"{key} assigns {stable_id} more than once")
            values[stable_id] = value
    if set(values) != expected_ids:
        missing = sorted(expected_ids - set(values))
        raise ManifestError(f"{key} does not cover every stable ID; missing {', '.join(missing)}")
    return values


def load_backlog(path: Path = MANIFEST_PATH) -> BacklogData:
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ManifestError(f"cannot read manifest {path}: {error}") from error

    if manifest.get("schema_version") != 1:
        raise ManifestError("unsupported manifest schema_version")
    baseline = manifest.get("baseline", {})
    baseline_count = baseline.get("item_count")
    if not isinstance(baseline_count, int) or baseline_count <= 0:
        raise ManifestError("baseline.item_count must be a positive integer")
    if baseline.get("todo_path") != "TODO.md" or baseline.get("retained_order") != "stable_id_ascending_excluding_removed":
        raise ManifestError("manifest baseline TODO mapping contract is unsupported")
    expected_ids = {f"UGA-{number:03d}" for number in range(1, baseline_count + 1)}
    statuses = apply_assignments(manifest, "status_assignments", "status", expected_ids)
    scopes = apply_assignments(manifest, "scope_assignments", "scope", expected_ids)
    allowed_statuses = set(STATUS_ORDER)
    if not set(statuses.values()) <= allowed_statuses:
        raise ManifestError("manifest contains an unknown STATUS")

    todo = manifest.get("todo", {})
    default_state = todo.get("default_state")
    if default_state not in {"unchecked", "checked", "removed"}:
        raise ManifestError("todo.default_state must be unchecked, checked, or removed")
    todo_states = {stable_id: default_state for stable_id in expected_ids}
    for override in todo.get("overrides", []):
        state = override.get("state")
        if state not in {"unchecked", "checked", "removed"}:
            raise ManifestError("todo override has an invalid state")
        for stable_id in assignment_ids(override.get("ids")):
            if stable_id not in expected_ids:
                raise ManifestError(f"todo override references out-of-baseline ID {stable_id}")
            if todo_states[stable_id] != default_state:
                raise ManifestError(f"todo override assigns {stable_id} more than once")
            todo_states[stable_id] = state

    validations = {stable_id: "" for stable_id in expected_ids}
    for entry in manifest.get("validation", []):
        stable_id = entry.get("id")
        value = entry.get("value")
        if stable_id not in expected_ids or value not in {"HARDWARE_REQUIRED", "HARDWARE_PENDING"}:
            raise ManifestError("validation entry has an invalid ID or value")
        if validations[stable_id]:
            raise ManifestError(f"validation assigns {stable_id} more than once")
        validations[stable_id] = value

    removed: dict[str, dict[str, str]] = {}
    for entry in manifest.get("removed", []):
        stable_id = entry.get("id")
        if stable_id not in expected_ids or stable_id in removed:
            raise ManifestError("removed entry has an invalid or duplicate ID")
        resolution = entry.get("resolution")
        if resolution not in {"implemented", "duplicate"}:
            raise ManifestError(f"removed entry {stable_id} has an invalid resolution")
        removed[stable_id] = entry
    removed_ids = {stable_id for stable_id, state in todo_states.items() if state == "removed"}
    if set(removed) != removed_ids:
        raise ManifestError("removed records and TODO removed states differ")

    for stable_id, entry in removed.items():
        status = statuses[stable_id]
        if entry["resolution"] == "implemented" and status != "IMPLEMENTED":
            raise ManifestError(f"removed implemented item {stable_id} is not IMPLEMENTED")
        if entry["resolution"] == "duplicate" and status != "DUPLICATE":
            raise ManifestError(f"removed duplicate item {stable_id} is not DUPLICATE")

    duplicate_ids = {stable_id for stable_id, status in statuses.items() if status == "DUPLICATE"}
    duplicate_removed = {stable_id for stable_id, entry in removed.items() if entry["resolution"] == "duplicate"}
    if duplicate_ids != duplicate_removed:
        raise ManifestError("every DUPLICATE must be an intentionally removed duplicate record")
    for stable_id in duplicate_ids:
        canonical_id = removed[stable_id].get("canonical_id")
        if canonical_id not in expected_ids or statuses[canonical_id] == "DUPLICATE":
            raise ManifestError(f"duplicate {stable_id} has no canonical non-DUPLICATE item")
        seen = {stable_id}
        while canonical_id in duplicate_ids:
            if canonical_id in seen:
                raise ManifestError(f"duplicate canonicalization has a cycle at {stable_id}")
            seen.add(canonical_id)
            canonical_id = removed[canonical_id].get("canonical_id", "")

    records = {
        stable_id: {
            "status": statuses[stable_id],
            "scope": scopes[stable_id],
            "validation": validations[stable_id],
            "todo_state": todo_states[stable_id],
        }
        for stable_id in sorted(expected_ids)
    }
    if len(records) != baseline_count:
        raise ManifestError("baseline conservation failed")
    return BacklogData(records=records, baseline_count=baseline_count)


def validate_todo(data: BacklogData, todo_path: Path = README_PATH.parent / "TODO.md") -> None:
    try:
        todo = todo_path.read_text(encoding="utf-8")
    except OSError as error:
        raise ManifestError(f"cannot read TODO file {todo_path}: {error}") from error
    if not any(TODO_PATTERN.match(line) for line in todo.splitlines()):
        raise ManifestError("TODO contains no checklist items")


def dashboard_counts(data: BacklogData) -> dict[str, int | Counter[str]]:
    todo_counts = data.todo_counts
    return {
        "baseline": data.baseline_count,
        "remaining": todo_counts["unchecked"],
        "closed": data.baseline_count - todo_counts["unchecked"],
        "removed": todo_counts["removed"],
        "status": data.status_counts,
        "validation": data.validation_counts,
    }


def project_progress_counts(todo_path: Path = README_PATH.parent / "TODO.md") -> dict[str, int]:
    states = [
        match.group(1).lower()
        for line in todo_path.read_text(encoding="utf-8").splitlines()
        if (match := TODO_PATTERN.match(line))
    ]
    return {"total": len(states), "complete": states.count("x"), "not_started": states.count(" ")}


def plan_status_counts(todo_path: Path = README_PATH.parent / "TODO.md") -> Counter[str]:
    return Counter(
        match.group(1)
        for line in todo_path.read_text(encoding="utf-8").splitlines()
        if (match := PLAN_STATUS_PATTERN.match(line))
    )


def release_progress_counts(todo_path: Path = README_PATH.parent / "TODO.md") -> dict[str, int]:
    todo = todo_path.read_text(encoding="utf-8")
    start, end = todo.find(V07_START), todo.find(V08_START)
    if start == -1 or end == -1 or end <= start:
        raise ManifestError("TODO does not contain an ordered v0.7 release scope")
    scope = todo[start:end]
    scope = scope.replace(NON_RELEASE_SECTION + scope.split(NON_RELEASE_SECTION, 1)[1].split("Deployment validation gates:", 1)[0], "") if NON_RELEASE_SECTION in scope else scope
    states = [
        match.group(1).lower()
        for line in scope.splitlines()
        if (match := TODO_PATTERN.match(line)) and not any(task in line for task in NON_RELEASE_TASKS)
    ]
    return {"total": len(states), "complete": states.count("x"), "not_started": states.count(" ")}


def render_svg(data: BacklogData) -> str:
    counts = dashboard_counts(data)
    status_counts = counts["status"]
    validation_counts = counts["validation"]
    width = 760
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} 326" role="img" aria-labelledby="title desc">',
        '<title id="title">Universal GNSS development status</title>',
        f'<desc id="desc">{counts["remaining"]} of {counts["baseline"]} baseline backlog items remain unchecked; {counts["closed"]} are checked or intentionally removed.</desc>',
        '<rect width="760" height="326" fill="#f8fafc" rx="12"/>',
        '<text x="28" y="38" font-family="sans-serif" font-size="22" font-weight="700" fill="#0f172a">Universal GNSS Development Status</text>',
        f'<text x="28" y="66" font-family="sans-serif" font-size="15" fill="#334155">Baseline {counts["baseline"]} · Unchecked {counts["remaining"]} · Checked/intentionally removed {counts["closed"]}</text>',
        '<rect x="28" y="82" width="704" height="14" rx="7" fill="#dbe4ef"/>',
        f'<rect x="28" y="82" width="{704 * counts["closed"] / counts["baseline"]:.2f}" height="14" rx="7" fill="#1f8a70"/>',
        '<text x="28" y="126" font-family="sans-serif" font-size="15" font-weight="700" fill="#0f172a">Lifecycle status</text>',
    ]
    y = 150
    for status in STATUS_ORDER:
        count = status_counts[status]
        if count == 0:
            continue
        bar_width = 430 * count / data.baseline_count
        lines.extend([
            f'<text x="28" y="{y + 12}" font-family="sans-serif" font-size="13" fill="#334155">{escape(status.title())} {count}</text>',
            f'<rect x="188" y="{y}" width="430" height="14" rx="7" fill="#dbe4ef"/>',
            f'<rect x="188" y="{y}" width="{bar_width:.2f}" height="14" rx="7" fill="{STATUS_COLORS[status]}"/>',
        ])
        y += 25
    hardware_required = validation_counts["HARDWARE_REQUIRED"]
    hardware_pending = validation_counts["HARDWARE_PENDING"]
    lines.extend([
        '<text x="28" y="306" font-family="sans-serif" font-size="13" font-weight="700" fill="#0f172a">Validation dependencies (orthogonal)</text>',
        f'<text x="320" y="306" font-family="sans-serif" font-size="13" fill="#334155">Hardware required: {hardware_required} · Hardware pending: {hardware_pending}</text>',
        '</svg>',
        '',
    ])
    return "\n".join(lines)


def render_readme_block(data: BacklogData) -> str:
    counts = dashboard_counts(data)
    project = project_progress_counts()
    release = release_progress_counts()
    plans = plan_status_counts()
    return "\n".join([
        BEGIN_MARKER,
        "### Current Release Progress — v0.6 → v0.7",
        "",
        f"**{release['complete']} / {release['total']}** release-scoped tasks complete ({release['complete'] * 100 / release['total']:.2f}%), **{release['not_started']}** not started.",
        "",
        "Calculation: equal-weight checked tasks in the v0.7 Docker/deployment, lifecycle, device, configuration, health, networking, validation, and documentation sections of `TODO.md`. The later non-ROS API surface and v0.8 BlueOS scope are excluded; PARTIAL/BLOCKED receive no fractional credit. New mandatory v0.7 work may increase this denominator.",
        "",
        "### Project Roadmap Progress",
        "",
        f"Current identified project work: **{project['complete']} / {project['total']}** complete ({project['complete'] * 100 / project['total']:.2f}%), **{project['not_started']}** not started.",
        "",
        f"Calculation: every current TODO checklist item has equal weight; only checked items count as complete. The `UG-PLAN` register is reported separately as **{plans['IMPLEMENTED']} COMPLETE**, **{plans['PARTIAL']} PARTIAL**, **{plans['BLOCKED']} BLOCKED**, and **{plans['OPEN']} NOT_STARTED**. PARTIAL/BLOCKED phases receive no fractional credit. This indicator includes implementation planning; it does not alter the 205-item UGA metric below.",
        "",
        "### UGA Quality / Audit Progress",
        "",
        "![Generated UGA backlog status](docs/status/uga_backlog.svg)",
        "",
        f"Baseline: **{counts['baseline']}** audited items. Unchecked worklist entries: **{counts['remaining']}**. Checked or intentionally removed: **{counts['closed']}**.",
        "",
        "The 33 accounted entries are 21 checked findings, 4 implemented findings intentionally removed from the worklist, and 8 intentionally removed duplicates; they are not a claim of 33 implemented findings.",
        "",
        "Generated from [`docs/status/uga_backlog.json`](docs/status/uga_backlog.json). Update with `python3 scripts/update_backlog_status.py`; verify CI/local state with `python3 scripts/update_backlog_status.py --check`.",
        "",
        END_MARKER,
    ])


def render_readme(readme: str, block: str) -> str:
    begin = readme.find(BEGIN_MARKER)
    end = readme.find(END_MARKER)
    if begin == -1 and end == -1:
        anchor = "Current phase: post-`v0.6.x` stabilization."
        if anchor not in readme:
            raise ManifestError("README does not contain the dashboard insertion anchor")
        return readme.replace(anchor, f"{anchor}\n\n{block}", 1)
    if begin == -1 or end == -1 or end < begin:
        raise ManifestError("README has malformed UGA status markers")
    end += len(END_MARKER)
    return readme[:begin] + block + readme[end:]


def generated_outputs(data: BacklogData) -> tuple[str, str]:
    readme = README_PATH.read_text(encoding="utf-8")
    return render_readme(readme, render_readme_block(data)), render_svg(data)


def write_or_check(data: BacklogData, check: bool) -> None:
    expected_readme, expected_svg = generated_outputs(data)
    actual_svg = SVG_PATH.read_text(encoding="utf-8") if SVG_PATH.exists() else ""
    actual_readme = README_PATH.read_text(encoding="utf-8")
    stale = []
    if actual_readme != expected_readme:
        stale.append(str(README_PATH.relative_to(ROOT)))
    if actual_svg != expected_svg:
        stale.append(str(SVG_PATH.relative_to(ROOT)))
    if stale and check:
        raise ManifestError("generated artifacts are stale: " + ", ".join(stale))
    if not check:
        if actual_readme != expected_readme:
            README_PATH.write_text(expected_readme, encoding="utf-8")
        if actual_svg != expected_svg:
            SVG_PATH.write_text(expected_svg, encoding="utf-8")
    print(f"UGA baseline={data.baseline_count} remaining={dashboard_counts(data)['remaining']} status={dict(data.status_counts)}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="fail when generated files are stale")
    args = parser.parse_args(argv)
    try:
        data = load_backlog()
        validate_todo(data)
        write_or_check(data, args.check)
    except ManifestError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
