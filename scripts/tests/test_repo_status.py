#!/usr/bin/env python3
"""Focused regression for compact-manifest repository status output."""

from __future__ import annotations

import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class RepoStatusTests(unittest.TestCase):
    def test_status_expands_compact_manifest_and_reports_shared_lifecycles(self) -> None:
        result = subprocess.run(
            ["bash", "scripts/agent/repo_status.sh"],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(0, result.returncode, result.stderr)
        self.assertIn("OPEN: 99", result.stdout)
        self.assertIn("PARTIAL: 20", result.stdout)
        self.assertIn("Remaining: 172", result.stdout)
        self.assertIn("ACTIVE:   2", result.stdout)
        self.assertIn("BLOCKED:  2", result.stdout)
        self.assertIn("RETAINED: 3", result.stdout)
        self.assertIn("CLOSED:   1", result.stdout)


if __name__ == "__main__":
    unittest.main()
