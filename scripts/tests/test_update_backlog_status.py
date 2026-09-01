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
            {"IMPLEMENTED": 9, "PARTIAL": 18, "OPEN": 116, "BLOCKED": 54, "DUPLICATE": 8},
            dict(data.status_counts),
        )
        self.assertEqual("BLOCKED", data.records["UGA-126"]["status"])
        self.assertEqual("DRIVER", data.records["UGA-126"]["scope"])
        self.assertEqual("HARDWARE_REQUIRED", data.records["UGA-126"]["validation"])
        self.assertEqual(188, data.todo_counts["unchecked"])

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

        self.assertIn("Lifecycle status", svg)
        self.assertIn("Validation dependencies (orthogonal)", svg)
        self.assertIn("Hardware required: 1", svg)
        self.assertNotIn(">Hardware required 1</text>", svg)


if __name__ == "__main__":
    unittest.main()
