#!/usr/bin/env bash
# Run the repository-authoritative C/C++ formatter on explicitly supplied files.
set -euo pipefail

if [[ $# -lt 2 || ( "$1" != "--check" && "$1" != "--apply" ) ]]; then
  echo "usage: $0 --check|--apply <C/C++ files...>" >&2
  exit 2
fi

mode="$1"
shift

if ! command -v clang-format-21 >/dev/null 2>&1; then
  echo "clang-format-21 is required; arbitrary clang-format versions are not authoritative." >&2
  exit 127
fi

if [[ "$mode" == "--check" ]]; then
  exec clang-format-21 --dry-run --Werror -style=file "$@"
fi

exec clang-format-21 -i -style=file "$@"
