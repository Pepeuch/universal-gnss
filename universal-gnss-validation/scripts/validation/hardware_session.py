#!/usr/bin/env python3
"""Passive Universal GNSS hardware-session evidence collection and analysis."""

from __future__ import annotations

import argparse
import datetime as dt
import fcntl
import hashlib
import json
import math
import os
import platform
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Callable


SCHEMA_VERSION = 1
FIX_TYPES = {0: "UNKNOWN", 1: "NO_FIX", 2: "FIX", 3: "RTK_FLOAT", 4: "RTK_FIXED", 5: "DEAD_RECKONING"}
RTK_MODES = {0: "UNKNOWN", 1: "NONE", 2: "FLOAT", 3: "FIXED"}
SENSITIVE_KEY = re.compile(r"pass(word)?|credential|secret|token|user(name)?", re.I)
URI_USERINFO = re.compile(r"(?P<scheme>[a-z][a-z0-9+.-]*://)[^/@\s:]+:[^/@\s]+@", re.I)
ASSIGNMENT_SECRET = re.compile(
    r"(?i)(password|credential|secret|token|username|user)\s*[=:]\s*[^\s,;]+"
)
STOP_REQUESTED = False


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def local_now() -> str:
    return dt.datetime.now().astimezone().isoformat(timespec="milliseconds")


def scrub_text(value: str, limit: int = 4096) -> str:
    value = URI_USERINFO.sub(r"\g<scheme>[REDACTED]@", value)
    value = ASSIGNMENT_SECRET.sub(lambda match: match.group(1) + "=[REDACTED]", value)
    return value[:limit]


def redact(value: Any, key: str = "") -> Any:
    if SENSITIVE_KEY.search(key):
        return "[REDACTED]"
    if isinstance(value, dict):
        return {str(k): redact(v, str(k)) for k, v in value.items()}
    if isinstance(value, list):
        return [redact(item) for item in value]
    if isinstance(value, float) and not math.isfinite(value):
        return None
    if isinstance(value, str):
        return scrub_text(value)
    return value


def run(command: list[str], *, timeout: float = 10.0, input_text: str | None = None) -> dict[str, Any]:
    try:
        result = subprocess.run(
            command,
            input=input_text,
            capture_output=True,
            check=False,
            text=True,
            timeout=timeout,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"ok": False, "error": scrub_text(str(error))}
    if result.returncode != 0:
        message = result.stderr.strip() or result.stdout.strip() or f"exit {result.returncode}"
        return {"ok": False, "error": scrub_text(message), "returncode": result.returncode}
    return {"ok": True, "stdout": result.stdout}


def json_command(command: list[str], *, timeout: float = 10.0) -> dict[str, Any]:
    result = run(command, timeout=timeout)
    if not result["ok"]:
        return result
    try:
        return {"ok": True, "value": json.loads(result["stdout"])}
    except json.JSONDecodeError as error:
        return {"ok": False, "error": f"invalid JSON output: {error}"}


