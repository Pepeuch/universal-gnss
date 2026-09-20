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
DOCKER_WORKFLOW = Path(__file__).resolve().parents[2] / ".github" / "workflows" / "docker.yml"


def docker_child_scripts() -> list[str]:
    """Extract the bodies passed to the workflow's actual Docker bash -lc calls."""
    lines = DOCKER_WORKFLOW.read_text(encoding="utf-8").splitlines()
    scripts = []
    for index, line in enumerate(lines):
        if " bash -lc '" not in line:
            continue
        body = []
        for following in lines[index + 1 :]:
            if following.strip() == "'":
                break
            body.append(following)
        else:
            raise AssertionError("unterminated Docker child shell")
        scripts.append("\n".join(body))
    return scripts


IMAGE_CONTRACT_STUBS = """
test() {
  if [[ "${FAIL_EARLY:-0}" == 1 && "$*" == "-x /opt/universal_gnss/install/lib/universal_gnss_ros2/receiver_node" ]]; then
    return 1
  fi
  return 0
}
command() { return 0; }
ldd() {
  if [[ "${FAIL_LDD:-0}" == 1 ]]; then return 1; fi
  printf 'libexample.so => /lib/libexample.so\\n'
}
ros2() {
  if [[ "$*" == "pkg executables universal_gnss_ros2" ]]; then
    printf 'universal_gnss_ros2 receiver_node\\nuniversal_gnss_ros2 ntrip_node\\n'
    if [[ "${FAIL_PIPELINE:-0}" == 1 ]]; then return 1; fi
  fi
  if [[ "$*" == "pkg executables universal_gnss_msgs" && "${FAIL_MSGS_ROS2:-0}" == 1 ]]; then
    return 1
  fi
  return 0
}
"""


class DockerWorkflowChildShellTests(unittest.TestCase):
    def run_image_contract(self, **environment_overrides: str) -> subprocess.CompletedProcess[str]:
        scripts = docker_child_scripts()
        self.assertEqual(2, len(scripts), "expected both Docker verification child shells")
        environment = os.environ.copy()
        environment.update(environment_overrides)
        return subprocess.run(
            ["bash", "-lc", IMAGE_CONTRACT_STUBS + scripts[0]],
            env=environment,
            capture_output=True,
            text=True,
            check=False,
        )

    def test_early_image_assertion_failure_is_not_masked_by_later_success(self) -> None:
        result = self.run_image_contract(FAIL_EARLY="1")
        self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)

    def test_left_side_of_image_pipeline_failure_is_not_masked(self) -> None:
        result = self.run_image_contract(FAIL_PIPELINE="1")
        self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)

    def test_normal_image_contract_and_intentional_negative_probe_succeed(self) -> None:
        result = self.run_image_contract()
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("universal_gnss_ros2 receiver_node", result.stdout)

    def test_failed_dependency_inspection_is_not_a_successful_negative_probe(self) -> None:
        result = self.run_image_contract(FAIL_LDD="1")
        self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)

    def test_failed_message_interface_listing_is_not_mistaken_for_empty_listing(self) -> None:
        result = self.run_image_contract(FAIL_MSGS_ROS2="1")
        self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)

    def test_persistence_child_shell_does_not_mask_early_failure(self) -> None:
        scripts = docker_child_scripts()
        self.assertEqual(2, len(scripts))
        prelude = scripts[1].split("test ! -w", maxsplit=1)[0]
        result = subprocess.run(
            ["bash", "-lc", prelude + "false\nprintf 'later command succeeds\\n'"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(0, result.returncode, result.stdout + result.stderr)


class DockerEntrypointContractTests(unittest.TestCase):
    def test_ros_ci_sources_ros_and_colcon_without_nounset(self) -> None:
        for distro, workflow in (("kilted", KILTED_WORKFLOW), ("lyrical", LYRICAL_WORKFLOW)):
            source = workflow.read_text(encoding="utf-8")
            setup = f"source /opt/ros/{distro}/setup.bash"

            self.assertIn("set -eo pipefail", source)
            self.assertIn(setup, source)
            self.assertIn("source install/setup.bash", source)
            self.assertNotIn("set -u", source)
            self.assertNotIn("set -o nounset", source)
            self.assertNotIn("set -euo pipefail", source)

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

    def test_runtime_installs_and_validates_cyclonedds_rmw(self) -> None:
        source = DOCKERFILE.read_text(encoding="utf-8")
        runtime = source.split("FROM ros:${ROS_DISTRO}-ros-base AS runtime", maxsplit=1)[1]

        self.assertIn("ros-${ROS_DISTRO}-rmw-cyclonedds-cpp", runtime)
        self.assertIn('test -e "/opt/ros/${ROS_DISTRO}/lib/librmw_cyclonedds_cpp.so"', runtime)
        self.assertIn("RMW_IMPLEMENTATION=rmw_cyclonedds_cpp ros2 --help > /dev/null", runtime)

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
