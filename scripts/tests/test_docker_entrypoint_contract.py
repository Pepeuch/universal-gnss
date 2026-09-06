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
COMPOSE = Path(__file__).resolve().parents[2] / "docker" / "compose.yaml"
KILTED_WORKFLOW = Path(__file__).resolve().parents[2] / ".github" / "workflows" / "ros2-kilted.yml"
LYRICAL_WORKFLOW = Path(__file__).resolve().parents[2] / ".github" / "workflows" / "ros2-lyrical.yml"


class DockerEntrypointContractTests(unittest.TestCase):
    def test_ros_ci_enables_nounset_only_after_distribution_setup(self) -> None:
        for distro, workflow in (("kilted", KILTED_WORKFLOW), ("lyrical", LYRICAL_WORKFLOW)):
            source = workflow.read_text(encoding="utf-8")
            setup = f"source /opt/ros/{distro}/setup.bash"

            self.assertIn("set -eo pipefail", source)
            self.assertIn(setup, source)
            self.assertGreater(source.index("set -u", source.index(setup)), source.index(setup))

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

    def test_external_log_and_export_directories_are_required_writable_surfaces(self) -> None:
        entrypoint = ENTRYPOINT.read_text(encoding="utf-8")
        dockerfile = DOCKERFILE.read_text(encoding="utf-8")
        compose = COMPOSE.read_text(encoding="utf-8")

        self.assertIn('ROS_LOG_DIR:=/var/log/universal_gnss', entrypoint)
        self.assertIn('UNIVERSAL_GNSS_EXPORT_DIR:=/var/lib/universal_gnss/export', entrypoint)
        self.assertIn('event=log_directory_not_writable', entrypoint)
        self.assertIn('event=export_directory_not_writable', entrypoint)
        self.assertIn('COPY scripts/collect_support_snapshot.py /usr/local/bin/universal-gnss-support-snapshot', dockerfile)
        self.assertIn('/var/log/universal_gnss', compose)
        self.assertIn('/var/lib/universal_gnss/export', compose)

    def test_ros_console_has_stable_non_colored_collection_envelope(self) -> None:
        source = DOCKERFILE.read_text(encoding="utf-8")

        self.assertIn('RCUTILS_COLORIZED_OUTPUT=0', source)
        self.assertIn(
            'RCUTILS_CONSOLE_OUTPUT_FORMAT="timestamp={time} severity={severity} logger={name} message={message}"',
            source,
        )

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
