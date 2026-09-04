#!/usr/bin/env python3
"""Focused contract tests for the non-secret deployment support snapshot."""

from __future__ import annotations

import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "collect_support_snapshot.py"
SPEC = importlib.util.spec_from_file_location("collect_support_snapshot", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class SupportSnapshotTests(unittest.TestCase):
    def test_snapshot_redacts_parameter_values_and_has_stable_shape(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            parameters = root / "parameters.yaml"
            parameters.write_text(
                """/universal_gnss_receiver:\n  ros__parameters:\n    receiver_family: unicore\n    serial_device: /dev/gnss-receiver\n/universal_gnss_ntrip:\n  ros__parameters:\n    password: secret-password\n    caster_host: private.example\n""",
                encoding="utf-8",
            )
            with mock.patch.dict(
                os.environ,
                {
                    "UNIVERSAL_GNSS_VERSION": "v0.7.0-test",
                    "UNIVERSAL_GNSS_REVISION": "revision-test",
                    "ROS_DISTRO": "kilted-test",
                    "UNIVERSAL_GNSS_CONFIGURATION_SCHEMA_VERSION": "1",
                },
                clear=False,
            ):
                snapshot = MODULE.build_snapshot(parameters, None, None, 10)

        serialized = json.dumps(snapshot, sort_keys=True)
        self.assertEqual(1, snapshot["schema_version"])
        self.assertEqual("v0.7.0-test", snapshot["runtime_identity"]["version"])
        self.assertEqual("revision-test", snapshot["runtime_identity"]["revision"])
        self.assertEqual("kilted-test", snapshot["runtime_identity"]["ros_distro"])
        self.assertEqual(
            ["caster_host", "password", "receiver_family", "serial_device"],
            snapshot["configuration"]["parameter_keys"],
        )
        self.assertTrue(snapshot["configuration"]["values_redacted"])
        self.assertNotIn("secret-password", serialized)
        self.assertNotIn("private.example", serialized)
        self.assertNotIn("/dev/gnss-receiver", serialized)

    def test_image_collection_whitelists_only_oci_labels(self) -> None:
        completed = mock.Mock(
            returncode=0,
            stdout=json.dumps(
                {
                    "org.opencontainers.image.version": "v0.7.0",
                    "org.opencontainers.image.revision": "abc123",
                    "universal_gnss.ntrip_password": "secret-password",
                }
            ),
        )
        with mock.patch.object(MODULE.subprocess, "run", return_value=completed):
            image = MODULE.image_labels("universal-gnss:test")

        self.assertTrue(image["available"])
        self.assertEqual(
            {
                "org.opencontainers.image.version": "v0.7.0",
                "org.opencontainers.image.revision": "abc123",
            },
            image["labels"],
        )

    def test_log_metadata_is_bounded_and_does_not_record_paths(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            logs = Path(directory) / "logs"
            logs.mkdir()
            (logs / "first.log").write_text("first", encoding="utf-8")
            (logs / "second.log").write_text("second", encoding="utf-8")

            metadata = MODULE.log_metadata(logs, 1)

        self.assertTrue(metadata["available"])
        self.assertEqual(1, len(metadata["files"]))
        self.assertNotIn(str(logs), json.dumps(metadata))


if __name__ == "__main__":
    unittest.main()
