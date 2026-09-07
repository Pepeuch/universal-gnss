#!/usr/bin/env python3
"""Create a bounded, non-secret Universal GNSS deployment support snapshot."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import subprocess
import tempfile
from pathlib import Path
from typing import Any


SNAPSHOT_SCHEMA_VERSION = 1
MAX_PARAMETER_BYTES = 1024 * 1024
OCI_LABELS = (
    "org.opencontainers.image.title",
    "org.opencontainers.image.description",
    "org.opencontainers.image.version",
    "org.opencontainers.image.revision",
    "org.opencontainers.image.source",
    "org.opencontainers.image.created",
)
PARAMETER_KEY = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*:")


def environment_value(name: str) -> str:
    value = os.environ.get(name, "")
    return value if value else "unknown"


def parameter_shape(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    if len(data) > MAX_PARAMETER_BYTES:
        raise ValueError("parameter file exceeds the support snapshot size limit")

    keys: set[str] = set()
    in_parameters = False
    parameters_indent = 0
    for line in data.decode("utf-8", errors="replace").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        indent = len(line) - len(line.lstrip(" "))
        if line.strip() == "ros__parameters:":
            in_parameters = True
            parameters_indent = indent
            continue
        if not in_parameters:
            continue
        if indent <= parameters_indent:
            in_parameters = False
            continue
        match = PARAMETER_KEY.match(line)
        if match is not None:
            keys.add(match.group(1))

    return {
        "provided": True,
        "sha256": hashlib.sha256(data).hexdigest(),
        "parameter_keys": sorted(keys),
        "values_redacted": True,
    }


def log_metadata(path: Path, maximum: int) -> dict[str, Any]:
    if maximum < 0:
        raise ValueError("max-log-files must not be negative")
    if not path.is_dir():
        return {"requested": True, "available": False, "files": []}

    entries = [entry for entry in path.iterdir() if entry.is_file()]
    entries.sort(key=lambda entry: (-entry.stat().st_mtime_ns, entry.name))
    files = [
        {
            "name": entry.name,
            "size_bytes": entry.stat().st_size,
            "modified_epoch_ns": entry.stat().st_mtime_ns,
        }
        for entry in entries[:maximum]
    ]
    return {"requested": True, "available": True, "files": files}


def image_labels(image: str) -> dict[str, Any]:
    try:
        result = subprocess.run(
            ["docker", "image", "inspect", "--format", "{{json .Config.Labels}}", image],
            capture_output=True,
            check=False,
            text=True,
        )
    except OSError:
        return {"requested": True, "available": False, "labels": {}}
    if result.returncode != 0:
        return {"requested": True, "available": False, "labels": {}}
    try:
        labels = json.loads(result.stdout)
    except json.JSONDecodeError:
        return {"requested": True, "available": False, "labels": {}}
    if not isinstance(labels, dict):
        return {"requested": True, "available": False, "labels": {}}
    return {
        "requested": True,
        "available": True,
        "labels": {key: labels[key] for key in OCI_LABELS if isinstance(labels.get(key), str)},
    }


def build_snapshot(parameters: Path | None, log_directory: Path | None, image: str | None,
                   max_log_files: int) -> dict[str, Any]:
    snapshot: dict[str, Any] = {
        "schema_version": SNAPSHOT_SCHEMA_VERSION,
        "runtime_identity": {
            "version": environment_value("UNIVERSAL_GNSS_VERSION"),
            "revision": environment_value("UNIVERSAL_GNSS_REVISION"),
            "ros_distro": environment_value("ROS_DISTRO"),
            "configuration_schema_version": environment_value(
                "UNIVERSAL_GNSS_CONFIGURATION_SCHEMA_VERSION"
            ),
            "architecture": platform.machine() or "unknown",
            "platform": platform.system().lower() or "unknown",
        },
        "redaction": {
            "parameter_values": "omitted",
            "credentials": "omitted",
            "raw_logs": "omitted",
            "docker_environment": "omitted",
        },
        "configuration": {"provided": False, "parameter_keys": [], "values_redacted": True},
        "logs": {"requested": False, "available": False, "files": []},
        "image": {"requested": False, "available": False, "labels": {}},
    }
    if parameters is not None:
        snapshot["configuration"] = parameter_shape(parameters)
    if log_directory is not None:
        snapshot["logs"] = log_metadata(log_directory, max_log_files)
    if image is not None:
        snapshot["image"] = image_labels(image)
    return snapshot


def write_snapshot(path: Path, snapshot: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent, delete=False) as handle:
        json.dump(snapshot, handle, indent=2, sort_keys=True)
        handle.write("\n")
        temporary_path = Path(handle.name)
    temporary_path.replace(path)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="JSON support artifact to write")
    parser.add_argument("--parameters", type=Path, help="optional external ROS parameter file")
    parser.add_argument("--log-directory", type=Path, help="optional direct log directory to summarize")
    parser.add_argument("--max-log-files", type=int, default=10, help="maximum direct log files to report")
    parser.add_argument("--image", help="optional local Docker image whose OCI labels to collect")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        snapshot = build_snapshot(args.parameters, args.log_directory, args.image, args.max_log_files)
        write_snapshot(args.output, snapshot)
    except (OSError, ValueError) as error:
        raise SystemExit(f"error: {error}") from error
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