def append_jsonl(path: Path, record: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        json.dump(redact(record), handle, sort_keys=True, allow_nan=False)
        handle.write("\n")
        handle.flush()
        os.fsync(handle.fileno())


def read_jsonl(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    if not path.exists():
        return records
    with path.open(encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            if not line.strip():
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as error:
                records.append({"record_type": "parse_error", "line": line_number, "error": str(error)})
                continue
            if isinstance(value, dict):
                records.append(value)
    return records


def file_sha256(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {"provided": False}
    try:
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
    except OSError as error:
        return {"provided": True, "available": False, "error": scrub_text(str(error))}
    return {"provided": True, "available": True, "sha256": digest}


def directory_size(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {"provided": False}
    total = 0
    try:
        for root, _, files in os.walk(path):
            for name in files:
                try:
                    total += (Path(root) / name).stat().st_size
                except OSError:
                    continue
    except OSError as error:
        return {"provided": True, "available": False, "error": scrub_text(str(error))}
    return {"provided": True, "available": path.is_dir(), "bytes": total}


def disk_usage(path: Path) -> dict[str, Any]:
    try:
        usage = shutil.disk_usage(path)
    except OSError as error:
        return {"available": False, "error": scrub_text(str(error))}
    return {"available": True, "total_bytes": usage.total, "used_bytes": usage.used, "free_bytes": usage.free}


def discover_container(explicit: str | None) -> dict[str, Any]:
    if explicit:
        return {"ok": True, "container": explicit, "method": "explicit"}
    result = run(
        ["docker", "ps", "--filter", "label=com.docker.compose.service=universal-gnss", "--format", "{{.Names}}"]
    )
    if not result["ok"]:
        return result
    names = [line for line in result["stdout"].splitlines() if line]
    if len(names) != 1:
        return {"ok": False, "error": f"expected one running Compose universal-gnss service, found {len(names)}"}
    return {"ok": True, "container": names[0], "method": "compose_label"}


def docker_inspect(container: str) -> dict[str, Any]:
    result = json_command(["docker", "inspect", container])
    if not result["ok"]:
        return result
    values = result["value"]
    if not isinstance(values, list) or len(values) != 1:
        return {"ok": False, "error": "docker inspect returned an unexpected result"}
    item = values[0]
    state = item.get("State", {})
    health = state.get("Health", {})
    config = item.get("Config", {})
    host = item.get("HostConfig", {})
    image = item.get("Image", "")
    image_info = json_command(["docker", "image", "inspect", image]) if image else {"ok": False}
    architecture = "unknown"
    repo_digests: list[str] = []
    labels: dict[str, str] = {}
    if image_info.get("ok") and image_info["value"]:
        image_item = image_info["value"][0]
        architecture = image_item.get("Architecture", "unknown")
        repo_digests = image_item.get("RepoDigests") or []
        all_labels = image_item.get("Config", {}).get("Labels") or {}
        labels = {k: v for k, v in all_labels.items() if k.startswith("org.opencontainers.image.")}
    environment = {}
    for entry in config.get("Env") or []:
        name, separator, value = entry.partition("=")
        if separator and name in {"ROS_DISTRO", "UNIVERSAL_GNSS_VERSION", "UNIVERSAL_GNSS_REVISION"}:
            environment[name] = value
    return {
        "ok": True,
        "container_id": item.get("Id"),
        "container_name": item.get("Name", "").lstrip("/"),
        "image_id": image,
        "image_reference": config.get("Image"),
        "image_architecture": architecture,
        "image_repo_digests": repo_digests,
        "image_oci_labels": labels,
        "runtime_public_identity": environment,
        "running": state.get("Running"),
        "status": state.get("Status"),
        "health": health.get("Status", "not_configured"),
        "restart_count": item.get("RestartCount"),
        "restart_policy": host.get("RestartPolicy", {}).get("Name", ""),
        "runtime_user": config.get("User") or "root",
        "group_add": host.get("GroupAdd") or [],
        "privileged": host.get("Privileged"),
        "devices": [
            {"host": d.get("PathOnHost"), "container": d.get("PathInContainer"), "permissions": d.get("CgroupPermissions")}
            for d in host.get("Devices") or []
        ],
    }


def docker_processes(container: str) -> dict[str, Any]:
    result = run(["docker", "top", container, "-eo", "pid,args"])
    if not result["ok"]:
        return {"available": False, "error": result["error"], "receiver_pid": None, "ntrip_pid": None}
    receiver_pid = None
    ntrip_pid = None
    for line in result["stdout"].splitlines()[1:]:
        fields = line.strip().split(None, 1)
        if len(fields) != 2:
            continue
        if "receiver_node" in fields[1]:
            receiver_pid = int(fields[0])
        if "ntrip_node" in fields[1]:
            ntrip_pid = int(fields[0])
    return {"available": True, "receiver_pid": receiver_pid, "ntrip_pid": ntrip_pid}


ROS_PROBE = r'''
import json, math, os, rclpy
from diagnostic_msgs.msg import DiagnosticArray
from rosidl_runtime_py.convert import message_to_ordereddict
from std_srvs.srv import Trigger
from universal_gnss_ros2.srv import GetReceiverSnapshot

def clean(value):
    if isinstance(value, dict): return {str(k): clean(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)): return [clean(v) for v in value]
    if isinstance(value, float) and not math.isfinite(value): return None
    return value

def call(node, service_type, name, timeout):
    client = node.create_client(service_type, name)
    if not client.wait_for_service(timeout_sec=timeout): return None
    future = client.call_async(service_type.Request())
    rclpy.spin_until_future_complete(node, future, timeout_sec=timeout)
    if not future.done() or future.exception() is not None: return None
    return future.result()

rclpy.init()
node = rclpy.create_node('universal_gnss_validation_probe')
timeout = float(os.environ.get('UG_PROBE_TIMEOUT', '3'))
live_diagnostics = []
def diagnostics_callback(message):
    live_diagnostics.append(clean(message_to_ordereddict(message)))
subscription = node.create_subscription(DiagnosticArray, '/diagnostics', diagnostics_callback, 10)
snapshot = call(node, GetReceiverSnapshot, '/universal_gnss_receiver/get_snapshot', timeout)
receiver_health = call(node, Trigger, '/universal_gnss_receiver/get_health', timeout)
ntrip_health = call(node, Trigger, '/universal_gnss_ntrip/get_health', timeout)
output = {
  'receiver_responsive': bool(receiver_health and receiver_health.success),
  'ntrip_responsive': bool(ntrip_health and ntrip_health.success),
  'snapshot_available': snapshot is not None,
  'live_diagnostics': live_diagnostics[-8:],
}
if snapshot is not None:
  output['status'] = clean(message_to_ordereddict(snapshot.status))
  output['diagnostics'] = clean(message_to_ordereddict(snapshot.diagnostics))
print(json.dumps(output, sort_keys=True, allow_nan=False))
node.destroy_node()
rclpy.shutdown()
'''


def ros_probe(container: str, timeout: float) -> dict[str, Any]:
    shell = (
        "source /opt/ros/${ROS_DISTRO}/setup.bash && "
        "if [[ -f /opt/universal_gnss/install/setup.bash ]]; then "
        "source /opt/universal_gnss/install/setup.bash; "
        "elif [[ -f /opt/gnss_sidecar/setup.bash ]]; then "
        "source /opt/gnss_sidecar/setup.bash; "
        "else echo 'Universal GNSS setup not found' >&2; exit 2; fi && "
        "UG_PROBE_TIMEOUT=" + str(timeout) + " python3 -"
    )
    result = run(["docker", "exec", "-i", container, "/bin/bash", "-lc", shell], timeout=timeout * 4 + 4, input_text=ROS_PROBE)
    if not result["ok"]:
        return {"available": False, "error": result["error"]}
    try:
        value = json.loads(result["stdout"])
    except json.JSONDecodeError as error:
        return {"available": False, "error": f"invalid ROS probe output: {error}"}
    status = value.get("status") or {}
    status["fix_type_name"] = FIX_TYPES.get(status.get("fix_type"), "UNKNOWN_VALUE")
    status["rtk_mode_name"] = RTK_MODES.get(status.get("rtk_mode"), "UNKNOWN_VALUE")
    return {"available": True, **redact(value)}


def docker_stats(container: str) -> dict[str, Any]:
    result = json_command(["docker", "stats", "--no-stream", "--format", "{{json .}}", container])
    if not result["ok"]:
        return {"available": False, "error": result["error"]}
    value = result["value"]
    return {
        "available": True,
        "cpu_percent": value.get("CPUPerc"),
        "memory_percent": value.get("MemPerc"),
        "memory_usage": value.get("MemUsage"),
        "block_io": value.get("BlockIO"),
    }


def metadata(args: argparse.Namespace, container_name: str | None) -> dict[str, Any]:
    git_revision = run(["git", "rev-parse", "HEAD"])
    git_branch = run(["git", "branch", "--show-current"])
    docker_version = json_command(["docker", "version", "--format", "{{json .}}"])
    receiver: dict[str, Any] = {"provided": args.receiver_by_id is not None}
    if args.receiver_by_id is not None:
        receiver["stable_by_id"] = str(args.receiver_by_id)
        try:
            receiver["resolved_device"] = str(args.receiver_by_id.resolve(strict=True))
            receiver["available"] = True
        except OSError as error:
            receiver.update({"available": False, "error": scrub_text(str(error))})
    container = docker_inspect(container_name) if container_name else {"ok": False, "error": "container unavailable"}
    identity = run(["docker", "exec", container_name, "id"], timeout=5) if container_name else {"ok": False}
    return redact({
        "schema_version": SCHEMA_VERSION,
        "record_type": "metadata",
        "started_utc": utc_now(),
        "started_local": local_now(),
        "hostname": socket.gethostname(),
        "boot_id": Path("/proc/sys/kernel/random/boot_id").read_text(encoding="utf-8").strip(),
        "kernel": platform.release(),
        "architecture": platform.machine(),
        "git_revision": args.git_revision or (git_revision.get("stdout", "").strip() if git_revision["ok"] else None),
        "git_branch": git_branch.get("stdout", "").strip() if git_branch["ok"] else None,
        "docker_version": docker_version.get("value") if docker_version.get("ok") else {"error": docker_version.get("error")},
        "ros_distro": (container.get("runtime_public_identity") or {}).get("ROS_DISTRO", "unknown"),
        "receiver": receiver,
        "configuration": file_sha256(args.parameters),
        "runtime_identity": identity.get("stdout", "").strip() if identity.get("ok") else None,
        "container": container,
        "paths": {
            "parameters": str(args.parameters) if args.parameters else None,
            "log_directory": str(args.log_directory) if args.log_directory else None,
            "export_directory": str(args.export_directory) if args.export_directory else None,
        },
        "mode": args.mode,
        "duration_seconds": args.duration,
        "sample_interval_seconds": args.interval,
        "snapshot_interval_seconds": args.snapshot_interval,
        "redaction": {"parameter_values": "omitted", "credentials": "omitted", "docker_environment": "omitted"},
    })


def sample(args: argparse.Namespace, container_name: str | None, index: int) -> dict[str, Any]:
    record: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "record_type": "sample",
        "sample_index": index,
        "timestamp_utc": utc_now(),
        "monotonic_seconds": time.monotonic(),
        "log_directory": directory_size(args.log_directory),
        "export_directory": directory_size(args.export_directory),
        "output_disk": disk_usage(args.output),
    }
    if args.dry_run or not container_name:
        record.update({
            "container": {"available": False, "error": "dry-run: Docker observation skipped"},
            "processes": {"available": False, "error": "dry-run: process observation skipped"},
            "ros": {"available": False, "error": "dry-run: ROS observation skipped"},
            "resources": {"available": False, "error": "dry-run: resource observation skipped"},
        })
        return record
    record["container"] = docker_inspect(container_name)
    record["processes"] = docker_processes(container_name)
    record["ros"] = ros_probe(container_name, args.probe_timeout)
    record["resources"] = docker_stats(container_name)
    return redact(record)


def support_snapshot(args: argparse.Namespace, container_name: str | None, index: int) -> dict[str, Any]:
    destination = args.output / "snapshots" / f"support-{index:06d}.json"
    command = [sys.executable, str(Path(__file__).resolve().parents[1] / "collect_support_snapshot.py"), "--output", str(destination)]
    if args.parameters:
        command += ["--parameters", str(args.parameters)]
    if args.log_directory:
        command += ["--log-directory", str(args.log_directory)]
    if args.image:
        command += ["--image", args.image]
    result = run(command, timeout=30)
    return {
        "schema_version": SCHEMA_VERSION,
        "record_type": "support_snapshot",
        "timestamp_utc": utc_now(),
        "sample_index": index,
        "created": result["ok"],
        "path": str(destination.relative_to(args.output)) if result["ok"] else None,
        "error": result.get("error"),
        "container": container_name,
    }


def duration_seconds(value: str) -> float:
    match = re.fullmatch(r"([0-9]+(?:\.[0-9]+)?)([smhd]?)", value.strip(), re.I)
    if not match:
        raise argparse.ArgumentTypeError("duration must be a number optionally followed by s, m, h, or d")
    factors = {"": 1, "s": 1, "m": 60, "h": 3600, "d": 86400}
    seconds = float(match.group(1)) * factors[match.group(2).lower()]
    if seconds <= 0:
        raise argparse.ArgumentTypeError("duration must be greater than zero")
    return seconds


def positive_seconds(value: str) -> float:
    seconds = duration_seconds(value)
    return seconds


def signal_handler(signum: int, _frame: Any) -> None:
    global STOP_REQUESTED
    STOP_REQUESTED = True


def summarize(session: Path, requested_status: str | None = None) -> dict[str, Any]:
    metadata_value: dict[str, Any] = {}
    try:
        metadata_value = json.loads((session / "metadata.json").read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        pass
    samples = [record for record in read_jsonl(session / "samples.jsonl") if record.get("record_type") == "sample"]
    events = [record for record in read_jsonl(session / "events.jsonl") if record.get("record_type") == "event"]
    sequences: list[int] = []
    incarnations: set[str] = set()
    container_ids: set[str] = set()
    receiver_pids: set[int] = set()
    ntrip_pids: set[int] = set()
    restarts: list[int] = []
    rtk_transitions: list[dict[str, Any]] = []
    last_rtk: str | None = None
    unavailable_ros = 0
    intervals: list[float] = []
    cpu_values: list[float] = []
    memory_values: list[float] = []
    log_sizes: list[int] = []
    export_sizes: list[int] = []
    diagnostic_last: dict[str, tuple[Any, Any]] = {}
    diagnostic_transitions: list[dict[str, Any]] = []
    stale_or_no_data_samples: list[int] = []
    for previous, current in zip(samples, samples[1:]):
        delta = current.get("monotonic_seconds", 0) - previous.get("monotonic_seconds", 0)
        if delta >= 0:
            intervals.append(delta)
    for item in samples:
        for size_key, target in (("log_directory", log_sizes), ("export_directory", export_sizes)):
            size = (item.get(size_key) or {}).get("bytes")
            if isinstance(size, int):
                target.append(size)
        container = item.get("container") or {}
        if container.get("container_id"):
            container_ids.add(container["container_id"])
        if isinstance(container.get("restart_count"), int):
            restarts.append(container["restart_count"])
        processes = item.get("processes") or {}
        if isinstance(processes.get("receiver_pid"), int):
            receiver_pids.add(processes["receiver_pid"])
        if isinstance(processes.get("ntrip_pid"), int):
            ntrip_pids.add(processes["ntrip_pid"])
        ros = item.get("ros") or {}
        if not ros.get("available"):
            unavailable_ros += 1
            continue
        status = ros.get("status") or {}
        if isinstance(status.get("position_observation_sequence"), int):
            sequences.append(status["position_observation_sequence"])
        if status.get("source_incarnation"):
            incarnations.add(status["source_incarnation"])
        rtk = status.get("rtk_mode_name")
        if rtk and rtk != last_rtk:
            rtk_transitions.append({"timestamp_utc": item.get("timestamp_utc"), "from": last_rtk, "to": rtk})
            last_rtk = rtk
        diagnostic_arrays = []
        if isinstance(ros.get("diagnostics"), dict):
            diagnostic_arrays.append(ros["diagnostics"])
        diagnostic_arrays.extend(value for value in ros.get("live_diagnostics") or [] if isinstance(value, dict))
        sample_stale = False
        for diagnostic_array in diagnostic_arrays:
            for diagnostic in diagnostic_array.get("status") or []:
                if not isinstance(diagnostic, dict) or not diagnostic.get("name"):
                    continue
                name = str(diagnostic["name"])
                state = (diagnostic.get("level"), diagnostic.get("message"))
                if name in diagnostic_last and diagnostic_last[name] != state:
                    diagnostic_transitions.append({
                        "timestamp_utc": item.get("timestamp_utc"),
                        "name": name,
                        "from": {"level": diagnostic_last[name][0], "message": diagnostic_last[name][1]},
                        "to": {"level": state[0], "message": state[1]},
                    })
                diagnostic_last[name] = state
                searchable = f"{name} {state[1]}".lower()
                if any(marker in searchable for marker in ("stale", "no_data", "no data", "disconnected", "unavailable")):
                    sample_stale = True
        if sample_stale:
            stale_or_no_data_samples.append(item.get("sample_index"))
        for key, target in (("cpu_percent", cpu_values), ("memory_percent", memory_values)):
            raw = (item.get("resources") or {}).get(key)
            if isinstance(raw, str) and raw.endswith("%"):
                try:
                    target.append(float(raw[:-1]))
                except ValueError:
                    pass
    duration = 0.0
    if samples:
        duration = max(0.0, samples[-1].get("monotonic_seconds", 0) - samples[0].get("monotonic_seconds", 0))
    expected_interval = metadata_value.get("sample_interval_seconds")
    missing_intervals = []
    if isinstance(expected_interval, (int, float)) and expected_interval > 0:
        missing_intervals = [value for value in intervals if value > expected_interval * 1.5]
    status = requested_status or ("INCONCLUSIVE" if not samples or unavailable_ros == len(samples) else "COMPLETED")
    return {
        "schema_version": SCHEMA_VERSION,
        "record_type": "summary",
        "generated_utc": utc_now(),
        "session_status": status,
        "duration_seconds": duration,
        "sample_count": len(samples),
        "event_count": len(events),
        "ros_unavailable_samples": unavailable_ros,
        "container_restart_count_min": min(restarts) if restarts else None,
        "container_restart_count_max": max(restarts) if restarts else None,
        "container_restart_count_change": (max(restarts) - min(restarts)) if restarts else None,
        "observed_container_ids": sorted(container_ids),
        "observed_receiver_pids": sorted(receiver_pids),
        "observed_ntrip_pids": sorted(ntrip_pids),
        "observed_source_incarnations": sorted(incarnations),
        "position_sequence_min": min(sequences) if sequences else None,
        "position_sequence_max": max(sequences) if sequences else None,
        "position_sequence_progression": (max(sequences) - min(sequences)) if sequences else None,
        "rtk_transitions": rtk_transitions,
        "diagnostic_state_transitions": diagnostic_transitions,
        "ntrip_diagnostic_transitions": [value for value in diagnostic_transitions if "ntrip" in value["name"].lower()],
        "correction_or_rtcm_transitions": [value for value in diagnostic_transitions if any(key in value["name"].lower() for key in ("correction", "rtcm"))],
        "stale_or_no_data_sample_indexes": stale_or_no_data_samples,
        "cpu_peak_percent": max(cpu_values) if cpu_values else None,
        "memory_peak_percent": max(memory_values) if memory_values else None,
        "log_growth_bytes": (log_sizes[-1] - log_sizes[0]) if log_sizes else None,
        "support_export_growth_bytes": (export_sizes[-1] - export_sizes[0]) if export_sizes else None,
        "unexpected_container_change": len(container_ids) > 1,
        "unexpected_receiver_process_change": len(receiver_pids) > 1,
        "unexpected_ntrip_process_change": len(ntrip_pids) > 1,
        "unexpected_source_incarnation_change": len(incarnations) > 1,
        "missing_collection_interval_count": len(missing_intervals),
        "largest_collection_interval_seconds": max(intervals) if intervals else None,
        "release_credit_awarded": False,
        "limitations": [
            "This summary reports mechanically observable evidence only.",
            "It does not prove a retired-byte cutoff or award release checklist credit.",
        ],
    }


def write_summary(session: Path, status: str | None = None) -> dict[str, Any]:
    value = summarize(session, status)
    temporary = session / ".summary.json.tmp"
    temporary.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    temporary.replace(session / "summary.json")
    return value


def collect(args: argparse.Namespace) -> int:
    global STOP_REQUESTED
    STOP_REQUESTED = False
    if args.mode == "endurance" and args.duration is None:
        raise SystemExit("error: --duration is required for endurance mode")
    if args.mode == "functional" and args.duration is not None:
        raise SystemExit("error: --duration is only valid for endurance mode")
    if args.output.exists() and any(args.output.iterdir()):
        raise SystemExit(f"error: output directory is not empty: {args.output}")
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "logs").mkdir(exist_ok=True)
    (args.output / "snapshots").mkdir(exist_ok=True)
    container_result = {"ok": True, "container": None, "method": "dry_run"} if args.dry_run else discover_container(args.container)
    container_name = container_result.get("container") if container_result.get("ok") else None
    metadata_value = metadata(args, container_name)
    metadata_value["container_selection"] = redact(container_result)
    (args.output / "metadata.json").write_text(json.dumps(metadata_value, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    append_jsonl(args.output / "events.jsonl", {
        "schema_version": SCHEMA_VERSION,
        "record_type": "event",
        "timestamp_utc": utc_now(),
        "timestamp_local": local_now(),
        "event": "session_started",
        "source": "collector",
    })
    for signum in (signal.SIGINT, signal.SIGTERM):
        signal.signal(signum, signal_handler)
    start = time.monotonic()
    next_sample = start
    next_snapshot = start
    index = 0
    requested_status: str | None = None
    try:
        while True:
            now = time.monotonic()
            if STOP_REQUESTED:
                break
            if args.duration is not None and now - start >= args.duration:
                break
            if now >= next_sample:
                append_jsonl(args.output / "samples.jsonl", sample(args, container_name, index))
                index += 1
                next_sample = now + args.interval
                if args.max_samples is not None and index >= args.max_samples:
                    break
            if args.snapshot_interval > 0 and now >= next_snapshot:
                append_jsonl(args.output / "events.jsonl", support_snapshot(args, container_name, index))
                next_snapshot = now + args.snapshot_interval
            time.sleep(min(0.2, max(0.01, next_sample - time.monotonic())))
    except Exception as error:  # preserve evidence on unexpected collector failure
        requested_status = "ABORTED"
        append_jsonl(args.output / "events.jsonl", {
            "schema_version": SCHEMA_VERSION,
            "record_type": "event",
            "timestamp_utc": utc_now(),
            "timestamp_local": local_now(),
            "event": "collector_error",
            "source": "collector",
            "error": scrub_text(str(error)),
        })
    append_jsonl(args.output / "events.jsonl", {
        "schema_version": SCHEMA_VERSION,
        "record_type": "event",
        "timestamp_utc": utc_now(),
        "timestamp_local": local_now(),
        "event": "session_stopped",
        "source": "collector",
        "signal_requested": STOP_REQUESTED,
    })
    result = write_summary(args.output, requested_status)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 1 if result["session_status"] == "ABORTED" else 0


def mark(args: argparse.Namespace) -> int:
    if not args.session.is_dir() or not (args.session / "metadata.json").is_file():
        raise SystemExit(f"error: not a hardware-session directory: {args.session}")
    message = " ".join(args.message).strip()
    if not message:
        raise SystemExit("error: event message must not be empty")
    append_jsonl(args.session / "events.jsonl", {
        "schema_version": SCHEMA_VERSION,
        "record_type": "event",
        "timestamp_utc": utc_now(),
        "timestamp_local": local_now(),
        "event": "operator_marker",
        "source": "operator",
        "message": message,
    })
    print(f"marked {args.session}: {message}")
    return 0


def analyze(args: argparse.Namespace) -> int:
    if not args.session.is_dir():
        raise SystemExit(f"error: session directory does not exist: {args.session}")
    result = write_summary(args.session)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


def parser() -> argparse.ArgumentParser:
    root = argparse.ArgumentParser(description=__doc__)
    subparsers = root.add_subparsers(dest="command", required=True)
    collect_parser = subparsers.add_parser("collect", help="run a passive collection session")
    collect_parser.add_argument("--mode", choices=("functional", "endurance"), required=True)
    collect_parser.add_argument("--duration", type=duration_seconds)
    collect_parser.add_argument("--output", required=True, type=Path)
    collect_parser.add_argument("--container", help="container name; otherwise discover one Compose universal-gnss service")
    collect_parser.add_argument("--image", help="image reference for support-snapshot OCI metadata")
    collect_parser.add_argument("--git-revision", help="exact Universal GNSS source revision represented by the runtime")
    collect_parser.add_argument("--receiver-by-id", type=Path, help="selected stable host receiver identity")
    collect_parser.add_argument("--parameters", type=Path, help="external ROS parameters file (only its SHA-256 is recorded)")
    collect_parser.add_argument("--log-directory", type=Path, help="external persistent ROS log directory")
    collect_parser.add_argument("--export-directory", type=Path, help="external support-export directory")
    collect_parser.add_argument("--interval", type=positive_seconds, default=300.0)
    collect_parser.add_argument("--snapshot-interval", type=positive_seconds, default=1800.0)
    collect_parser.add_argument("--probe-timeout", type=float, default=3.0)
    collect_parser.add_argument("--dry-run", action="store_true", help=argparse.SUPPRESS)
    collect_parser.add_argument("--max-samples", type=int, help=argparse.SUPPRESS)
    collect_parser.set_defaults(func=collect)
    mark_parser = subparsers.add_parser("mark", help="append an operator event marker")
    mark_parser.add_argument("--session", required=True, type=Path)
    mark_parser.add_argument("message", nargs="+")
    mark_parser.set_defaults(func=mark)
    analyze_parser = subparsers.add_parser("analyze", help="regenerate and print the mechanical summary")
    analyze_parser.add_argument("session", type=Path)
    analyze_parser.set_defaults(func=analyze)
    return root


def main() -> int:
    args = parser().parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
