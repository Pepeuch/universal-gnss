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

Keep shared handoffs concise. Do not copy conversation transcripts, large logs,
generated artifacts, secrets, credentials, or unnecessary source content.

Remove or promote a handoff once its collaboration purpose is complete.