#!/usr/bin/env python3
"""Analyze one passive Universal GNSS hardware-session directory."""

from __future__ import annotations

import sys

from hardware_session import main


if __name__ == "__main__":
    sys.argv.insert(1, "analyze")
    raise SystemExit(main())
