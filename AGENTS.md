# Universal GNSS — Agent Rules v1.4

Repository-local execution policy for automated or AI-assisted contributors.

This file is the **always-loaded core**. Detailed policies live under
`.agent/policies/` and are loaded only when their trigger applies.

The objective is **validated progress per unit of resource**.
Never trade correctness, public-contract integrity, hardware safety, or recoverability
for token, context, compute, or time savings.

---

## 1. Instruction and evidence roles

Use each layer for its intended role:

- `AGENTS.md` → stable always-on agent execution policy;
- `.agent/policies/*.md` → conditional detailed execution policy;
- `.agent/checkpoints/` → local working checkpoints and session scratch, never versioned;
- `.agent/shared/checkpoints/` → intentionally versioned resumable finding memory, classified by lifecycle;
- other `.agent/shared/` content → intentional short-lived contributor/agent handoff state;
- versioned audits/analysis/architecture/vendor docs → durable project knowledge;
- source code → authoritative current implementation;
- tests → executable behavioural evidence;
- Git → exact repository and implementation state.

Do not duplicate large bodies of information between these layers.

If applicable sources conflict:

1. determine whether one is stale or refers to another repository state;
2. prefer current executable evidence where appropriate;
3. if the contradiction cannot be resolved safely, stop and report it;
4. never silently guess.

A context compaction, model switch, or new agent session is **not new evidence**.

---

## 2. Session startup and policy loading

Before substantial work:

1. read this `AGENTS.md` completely;
2. read `.agent/shared/checkpoints/INDEX.md` when present, then inspect only the local/shared
   checkpoint(s) whose scope may overlap the current task;
3. establish repository path, branch, `HEAD`, dirty state, remotes, and relevant
   submodule state;
4. compare any applicable checkpoint/handoff baseline with actual repository state;
5. identify the owning subsystem and public/architectural contracts touched;
6. load the smallest applicable policy module(s);
7. read only the relevant durable project references;
8. reuse established evidence unless a recorded dependency changed.

Do not begin with repository-wide rediscovery.

### Conditional modules

Load `.agent/policies/LONGTASK.md` when any of these becomes true:

- the task spans multiple dependent implementation/investigation steps;
- an **applicable** checkpoint or handoff exists for the task;
- architectural, lifecycle, ownership, freshness, provenance, or concurrency
  reasoning is materially required;
- work must survive interruption, compaction, or another session;
- investigation expands materially beyond the initially identified component;
- collaboration or durable multi-finding analysis is involved.

Load `.agent/policies/AUDIT.md` for backlog/audit reconciliation, stable finding IDs,
classification, conservation accounting, duplicate graphs, or durable audit ledgers.

Load `.agent/policies/MIGRATION.md` when using historical PRs, forks, old branches,
patches, or semantic porting/adaptation.

Load `.agent/policies/ROS2.md` when modifying C++ source/header files under `ros2/`
or when ROS2-specific validation/formatting rules apply.

Load `.agent/policies/HARDWARE.md` when correctness depends on physical receiver,
serial/USB transport, firmware, RF, reconnect/hotplug, kernel/driver behaviour,
electrical behaviour, or another physical boundary that software evidence cannot
deterministically prove.

Modules are cumulative and supplement this core; they never weaken it.

---

## 3. Reread and compaction rules

Already-loaded rules do not need to be reread after every command.

Reread relevant material when:

- entering a different work phase;
- crossing into another subsystem;
- changing repository or submodule;
- context is compacted;
- execution environment/devcontainer is rebuilt;
- agent session is restarted;
- repository state differs from checkpoint/handoff assumptions;
- a contradiction with established evidence appears;
- work starts touching a public/architectural contract not previously loaded.

After compaction, restart, or model switch:

1. reload every policy module that was active for the current task;
2. read the applicable checkpoint/handoff;
3. verify actual Git/repository state;
4. invalidate only conclusions whose dependencies changed;
5. resume from the recorded exact next step.

Do not reconstruct the task by rereading the repository.

---

## 4. Universal GNSS project invariants

Universal GNSS is hardware-agnostic.

MowgliNext is a downstream validation platform only.
Never introduce MowgliNext-specific assumptions into Universal GNSS core.

Always preserve these boundaries unless an intentional public compatibility change is
explicitly authorized and documented:

- do not silently change public behaviour;
- do not reduce coordinate precision;
- do not change protocol behaviour without documentation;
- keep receiver-specific logic isolated;
- generic NMEA remains vendor-independent;
- prefer portable behaviour over assumptions tied to one robot, receiver, OS, ROS2
  deployment, or integration environment;
