#!/usr/bin/env python3
"""Focused tests for the passive hardware-session evidence harness."""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "validation" / "hardware_session.py"
SPEC = importlib.util.spec_from_file_location("hardware_session", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class HardwareSessionTests(unittest.TestCase):
    def test_discovers_standalone_compose_service(self) -> None:
        output = "ug-runtime\tuniversal-gnss\t\t\n"
        with mock.patch.object(MODULE, "run", return_value={"ok": True, "stdout": output}):
            result = MODULE.discover_container(None)
        self.assertEqual({"ok": True, "container": "ug-runtime", "method": "runtime_labels"}, result)

    def test_discovers_mowgli_sidecar_from_image_identity(self) -> None:
        output = (
            "mowgli-gps\tgps\thttps://github.com/pepeuch/universal-gnss\t"
            "MowgliNext Universal GNSS sidecar\n"
            "mowgli-ros2\tros2\thttps://github.com/mowglinext/mowglinext\tMowgliNext ROS 2\n"
        )
        with mock.patch.object(MODULE, "run", return_value={"ok": True, "stdout": output}):
            result = MODULE.discover_container(None)
        self.assertEqual({"ok": True, "container": "mowgli-gps", "method": "runtime_labels"}, result)

    def test_discovery_rejects_ambiguous_universal_gnss_containers(self) -> None:
        output = (
            "one\tuniversal-gnss\t\t\n"
            "two\tgps\thttps://github.com/example/universal-gnss.git\t\n"
        )
        with mock.patch.object(MODULE, "run", return_value={"ok": True, "stdout": output}):
            result = MODULE.discover_container(None)
        self.assertFalse(result["ok"])
        self.assertIn("found 2", result["error"])

    def test_duration_units(self) -> None:
        self.assertEqual(5.0, MODULE.duration_seconds("5"))
        self.assertEqual(300.0, MODULE.duration_seconds("5m"))
        self.assertEqual(86400.0, MODULE.duration_seconds("24h"))

    def test_redaction_removes_sensitive_values_and_uri_userinfo(self) -> None:
        value = MODULE.redact(
            {
                "password": "do-not-record",
                "message": "password=also-secret ntrip://person:key@caster.example/mount",
                "safe": "streaming",
            }
        )
        serialized = json.dumps(value)
        self.assertNotIn("do-not-record", serialized)
        self.assertNotIn("also-secret", serialized)
        self.assertNotIn("person:key", serialized)
        self.assertEqual("streaming", value["safe"])

    def test_summary_reports_incarnation_sequence_and_restart_changes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            (session / "metadata.json").write_text(
                json.dumps({"sample_interval_seconds": 300}), encoding="utf-8"
            )
            first = {
                "record_type": "sample",
                "timestamp_utc": "2026-09-07T10:00:00Z",
                "monotonic_seconds": 10.0,
                "container": {"container_id": "one", "restart_count": 2},
                "processes": {"receiver_pid": 100, "ntrip_pid": 101},
                "resources": {"cpu_percent": "2.5%", "memory_percent": "1.0%"},
                "ros": {"available": True, "status": {"source_incarnation": "inc-a", "position_observation_sequence": 7, "rtk_mode_name": "FLOAT"}},
            }
            second = {
                "record_type": "sample",
                "timestamp_utc": "2026-09-07T10:05:00Z",
                "monotonic_seconds": 310.0,
                "container": {"container_id": "two", "restart_count": 3},
                "processes": {"receiver_pid": 200, "ntrip_pid": 201},
                "resources": {"cpu_percent": "4.5%", "memory_percent": "1.5%"},
                "ros": {"available": True, "status": {"source_incarnation": "inc-b", "position_observation_sequence": 11, "rtk_mode_name": "FIXED"}},
            }
            MODULE.append_jsonl(session / "samples.jsonl", first)
            MODULE.append_jsonl(session / "samples.jsonl", second)
            summary = MODULE.summarize(session)

        self.assertEqual("COMPLETED", summary["session_status"])
        self.assertEqual(1, summary["container_restart_count_change"])
        self.assertEqual(4, summary["position_sequence_progression"])
        self.assertEqual(["inc-a", "inc-b"], summary["observed_source_incarnations"])
        self.assertEqual("FIXED", summary["rtk_transitions"][-1]["to"])
        self.assertFalse(summary["release_credit_awarded"])

    def test_all_unavailable_samples_are_inconclusive(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            (session / "metadata.json").write_text("{}", encoding="utf-8")
            MODULE.append_jsonl(session / "samples.jsonl", {"record_type": "sample", "monotonic_seconds": 1.0, "ros": {"available": False}})
            summary = MODULE.summarize(session)
        self.assertEqual("INCONCLUSIVE", summary["session_status"])

    def test_missing_container_is_not_mislabeled_as_dry_run(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            args = MODULE.argparse.Namespace(
                dry_run=False,
                log_directory=None,
                export_directory=None,
                output=Path(directory),
            )
            value = MODULE.sample(args, None, 0)
        self.assertIn("container unavailable", value["container"]["error"])
        self.assertNotIn("dry-run", value["container"]["error"])


if __name__ == "__main__":
    unittest.main()
