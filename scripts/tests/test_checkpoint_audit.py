#!/usr/bin/env python3
"""Focused regressions for shared-checkpoint audit ownership and manifest parsing."""

from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "agent" / "checkpoint_audit.py"
SPEC = importlib.util.spec_from_file_location("checkpoint_audit", SCRIPT_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class CheckpointAuditTests(unittest.TestCase):
    def test_compact_manifest_expands_complete_current_dataset(self) -> None:
        findings = MODULE.load_manifest()

        self.assertEqual(205, len(findings))
        self.assertEqual("OPEN", findings["UGA-001"]["status"])
        self.assertEqual("PARTIAL", findings["UGA-126"]["status"])
        self.assertEqual("HARDWARE_REQUIRED", findings["UGA-126"]["validation"])
        self.assertEqual("PARTIAL", findings["UGA-170"]["status"])
        self.assertEqual("HARDWARE_REQUIRED", findings["UGA-170"]["validation"])

    def test_incidental_uga_references_do_not_claim_checkpoint_ownership(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            active = base / "active"
            active.mkdir()
            (active / "UG-PLAN-DEPLOYMENT-001_CHECKPOINT.md").write_text(
                "Evidence mentions UGA-126 and UGA-170, but owns neither.\n", encoding="utf-8"
            )
            (active / "UG-DRIVER-RESPONSE-FENCE-001_CHECKPOINT.md").write_text(
                "UGA-126 is an incidental citation.\n", encoding="utf-8"
            )

            entries, duplicates = MODULE.shared_entries(base)

        self.assertEqual({}, entries)
        self.assertEqual([], duplicates)

    def test_duplicate_filename_ownership_remains_an_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            active = base / "active"
            blocked = base / "blocked"
            active.mkdir()
            blocked.mkdir()
            (active / "UGA-126_receiver.md").write_text("owner\n", encoding="utf-8")
            (blocked / "UGA-126_recovery.md").write_text("owner\n", encoding="utf-8")

            _, _, problems, _ = MODULE.audit(checkpoint_base=base)

        self.assertEqual(1, len(problems))
        self.assertIn("UGA-126: duplicated shared checkpoint", problems[0])

    def test_current_deployment_checkpoint_has_no_false_uga126_duplicate(self) -> None:
        findings, entries, problems, warnings = MODULE.audit()

        self.assertEqual(205, len(findings))
        self.assertFalse(
            any("UG-PLAN-DEPLOYMENT-001_CHECKPOINT.md" in str(path) for _, path in entries.values())
        )
        self.assertFalse([problem for problem in problems if problem.startswith("UGA-126:")])
        self.assertEqual([], warnings)


if __name__ == "__main__":
    unittest.main()
