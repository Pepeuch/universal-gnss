# UG-DOCS-STATUS-001 closure

Lifecycle: CLOSED

## Decision

The versioned `docs/status/uga_backlog.json` is the public UGA status source.
`scripts/update_backlog_status.py` validates the compact 205-item manifest,
TODO mapping, conservation, duplicate graph, release-gate classifications, and
generated README/SVG output. CI runs focused tests and `--check`; it never
silently regenerates stale output.

## Evidence

- Dashboard implementation: `6368ed9`.
- Dedicated CI follow-up and later parser repairs are present on `dev`.
- Current counts and release-gate dependencies must always be derived from the
  manifest and `TODO.md`; historical counts in this closure are non-authoritative.

## Validation

Focused generator tests, Python compilation, generator `--check`, checkpoint
audit, and `git diff --check` passed when implemented. Current CI preserves
these checks.

## Invalidation conditions

Reopen only if the manifest schema, TODO accounting model, generated artifact
contract, or CI validation path changes materially.
