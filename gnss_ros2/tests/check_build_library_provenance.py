"""Verify the ELF loader selects the low-level libraries from this build."""

import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    executable, *expected_libraries = sys.argv[1:]
    result = subprocess.run(
        ["ldd", executable],
        check=True,
        capture_output=True,
        text=True,
        env={**os.environ, "LC_ALL": "C"},
    )
    resolved = dict(re.findall(r"^\s*(\S+) => (\S+) \(", result.stdout, re.MULTILINE))
    for library in expected_libraries:
        expected = Path(library).resolve()
        actual = resolved.get(Path(library).name)
        if actual is None or Path(actual).resolve() != expected:
            raise AssertionError(
                f"{Path(library).name}: expected {expected}, loaded {actual}\n{result.stdout}"
            )
        print(f"{Path(library).name}: {actual}")


if __name__ == "__main__":
    main()
