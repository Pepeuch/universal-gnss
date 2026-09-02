# Universal GNSS — Audit and Backlog Policy v1.3

Load this module for repository audits, backlog reconciliation, stable finding IDs,
classification, duplicate handling, evidence freshness, or durable audit ledgers.

Root `AGENTS.md` remains authoritative for Git, scope, budget, and completion.

## 1. Stable finding identity

Use stable IDs for durable findings (for example `UGA-xxx`) and do not silently reuse
an ID for another finding.

Before changing a finding classification, verify its canonical ID and current durable
manifest/ledger. Historical local checkpoints are not the source of truth when a
versioned durable manifest exists.

## 2. Orthogonal classification

Keep these dimensions separate.

### STATUS

Use only the repository's canonical status vocabulary:

- `IMPLEMENTED`
- `PARTIAL`
- `OPEN`
- `BLOCKED`
- `SUPERSEDED`
- `OBSOLETE`
- `DUPLICATE`

`PARTIAL` means part of the requested contract is actually implemented. Groundwork alone
is not partial.

`BLOCKED` requires both:

```text
Blocked by:
Unblocks when:
```

### SCOPE

Scope answers where the work belongs, for example:

- `CORE`
- `PROTOCOL`
- `DRIVER`
- `NTRIP`
- `TOOLS`
- `ROS2`
- `DEPLOYMENT`
- `RECEIVER`
- `DOCUMENTATION`
- `VALIDATION`
- `DOWNSTREAM`

Do not use a scope label as a status.

### VALIDATION

Validation is orthogonal to STATUS and SCOPE.
Use when applicable:

- `HARDWARE_REQUIRED`
- `HARDWARE_PENDING`

Missing hardware proof is not evidence freshness and must not be encoded as `STALE`.

## 3. Conservation accounting

For a reconciled backlog preserve:

```text
original_items = sum(all classified items)
```

After cleanup:

```text
original_items = remaining_items + intentionally_removed_items
```

Do not “fix” counts by silently dropping unmatched items. Every durable audited item
must remain accounted for as present, intentionally removed, or explicitly replaced /
superseded according to repository policy.

## 4. Duplicate graph

Every `DUPLICATE` must point directionally to exactly one canonical non-duplicate item.

Requirements:

- no duplicate cycles;
- no duplicate chain terminating in another duplicate;
- no ambiguous multiple canonical targets;
- canonical target remains explicit after cleanup.

Do not delete duplicate information before canonical ownership is proven.

## 5. Evidence freshness

For durable analysis evidence, distinguish:

- `CURRENT`
- `PARTIALLY_STALE`
- `SUPERSEDED`

Freshness describes whether durable evidence still applies to the current repository
baseline.

Do not mark evidence stale merely because time passed, context compacted, model changed,
or a new session began.

Repair/promote stale durable evidence only when necessary for the active finding; do not
broaden into unrelated documentation cleanup.

## 6. Hardware validation classification

When acceptance depends on physical receiver/device/electrical/timing/firmware/USB/
serial/reconnect/hotplug/RF/kernel-driver/physical-link behaviour, load `HARDWARE.md`.

Examples:

```text
STATUS: BLOCKED
SCOPE: DRIVER
VALIDATION: HARDWARE_REQUIRED
```

or, only where the implementation contract itself is complete without physical
acceptance:

```text
STATUS: IMPLEMENTED
SCOPE: DRIVER
VALIDATION: HARDWARE_PENDING
```

Do not force `IMPLEMENTED` when physical validation is part of acceptance.

## 7. Durable manifest/dashboard discipline

When the repository has a versioned backlog/status manifest, treat it as the durable
public source of truth for generated status/dashboard artifacts.

Generated dashboard/status views must derive from that versioned source, never from
ignored `.agent/checkpoints/`.

Shared checkpoints under `.agent/shared/checkpoints/` are resumable evidence caches, not a second classification ledger. Their directory (`active`, `blocked`, `retained`, `closed`) must agree with the canonical finding status, but status changes are made in the durable manifest/TODO first.

When a finding changes status, evaluate checkpoint disposition: OPEN/PARTIAL → `active` when resumable state is useful; BLOCKED → `blocked`; IMPLEMENTED with future reusable detail → `retained`; IMPLEMENTED with only anti-rediscovery value → compact `closed`; no reusable value → delete checkpoint after durable promotion.

Do not derive backlog counts from checkpoint directories.

When a generator provides `--check`, CI/check mode must verify consistency rather than
silently regenerate stale artifacts.

Update backlog/TODO/dashboard only when the finding's durable classification actually
changes.

## 8. Audit completion

Before declaring reconciliation complete:

- recount all status buckets;
- verify conservation;
- verify stable ID uniqueness;
- verify duplicate graph directionality/acyclicity;
- verify STATUS/SCOPE/VALIDATION separation;
- verify intentional removals;
- verify durable evidence freshness;
- run deterministic manifest/dashboard checks where available.

Do not claim completion while any required acceptance check is unknown or blocked.
