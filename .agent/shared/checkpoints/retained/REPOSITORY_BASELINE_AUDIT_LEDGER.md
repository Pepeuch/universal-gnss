# Repository Baseline Audit Ledger

Lifecycle: RETAINED

This ledger preserves stable IDs, duplicate relationships, and baseline audit
evidence. Current classifications and counts come from
`docs/status/uga_backlog.json` and `TODO.md`; historical current-state prose
below is not a live dashboard.

Repository: `/workspaces/universal-gnss`  
Branch: `audit/repository-baseline`  
HEAD: `b4fa7f0fd51bab8b7562795416e1211e10623a05`  
Reconciled: `2026-08-31`

## Baseline and identity

The immutable audit baseline is the 205 unchecked items in `HEAD:TODO.md`.
Each has a stable `UGA-###` identity based on that frozen ordinal, not its
mutable Markdown line number. The current `TODO.md` retains 189 items after
the prior authorized cleanup and the completed `UGA-128`, `UGA-133`, `UGA-134`,
and `UGA-141` tasks. IDs `UGA-129`,
`UGA-132`, `UGA-148` through
`UGA-154`, `UGA-176`, `UGA-199`, and `UGA-200` are removed baseline records.

Status and scope are independent. `DOCUMENTATION` and `DOWNSTREAM` are scope
values, not lifecycle states. Evidence is VERIFIED unless noted otherwise.

## Status Classification (All 205 Baseline Items)

| Stable IDs | Status |
| --- | --- |
| `UGA-001`-`UGA-017` | OPEN |
| `UGA-018`-`UGA-020` | PARTIAL |
| `UGA-021`-`UGA-033` | OPEN |
| `UGA-034`-`UGA-036` | PARTIAL |
| `UGA-037`-`UGA-045` | OPEN |
| `UGA-046` | PARTIAL |
| `UGA-047`-`UGA-069` | OPEN |
| `UGA-070` | PARTIAL |
| `UGA-071` | OPEN |
| `UGA-072` | PARTIAL |
| `UGA-073` | OPEN |
| `UGA-074` | PARTIAL |
| `UGA-075`-`UGA-076` | OPEN |
| `UGA-077`-`UGA-125` | BLOCKED |
| `UGA-126` | PARTIAL |
| `UGA-127` | PARTIAL |
| `UGA-128` | IMPLEMENTED |
| `UGA-129` | IMPLEMENTED (removed) |
| `UGA-130` | IMPLEMENTED |
| `UGA-131` | PARTIAL |
| `UGA-132` | IMPLEMENTED (removed) |
| `UGA-133` | IMPLEMENTED |
| `UGA-134` | IMPLEMENTED |
| `UGA-135`-`UGA-138` | OPEN |
| `UGA-139` | PARTIAL |
| `UGA-140` | OPEN |
| `UGA-141` | IMPLEMENTED |
| `UGA-142`-`UGA-147` | OPEN |
| `UGA-148`-`UGA-154` | DUPLICATE (removed) |
| `UGA-155`-`UGA-159` | OPEN |
| `UGA-160` | PARTIAL |
| `UGA-161`-`UGA-165` | OPEN |
| `UGA-166`-`UGA-167` | PARTIAL |
| `UGA-168`-`UGA-169` | OPEN |
| `UGA-170` | PARTIAL |
| `UGA-171`-`UGA-174` | OPEN |
| `UGA-175` | BLOCKED |
| `UGA-176` | DUPLICATE (removed) |
| `UGA-177` | OPEN |
| `UGA-178` | BLOCKED |
| `UGA-179` | PARTIAL |
| `UGA-180` | OPEN |
| `UGA-181`-`UGA-182` | BLOCKED |
| `UGA-183`-`UGA-198` | OPEN |
| `UGA-199`-`UGA-200` | IMPLEMENTED (removed) |
| `UGA-201`-`UGA-205` | OPEN |

### Corrected Status Counts

| Status | Count |
| --- | ---: |
| IMPLEMENTED | 29 |
| PARTIAL | 16 |
| OPEN | 99 |
| BLOCKED | 53 |
| SUPERSEDED | 0 |
| OBSOLETE | 0 |
| DUPLICATE | 8 |
| **Total** | **205** |

## Hardware Validation Properties

| Stable ID | STATUS | SCOPE | VALIDATION | Blocked by | Unblocks when |
| --- | --- | --- | --- | --- | --- |
| `UGA-126` | PARTIAL | DRIVER | HARDWARE_REQUIRED | Physical transport-incarnation recovery validation after an indeterminate receiver command, on the exact supported receiver/firmware/host/kernel/driver/topology baseline. | The hardware matrix proves a qualified recovery boundary prevents A(target X)'s late response from being delivered or accepted for B(target X), and emits a new incarnation token only after that cutoff. |

## Scope/Type Classification (All 205 Baseline Items)

