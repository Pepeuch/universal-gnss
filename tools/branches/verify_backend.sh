#!/usr/bin/env bash
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1
exec python3 "$(dirname -- "${BASH_SOURCE[0]}")/distribution.py" verify "$@"
