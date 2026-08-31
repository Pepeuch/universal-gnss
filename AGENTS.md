# Universal GNSS - Agent Rules

Repository-local working rules for code agents contributing to Universal GNSS.

These rules apply to all automated or AI-assisted work unless a more specific
repository-local instruction explicitly overrides them.

---

## 0. How agents must use this file

`AGENTS.md` defines **how agents must work in this repository**: investigation
discipline, checkpoints, evidence handling, resource usage, validation, Git
safety, scope control, collaboration, and handoff requirements.

### At the beginning of every new agent session

Before substantial work:

1. read this `AGENTS.md` completely;
2. inspect the latest task checkpoint under `.agent/checkpoints/`, when one
   exists;
3. inspect any relevant shared handoff under `.agent/shared/`, when one exists;
4. verify checkpoint or handoff assumptions against the actual repository
   state;
5. establish the current repository path, branch, `HEAD`, dirty state, remotes,
   and submodule state when relevant;
6. determine the owning subsystem and contracts touched by the task;
7. read the relevant versioned audit, analysis, vendor, protocol, terminology,
   ROS2, or architecture documentation when applicable;
8. do not repeat investigation already recorded as established evidence unless
   repository state or new evidence invalidates it.

During the same uninterrupted task, this file and already-read references do
not need to be reread in full after every action.

Reread the relevant material when:

- entering a different work phase;
- crossing into another subsystem;
- changing repository or submodule;
- context was compacted;
- the execution environment was rebuilt;
- the agent session was restarted;
- repository state differs from the checkpoint or shared handoff;
- a contradiction with established evidence is discovered;
- work begins to touch a public or architectural contract not previously
  loaded.

After context compaction or session restart, prefer:

```text
AGENTS.md
    -> latest local checkpoint and/or relevant shared handoff
    -> actual Git state
    -> relevant durable project references
    -> targeted verification
    -> resume work
```

instead of:

```text
repository-wide rediscovery
    -> historical archaeology
    -> repeated tests
    -> reconstructed reasoning
    -> eventual continuation
```

A context compaction, model switch, or new agent session is not new evidence.

### Instruction and evidence roles

Use each repository knowledge layer for its intended role:

- `AGENTS.md` -> stable agent execution policy;
- `.agent/checkpoints/` -> local current-task operational state;
- `.agent/shared/` -> intentionally shared, short-lived contributor/agent
  handoff state;
- versioned audits/analysis/architecture/vendor documentation -> durable shared
  knowledge and technical contracts;
- source code -> authoritative current implementation;
- tests -> executable behavioural evidence;
- Git -> exact repository and implementation state.

Do not duplicate large amounts of information between these layers.

If two applicable sources appear to conflict, do not guess which one to ignore.
Determine whether the conflict is caused by stale documentation, changed code,
or a real architectural contradiction. If it cannot be resolved safely from
repository evidence, stop and report it.

---

## 1. Project identity and architectural boundaries

Universal GNSS is hardware-agnostic.

MowgliNext is a downstream validation platform only.

Never introduce Mowgli-specific assumptions into Universal GNSS core.

### General invariants

- Never silently change public behaviour.
- Never reduce coordinate precision.
- Never change protocol behaviour without documentation.
- Keep receiver-specific logic isolated.
- Generic NMEA must remain vendor-independent.
- Preserve public contracts unless an intentional compatibility change is
  explicitly authorized and documented.
- Prefer portable behaviour over assumptions tied to one robot, receiver,
  operating system, ROS2 deployment, or integration environment.
- Keep public time, freshness, sequence, source, and invalidation semantics
  explicit when they affect downstream authority.
- Do not let convenience wrappers redefine the authoritative semantics of the
  core library.

---

## 2. Planning and scope control

Before editing code, identify:

- the requested behaviour;
- the owning component;
- affected public contracts;
- relevant receiver/vendor scope;
- downstream consumers;
- tests that prove the current and intended behaviour;
- explicitly out-of-scope neighbouring systems.

Keep work separated across these buckets:

- Universal GNSS core tasks;
- ROS2 package tasks;
- receiver-specific backend tasks;
- downstream integration tasks.

Do not silently widen a task into unrelated cleanup, modernization, naming
changes, formatting churn, refactors, or neighbouring bug fixes.

