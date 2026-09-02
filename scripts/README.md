# Universal GNSS — Codex repository-maintenance pack

This pack adds deterministic helper scripts so Codex can inspect and maintain repository state
without repeatedly reconstructing it from large context.

## Install

Copy `scripts/agent/` into the repository, then:

```bash
chmod +x scripts/agent/*.sh scripts/agent/*.py
python3 scripts/agent/bootstrap_checkpoints.py
```

Merge `AGENTS_REPO_MAINTENANCE_SNIPPET.md` into root `AGENTS.md`.

For Make integration, either append `Makefile.agent` to the existing Makefile or include it:

```make
include Makefile.agent
```

## Commands

```bash
make agent-status
make agent-checkpoints
make agent-next
make agent-ci
make agent-maintenance
```

`agent-status` reports Git state, backlog counts, and shared-checkpoint counts.

`agent-checkpoints` validates lifecycle coherence between the canonical manifest and shared
checkpoints. Missing shared checkpoints are warnings only: promotion remains an explicit choice.

`agent-next` selects the first finding by default priority:

```text
PARTIAL -> OPEN -> BLOCKED
```

Override with, for example:

```bash
python3 scripts/agent/finding_next.py --status OPEN --status PARTIAL
```

`agent-ci` uses authenticated `gh` when available and otherwise exits cleanly.

`agent-maintenance` performs deterministic local consistency checks. It does not edit, commit,
push, or silently repair state.

The manifest/TODO/ledger remains authoritative. Shared checkpoints are resumable evidence caches.
