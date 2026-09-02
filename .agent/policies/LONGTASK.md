# Universal GNSS — Long-Task Policy v1.4

Load this module only through the trigger in root `AGENTS.md`.

This module adds detail for substantial multi-step work. Root `AGENTS.md` always wins
where this module is silent or more permissive.

## 1. Checkpoint lifecycle

Use `.agent/checkpoints/` for compact resumable **local working state**.

Create the first checkpoint early on a task likely to be long, normally before roughly
one third of the expected investigation effort.

Update it before substantial edits, after meaningful milestones, before large
builds/test suites/repo-wide searches, before migrations/refactors, before changing
repository/submodule context, and before ending an unfinished session.

Local checkpoints are scratch/restart memory, not the durable archive. Before a substantial finding is closed or intentionally paused for future work, decide whether any of that state must be promoted into `.agent/shared/checkpoints/`.

Recommended structure:

```markdown
# Agent checkpoint

Repository:
Branch:
HEAD:
Base/upstream:

## Objective
## Authorized scope
## Established facts — DO NOT REDISCOVER
## Semantic decisions — DO NOT RE-LITIGATE WITHOUT NEW EVIDENCE
## Files/symbols involved
## Validation evidence — PASS / FAIL / BLOCKED
## Validation pending
## Known blockers
## Do not touch
## Exact next step
```

For important decisions record:

```text
Decision:
Evidence:
Rejected alternatives:
What would invalidate this decision:
```

Prefer exact SHAs, file paths, symbols, concise contracts, validation command plus
PASS/FAIL, and exact next action. Do not copy large logs, diffs, source files, PR
discussions, or narrative reasoning. Reference existing durable evidence instead.

Root `AGENTS.md` owns the `.agent` Git-ignore contract; do not duplicate it here.

### 1.1 Durable checkpoint disposition

Use exactly one disposition at a handoff/closure boundary:

```text
LOCAL_ONLY
ACTIVE
BLOCKED
RETAINED
CLOSED
DELETE
```

- `LOCAL_ONLY` → local working checkpoint only; no cross-workspace reuse justified yet.
- `ACTIVE` → OPEN/PARTIAL and resumable from versioned shared state.
- `BLOCKED` → real blocker with exact unblock condition and completed evidence.
- `RETAINED` → IMPLEMENTED but detailed evidence is expected to support future work.
- `CLOSED` → IMPLEMENTED with only a compact anti-rediscovery record.
- `DELETE` → no checkpoint value remains after durable evidence promotion.

Recommended shared layout:

```text
.agent/shared/checkpoints/
├── INDEX.md
├── active/
├── blocked/
├── retained/
└── closed/
```

### 1.2 Shared checkpoint content

For `ACTIVE`, `BLOCKED`, and `RETAINED`, preserve only: contract, current state, proven evidence, remaining delta, dependencies/invalidation conditions, validation, do-not-redo, exact next step, and blocker/unblock condition where applicable.

For `CLOSED`, reduce aggressively to: decision, evidence, validation, durable references, and invalidation conditions. Do not preserve chronology.

### 1.3 Promotion and cleanup

```text
working checkpoint
    -> classify lifecycle
    -> remove transient/session noise
    -> promote only reusable evidence
    -> update shared checkpoint INDEX
    -> verify durable manifest/TODO remains authoritative
    -> delete/reduce local checkpoint when appropriate
```

Never delete the only copy of evidence required for future continuation. Do not automatically keep every completed checkpoint, and do not automatically version every working checkpoint. Shared state must earn its place by future reuse value. `INDEX.md` is navigation only, never the backlog source of truth.

## 2. Resumption and evidence reuse

On resume:

1. read the applicable checkpoint/shared handoff first;
2. verify repository path/branch/HEAD/worktree/submodules;
3. compare actual state with the recorded baseline;
4. identify dependencies that changed;
5. invalidate only conclusions affected by those changes;
6. reuse everything else;
7. investigate only missing, ambiguous, stale, or contradicted evidence;
8. resume from the recorded exact next step.

Do not repeat expensive analysis because a different model/agent produced it.

Do not rerun successful expensive validation unless relevant implementation,
dependencies, environment/build configuration, generated interfaces, or contradictory
later evidence changed, or a release/integration gate explicitly requires it.

A compaction/model/session change alone is not invalidation; see root `AGENTS.md`.

## 3. Progressive investigation

Investigate from narrowest to broadest:

