#!/usr/bin/env python3
"""Static safety contract for the tag-gated multi-architecture release workflow."""

from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


WORKFLOW = Path(__file__).resolve().parents[2] / ".github" / "workflows" / "docker-release.yml"
CASTER = Path(__file__).resolve().parent / "fake_ntrip_caster.py"


class DockerReleaseWorkflowTests(unittest.TestCase):
    def test_fake_caster_emits_a_known_valid_minimal_rtcm_frame(self) -> None:
        spec = importlib.util.spec_from_file_location("fake_ntrip_caster", CASTER)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)

        self.assertEqual("d30002435006a27e", module.rtcm_frame().hex())

    def test_builds_both_supported_architectures_and_ros_distributions(self) -> None:
        source = WORKFLOW.read_text(encoding="utf-8")

        self.assertIn("ros_distro: [kilted, lyrical]", source)
        self.assertIn("platforms: linux/amd64,linux/arm64", source)
        self.assertIn("uses: docker/setup-qemu-action@v4", source)
        self.assertIn("uses: docker/setup-buildx-action@v4", source)

    def test_publication_is_tag_gated_and_emits_attestations(self) -> None:
        source = WORKFLOW.read_text(encoding="utf-8")

        self.assertIn('"${GITHUB_REF}" != refs/tags/v*', source)
        self.assertIn("push: ${{ steps.identity.outputs.publish == 'true' }}", source)
        self.assertIn("sbom: true", source)
        self.assertIn("provenance: mode=max", source)
        self.assertIn("docker buildx imagetools inspect", source)
        self.assertIn("Platform:    linux/amd64", source)
        self.assertIn("Platform:    linux/arm64", source)


if __name__ == "__main__":
    unittest.main()
