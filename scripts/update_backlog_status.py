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
RELEASE_DEPENDENCY_CLASSES = (
    "NATIVE_ARM64_REQUIRED",
    "PUBLICATION_REQUIRED",
    "HARDWARE_RECEIVER_REQUIRED",
    "USB_PHYSICAL_ACTION_REQUIRED",
    "POWER_CYCLE_REQUIRED",
    "ALREADY_PARTIAL",
    "DESIGN_CONTRACT_REQUIRED",
    "ROBOT_REQUIRED",
    "LONG_DURATION_REQUIRED",
)
HARDWARE_DEPENDENCY_CLASSES = frozenset({
    "NATIVE_ARM64_REQUIRED",
    "HARDWARE_RECEIVER_REQUIRED",
    "USB_PHYSICAL_ACTION_REQUIRED",
    "POWER_CYCLE_REQUIRED",
    "ROBOT_REQUIRED",
    "LONG_DURATION_REQUIRED",
})


class ManifestError(ValueError):
    """Raised when durable backlog evidence violates an invariant."""


@dataclass(frozen=True)
class BacklogData:
    records: dict[str, dict[str, str]]
    baseline_count: int
    release_dependencies: dict[str, str]

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


def load_release_dependencies(manifest: dict[str, Any]) -> dict[str, str]:
    section = manifest.get("release_gate_dependencies")
    if not isinstance(section, dict) or section.get("release") != "v0.7":
        raise ManifestError("release_gate_dependencies must identify v0.7")
    entries = section.get("gates")
    if not isinstance(entries, list):
        raise ManifestError("release_gate_dependencies.gates must be a list")
    dependencies: dict[str, str] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            raise ManifestError("release dependency entry must be an object")
        task, classification = entry.get("task"), entry.get("classification")
        if not isinstance(task, str) or not task:
            raise ManifestError("release dependency entry has no task")
        if classification not in RELEASE_DEPENDENCY_CLASSES:
            raise ManifestError(f"release dependency {task!r} has an unknown classification")
        if task in dependencies:
            raise ManifestError(f"release dependency assigns {task!r} more than once")
        dependencies[task] = classification
    return dependencies


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
    return BacklogData(
        records=records,
        baseline_count=baseline_count,
        release_dependencies=load_release_dependencies(manifest),
    )


def release_task_states(todo_path: Path = README_PATH.parent / "TODO.md") -> dict[str, str]:
    todo = todo_path.read_text(encoding="utf-8")
    start, end = todo.find(V07_START), todo.find(V08_START)
    if start == -1 or end == -1 or end <= start:
        raise ManifestError("TODO does not contain an ordered v0.7 release scope")
    scope = todo[start:end]
    if NON_RELEASE_SECTION in scope:
        before, after = scope.split(NON_RELEASE_SECTION, 1)
        if "Deployment validation gates:" not in after:
            raise ManifestError("TODO has malformed non-release v0.7 section")
        scope = before + after.split("Deployment validation gates:", 1)[1]
    states: dict[str, str] = {}
    for line in scope.splitlines():
        match = TODO_PATTERN.match(line)
        if match is None or any(task in line for task in NON_RELEASE_TASKS):
            continue
        task = line[match.end():].strip()
        if task in states:
            raise ManifestError(f"TODO repeats v0.7 checklist task {task!r}")
        states[task] = "checked" if match.group(1).lower() == "x" else "unchecked"
    return states


def validate_todo(data: BacklogData, todo_path: Path = README_PATH.parent / "TODO.md") -> None:
    try:
        todo = todo_path.read_text(encoding="utf-8")
    except OSError as error:
        raise ManifestError(f"cannot read TODO file {todo_path}: {error}") from error
    if not any(TODO_PATTERN.match(line) for line in todo.splitlines()):
        raise ManifestError("TODO contains no checklist items")
    unchecked_release_tasks = {
        task for task, state in release_task_states(todo_path).items() if state == "unchecked"
    }
    classified_tasks = set(data.release_dependencies)
    if classified_tasks != unchecked_release_tasks:
        missing = sorted(unchecked_release_tasks - classified_tasks)
        extra = sorted(classified_tasks - unchecked_release_tasks)
        details = []
        if missing:
            details.append("missing " + ", ".join(repr(task) for task in missing))
        if extra:
            details.append("not unchecked release tasks " + ", ".join(repr(task) for task in extra))
        raise ManifestError("release dependency classification is out of sync: " + "; ".join(details))


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


