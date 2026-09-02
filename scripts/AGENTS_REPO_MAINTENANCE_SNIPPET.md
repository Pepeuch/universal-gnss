## Repository maintenance assistance

When asked to help manage or maintain the repository:

1. establish repository path, branch, `HEAD`, worktree state, remotes, and relevant submodule state;
2. read `.agent/shared/checkpoints/INDEX.md` when present;
3. use deterministic repository-maintenance scripts when available rather than reconstructing state manually;
4. treat TODO/manifest/ledger as the canonical finding classification source;
5. never derive backlog counts or finding status from checkpoint directories;
6. reconcile shared checkpoint lifecycle only after canonical finding status is established;
7. report stale, duplicated, missing, or lifecycle-inconsistent checkpoint entries rather than silently rewriting evidence;
8. prefer mechanical checks for routine maintenance and reserve semantic investigation for actual contradictions;
9. never commit or push without explicit authorization.

For routine maintenance, prefer:

```bash
make agent-status
make agent-checkpoints
make agent-maintenance
```

A repository-maintenance request does not authorize unrelated implementation work.
