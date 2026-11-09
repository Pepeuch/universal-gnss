# Universal GNSS — Codex repository-maintenance pack

Hardware-session preparation uses the passive collector documented in
[`docs/validation/README.md`](../docs/validation/README.md):

- `validation/run_hardware_session.sh` — functional/endurance collection;
- `validation/mark_hardware_event.sh` — append an operator marker safely;
- `validation/analyze_hardware_session.py` — regenerate the mechanical summary.

These tools observe an existing production runtime. They do not start, stop, or
reconfigure it and never award release credit.

This pack adds deterministic helper scripts so Codex can inspect and maintain repository state
without repeatedly reconstructing it from large context.

## Repository usage

The helpers are already present under `scripts/agent/`. Run the commands below
from the repository root with the explicit `scripts/Makefile.agent` file.
The root Makefile currently defines formatting targets only; a bare
`make agent-status` does not work in this checkout.

The bootstrap target creates missing shared-checkpoint scaffolding. Run it only
when that initialization is intended; it is not part of a read-only audit:

```bash
make -f scripts/Makefile.agent agent-bootstrap
```

The root `AGENTS.md` already contains the repository execution policy.
`AGENTS_REPO_MAINTENANCE_SNIPPET.md` is supporting integration material, not a
command to overwrite that policy. No Makefile integration or alias is required
for the explicit invocations here.

## Commands

```bash
make -f scripts/Makefile.agent agent-status
make -f scripts/Makefile.agent agent-checkpoints
make -f scripts/Makefile.agent agent-next
make -f scripts/Makefile.agent agent-ci
make -f scripts/Makefile.agent agent-maintenance
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
