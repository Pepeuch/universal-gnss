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
        for lifecycle in ("active", "blocked", "retained", "closed"):
            with self.subTest(lifecycle=lifecycle):
                directory = ROOT / ".agent/shared/checkpoints" / lifecycle
                expected = sum(
                    1
                    for path in directory.glob("*.md")
                    if path.is_file() and not path.is_symlink() and path.name != "README.md"
                )
                self.assertRegex(
                    result.stdout,
                    rf"(?m)^  {lifecycle.upper()}:[ \t]+{expected}$",
                )


if __name__ == "__main__":
    unittest.main()
