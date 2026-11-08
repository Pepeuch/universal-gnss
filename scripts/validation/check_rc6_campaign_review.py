#!/usr/bin/env python3
"""Offline link, evidence, SVG and targeted credential checks for this review."""

import hashlib
import json
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[2]
METRICS = ROOT / "docs/validation/evidence/2026-11-08-rc6-mowing.metrics.json"
SERIES = ROOT / "docs/validation/evidence/2026-11-08-rc6-mowing.series.json"
SECRET_PATTERNS = [
    re.compile(r"-----BEGIN (?:OPENSSH |RSA |EC |DSA )?PRIVATE KEY-----"),
    re.compile(r"\bgh[pousr]_[A-Za-z0-9]{30,}\b|\bgithub_pat_[A-Za-z0-9_]{30,}\b"),
    re.compile(r"[a-z]+://[^\s/@:]+:[^\s/@]+@", re.I),
]
SENSITIVE_KEY = re.compile(r"password|credential|secret|token", re.I)


def json_secrets(value, where):
    problems = []
    if isinstance(value, dict):
        for key, item in value.items():
            if SENSITIVE_KEY.search(key) and item not in (None, False, "", "omitted", "[REDACTED]"):
                problems.append(f"credential-like JSON value at {where}/{key}")
            problems.extend(json_secrets(item, f"{where}/{key}"))
    elif isinstance(value, list):
        for index, item in enumerate(value):
            problems.extend(json_secrets(item, f"{where}/{index}"))
    return problems


def anchors(text):
    found = set(re.findall(r'\bid=["\']([^"\']+)["\']', text))
    occurrences = {}
    for heading in re.findall(r"^#{1,6}\s+(.+)$", text, re.M):
        slug = re.sub(r"[^\w\- ]", "", heading.lower()).replace(" ", "-")
        count = occurrences.get(slug, 0)
        occurrences[slug] = count + 1
        found.add(f"{slug}-{count}" if count else slug)
    return found


def main():
    names = subprocess.check_output([
        "git", "ls-files", "--modified", "--others", "--exclude-standard", "-z",
    ], cwd=ROOT).decode().split("\0")
    paths = sorted({ROOT / name for name in names if name})
    failures = []
    checked_links = 0
    for path in paths:
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        if any(pattern.search(text) for pattern in SECRET_PATTERNS):
            failures.append(f"credential pattern requires review: {path.relative_to(ROOT)}")
        if path.suffix == ".json":
            failures.extend(json_secrets(json.loads(text), str(path.relative_to(ROOT))))
        if path.suffix == ".md":
            prose = re.sub(r"```.*?```", "", text, flags=re.S)
            for target in re.findall(r"!?\[[^\]]*\]\(([^)]+)\)", prose):
                target = target.strip().strip("<>")
                parsed = urlsplit(target)
                if parsed.scheme or parsed.netloc:
                    continue
                destination = (path.parent / unquote(parsed.path)).resolve() if parsed.path else path
                checked_links += 1
                if not destination.is_file():
                    failures.append(f"missing link from {path.relative_to(ROOT)}: {target}")
                elif parsed.fragment and destination.suffix == ".md":
                    if unquote(parsed.fragment) not in anchors(destination.read_text()):
                        failures.append(f"missing anchor from {path.relative_to(ROOT)}: {target}")
        if path.suffix == ".svg":
            tree = ET.fromstring(text)
            if any(element.tag.rsplit("}", 1)[-1] == "script" for element in tree.iter()):
                failures.append(f"executable SVG content: {path.relative_to(ROOT)}")
            identifiers = {element.attrib["id"] for element in tree.iter() if "id" in element.attrib}
            for element in tree.iter():
                for key, value in element.attrib.items():
                    if key.rsplit("}", 1)[-1] == "href" and value.startswith("#"):
                        if value[1:] not in identifiers:
                            failures.append(f"missing SVG reference: {path.relative_to(ROOT)}")

    svg = ROOT / "docs/status/uga_backlog.svg"
    baseline = subprocess.check_output(["git", "show", "HEAD:docs/status/uga_backlog.svg"], cwd=ROOT)
    if svg.read_bytes() != baseline:
        failures.append("status SVG changed despite frozen canonical counts")
    for text in ["61 / 65", "88 / 194", "33 / 205"]:
        if text not in svg.read_text():
            failures.append(f"status SVG missing {text}")
    ET.fromstring(svg.read_bytes())
    metrics = json.loads(METRICS.read_text())
    series = json.loads(SERIES.read_text())
    if hashlib.sha256(METRICS.read_bytes()).hexdigest() != series["metrics_sha256"]:
        failures.append("plot series is associated with different metrics")
    for key, value in series["verified_counts"].items():
        if value != metrics["continuous"]["counts"].get(key, 0):
            failures.append(f"series/metrics count mismatch: {key}")
    for key, value in series["verified_statistics"].items():
        if value != metrics["continuous"][key]:
            failures.append(f"series/metrics statistic mismatch: {key}")
    if len(series["sources"]) != 3:
        failures.append("expected three independently hashed raw streams")
    expected_sources = {entry["path"]: entry for entry in metrics["manifest"]}
    for source in series["sources"]:
        if source != expected_sources.get(source["path"]):
            failures.append(f"plot source differs from campaign manifest: {source['path']}")
    figure_dir = ROOT / "docs/validation/figures/2026-11-08-rc6"
    rendering = json.loads((figure_dir/"render-manifest.json").read_text())
    if rendering["script_sha256"] != hashlib.sha256((ROOT/"scripts/validation/plot_hardware_campaign.py").read_bytes()).hexdigest():
        failures.append("figure script identity differs from render manifest")
    canonical = hashlib.sha256(json.dumps(series, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()
    if rendering["canonical_series_sha256"] != canonical:
        failures.append("figures use different series")
    for entry in rendering["figures"]:
        content = (figure_dir/entry["path"]).read_bytes()
        if len(content) != entry["bytes"] or hashlib.sha256(content).hexdigest() != entry["sha256"]:
            failures.append(f"figure differs from manifest: {entry['path']}")
    print(f"Reviewed {len(paths)} candidate files; {checked_links} relative Markdown targets; JSON/SVG structure and credential-pattern checks")
    print(f"Status SVG SHA256 {hashlib.sha256(svg.read_bytes()).hexdigest()} (unchanged from HEAD)")
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        raise SystemExit(1)
    print("PASS: local links, data accounting, identities, frozen SVG and targeted secret checks")


if __name__ == "__main__":
    main()