When historical code or pull requests contain both required behaviour and
obsolete mechanisms, port only the required behaviour.

Historical PRs and commits are behavioural evidence, not merge instructions.

Never blindly cherry-pick or restore old implementation without comparing it
to the current architecture.

### Investigation freeze

Before substantial implementation begins, summarize the established contract
and authorized implementation plan in the current checkpoint.

Once implementation starts, do not return to broad exploratory mode unless:

- implementation exposes a contradiction;
- a deterministic test disproves an established assumption;
- required information is genuinely missing;
- repository state has changed in a way that invalidates prior evidence.

An interesting neighbouring observation is not sufficient reason to reopen
broad investigation.

Record it as a follow-up finding and continue the authorized task.

---

## 3. Agent execution discipline

For non-trivial work, establish the repository state before making changes.

Record or verify at minimum:

- repository path;
- branch;
- `HEAD`;
- relevant upstream/base commit;
- remotes when repository identity matters;
- dirty files already present;
- submodule state when applicable.

Do not assume that the current directory name proves repository identity.

Do not overwrite, discard, reset, or reformat unrelated user work.

Prefer focused searches, focused builds, and targeted tests before expanding
to repository-wide validation.

When multiple independent read-only checks are required, batch or parallelize
them when the environment safely supports it.

Do not perform the same expensive search through multiple tools unless the
first result is incomplete, contradictory, or unsuitable.

---

## 4. Agent state persistence and collaboration

Agent working state has three distinct persistence levels.

Do not mix their roles.

### 4.1 Local checkpoints

For substantial edits, long audits, risky migrations, multi-repository work,
or long validation runs, maintain a resumable checkpoint under:

```text
.agent/checkpoints/
```

Examples:

```text
.agent/checkpoints/UGA021_CHECKPOINT.md
.agent/checkpoints/ROS2_STATUS_AUDIT_CHECKPOINT.md
.agent/checkpoints/DOCKERIZATION_RECON_CHECKPOINT.md
```

Local checkpoints are operational memory for one contributor/agent and must
remain ignored by Git.

They exist primarily to survive:

- context compaction;
- model changes;
- agent restarts;
- interrupted sessions;
- long-running investigations;
- execution-environment restarts when the repository itself persists.

Do not use `/tmp` for checkpoints that are expected to survive those events.

Prefer a repository-local checkpoint over information preserved only in
conversation context.

A recommended checkpoint structure is:

```markdown
# Agent checkpoint

Repository:
Branch:
HEAD:
Base/upstream:

## Objective

## Authorized scope

## Established facts — DO NOT REDISCOVER

- ...

## Semantic decisions — DO NOT RE-LITIGATE WITHOUT NEW EVIDENCE

### Decision

...

### Evidence

...

### Rejected alternatives

...

### What would invalidate this decision

...

## Files modified

## Validation evidence — DO NOT RERUN UNLESS INVALIDATED

- command:
- result:
- repository HEAD when executed:
- relevant files/state when executed:

## Validation pending

## Known blockers

## Do not touch

## Exact next step
```

Update the checkpoint:

- before substantial edits;
- after every meaningful milestone;
- before long or expensive validation;
- before changing repository or submodule context;
- before ending a session with unfinished work;
- before entering low-budget or critical-budget mode.

If context is compacted, the agent session restarts, the execution environment
is rebuilt, or work is interrupted, resume from the latest checkpoint and the
actual repository state instead of reconstructing work from memory.

### 4.2 Shared agent/contributor handoffs

Use:

```text
.agent/shared/
```

only when short-lived working knowledge genuinely needs to be exchanged through
Git between contributors or agents.

Files under `.agent/shared/` may be committed intentionally.

Appropriate uses include:

- an unfinished cross-contributor handoff;
- a large audit that another contributor must resume before it is complete;
- an investigation state that must be transferred;
- a temporary remediation matrix;
- a compact shared execution plan;
- a temporary split of work between contributors or agents.

A shared handoff must remain concise and should record, as applicable:

- repository baseline and commit;
- branch/base relationship;
- established evidence;
- semantic decisions;
- unresolved questions;
- authorized scope;
- explicit out-of-scope boundaries;
- validation already executed;
- validation still required;
- exact next step.

Shared handoffs must not contain:

- secrets or credentials;
- large logs;
- generated artifacts;
- copied source trees;
- conversation transcripts;
- unnecessary narrative reasoning;
- local environment noise.

Do not use `.agent/shared/` as permanent project documentation.

Remove or promote a shared handoff once its collaboration purpose is complete.

### 4.3 Durable shared knowledge

Validated knowledge that will remain useful after the current task belongs in
versioned project documentation.

Preferred locations include:

```text
docs/audits/
docs/analysis/
docs/architecture/
```

Existing repository-specific durable ledgers or references may continue to live
at their established versioned path when moving them would create needless
churn.

Durable shared knowledge includes, when useful:

- completed or long-lived audits;
- architecture and data-flow maps;
- public semantic contracts;
- ownership and invalidation rules;
- source/incarnation/reconnect semantics;
- clock-domain rules;
- receiver/vendor compatibility conclusions;
- historical migration classifications;
- confirmed defects and their evidence;
- remediation decisions;
- regression matrices;
- residual limitations;
- unresolved findings;
- dependency and ordering constraints.

The intended lifecycle is:

```text
local investigation
    -> .agent/checkpoints/
    -> shared work required?
         -> no: remain local while active
         -> yes: .agent/shared/
    -> validated and reusable
    -> docs/audits/ | docs/analysis/ | docs/architecture/
       or an existing durable versioned ledger
```

Do not force another contributor or agent to repeat expensive validated
analysis merely because the original work happened in another session.

At the same time, do not preserve transient session noise as permanent project
documentation.

### 4.4 Checkpoint compression rule

A checkpoint is a compact operational state, not a conversation transcript.

Record conclusions and evidence, not the full reasoning process.

Prefer:

- exact commit SHAs;
- exact file paths;
- concise semantic contracts;
- test command plus `PASS` / `FAIL`;
- rejected approach plus a one-line reason;
- exact next action.

Do not copy large logs, diffs, source files, pull-request discussions, or
narrative analysis into checkpoints.

When detailed evidence already exists in a repository document, test, commit,
audit ledger, issue, or shared handoff, reference it instead of duplicating it.

A checkpoint should make resumption cheap, not become another large document
that must be reprocessed.

### 4.5 Git ignore contract for agent state

The repository `.gitignore` is expected to keep local `.agent` state ignored
while allowing intentionally shared handoffs.

Use the equivalent of:

```gitignore
# Agent state
# Local checkpoints/caches remain private, while short-lived shared handoffs
# may be versioned intentionally.
.agent/*
!.agent/shared/
!.agent/shared/**
```

If repository ignore rules do not match this policy, fix or report the mismatch
before relying on `.agent/shared/`.

Never stage `.agent/checkpoints/` or other local `.agent` state.

Stage `.agent/shared/` only when the handoff is intentionally meant to be
exchanged through Git.

---

## 5. Resource, reasoning, and context discipline

Reasoning budget, context window, execution time, tool calls, and compute are
finite engineering resources.

Use them deliberately.

The goal is not to minimize reasoning at the expense of correctness. The goal
is to avoid spending expensive reasoning on information that is already known,
already verified, irrelevant to the authorized scope, or recoverable cheaply
from repository state.

This policy intentionally reduces redundant reasoning, repository exploration,
tool calls, broad test execution, and repeated reconstruction of validated
knowledge.

The primary goals are:

- faster engineering iteration;
- lower unnecessary compute use;
- better use of limited agent/model resources;
- preserving enough budget for final review and validation;
- reducing duplicated work between contributors and agents.

Avoidable compute also means avoidable energy use. Resource efficiency is
therefore an engineering and environmental benefit, but do not invent or claim
quantified energy or carbon savings unless they have actually been measured.

### 5.1 Prefer progressive investigation

Start with the smallest amount of exploration capable of answering the current
question.

Prefer this progression:

1. inspect repository identity, branch, `HEAD`, and current diff;
2. search for the exact symbols, files, tests, or contracts involved;
3. read only the relevant surrounding code;
4. identify authoritative producers, consumers, ownership, lifecycle, and
   invalidation boundaries when applicable;
5. inspect neighbouring components only when the dependency requires it;
6. expand to repository-wide exploration only when targeted evidence is
   insufficient.

