# Universal GNSS — Agent Rules v1.3

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
- `.agent/checkpoints/` → local current-task operational state, never versioned;
- `.agent/shared/` → intentional short-lived contributor/agent handoff state;
- versioned audits/analysis/architecture/vendor docs → durable shared knowledge;
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
2. inspect only checkpoints/handoffs whose scope may overlap the current task;
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
- `clang-format`
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

---

## 7. Agent-state persistence and Git boundary

Agent state has exactly three classes:

1. `.agent/checkpoints/` → local operational state, never versioned;
2. `.agent/shared/` → short-lived shared state, versioned only intentionally;
3. durable project docs → validated reusable knowledge, versioned normally.

Do not mix their roles.

Repository ignore rules must preserve the equivalent of:

```gitignore
.agent/*
!.agent/policies/
!.agent/policies/**
!.agent/shared/
!.agent/shared/**
```

Never stage `.agent/checkpoints/`.

Before staging `.agent/shared/`, review it explicitly for:

- secrets/credentials;
- stale session noise;
- large logs;
- generated content;
- transcripts;
- accidental local-only state.

A checkpoint expected to survive compaction/restart must be repository-local under
`.agent/checkpoints/`; do not use `/tmp`.

---

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

### Low-budget mode: below 20%

When visible remaining budget is below 20%:

- stop broad exploration;
- stop optional refactoring/cleanup;
- do not open unrelated findings/subsystems;
- preserve current semantic decisions and validation evidence;
- checkpoint promptly when work is resumable;
- prefer the narrowest useful validation;
- finish only the smallest safe operation already in progress;
- preserve a precise next step.

Do not load `LONGTASK.md` solely because budget dropped below 20%.

### Critical hard stop: at or below 10%

At or below 10%, recoverability has absolute priority.

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
9. local/shared agent-state disposition is explicit when relevant;
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
- whether a shared handoff was created/updated;
- validation pending/blocked;
- exact next step.