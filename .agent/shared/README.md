# Shared agent handoffs

This directory is for short-lived handoff material that must be shared through
Git between Universal GNSS contributors or agents.

Use it for compact unfinished-work state such as:

- cross-contributor handoffs;
- temporary remediation matrices;
- shared execution plans;
- resumable investigation state;
- large audit state that another contributor must continue before the audit is
  mature enough for permanent documentation.

Do not use it for local session checkpoints. Those belong under:

```text
.agent/checkpoints/
```

and remain ignored by Git.

Do not use this directory as permanent project documentation.

Validated reusable knowledge must be promoted to the appropriate durable
versioned location, preferably:

- `docs/audits/`
- `docs/analysis/`
- `docs/architecture/`

or an existing project-specific versioned audit/analysis ledger when keeping
its established location is clearer than moving it.

Every shared handoff should identify, as applicable:

- repository baseline / commit;
- branch or upstream/base relationship;
- established evidence;
- semantic decisions;
- unresolved items;
- scope boundaries and "do not touch" areas;
- validation already completed;
- validation still required;
- exact next step.

When a handoff transfers audit, backlog, or remediation state:

- preserve the stable finding/item identifiers used by the authoritative ledger;
- use line numbers only as secondary baseline information, not as the primary
  identity of a finding or task;
- identify relevant durable evidence sources as `CURRENT`, `PARTIALLY_STALE`, or
  `SUPERSEDED` when freshness matters;
- for `PARTIALLY_STALE` evidence, state the known stale conclusion/section and
  the replacement evidence;
- preserve canonical duplicate/dependency relationships rather than creating a
  second incompatible classification graph;
- reference the authoritative audit/ledger instead of copying the full ledger
  into the handoff;
- include only the minimum state needed for another contributor or agent to
  resume safely.

A shared handoff is not a second source of truth. If an authoritative durable
ledger or audit already exists, reference it and record only the temporary
execution state, unresolved delta, ownership transfer, and next action.

Keep shared handoffs concise. Do not copy conversation transcripts, large logs,
generated artifacts, secrets, credentials, unnecessary source content, or full
copies of durable audit ledgers.

Remove or promote a handoff once its collaboration purpose is complete. If its
findings become stable and reusable, move that knowledge into durable versioned
project documentation and retire the temporary handoff.