Do not begin a task by reading large portions of the repository "just in case".

Prefer targeted search over full-file reading when the exact symbol or concept
is known.

Prefer reading a focused line range over repeatedly reopening an entire large
file.

Batch related searches and independent read-only checks when practical.

### 5.2 Reuse established evidence

Do not repeatedly rediscover facts that have already been verified during the
current task or preserved in a valid shared/durable record.

Once a fact is established and recorded in a local checkpoint, shared handoff,
or durable repository document, treat it as an input unless later repository
changes could invalidate it.

Examples include:

- repository identity;
- branch and base commit;
- architectural ownership;
- public semantic contracts;
- clock-domain rules;
- freshness semantics;
- source/incarnation/reconnect semantics;
- historical PR conclusions;
- explicitly rejected approaches;
- tests already executed successfully;
- files known to be out of scope.

Do not rerun successful expensive validation unless:

- relevant code changed afterward;
- a dependency affecting that contract changed;
- the environment changed in a way that may affect the result;
- generated interfaces or artifacts relevant to the result changed;
- a later failure calls the previous result into question;
- final release or integration validation explicitly requires it.

When revalidation is required, rerun the narrowest affected layer first.

### 5.3 Do not re-litigate settled decisions

Semantic decisions that have already been investigated, justified, and written
to a valid checkpoint, shared handoff, or durable analysis must not be
repeatedly reconsidered without new evidence.

Record important decisions together with their reason:

```text
Decision:
Evidence:
Rejected alternatives:
What would invalidate this decision:
```

This is especially important for:

- clock-domain contracts;
- freshness semantics;
- ownership and invalidation rules;
- source/incarnation/reconnect semantics;
- receiver-specific policy;
- public API compatibility;
- historical implementation archaeology;
- explicitly rejected legacy code.

A context compaction, model switch, or new agent session is not new evidence.

### 5.4 Use a validation ladder

Validation should progress from cheap and focused to expensive and broad.

Typical order:

1. static inspection or exact contract check;
2. focused regression test;
3. affected component test suite;
4. build of directly affected binaries or packages;
5. generated-interface equivalence checks when relevant;
6. integration or ROS2 validation;
7. sanitizer or replay validation when relevant;
8. broader repository validation only when justified.

Do not run the most expensive validation first unless the task specifically
requires it.

If an early validation step fails, investigate that failure before spending
resources on broader validation.

### 5.5 Protect the final validation budget

Do not consume nearly all available reasoning/context budget during
investigation or implementation and leave insufficient capacity for:

- reviewing the resulting diff;
- running targeted regressions;
- checking unintended scope expansion;
- confirming public and architectural invariants;
- updating documentation or audit ledgers;
- updating the checkpoint/shared handoff;
- producing an accurate final handoff.

Reserve enough capacity to prove and report the work.

Finishing implementation without enough budget to validate it is not a
successful completion.

### 5.6 Low-budget mode

When the platform exposes remaining context, reasoning, execution, or quota,
take increasingly conservative action as the remaining budget decreases.

When no explicit budget indicator is available, enter low-budget mode when
there are signs such as:

- repeated context compaction;
- a very long-running agent session;
- substantial repository archaeology already performed;
- many large tool results accumulated;
- validation still pending after significant implementation work.

In low-budget mode:

1. stop broad exploration;
2. stop optional refactoring and cleanup;
3. update the durable checkpoint immediately;
4. preserve all important semantic decisions and test evidence;
5. finish only the smallest safe unit already in progress;
6. prefer targeted tests over broad suites;
7. do not open a new neighbouring finding or feature;
8. do not begin another repository-wide audit;
9. leave an exact next command or next action when work must continue later.

If an unfinished task must be transferred to another contributor, update an
appropriate `.agent/shared/` handoff before stopping.

### 5.7 Critical-budget mode

If the remaining budget becomes critically low, prioritize recoverability over
completion.

Immediately record:

- repository and branch;
- current `HEAD`;
- dirty files;
- current objective;
- completed implementation;
- validation already passed;
- validation still required;
- unresolved questions;
- exact next step.

Do not use the last available budget attempting speculative fixes,
repository-wide searches, broad test suites, or unrelated cleanup.

If the implementation is complete but validation is incomplete, report it as
implemented but not yet validated.

