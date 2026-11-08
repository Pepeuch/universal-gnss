"""Keep every real consumer honest while allowing unused Release dependencies."""

import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[2] / "gnss_ros2/tests/check_build_library_provenance.py"
SPEC = importlib.util.spec_from_file_location("build_library_provenance", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class LibraryProvenanceTests(unittest.TestCase):
    @staticmethod
    def result(libraries):
        output = "\n".join(f"{name} => {path} (0x123)" for name, path in libraries.items())
        return subprocess.CompletedProcess([], 0, stdout=output)

    def test_real_consumers_collectively_cover_required_libraries(self):
        with patch.object(MODULE.subprocess, "run", side_effect=[
            self.result({"libdriver.so": "/build/libdriver.so"}),
            self.result({"libntrip.so": "/build/libntrip.so"}),
        ]):
            MODULE.verify(["replay", "ntrip"], ["/build/libdriver.so", "/build/libntrip.so"])

    def test_a_library_missing_from_every_consumer_is_still_rejected(self):
        with patch.object(MODULE.subprocess, "run", return_value=self.result({})):
            with self.assertRaisesRegex(AssertionError, "not loaded by any"):
                MODULE.verify(["replay", "ntrip"], ["/build/libntrip.so"])

    def test_one_correct_consumer_cannot_hide_a_stale_copy_in_another(self):
        with patch.object(MODULE.subprocess, "run", side_effect=[
            self.result({"libntrip.so": "/build/libntrip.so"}),
            self.result({"libntrip.so": "/old-install/libntrip.so"}),
        ]):
            with self.assertRaisesRegex(AssertionError, "old-install"):
                MODULE.verify(["ntrip", "replay"], ["/build/libntrip.so"])


if __name__ == "__main__":
    unittest.main()