- keep public time, freshness, sequence, source, and invalidation semantics explicit;
- convenience wrappers must not redefine authoritative core semantics.

For stateful GNSS/correction/diagnostic/transport/cached data, determine explicitly:

- owner and updater;
- invalidation authority;
- source identity and incarnation/reconnect semantics;
- freshness clock and clock domain;
- cache lifetime and reset behaviour;
- whether numerically identical values are still distinct physical observations.

Never treat cached publication as a new physical observation.
Never compare unrelated clock domains without an explicit conversion contract.
Never let stale state survive source replacement/reconnect/reset unless the contract
explicitly requires retention.

---

## 5. Evidence discipline

Distinguish verified behaviour from assumptions.

For consequential conclusions:

- prefer inspectable current implementation and deterministic tests over names/comments;
- cite exact files, symbols, tests, commits, protocol sections, or runtime observations;
- state uncertainty when evidence does not prove a claim;
- do not re-litigate an established conclusion without new evidence.

Established evidence remains valid until something relevant changes, such as:

- a file involved in the proven contract;
- an affecting dependency;
- meaningful `HEAD`/base/submodule state;
- relevant build/runtime environment;
- generated interface;
- contradictory new evidence.

Time passing, compaction, model switch, or another agent session alone do not
invalidate evidence.

Historical material requires `MIGRATION.md`.

---

## 6. Non-root execution

Builds, tests, formatters, package managers, code generators, and commands that may
write repository artifacts must run as the normal project user.

This includes at least:

- `cmake`
- `ninja`
- `make`
- `colcon`
- `clang-format-21`
- `npm`
- `yarn`
- `pnpm`
- `npx`
- Python package tooling
- generated-file workflows

Root may be used only for operations genuinely requiring system-level privileges.
Read-only inspection may run as root when necessary.

Do not leave root-owned repository artifacts.
Repair ownership or remove only generated root-owned artifacts before continuing.

### Forced sandbox identity exception

The normal project-user rule remains mandatory whenever that user is actually
available and usable. A constrained agent/sandbox may instead set
`SANDBOX_IDENTITY_FORCED = true` only when inspectable environment evidence shows
that its effective identity is imposed and the project user cannot be used
technically: for example, `setuid`/`setgroups`/`runuser` are denied, workspace
ownership is managed by an external mount, or the relevant mount is read-only.

This is an environmental constraint, not evidence of project ownership failure.
In that case:

- do not recursively repair or infer host ownership from sandbox mount metadata;
- do not attempt `chown`/`chmod` repairs on externally managed mounts;
- the sole executable sandbox identity may run reads, builds, tests, temporary
  generation, and validation when those actions require no additional privilege;
- prefer disposable or externally located generated artifacts when persistent
  ownership would be undesirable;
- report `SANDBOX_IDENTITY_FORCED = true` and the concrete evidence in the final
  handoff;
- do not classify validation as blocked or downgrade a finding solely because
  this identity is forced.

This exception never authorizes system-file changes, permission expansion,
`chmod 777`, protection bypasses, additional privileges, or aggressive repair of
external mounts. If the project user is usable, it remains the required identity.

Examples:

- Normal devcontainer: run build/test/generation as `ubuntu`.
- Sandbox with imposed `root` and denied `setgroups`: set
  `SANDBOX_IDENTITY_FORCED = true`; run ordinary repository validation as that
  imposed identity, without treating mount ownership as a project defect.

### C/C++ formatting baseline

The repository-authoritative C/C++ formatter is `clang-format-21`, using the
repository-root `.clang-format`. Apply it only to project-owned C/C++ files:
`make format` and `make format-check` deterministically select tracked files in
the owned component roots, excluding vendor, external, generated, and submodule
content. Do not use an arbitrary unversioned `clang-format` binary as
authoritative validation. For an explicit touched-file set, use
`bash scripts/clang_format_21.sh --apply <files>` or
`bash scripts/clang_format_21.sh --check <files>`.

---

## 7. Agent-state persistence and Git boundary

Agent state has four distinct roles:

1. `.agent/checkpoints/` → local working checkpoint/session state, never versioned;
2. `.agent/shared/checkpoints/` → intentionally versioned resumable finding memory;
3. other `.agent/shared/` content → short-lived shared contributor/agent handoff state;
4. durable project docs → validated reusable project knowledge, versioned normally.

Do not mix their roles.

### Shared checkpoint lifecycle

