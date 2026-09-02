# Agent checkpoint

Repository: `/workspaces/universal-gnss`
Branch: `main`
HEAD: `578930e914f078dbb7b4c5b86d3d936babe88890`
Upstream: `origin/main` at the same commit

## Objective

Implement `UG-DOCS-STATUS-001`: a generated public development-status dashboard
for `README.md` from a versioned UGA backlog manifest.

## Authorized scope

- Versioned status manifest, deterministic standard-library generator/tests,
  generated SVG/README block, and concise contributor documentation.
- Do not change GNSS runtime behavior, TODO classifications, or use ignored
  `.agent/checkpoints/` at generator runtime.

## Evidence cache

- CURRENT: corrected audit classifications at `578930e`; the durable manifest
  must preserve 205 baseline items, status/scope orthogonality, UGA-126
  `HARDWARE_REQUIRED`, and eight directional duplicate relations.
- PARTIALLY_STALE: local audit baseline prose says 191 remaining, while current
  committed `TODO.md` has later completed UGA-133/134 work. Derive public counts
  from the manifest plus current TODO rather than reuse that text.

## Plan

1. Encode compact range assignments, TODO state, removals, duplicates, and
   validation properties in `docs/status/uga_backlog.json`.
2. Add a deterministic validator/generator with `--check`.
3. Add focused fixture tests, generate README/SVG, and document the workflow.

## Exact next step

Run focused generator tests and `--check`, then inspect generated README/SVG
and update the checkpoint with durable count/invariant conclusions.

## Established contract

- `docs/status/uga_backlog.json` is the public source of truth. It expands
  compact status/scope/TODO assignments into all 205 unique stable IDs; ignored
  local checkpoint state is never read at runtime.
- `scripts/update_backlog_status.py` validates coverage, status/scope/validation
  orthogonality, TODO retained-order/state mapping, conservation, removed-item
  resolutions, directional non-circular duplicate canonicalization, and stale
  README/SVG output.
- Generated output is limited to the `README.md` marker block and
  `docs/status/uga_backlog.svg`. Dashboard counts are baseline 205, unchecked
  189, closed/resolved 16; status counts are IMPLEMENTED 8, PARTIAL 19, OPEN
  116, BLOCKED 54, DUPLICATE 8; UGA-126 is the sole HARDWARE_REQUIRED item.

## Validation evidence

- `python3 scripts/update_backlog_status.py` — PASS; generated only README
  marker content and `docs/status/uga_backlog.svg`.
- `python3 scripts/tests/test_update_backlog_status.py` — PASS (3 tests).
- `python3 scripts/update_backlog_status.py --check` — PASS.
- `python3 -m py_compile ...` and XML parse of generated SVG — PASS.
- `git diff --check` — PASS.

## Exact next step

Add a dedicated lightweight GitHub Actions workflow that runs the dashboard
unit tests, Python compilation, and `--check` on every push and pull request.

## CI increment

- Current dashboard commit: `6368ed9`.
- Existing `cmake.yml` is a GCC/Clang build matrix; dashboard validation needs
  only Python's standard library and belongs in a separate single-job workflow.
- Added `.github/workflows/backlog-status.yml` on every push and pull request:
  Python compilation, focused dashboard tests, then `--check`. It never runs
  normal generation, so stale README/SVG output fails rather than being repaired
  in CI.

## CI validation

- Workflow YAML structure manually inspected; no YAML parser is installed in
  this environment (`ruby`, `yamllint`, `node`, and `yq` unavailable).
- `python3 -m py_compile scripts/update_backlog_status.py
  scripts/tests/test_update_backlog_status.py` — PASS.
- `python3 scripts/tests/test_update_backlog_status.py` — PASS (3 tests).
- `python3 scripts/update_backlog_status.py --check` — PASS.
- `git diff --check` — PASS.

## Exact next step

Task complete. GitHub Actions will run the dashboard gate on the next push or
pull request; do not add a regeneration step to this workflow.