Never convert incomplete evidence into a successful completion claim merely
because the session is ending.

### 5.8 Context compaction recovery

After automatic context compaction, model switch, execution-environment rebuild,
or restarted agent session:

1. read the required project rules according to Section 0;
2. read the latest local checkpoint;
3. read any relevant `.agent/shared/` handoff;
4. verify repository path, branch, `HEAD`, dirty state, remotes, and relevant
   submodule state;
5. compare actual state with the checkpoint/handoff;
6. inspect only information needed to resolve discrepancies;
7. continue from the recorded exact next step.

Do not reconstruct the entire task by rereading the repository.

Do not repeat historical archaeology already summarized in valid evidence
unless repository state or new evidence contradicts it.

### 5.9 Avoid output and tool-call waste

Do not generate long narrative progress reports during active implementation
unless they are required for a checkpoint, decision record, shared handoff, or
final handoff.

Prefer concise progress state containing:

- what was established;
- what changed;
- what passed;
- what remains.

Avoid repeatedly printing large diffs, logs, generated files, or test output
when a concise summary plus the relevant failing lines is sufficient.

For successful commands, preserve the command, exit status, and meaningful
result rather than unnecessary full output.

For failures, retain enough output to diagnose the failure accurately.

### 5.10 Stop conditions

Stop expanding the task when the authorized contract is satisfied.

Do not continue modifying code merely because additional improvements are
visible.

New findings discovered during implementation should normally be:

1. documented;
2. classified;
3. left for a separate authorized task,

unless they are required for correctness or safety of the current change.

A partially completed task must always remain resumable from repository state,
checkpoint state, shared handoff when applicable, and recorded validation
evidence.

### 5.11 Evidence invalidation rule

Previously established evidence remains valid until something relevant changes.

Do not invalidate evidence merely because:

- time passed during the same task;
- context was compacted;
- another agent session started;
- the agent no longer remembers the original reasoning;
- a different model or reasoning tier is now executing the task.

Evidence must be reconsidered when:

- a file involved in the proven contract changed;
- a dependency affecting that contract changed;
- repository `HEAD`, base, or relevant submodule changed in a meaningful way;
- build configuration or environment affecting the result changed;
- generated interfaces changed;
- new evidence contradicts the previous conclusion.

When uncertain, determine whether the dependency changed before rerunning
expensive validation.

Record the repository `HEAD` and relevant changed files with expensive
validation results whenever practical.

### 5.12 Reasoning-tier discipline

If the execution platform offers multiple models, reasoning tiers, or cost
levels, use the least expensive tier that can reliably perform the current
work.

Prefer lower-cost reasoning for:

- deterministic mechanical edits;
- straightforward test additions;
- formatting;
- documentation synchronization;
- known API plumbing;
- repetitive implementation with an already established contract;
- routine validation follow-through.

Reserve higher-cost reasoning for work that materially benefits from it, such
as:

- semantic ambiguity;
- architecture-sensitive changes;
- difficult defect isolation;
- historical implementation archaeology;
- risky compatibility analysis;
- concurrency, timing, ownership, freshness, provenance, or lifecycle
  semantics;
- final surgical review of high-impact changes.

Do not repeatedly escalate and de-escalate reasoning tiers without a concrete
reason.

When a higher-cost reasoning pass establishes a semantic decision, store that
decision and its evidence in a checkpoint or durable/shared record so cheaper
subsequent work can use it without reproducing the expensive analysis.

### 5.13 Search and reading economy

Use repository search as an index, not as an excuse to read everything.

When a symbol, field, command, message, topic, parameter, or test name is
known:

1. search for that exact term;
2. identify producers, consumers, configuration sources, and tests;
3. inspect only relevant contexts;
4. widen the search only if the dependency graph remains unclear.

Avoid reopening the same large file repeatedly.

Keep short notes in the checkpoint about relevant files and symbols once they
are understood.

When historical PRs or commits are required, first identify the precise
behavioural question being answered. Do not browse historical changes without
a defined question.

### 5.14 Test execution economy

Do not run multiple broad suites merely for reassurance.

Use dependency-aware test selection:

- changed leaf implementation -> focused unit test first;
- changed shared library -> focused tests plus affected consumers;
- changed public contract -> all known contract consumers;
- changed `.msg` / `.srv` / generated interface -> generation plus affected
  language bindings and consumers;
