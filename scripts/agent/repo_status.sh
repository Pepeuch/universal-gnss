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
import json
from pathlib import Path

data = json.loads(Path("docs/status/uga_backlog.json").read_text())

def walk(obj):
    if isinstance(obj, dict):
        yield obj
        for v in obj.values():
            yield from walk(v)
    elif isinstance(obj, list):
        for v in obj:
            yield from walk(v)

findings = {}
for obj in walk(data):
    if not isinstance(obj, dict) or "status" not in obj:
        continue
    fid = obj.get("id") or obj.get("finding_id") or obj.get("uga_id")
    if isinstance(fid, str) and fid.startswith("UGA-"):
        findings[fid] = obj

counts = {}
for obj in findings.values():
    status = str(obj.get("status", "UNKNOWN")).upper()
    counts[status] = counts.get(status, 0) + 1

print("Backlog/status manifest:")
for key in ("OPEN", "PARTIAL", "BLOCKED", "IMPLEMENTED", "DUPLICATE", "SUPERSEDED", "OBSOLETE"):
    if key in counts:
        print(f"  {key}: {counts[key]}")
remaining = sum(counts.get(k, 0) for k in ("OPEN", "PARTIAL", "BLOCKED"))
print(f"  Remaining: {remaining}")
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
