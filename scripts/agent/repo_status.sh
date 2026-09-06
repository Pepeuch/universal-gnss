#!/usr/bin/env bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
cd "$repo_root"

branch="$(git branch --show-current)"
head="$(git rev-parse --short=12 HEAD)"
worktree="clean"
[[ -n "$(git status --porcelain)" ]] && worktree="dirty"

echo "Repository: $(basename "$repo_root")"
echo "Branch: ${branch:-DETACHED}"
echo "HEAD: $head"
echo "Worktree: $worktree"
echo

if [[ -f docs/status/uga_backlog.json ]]; then
python3 - <<'PY'
from scripts.update_backlog_status import dashboard_counts, load_backlog

data = load_backlog()
counts = data.status_counts

print("Backlog/status manifest:")
for key in ("OPEN", "PARTIAL", "BLOCKED", "IMPLEMENTED", "DUPLICATE", "SUPERSEDED", "OBSOLETE"):
    if key in counts:
        print(f"  {key}: {counts[key]}")
print(f"  Remaining: {dashboard_counts(data)['remaining']}")
PY
else
  echo "Backlog/status manifest: missing docs/status/uga_backlog.json"
fi

echo
base=".agent/shared/checkpoints"
echo "Shared checkpoints:"
for d in active blocked retained closed; do
  if [[ -d "$base/$d" ]]; then
    count="$(find "$base/$d" -maxdepth 1 -type f -name '*.md' ! -name 'README.md' | wc -l | tr -d ' ')"
  else
    count=0
  fi
  printf "  %-9s %s\n" "${d^^}:" "$count"
done

echo
[[ -f "$base/INDEX.md" ]] && echo "Checkpoint index: present" || echo "Checkpoint index: missing"