- changed ROS2 node/launch/QoS -> affected node and integration tests;
- changed transport/parser/state machine -> focused regression plus relevant
  replay/sanitizer coverage;
- final release/integration gate -> broad suite when explicitly warranted.

If no relevant source changed after a successful expensive test, preserve that
result as valid evidence.

Do not rerun identical expensive tests solely because another unrelated file,
documentation file, checkpoint, or handoff changed.

### 5.15 Avoid speculative implementation

Do not write code to test a theory that can be resolved more cheaply by:

- inspecting an existing test;
- checking the owning API;
- reading a vendor protocol section;
- tracing the exact producer/consumer path;
- examining a small historical diff;
- reproducing the failure with a focused test.

Use implementation as evidence only when cheaper evidence is insufficient.

### 5.16 Preserve a reasoning cache

The combination of:

```text
AGENTS.md
.agent/checkpoints/
.agent/shared/
Git state
tests and generated-interface checks
versioned audits / analyses / architecture / vendor references
```

forms the durable reasoning cache for a task.

Use each layer for its intended purpose:

- `AGENTS.md` stores stable project and agent rules;
- local checkpoints store compact current-task conclusions and next state;
- `.agent/shared/` stores intentionally shared short-lived handoff state;
- Git stores exact implementation state;
- tests store executable behavioural evidence;
- versioned audits/analyses/architecture/vendor documentation store durable
  cross-session findings and contracts.

Thinking deeply once and preserving the conclusion is preferred over repeatedly
reconstructing the same reasoning.

---

## 6. Evidence and regression discipline

Do not mark a finding, bug, regression, or task as fixed only because the code
looks correct.

A fix requires evidence appropriate to the affected layer, for example:

- focused unit or regression tests;
- parser fixtures;
- CLI behaviour checks;
- integration tests;
- ROS2 tests;
- replay tests;
- sanitizer runs;
- build verification;
- shell or syntax checks;
- formatting checks;
- `git diff --check`.

Prefer creating or identifying a regression test that demonstrates the
required contract before or together with the implementation.

When an existing test already proves the contract, document that evidence
instead of duplicating it.

Always distinguish explicitly between:

- verified behaviour;
- inferred behaviour;
- historical behaviour;
- untested assumptions.

Do not claim validation that was not actually executed.

---

## 7. Stateful data, freshness, and ownership

For stateful GNSS, correction, diagnostic, transport, or cached data, determine
explicitly:

- who owns the value;
- who may update it;
- who may invalidate it;
- source identity;
- source incarnation or reconnect semantics;
- freshness clock and clock domain;
- cache lifetime;
- reset behaviour;
- whether identical numeric values represent a new physical observation.

Do not treat cached publication as a new physical observation.

Do not compare timestamps from different clock domains unless an explicit
conversion contract exists.

Do not let stale state survive source replacement, reconnect, reset, or
invalidation unless the public contract explicitly requires retention.

For liveness/freshness defects, do not solve ownership or provenance problems
with an arbitrary timeout alone.

---

## 8. Vendor documentation and receiver-specific behaviour

All specific vendor protocols are documented under:

```text
docs/vendors/
```

Receiver-specific behaviour must remain isolated from generic GNSS behaviour.

When modifying a vendor backend:

- verify the vendor protocol or command contract;
- document receiver-specific defaults and limitations;
- preserve generic fallback behaviour where appropriate;
- avoid leaking vendor-specific terminology into generic public APIs unless
  intentionally part of the abstraction.

Historical vendor implementations may be used as evidence, but must not be
restored blindly when current abstractions differ.

---

## 9. Universal GNSS terminology rule

Universal GNSS uses GNSS/geodesy-first terminology in core APIs, protocol
records, diagnostics, documentation, and ROS2 surfaces.

ROS2 integration must preserve the canonical Universal GNSS vocabulary instead
of translating GNSS concepts into robot-specific terms.

Use canonical GNSS/geodetic terms such as:

- `baseline_azimuth_deg`
- `baseline_length_m`
- `baseline_pitch_deg`
- `baseline_solution_status`
- `antenna_baseline`
- `course_over_ground_deg`
- `correction_stream`
- `rtk_mode`