1. repository identity and current diff;
2. exact symbol/file/test/contract search;
3. relevant surrounding implementation;
4. producer/consumer/ownership/lifecycle/invalidation boundary;
5. neighbouring components only when dependency requires it;
6. repository-wide exploration only when targeted evidence is insufficient.

Use search as an index, not an excuse to read everything.
State a precise behavioural question before historical archaeology.
Do not repeat the same expensive search through several tools unless earlier evidence is
insufficient, contradictory, or unsuitable.

Once implementation starts, obey the root post-implementation investigation freeze.

Avoid speculative implementation when a cheaper proof exists: existing test, owning API,
vendor protocol, exact producer/consumer trace, focused historical diff, or focused
reproduction.

## 4. Validation ladder and test economy

Progress from cheap/focused to expensive/broad:

1. static inspection / exact contract check;
2. focused regression;
3. affected component suite;
4. directly affected build targets;
5. generated-interface equivalence where relevant;
6. integration / ROS2 validation;
7. replay / sanitizer / hardware validation where relevant;
8. broader repository validation only when justified.

Investigate an early relevant failure before widening validation.

Select tests by dependency impact:

- changed leaf implementation → focused unit/regression first;
- changed shared library → focused tests plus affected consumers;
- changed public contract → known contract consumers;
- changed generated interface → generation plus affected bindings/consumers;
- changed ROS2 node/launch/QoS → affected node/integration tests;
- changed transport/parser/state machine → regression plus relevant replay/sanitizer;
- receiver/physical behaviour → load `HARDWARE.md`;
- release/integration gate → broad suite only when explicitly warranted.

Do not rerun identical expensive tests solely because docs/checkpoints/unrelated files
changed.

## 5. Reasoning and output economy

Use the least expensive reasoning tier that can reliably perform the current work.

Prefer lower-cost reasoning for deterministic mechanical edits, straightforward tests,
formatting, documentation synchronization, known API plumbing, repetitive implementation
under an established contract, and routine validation follow-through.

Reserve higher-cost reasoning for semantic ambiguity, architecture-sensitive change,
difficult defect isolation, historical archaeology, risky compatibility analysis,
concurrency/timing/ownership/freshness/provenance/lifecycle semantics, or final surgical
review of high-impact changes.

Do not oscillate tiers without a concrete reason. Preserve expensive semantic conclusions
in checkpoint/durable evidence so later work can reuse them.

Keep progress concise: established / changed / passed / remains. Avoid repeatedly
printing large logs/diffs when a compact diagnostic is sufficient.

## 6. Protect final validation capacity

Reserve enough capacity for diff review, targeted regression, scope verification,
public/architectural invariant checks, checkpoint/handoff update, and accurate final
report.

Do not spend almost all capacity on exploration/implementation and leave validation
impossible.

Root `AGENTS.md` owns the <20% and <=10% budget rules. This module must never relax or
reinterpret them.

## 7. Durable knowledge

Validated reusable project knowledge belongs in versioned project documentation, commonly `docs/audits/`, `docs/analysis/`, `docs/architecture/`, vendor docs, or established durable ledgers.

Shared checkpoints are **agent memory**, not a replacement for project documentation or the authoritative backlog manifest.

```text
local investigation
    -> local working checkpoint
    -> shared checkpoint when resumability/reuse justifies it
    -> durable project documentation when the conclusion becomes project knowledge
    -> reduce/reclassify/delete redundant checkpoint state
```

A shared checkpoint may remain `RETAINED` after project documentation exists only when it contains concise operational resumption information that would still be costly to reconstruct.

Durable records should preserve baseline/date/scope, verified facts versus assumptions, evidence, contracts, open questions, status, and invalidation conditions. Remove transient noise during promotion. Do not discard the only source checkpoint before the promoted record has been reviewed.

## 8. Collaboration

For large multi-finding analyses:

- establish one shared repository/architecture baseline;
- record common invariants once;
- use stable finding IDs;
- record evidence, scope, dependencies, status, and completion criteria;
- identify independent versus ordered work;
- maintain one authoritative mature ledger.

A receiving contributor should be able to determine: what is known, what is validated,
what must not change, baseline, fixed dependencies, open findings, tests establishing
completion, and exact next action.

Use `.agent/shared/` intentionally. Generic handoffs should remain minimal and short-lived; `.agent/shared/checkpoints/` may persist according to the lifecycle above. Do not duplicate repository-wide analysis when a validated shared baseline exists. Promote mature project knowledge to durable docs and reduce/delete redundant checkpoint material afterward.
