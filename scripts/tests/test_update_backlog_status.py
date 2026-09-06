#!/usr/bin/env python3
"""Focused regressions for the public UGA status dashboard generator."""

from __future__ import annotations

import copy
import importlib.util
import json
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "update_backlog_status.py"
SPEC = importlib.util.spec_from_file_location("update_backlog_status", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class BacklogStatusTests(unittest.TestCase):
    def test_current_manifest_conserves_and_tracks_hardware_orthogonally(self) -> None:
        data = MODULE.load_backlog()

        self.assertEqual(205, data.baseline_count)
        self.assertEqual(
            {"IMPLEMENTED": 25, "PARTIAL": 20, "OPEN": 99, "BLOCKED": 53, "DUPLICATE": 8},
            dict(data.status_counts),
        )
        self.assertEqual("PARTIAL", data.records["UGA-126"]["status"])
        self.assertEqual("DRIVER", data.records["UGA-126"]["scope"])
        self.assertEqual("HARDWARE_REQUIRED", data.records["UGA-126"]["validation"])
        self.assertEqual("PARTIAL", data.records["UGA-170"]["status"])
        self.assertEqual("RECEIVER", data.records["UGA-170"]["scope"])
        self.assertEqual("HARDWARE_REQUIRED", data.records["UGA-170"]["validation"])
        self.assertEqual(172, data.todo_counts["unchecked"])
        self.assertFalse(
            [
                stable_id
                for stable_id, record in data.records.items()
                if record["status"] == "PARTIAL" and record["todo_state"] == "checked"
            ]
        )
        MODULE.validate_todo(data)
        self.assertEqual({"total": 194, "complete": 80, "not_started": 114}, MODULE.project_progress_counts())
        self.assertEqual({"total": 65, "complete": 54, "not_started": 11}, MODULE.release_progress_counts())
        self.assertEqual({"IMPLEMENTED": 1, "PARTIAL": 3, "OPEN": 2}, dict(MODULE.plan_status_counts()))
        dependency_counts = MODULE.release_dependency_counts(data)
        self.assertEqual(11, sum(dependency_counts.values()))
        self.assertEqual(8, sum(
            dependency_counts[classification]
            for classification in MODULE.HARDWARE_DEPENDENCY_CLASSES
        ))
        self.assertEqual(11, len(data.release_dependencies))
        self.assertEqual(
            {"SOFTWARE_IMPLEMENTATION_REQUIRED": 3, "NONE": 8},
            dict(MODULE.release_prerequisite_counts(data)),
        )
        self.assertIn(
            "33 accounted entries are 21 checked findings, 4 implemented findings",
            MODULE.accounting_prose(data),
        )

    def test_unknown_release_dependency_classification_is_rejected(self) -> None:
        manifest = json.loads(MODULE.MANIFEST_PATH.read_text(encoding="utf-8"))
        invalid = copy.deepcopy(manifest)
        invalid["release_gate_dependencies"]["gates"][0]["classification"] = "UNKNOWN"

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(invalid), encoding="utf-8")
            with self.assertRaisesRegex(MODULE.ManifestError, "unknown classification"):
                MODULE.load_backlog(path)

    def test_accounting_prose_is_derived_from_manifest_state(self) -> None:
        data = MODULE.load_backlog()
        records = copy.deepcopy(data.records)
        records["UGA-128"]["todo_state"] = "unchecked"
        changed = replace(data, records=records)

        prose = MODULE.accounting_prose(changed)

        self.assertIn("32 accounted entries are 20 checked findings", prose)
        self.assertIn("not a claim of 32 implemented findings", prose)

    def test_unknown_release_prerequisite_is_rejected(self) -> None:
        manifest = json.loads(MODULE.MANIFEST_PATH.read_text(encoding="utf-8"))
        invalid = copy.deepcopy(manifest)
        invalid["release_gate_dependencies"]["gates"][0]["prerequisite"] = "UNKNOWN"

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(invalid), encoding="utf-8")
            with self.assertRaisesRegex(MODULE.ManifestError, "unknown prerequisite"):
                MODULE.load_backlog(path)

    def test_duplicate_cycle_is_rejected(self) -> None:
        manifest = json.loads(MODULE.MANIFEST_PATH.read_text(encoding="utf-8"))
        invalid = copy.deepcopy(manifest)
        duplicate = next(item for item in invalid["removed"] if item["id"] == "UGA-148")
        duplicate["canonical_id"] = "UGA-148"

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(invalid), encoding="utf-8")
            with self.assertRaises(MODULE.ManifestError):
                MODULE.load_backlog(path)

    def test_svg_keeps_validation_outside_lifecycle_status(self) -> None:
        data = MODULE.load_backlog()
        svg = MODULE.render_svg(data)

        self.assertIn("CURRENT RELEASE", svg)
        self.assertIn("v0.6 → v0.7", svg)
        self.assertIn("54 / 65 complete · 83.08%", svg)
        self.assertIn("PROJECT ROADMAP", svg)
        self.assertIn("80 / 194 complete · 41.24%", svg)
        self.assertIn("UGA QUALITY / AUDIT", svg)
        self.assertIn("33 / 205 complete · 16.10%", svg)
        self.assertIn("Lifecycle status", svg)
        self.assertIn("Validation dependencies (orthogonal)", svg)
        self.assertIn("Open v0.7 gate classifications (exclusive)", svg)
        self.assertIn("Open hardware-dependent gates: 8", svg)
        self.assertIn("External-LAN DDS is a separate completed acceptance matrix", svg)
        self.assertIn("Orthogonal prerequisites: software 3 · design decision 0 · none 8", svg)

        changed = replace(
            data,
            release_dependencies={
                **data.release_dependencies,
                "multi-architecture CI build/publish pipeline": "ROBOT_REQUIRED",
            },
        )
        self.assertIn("Open hardware-dependent gates: 9", MODULE.render_svg(changed))

    def test_check_rejects_stale_dashboard_output(self) -> None:
        data = MODULE.load_backlog()
        with tempfile.TemporaryDirectory(dir=MODULE.ROOT) as directory:
            root = Path(directory)
            readme = root / "README.md"
            svg = root / "uga_backlog.svg"
            readme.write_text(MODULE.README_PATH.read_text(encoding="utf-8"), encoding="utf-8")
            svg.write_text("stale", encoding="utf-8")
            with mock.patch.object(MODULE, "README_PATH", readme), mock.patch.object(MODULE, "SVG_PATH", svg):
                with self.assertRaisesRegex(MODULE.ManifestError, "generated artifacts are stale"):
                    MODULE.write_or_check(data, check=True)


if __name__ == "__main__":
    unittest.main()
