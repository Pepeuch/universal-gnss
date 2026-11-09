"""Verify the ELF loader selects the low-level libraries from this build."""

import argparse
import os
from pathlib import Path
import re
import subprocess


def verify(executables, expected_libraries):
    resolutions = []
    for executable in executables:
        result = subprocess.run(
            ["ldd", executable],
            check=True,
            capture_output=True,
            text=True,
            env={**os.environ, "LC_ALL": "C"},
        )
        resolved = dict(re.findall(r"^\s*(\S+) => (\S+) \(", result.stdout, re.MULTILINE))
        resolutions.append((executable, resolved, result.stdout))
    for library in expected_libraries:
        expected = Path(library).resolve()
        loaded = False
        for executable, resolved, output in resolutions:
            actual = resolved.get(expected.name)
            if actual is None:
                continue
            loaded = True
            if Path(actual).resolve() != expected:
                raise AssertionError(
                    f"{executable}: {expected.name}: expected {expected}, loaded {actual}\n{output}"
                )
            print(f"{executable}: {expected.name}: {actual}")
        if not loaded:
            raise AssertionError(f"{expected.name}: expected {expected}, not loaded by any checked consumer")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable")
    parser.add_argument("expected_libraries", nargs="+")
    parser.add_argument("--executable", action="append", default=[], dest="additional")
    args = parser.parse_args()
    verify([args.executable, *args.additional], args.expected_libraries)


if __name__ == "__main__":
    main()
