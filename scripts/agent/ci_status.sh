#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

if ! command -v gh >/dev/null 2>&1; then
  echo "gh CLI unavailable; skipping remote GitHub Actions status."
  exit 0
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "gh CLI is not authenticated; skipping remote GitHub Actions status."
  exit 0
fi

echo "Recent GitHub Actions runs:"
gh run list --limit 12 \
  --json workflowName,displayTitle,status,conclusion,headBranch,headSha,createdAt \
  --template '{{range .}}{{printf "%-24s %-10s %-10s %s\n" .workflowName .status .conclusion .displayTitle}}{{end}}'
