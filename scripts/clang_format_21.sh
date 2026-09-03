#!/usr/bin/env bash
# Run the repository-authoritative C/C++ formatter on project-owned C/C++ files.
set -euo pipefail

if [[ $# -lt 2 || ( "$1" != "--check" && "$1" != "--apply" ) ]]; then
  echo "usage: $0 --check|--apply (--all | --changed <base> | <C/C++ files...>)" >&2
  exit 2
fi

mode="$1"
shift

repository_root="$(git rev-parse --show-toplevel)"
cd "$repository_root"

project_roots=(
  gnss_core
  gnss_protocols
  gnss_driver
  gnss_transport
  gnss_ntrip
  gnss_tools
  gnss_ros2
)

is_cpp_file() {
  case "$1" in
    *.c | *.cc | *.cpp | *.cxx | *.h | *.hh | *.hpp | *.hxx) return 0 ;;
    *) return 1 ;;
  esac
}

collect_project_files() {
  local -n collected_files="$1"
  shift

  local path
  while IFS= read -r -d '' path; do
    if is_cpp_file "$path"; then
      collected_files+=("$path")
    fi
  done < <("$@")
}

files=()
case "$1" in
  --all)
    if [[ $# -ne 1 ]]; then
      echo "--all does not accept file arguments" >&2
      exit 2
    fi
    collect_project_files files git ls-files -z -- "${project_roots[@]}"
    ;;
  --changed)
    if [[ $# -ne 2 ]]; then
      echo "--changed requires exactly one base revision" >&2
      exit 2
    fi
    collect_project_files files git diff --name-only -z "$2" HEAD -- "${project_roots[@]}"
    ;;
  *)
    files=("$@")
    ;;
esac

if ((${#files[@]} == 0)); then
  echo "No project-owned C/C++ files selected."
  exit 0
fi

if ! command -v clang-format-21 >/dev/null 2>&1; then
  echo "clang-format-21 is required; arbitrary clang-format versions are not authoritative." >&2
  exit 127
fi

if [[ "$mode" == "--check" ]]; then
  exec clang-format-21 --dry-run --Werror -style=file "${files[@]}"
fi

exec clang-format-21 -i -style=file "${files[@]}"
