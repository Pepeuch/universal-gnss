#!/usr/bin/env python3
"""Focused regressions for deterministic Docker entrypoint configuration guards."""

from __future__ import annotations

import os
import subprocess
import unittest
from pathlib import Path


ENTRYPOINT = Path(__file__).resolve().parents[2] / "docker" / "entrypoint.sh"
HEALTHCHECK = Path(__file__).resolve().parents[2] / "docker" / "healthcheck.sh"
DOCKERFILE = Path(__file__).resolve().parents[2] / "Dockerfile"


class DockerEntrypointContractTests(unittest.TestCase):
    def test_unknown_configuration_schema_fails_before_ros_setup(self) -> None:
        environment = os.environ.copy()
        environment["ROS_DISTRO"] = "kilted"
        environment["UNIVERSAL_GNSS_CONFIGURATION_SCHEMA_VERSION"] = "2"

        result = subprocess.run(
            ["bash", str(ENTRYPOINT)], env=environment, capture_output=True, text=True, check=False
        )

        self.assertEqual(2, result.returncode)
        self.assertEqual("", result.stdout)
        self.assertIn("universal_gnss_entrypoint event=unsupported_configuration_schema_version", result.stderr)

    def test_current_schema_defaults_to_v1(self) -> None:
        source = ENTRYPOINT.read_text(encoding="utf-8")
        self.assertIn(': "${UNIVERSAL_GNSS_CONFIGURATION_SCHEMA_VERSION:=1}"', source)
        self.assertIn('  1) ;;', source)

    def test_image_healthcheck_uses_bounded_component_responsiveness(self) -> None:
        source = DOCKERFILE.read_text(encoding="utf-8")
        self.assertIn('CMD ["/usr/local/bin/universal-gnss-healthcheck", "--timeout", "2"]', source)
        self.assertNotIn("pgrep -f", source)

    def test_healthcheck_enables_nounset_only_after_ros_setup(self) -> None:
        source = HEALTHCHECK.read_text(encoding="utf-8")
        self.assertNotIn("set -euo pipefail", source)
        self.assertGreater(source.index("set -u"), source.index("install/setup.bash"))


if __name__ == "__main__":
    unittest.main()