| Stable IDs | Scope / type |
| --- | --- |
| `UGA-001`-`UGA-008`, `UGA-010`-`UGA-029`, `UGA-031`-`UGA-038`, `UGA-040`-`UGA-069` | DEPLOYMENT |
| `UGA-009`, `UGA-030`, `UGA-039`, `UGA-070`-`UGA-076`, `UGA-129`, `UGA-139`, `UGA-197`-`UGA-200` | DOCUMENTATION |
| `UGA-077`-`UGA-125` | BLUEOS |
| `UGA-126`-`UGA-128`, `UGA-130` | DRIVER |
| `UGA-131`, `UGA-137`-`UGA-138`, `UGA-193`-`UGA-196` | CORE |
| `UGA-132`, `UGA-142`-`UGA-147`, `UGA-149`-`UGA-154` | NTRIP |
| `UGA-133`-`UGA-135`, `UGA-141` | TOOLS |
| `UGA-136`, `UGA-201`-`UGA-205` | VALIDATION |
| `UGA-140`, `UGA-148` | PROTOCOL |
| `UGA-155`-`UGA-169` | ROS2 |
| `UGA-170`-`UGA-182` | RECEIVER |
| `UGA-183`-`UGA-192` | DOWNSTREAM |

### Scope/Type Counts

| Scope / type | Count |
| --- | ---: |
| DEPLOYMENT | 66 |
| DOCUMENTATION | 16 |
| BLUEOS | 49 |
| DRIVER | 4 |
| CORE | 7 |
| NTRIP | 13 |
| TOOLS | 4 |
| VALIDATION | 6 |
| PROTOCOL | 2 |
| ROS2 | 15 |
| RECEIVER | 13 |
| DOWNSTREAM | 10 |
| **Total** | **205** |

## Strict PARTIAL Evidence

Every PARTIAL below has a portion of the same requested contract in place.
Adjacent groundwork was otherwise downgraded to OPEN.

| ID | Existing | Missing / completion criterion |
| --- | --- | --- |
| `UGA-018` | Discovery and devcontainer guidance use stable serial identities. | Supported production-container device contract and validation. |
| `UGA-019` | `docs/devcontainer.md` and `docs/tools.md` describe fallback paths. | Fallback in the production deployment contract. |
| `UGA-020` | Device examples avoid mandatory `--privileged`. | Least-privilege production image/runtime policy and proof. |
| `UGA-034` | Runtime/ROS2 diagnostics distinguish service-adjacent and receiver health. | Container healthcheck contract preserves that separation. |
| `UGA-035` | `ReceiverNode` stale-state tests distinguish observations from publication. | Deployment-health behavior consumes the distinction. |
| `UGA-036` | NTRIP/RTCM diagnostics distinguish the listed lower-layer states. | Container health/operator contract exposes them coherently. |
| `UGA-046` | Devcontainer ROS2 guidance covers discovery constraints. | Common production Docker deployment guidance. |
| `UGA-070` | `docs/devcontainer.md` offers a Docker/devcontainer quick path. | Production quick-start. |
| `UGA-072` | Existing device-permission guidance covers the direct Docker path. | Production deployment-specific guidance. |
| `UGA-074` | ROS2/devcontainer docs cover a limited Docker integration path. | Supported production ROS2/container guide. |
| `UGA-127` | Auto-config plan/apply has guarded rollback/reporting. | Complete production failure contract across deployment lifecycle. |
| `UGA-128` | Discovery projects fresh receiver-incarnation metadata from documented passive Unicore `VERSIONA` and u-blox `MON-VER` responses; plan/report tests cover the copied output. | Implemented at `b16ae8c` plus the current uncommitted passive physical-identity increment. |
| `UGA-130` | Built-in profiles declare generated config-profile support and focused driver regressions prove capability-subset and exact config-support consistency for each concrete driver. | No remaining work within this finding's scope. |
| `UGA-131` | Native Unicore correction-age fields are mapped. | Portable estimation for other sources. |
| `UGA-139` | `docs/terminology.md` contains a compatibility/deprecation plan. | Final removal decision and execution after `v0.6.x`. |
| `UGA-160` | Kilted local validation is recorded. | Ongoing CI guard that keeps it green. |
| `UGA-166` | Receiver reconnect/stale-state node tests exist. | Broader ROS2 operational validation. |
| `UGA-167` | NTRIP reconnect diagnostics and node tests exist. | Broader ROS2 operational validation. |
| `UGA-170` | Unicore supports guarded `factory_reset` planning/apply. | Portable u-blox or common support. |
| `UGA-179` | Docs preserve the generic-NMEA boundary for the placeholder profile. | Enforce and verify that boundary during the Quectel backend implementation. |

Downgraded from the prior PARTIAL interpretation because only adjacent
groundwork exists: `UGA-001`, `UGA-004`, `UGA-047`, `UGA-049`-`UGA-057`,
`UGA-136`, `UGA-141`, `UGA-147`, `UGA-157`, `UGA-161`, `UGA-171`,
`UGA-197`, and `UGA-198`.