def release_dependency_counts(data: BacklogData) -> Counter[str]:
    return Counter(data.release_dependencies.values())


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
    states = list(release_task_states(todo_path).values())
    return {"total": len(states), "complete": states.count("checked"), "not_started": states.count("unchecked")}


def render_svg(data: BacklogData) -> str:
    counts = dashboard_counts(data)
    release = release_progress_counts()
    project = project_progress_counts()
    status_counts = counts["status"]
    dependencies = release_dependency_counts(data)
    hardware_dependent = sum(dependencies[classification] for classification in HARDWARE_DEPENDENCY_CLASSES)
    width, height = 760, 570
    bar_x, bar_width = 28, 704
    def progress_section(title: str, subtitle: str, complete: int, total: int, y: int, color: str) -> list[str]:
        percent = complete * 100 / total
        return [
            f'<text x="28" y="{y}" font-family="sans-serif" font-size="16" font-weight="700" fill="#0f172a">{title}</text>',
            f'<text x="28" y="{y + 21}" font-family="sans-serif" font-size="13" fill="#334155">{subtitle}</text>',
            f'<text x="732" y="{y + 21}" text-anchor="end" font-family="sans-serif" font-size="13" font-weight="700" fill="#0f172a">{complete} / {total} complete · {percent:.2f}%</text>',
            f'<rect x="{bar_x}" y="{y + 31}" width="{bar_width}" height="14" rx="7" fill="#dbe4ef"/>',
            f'<rect x="{bar_x}" y="{y + 31}" width="{bar_width * complete / total:.2f}" height="14" rx="7" fill="{color}"/>',
        ]
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
        '<title id="title">Universal GNSS development status</title>',
        f'<desc id="desc">{counts["remaining"]} of {counts["baseline"]} baseline backlog items remain unchecked; {counts["closed"]} are checked or intentionally removed.</desc>',
        f'<rect width="{width}" height="{height}" fill="#f8fafc" rx="12"/>',
        '<text x="28" y="38" font-family="sans-serif" font-size="22" font-weight="700" fill="#0f172a">Universal GNSS Development Status</text>',
    ]
    lines.extend(progress_section("CURRENT RELEASE", "v0.6 → v0.7", release["complete"], release["total"], 68, "#2563eb"))
    lines.extend(progress_section("PROJECT ROADMAP", "Currently identified project work completed", project["complete"], project["total"], 132, "#7c3aed"))
    lines.extend(progress_section("UGA QUALITY / AUDIT", "Checked or intentionally removed", counts["closed"], counts["baseline"], 196, "#1f8a70"))
    lines.extend([
        f'<text x="28" y="270" font-family="sans-serif" font-size="13" fill="#334155">Baseline {counts["baseline"]} · Unchecked {counts["remaining"]} · Checked/intentionally removed {counts["closed"]}</text>',
        '<text x="28" y="305" font-family="sans-serif" font-size="15" font-weight="700" fill="#0f172a">UGA Lifecycle status</text>',
    ])
    y = 329
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
    lines.extend([
        '<text x="28" y="500" font-family="sans-serif" font-size="13" font-weight="700" fill="#0f172a">Validation dependencies (orthogonal)</text>',
        f'<text x="28" y="523" font-family="sans-serif" font-size="13" fill="#334155">Open v0.7 gate classifications (exclusive): receiver hardware {dependencies["HARDWARE_RECEIVER_REQUIRED"]} · USB action {dependencies["USB_PHYSICAL_ACTION_REQUIRED"]} · power cycle {dependencies["POWER_CYCLE_REQUIRED"]} · robot {dependencies["ROBOT_REQUIRED"]}</text>',
        f'<text x="28" y="545" font-family="sans-serif" font-size="13" fill="#334155">native arm64 {dependencies["NATIVE_ARM64_REQUIRED"]} · long duration {dependencies["LONG_DURATION_REQUIRED"]} · design contract {dependencies["DESIGN_CONTRACT_REQUIRED"]} · publication {dependencies["PUBLICATION_REQUIRED"]} · already partial {dependencies["ALREADY_PARTIAL"]}</text>',
        f'<text x="28" y="565" font-family="sans-serif" font-size="12" fill="#475569">Open hardware-dependent gates: {hardware_dependent} (derived overlap group; do not add to the exclusive classifications). External-LAN DDS remains a separate blocked acceptance matrix outside the 65-item checklist.</text>',
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