Versioned checkpoints under `.agent/shared/checkpoints/` use these lifecycle classes:

```text
active/    OPEN or PARTIAL finding that another session/agent may need to resume
blocked/   finding stopped by a real blocker; exact unblock condition must be recorded
retained/  IMPLEMENTED finding whose detailed reasoning/evidence is expected to be reused
closed/    IMPLEMENTED finding requiring only a compact anti-rediscovery closure record
```

A local working checkpoint is not automatically promoted. Before ending or closing a substantial finding, classify its durable disposition explicitly:

```text
LOCAL_ONLY | ACTIVE | BLOCKED | RETAINED | CLOSED | DELETE
```

- `ACTIVE` and `BLOCKED` preserve enough verified state for direct continuation.
- `BLOCKED` records the blocker, exact unblock condition, completed evidence, and exact next action.
- `RETAINED` is for completed work whose detailed semantic decisions, migration evidence, hardware plan, or future dependency would otherwise be expensive to reconstruct.
- `CLOSED` is a compact closure record: contract, decision, evidence, validation, durable references, and invalidation conditions.
- `DELETE` is allowed only when no durable value remains and any needed project evidence has already been promoted elsewhere.
- Never preserve narrative investigation history, large logs, transcripts, or copied diffs merely for completeness.

When present, `.agent/shared/checkpoints/INDEX.md` is the navigation surface. Read it first and load only checkpoint(s) relevant to the current finding. The index is not a backlog source of truth and must not override TODO/manifest/ledger status.

Repository ignore rules must preserve the equivalent of:

```gitignore
.agent/*
!.agent/policies/
!.agent/policies/**
!.agent/shared/
!.agent/shared/**
```

Never stage `.agent/checkpoints/`.

Before staging `.agent/shared/`, review it explicitly for secrets/credentials, stale session noise, large logs, generated content, transcripts, and accidental local-only state.

A local checkpoint expected to survive compaction/restart within the same workspace must be repository-local under `.agent/checkpoints/`; do not use `/tmp`.

A finding expected to survive machine/workspace loss or be reusable by another contributor must instead have an intentionally reviewed shared checkpoint or durable project record.

## 8. Scope control

Requests framed as audit, analysis, review, investigation, reconnaissance, or
compatibility study are analysis-only unless implementation is explicitly requested.

Before editing, identify:

- requested behaviour;
- owning component;
- affected public contracts;
- relevant vendor/receiver scope;
- consumers;
- acceptance evidence;
- explicit out-of-scope neighbours.

Complete one coherent finding/subsystem/phase before opening another.

Do not opportunistically fix unrelated defects.
Record them separately unless they block correctness/safety of the requested task.

Once implementation starts, broad investigation is frozen unless:

- implementation exposes a contradiction;
- deterministic evidence disproves an assumption;
- required information is genuinely missing;
- repository state invalidates prior evidence.

If a correct fix requires an explicitly out-of-scope component, stop and explain the
dependency before modifying it.

Never weaken an invariant, compatibility contract, safety boundary, or test expectation
merely to fit the requested scope.

---

## 9. Budget policy

Budget policy is always active and does **not** require `LONGTASK.md`.

Apply numeric thresholds only when the execution environment explicitly exposes
a reliable remaining-budget or remaining-capacity value applicable to the task.
Never infer a budget percentage from context length, tool-call count, elapsed
time, compaction, task complexity, output length, or model intuition. If no
such metric is available, do not enter low-budget or hard-stop mode. An explicit
environment report that capacity is exhausted or critically unsafe remains a
hard-stop signal.

### Low-budget mode: below 20%

When an explicitly available applicable remaining-budget metric is below 20%:

- stop broad exploration;
- stop optional refactoring/cleanup;
- do not open unrelated findings/subsystems;
- preserve current semantic decisions and validation evidence;
- checkpoint promptly when work is resumable;
- prefer the narrowest useful validation;
- finish only the smallest safe operation already in progress;
- preserve a precise next step.

Do not load `LONGTASK.md` solely because the applicable budget metric dropped below 20%.

### Critical hard stop: at or below 10%

When an explicitly available applicable remaining-budget metric is at or below
10%, recoverability has absolute priority.

Do **not** start any new:

- patch;
- failed-patch retry;
- edit;
- test or validation command;
- investigation branch;
- implementation substep;
- documentation edit/promotion;
- finding;
- subsystem task.

There is no “small enough” or “atomic enough” exception.

Only these actions are permitted:

1. allow a command already executing to finish;
2. inspect the minimum repository/Git state needed for handoff;
3. consolidate already-known facts into the checkpoint;
4. record validation already passed/failed/blocked/pending;
5. record dirty files and current objective;
6. record the exact next action;
7. stop.

“Consolidate” means recording already-established state, **not deriving or verifying new
facts**.

If `LONGTASK.md` was never loaded, use this emergency checkpoint:

```text
HEAD / dirty state:
Established current state:
Exact next step:
```

Never claim completion merely because the session is ending.

---

## 10. Validation and completion

Validate from the cheapest sufficient layer upward.

Always:

- begin with focused validation appropriate to the changed contract;
- distinguish pre-existing failures from task regressions;
- investigate a relevant early failure before widening validation;
- verify formatting/syntax for touched files;
- run `git diff --check`;
- preserve unrelated files and expected submodule state.

Never weaken a test merely to make a suspect implementation pass.

Do not declare a finding fixed or a task complete while required validation is failed,
blocked, running, or unknown.

A task is complete only when:

1. requested behaviour is implemented;
2. acceptance criteria are deterministically supported;
3. targeted regression evidence passes;
4. relevant broader validation passes, or its absence is explicitly stated;
5. formatting/syntax checks pass;
6. `git diff --check` passes;
7. required documentation/audit status is updated;
8. remaining limitations/open findings are explicit;
9. local/shared agent-state disposition is explicit when relevant, including lifecycle class for substantial findings;
10. commit and push status are explicit.

---

## 11. Documentation and downstream tracking

Update documentation when the changed contract requires it.

At minimum evaluate:

- `TODO.md` when task status changes;
- `ROADMAP.md` when milestone status changes;
- `README.md` when user-visible behaviour/features change;
- relevant protocol/vendor/terminology/configuration/audit/analysis/architecture docs.

Documentation must describe current behaviour, not implementation intent.

### Progress indicators

After every meaningful proven milestone, update every applicable progress bar,
completion indicator, TODO, roadmap, and checkpoint from the repository's
documented accounting model. Keep the UGA quality/audit metric, project roadmap
metric, and current-release metric separate, and keep each mutually consistent with its
own formula; do not count `PARTIAL`, `BLOCKED`, or `HARDWARE_REQUIRED` work as
complete. Context compaction, model switch, or resume is not new evidence.
Leaving an applicable progress indicator stale after a milestone is incomplete
bookkeeping.

When a Universal GNSS change affects ROS2 surfaces, diagnostics, launch/runtime status,
correction observability, operator visibility, or robot-side integration, evaluate
whether downstream work belongs in `MOWGLINEXT_TODO.md`.

`MOWGLINEXT_TODO.md` is downstream guidance only.
Never add MowgliNext-specific logic to Universal GNSS.

---

## 12. Git safety

Never commit or push unless explicitly authorized.

Do not:

- modify or discard unrelated user work;
- delete unrelated untracked files;
- mix generated/environment noise into the patch;
- rewrite history, force-push, or destructively reset without explicit authorization;
- update submodules or gitlinks silently;
- stage local agent checkpoints.

Before broad changes, verify repository identity and baseline.
Before concluding substantial work, revalidate repository state.

If actual state differs from an explicitly required baseline, stop before production
modification and report the discrepancy.

For submodules, verify both the submodule worktree `HEAD` and parent gitlink; never assume
they are identical.

---

## 13. Final handoff

Before stopping, report concisely, as applicable:

- files changed and why;
- tests/validation executed;
- important evidence established;
- known failures/limitations;
- remaining findings;
- repository / branch / `HEAD`;
- relevant submodule/gitlink state;
- commit status;
- push status.

Never omit changes performed during the task.

If work is incomplete, also report:

- what remains and why;
- latest local checkpoint;
- whether a shared checkpoint/handoff was created, updated, retained, reduced, or deleted;
- its lifecycle class when applicable;
- validation pending/blocked;
- exact next step.

---

## Repository maintenance assistance

When asked to help manage the repository:

1. establish Git state first;
2. read `.agent/shared/checkpoints/INDEX.md`;
3. use repository maintenance scripts when available rather than reconstructing
   status manually;
4. never derive backlog state from checkpoints;
5. keep TODO/manifest as canonical finding state;
6. reconcile shared checkpoint lifecycle after a finding changes status;
7. report stale/missing checkpoint entries rather than silently repairing them;
8. never commit or push without explicit authorization.

For routine repository maintenance, prefer deterministic scripts and mechanical
checks over broad repository analysis.