## Duplicate Canonicalization

All duplicate relations are directional and terminate at a canonical item whose
status is OPEN. No canonical item is a DUPLICATE and no cycle exists.

| Duplicate | Canonical | Disposition |
| --- | --- | --- |
| `UGA-148` RTCM observation-level decode | `UGA-140` RTCM semantic expansion | Removed from `TODO.md` |
| `UGA-149` multi-caster | `UGA-146` multi-caster | Removed from `TODO.md` |
| `UGA-150` local caster/base | `UGA-147` local caster/base | Removed from `TODO.md` |
| `UGA-151` UDP | `UGA-145` UDP | Removed from `TODO.md` |
| `UGA-152` TLS | `UGA-142` TLS | Removed from `TODO.md` |
| `UGA-153` client certificates | `UGA-143` client certificates | Removed from `TODO.md` |
| `UGA-154` custom CA | `UGA-144` custom CA | Removed from `TODO.md` |
| `UGA-176` Unicore raw observations | `UGA-174` raw observations | Removed from `TODO.md` |

## BLOCKED Dependencies

`UG-AUD-DEPLOY-001` is the stable finding for the supported `v0.7` production
deployment contract (production image, lifecycle/configuration boundaries, and
portable service API). It is not yet complete.

| Blocked IDs | Blocked by | Unblocks when |
| --- | --- | --- |
| `UGA-077` | `UG-AUD-DEPLOY-001` | The v0.7 production deployment contract is accepted. |
| `UGA-078`-`UGA-109` | `UGA-077` | A BlueOS extension architecture defines package, image, lifecycle, hardware, UI, and monitoring boundaries. |
| `UGA-110` | `UGA-049` | A versioned portable non-ROS API exists. |
| `UGA-111`-`UGA-114` | `UGA-077`, `UGA-110` where API-consuming | The extension architecture and required API boundary are available. |
| `UGA-115`-`UGA-125` | `UGA-077` | A runnable BlueOS extension exists to validate. |
| `UGA-175` | `UG-AUD-UNICORE-AGCA-001` | Vendor/field evidence establishes a safe portable AGCA threshold policy. |
| `UGA-178` | `UGA-177` | The Quectel backend audit establishes supported runtime mappings. |
| `UGA-181`-`UGA-182` | `UGA-180` | The Septentrio backend audit establishes supported protocol/session contracts. |

## Evidence Freshness

| Source | Freshness | Reuse / correction |
| --- | --- | --- |
| `docs/runtime_audit.md` | CURRENT | Core/runtime limits and mappings remain usable. |
| `docs/low_level_readiness_audit.md` | PARTIALLY_STALE | Periodic-scheduling deferred text conflicts with `gnss_ntrip` GGA tests; low-level framing/readiness evidence remains usable. |
| `docs/ros2_end_to_end_audit.md` | PARTIALLY_STALE | Line 922 says no ROS2 replay node; replacement is `gnss_ros2/src/replay_node.cpp`, its launch file, and `docs/ros2.md`. |
| `docs/ntrip.md` | PARTIALLY_STALE | Deferred ROS2 NTRIP-node and periodic-GGA statements conflict with current NTRIP code/tests. |
| `ROADMAP.md` | PARTIALLY_STALE | v0.7/v0.8 labels conflict with current `TODO.md`; retain earlier phases only as historical context. |
| `docs/auto_configuration.md` | CURRENT | Current configuration limits, rollback behavior, and deferred survey/base orchestration remain supported. |
| `docs/diagnostics.md` | CURRENT | Health distinction evidence remains supported. |
| `docs/protocols.md` | CURRENT | Protocol deferrals/mappings remain supported by targeted code/tests. |
| `docs/terminology.md` | CURRENT | Compatibility-field deprecation plan remains current. |
| `testdata/README.md` | CURRENT | Sanitized corpus guidance remains current. |

## Conservation and Cleanup Verification

Frozen baseline: `205` items. The prior cleanup removed exactly `12` traced
items: four IMPLEMENTED (`UGA-129`, `UGA-132`, `UGA-199`, `UGA-200`) and eight
DUPLICATE (`UGA-148`-`UGA-154`, `UGA-176`).

`205 = 193 retained + 12 removed`; therefore the earlier `205 -> 193` cleanup
remains valid. `UGA-128`, `UGA-133`, `UGA-134`, and `UGA-141` were subsequently
completed, leaving 189 currently unchecked items without changing the cleanup
accounting.

## Authoritative Evidence Cache

Implementation/test evidence is recorded in the prior checkpoint and remains
valid because `HEAD` did not change. Key direct evidence includes
`gnss_ros2/src/replay_node.cpp`, `gnss_ros2/src/ntrip_node.cpp`,
`gnss_ntrip/src/ntrip_client.cpp`, `gnss_ntrip/tests/test_ntrip_client.cpp`,
`gnss_ros2/tests/test_receiver_node.cpp`, and
`gnss_ros2/tests/test_ntrip_node.cpp`.
