#!/usr/bin/env python3
"""Focused regressions for the public UGA status dashboard generator."""

from __future__ import annotations

import copy
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


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
        self.assertEqual({"total": 194, "complete": 50, "not_started": 144}, MODULE.project_progress_counts())
        self.assertEqual({"total": 65, "complete": 25, "not_started": 40}, MODULE.release_progress_counts())
        self.assertEqual({"IMPLEMENTED": 1, "PARTIAL": 3, "OPEN": 2}, dict(MODULE.plan_status_counts()))

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
        svg = MODULE.render_svg(MODULE.load_backlog())

        self.assertIn("CURRENT RELEASE", svg)
        self.assertIn("v0.6 → v0.7", svg)
        self.assertIn("25 / 65 complete · 38.46%", svg)
        self.assertIn("PROJECT ROADMAP", svg)
        self.assertIn("50 / 194 complete · 25.77%", svg)
        self.assertIn("UGA QUALITY / AUDIT", svg)
        self.assertIn("33 / 205 complete · 16.10%", svg)
        self.assertIn("Lifecycle status", svg)
        self.assertIn("Validation dependencies (orthogonal)", svg)
        self.assertIn("Hardware required: 2", svg)
        self.assertNotIn(">Hardware required 2</text>", svg)


if __name__ == "__main__":
    unittest.main()
