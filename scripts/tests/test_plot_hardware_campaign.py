#!/usr/bin/env python3
"""Evidence-contract regressions for passive campaign extraction."""

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "validation/plot_hardware_campaign.py"
SPEC = importlib.util.spec_from_file_location("plot_hardware_campaign", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def sample(sequence, mono, *, incarnation="one", stamp=1):
    return {"monotonic_seconds": mono, "ros": {"status": {
        "source_id": "receiver", "source_incarnation": incarnation,
        "position_observation_sequence": sequence, "stamp": stamp,
    }}}


class CampaignEvidenceTests(unittest.TestCase):
    def test_republished_runtime_stamp_is_not_a_new_position(self):
        before = sample(100, 1, stamp=11)
        cached = sample(100, 21, stamp=9999)
        self.assertEqual(0, MODULE.position_rate(before, cached))
        # Wall/ROS stamp is deliberately irrelevant to the rate denominator.
        self.assertEqual(7, MODULE.position_rate(before, sample(240, 21, stamp=-50)))

    def test_replacement_regression_or_missing_provenance_is_not_net_progress(self):
        before = sample(100, 1)
        self.assertIsNone(MODULE.position_rate(before, sample(200, 21, incarnation="two")))
        self.assertIsNone(MODULE.position_rate(before, sample(1, 21)))
        self.assertIsNone(MODULE.position_rate(before, sample(200, 1)))
        self.assertIsNone(MODULE.position_rate(before, sample(200, 21, incarnation="")))

    def test_paris_and_utc_represent_the_same_instant(self):
        self.assertEqual(MODULE.utc_seconds("2026-11-08T13:06:40.246107+01:00"),
                         MODULE.utc_seconds("2026-11-08T12:06:40.246107Z"))
        with self.assertRaisesRegex(ValueError, "explicit timezone"):
            MODULE.utc_seconds("2026-11-08T13:06:40")

    def test_missing_measurement_is_not_a_zero_measurement(self):
        row = [0, None, None]
        MODULE.combine(row, 1, None)
        MODULE.combine(row, 1, "nan")
        self.assertIsNone(row[1])
        MODULE.combine(row, 1, "0")
        self.assertEqual(0, row[1])
        MODULE.combine(row, 2, 1, minimum=True)
        MODULE.combine(row, 2, 0, minimum=True)
        self.assertEqual(0, row[2])

    def test_changed_raw_evidence_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content = b'{"sequence":1}\n'
            (root / "input.jsonl").write_bytes(content)
            expected = {"input.jsonl": {"path": "input.jsonl", "bytes": len(content),
                                       "sha256": hashlib.sha256(content).hexdigest()}}
            sources = []
            self.assertEqual([{"sequence": 1}], list(MODULE.records(root, "input.jsonl", expected, sources)))
            self.assertEqual(1, len(sources))
            (root / "input.jsonl").write_bytes(b'{"sequence":2}\n')
            with self.assertRaisesRegex(ValueError, "identity mismatch"):
                list(MODULE.records(root, "input.jsonl", expected, []))

    def test_versioned_series_conserves_measured_counts_and_missing_ram(self):
        root = SCRIPT.parents[2]
        series = json.loads((root / "docs/validation/evidence/2026-11-08-rc6-mowing.series.json").read_text())
        metrics_path = root / "docs/validation/evidence/2026-11-08-rc6-mowing.metrics.json"
        metrics = json.loads(metrics_path.read_text())
        self.assertEqual(hashlib.sha256(metrics_path.read_bytes()).hexdigest(), series["metrics_sha256"])
        count = series["verified_counts"]
        self.assertEqual(count["publications"], 1 + count["publications_with_new_position"] + count["same_position_sequence"])
        for key, value in count.items():
            self.assertEqual(metrics["continuous"]["counts"].get(key, 0), value, key)
        self.assertEqual(metrics["functional"]["samples"], len(series["functional"]))
        periodic_rss = [row for row in series["resources"]["monitor"] if row[2] is not None]
        self.assertEqual(27, len(periodic_rss))
        self.assertGreater(periodic_rss[0][0], series["functional"][0][0] + 40 * 60)
        self.assertEqual(6, len(series["rtk"]["transitions"]))
        duration = series["rtk"]["transitions"][-1][0] - series["rtk"]["transitions"][0][0]
        self.assertAlmostEqual(8.999196, duration, places=5)


if __name__ == "__main__":
    unittest.main()