Canonical public baseline data must use:

- `dual_antenna_baseline`
- `baseline_azimuth_deg`
- `baseline_pitch_deg`
- `baseline_length_m`
- `baseline_solution_status`

`heading_deg`, `heading_accuracy_deg`, and `dual_antenna_heading` are
compatibility surfaces only during the current `v0.6.x` transition window.

Avoid robot/application-specific terms in Universal GNSS APIs unless the data
truly comes from that domain.

Do not use these terms for dual-antenna GNSS baseline data in Universal GNSS
core or ROS2 surfaces:

- `yaw`
- `robot_yaw`
- `vehicle_yaw`
- `robot_heading`
- `body_heading`

Documentation may mention common aliases such as "GNSS heading" or "vehicle
heading" only as explanatory text, but the canonical field name should remain
geodetic.

When a risky public term cannot be renamed immediately, document the
compatibility/deprecation plan in:

```text
docs/terminology.md
```

Downstream applications such as MowgliNext may convert
`baseline_azimuth_deg` into robot yaw or vehicle heading after applying their
own antenna mounting transform.

---

## 10. Downstream MowgliNext tracking rule

When implementing or modifying any Universal GNSS feature that affects ROS2
surfaces, diagnostics, launch behaviour, runtime status, correction
observability, operator visibility, or robot-side integration, always evaluate
whether MowgliNext should consume or react to it.

If the change creates new downstream integration work, update:

```text
MOWGLINEXT_TODO.md
```

This applies especially to:

- new ROS2 messages, topics, parameters, diagnostics, or status fields;
- changes to `ReceiverNode`, `NtripNode`, `ReplayNode`, launch files, or ROS2
  adapters;
- new GNSS, RTK, NTRIP, RTCM, correction-stream, parser, or receiver-health
  observability;
- new warning/error states that a robot operator should see;
- new localization or safety conditions that could affect autonomous
  navigation;
- new field-validation requirements for real robot testing.

`MOWGLINEXT_TODO.md` must remain downstream guidance only.

Do not add MowgliNext-specific logic inside Universal GNSS.

When updating `MOWGLINEXT_TODO.md`, describe:

- the new Universal GNSS capability;
- why it matters for MowgliNext;
- where the robot stack should consume it;
- expected GUI/operator behaviour;
- expected localization or safety behaviour, if relevant;
- suggested robot field-validation checks.

Mark these items as pending MowgliNext integration work, not as completed
Universal GNSS work.

---

## 11. Documentation requirements

Every implementation must update:

- `TODO.md` if task status changed;
- `ROADMAP.md` if milestone status changed;
- `README.md` if user-visible behaviour or features changed.

Also update relevant protocol, vendor, terminology, configuration, audit,
analysis, architecture, or integration documentation when the changed contract
requires it.

Documentation must describe current behaviour, not merely implementation
intent.

When an analysis produces reusable validated knowledge, promote it to durable
versioned documentation rather than leaving the only useful copy in a local
checkpoint or temporary shared handoff.

---

## 12. Non-root builds and repository ownership

Builds, tests, formatters, package managers, code generators, and tools that
may write into the repository must run as the normal project user.

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

Root may be used only for operations that genuinely require system-level
privileges.

Repository inspection commands may run with elevated privileges when necessary,
but commands capable of creating/modifying repository artifacts must not.

Do not leave root-owned repository artifacts behind.

If previous work created root-owned generated files or build artifacts, repair
ownership or remove only those generated artifacts before continuing.

---

## 13. Formatting and hygiene

Before finishing work:

- apply the repository's existing formatting conventions;
- run relevant formatter checks on touched code;
- run shell or syntax checks for modified scripts;
- run `git diff --check`;
- verify no unrelated generated files were introduced;
- verify `.agent/checkpoints/` and other local agent state remain ignored;
- verify `.agent/shared/` contains only intentionally shared files.

Do not introduce formatting-only churn outside the authorized scope.

---

## 14. Collaborative analysis and work sharing

Large analyses should be structured so findings can be safely distributed
between contributors or agents without requiring each one to rediscover the
entire repository.

When a broad analysis identifies multiple independent findings or subsystems:

- establish the shared repository/architecture baseline first;
- record common invariants and contracts once;
- split work only after ownership and dependency boundaries are understood;
- give each finding a stable identifier when practical;
- record severity, evidence, scope, dependencies, and remediation status;
- identify which findings can proceed independently;
- identify ordering constraints between findings;
- keep the shared ledger or audit updated as remediation progresses.

A contributor or agent receiving a subtask should be able to determine:

- what is already known;
- what has already been validated;
- what must not be changed;
- which repository baseline the conclusions refer to;
- which dependencies are already fixed;
- which findings remain open;
- which tests establish completion;
- what exact next action is expected.

### Sharing unfinished work

If another contributor must resume work before the analysis is mature enough
for permanent documentation, create or update a concise handoff under:

```text
.agent/shared/
```

The receiving contributor should:

1. read the shared handoff before broad exploration;
2. revalidate repository/branch/HEAD/submodule state;
3. reuse still-valid evidence;
4. investigate only missing, ambiguous, stale, or invalidated conclusions;
5. update, retire, or promote the handoff as ownership changes.

### Sharing a large audit

For a large audit shared between contributors:

- keep the authoritative finding ledger in durable versioned documentation once
  it is mature enough;
- use `.agent/shared/` only for temporary cross-contributor execution state;
- keep local session checkpoints in `.agent/checkpoints/`;
- avoid maintaining separate incompatible copies of the same findings;
- promote stable reusable conclusions to `docs/audits/`, `docs/analysis/`,
  `docs/architecture/`, or an existing durable audit ledger.

Do not duplicate expensive repository-wide analysis across contributors when a
validated shared baseline already exists.

Do not commit every local checkpoint merely to make it shareable.

Share only the minimum state required for collaboration.

---

## 15. Git safety

Never commit or push unless the user explicitly authorizes it.

Before concluding a task, report:

- current repository;
- current branch;
- current `HEAD`;
- modified files;
- untracked files;
- commit status;
- push status.

Never include unrelated:

- editor artifacts;
- generated build files;
- container lock files;
- temporary patches;
- local checkpoint files;
- logs;
- local environment state.

Never stage `.agent/checkpoints/` or other local agent state.

Stage `.agent/shared/` only when the handoff is intentionally meant to be
shared through Git.

Before committing `.agent/shared/`, review it for:

- secrets/credentials;
- stale session noise;
- large logs;
- generated content;
- accidental local-only state.

Prefer promoting mature reusable knowledge into durable versioned documentation
instead of keeping permanent handoffs under `.agent/shared/`.

Do not rewrite history, force-push, destructively reset, or discard unrelated
user work unless explicitly authorized.

### Submodules

For submodules, verify both:

- the submodule worktree `HEAD`;
- the parent repository gitlink.

Do not assume they are identical.

When a task modifies both a submodule and its parent:

1. validate the submodule independently;
2. commit/push the submodule only when authorized;
3. update the parent gitlink to that exact commit;
4. validate the parent;
5. commit/push the parent only when authorized.

---

## 16. Completion contract

A task is complete only when:

1. the requested behaviour is implemented;
2. targeted regression evidence passes;
3. relevant broader validation passes, or its absence is explicitly stated;
4. formatting and syntax checks pass;
5. `git diff --check` passes;
6. required documentation and audit ledgers are updated;
7. remaining known limitations and open findings are reported;
8. local/shared agent-state disposition is explicit when relevant;
9. commit status is explicit;
10. push status is explicit.

Do not claim completion while required validation is still running, blocked, or
unknown.

For substantial audits or architecture studies, also verify whether validated
reusable knowledge should be promoted into durable shared documentation before
closing the task.

---

## 17. Handoff

Every final response for implementation, audit, migration, debugging, or
architecture work must explicitly report, as applicable:

- files modified;
- why each file changed;
- tests executed;
- validation performed;
- important evidence established;
- remaining known limitations;
- relevant open findings;
- current repository, branch, and `HEAD`;
- relevant submodule state when applicable;
- commit status;
- push status.

Never omit changes performed during the task.

If the task is intentionally left incomplete, also report:

- what remains;
- why it remains;
- the latest local checkpoint;
- whether a `.agent/shared/` handoff was created or updated for another
  contributor;
- validation still pending;
- the exact recommended next step.

Do not describe a task as complete while required validation is still running,
blocked, or unknown.