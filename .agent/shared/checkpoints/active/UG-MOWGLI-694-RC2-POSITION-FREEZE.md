# UG-MOWGLI-694-RC2 position-freeze checkpoint

Lifecycle: `ACTIVE`

Baseline: `main` at `a8583e774786e5da586f5d06c428578b3929facb`

Durable report: `docs/validation/mowglinext_694_rc2_position_freeze.md`

## Contract and current state

Investigate MowgliNext #694 without changing observation identity, declaring unchanged
coordinates alone a fault, adding restart workarounds, or making downstream production changes.

The RC2 field session proves no differing valid latitude/longitude reached the
post-mapping/pre-aggregation `PositionPayloadFreshnessTracker` boundary while successfully
parsed position observations continued. It does not yet distinguish repeated valid mapped
coordinates from invalid mapped position observations or wire content from decode/mapping.

The supplied `gnss-rc1-test-20260921-1523.jsonl` has been exhaustively inspected. It confirms
the cached fix, observation/runtime, source, health, satellite, and motion facts, but contains no
freshness/payload/epoch keys or diagnostic array. PR #7 is external evidence only; its
implementation must not be copied, cherry-picked, reimplemented, or used as a code source.

## Proven evidence

- BESTNAVA, BESTNAVB, PVTSLNA, PVTSLNB, and Unicore-session GGA each increment runtime and
  position observations after successful parsing; observation identity is independent of value
  identity, validity, aggregate mutation, and publication.
- Freshness tracking runs after mapping but before aggregation and before GGA fallback pruning.
- Valid native position maps copy mandatory parsed coordinates through fresh objects; no
  framer/parser/header/record/session/runner replay or retention path was found.
- Unknown Unicore position types map invalid and omit coordinates; tri-state aggregation then
  preserves prior coordinates. Valid/invalid counter deltas decide whether this path is
  compatible with the supplied field session.
- ReceiverNode publishes the cached fieldwise aggregate on a timer whenever transport and any
  runtime observation remain fresh; publication is not driven by position change.
- Added integrated BESTNAVA replay proves advancing/frozen native epochs are independent from
  unchanged/changed payload counters and that a changed coordinate reaches aggregate state.
- Supplied JSONL: 756 samples over 75.6 sampled seconds; coordinates/altitude/accuracy are each
  single-valued, position sequence is `1015 -> 1388`, runtime observations are `3741 -> 5215`,
  satellites used vary `18..25`, and summary wheel travel is `4.6914` m. SHA-256:
  `ffbc964316cc9d971ee4d1736b4c9e362f7c6e3005c3a30c5b752089dfb9e964`.
- The JSONL has none of `valid_position_payload_observations`,
  `invalid_position_observations`, `position_payload_change_sequence`, or receiver-epoch fields;
  absence must not be interpreted as zero.
- PR #7's indefinite GGA/GST fallback pruning matches the frozen fields, advancing GGA identity,
  changing satellite counts, and healthy stable source. It conflicts with a confirmed flat
  pre-prune payload-change counter if moving GGA coordinates reached mapping, because RC2 tracks
  GGA before pruning. It is therefore a likely contributing or separate Unicore defect, not yet
  proven to be the same #694 root cause.
- PR #7 migration disposition is `ADAPT`, hypothesis only; no code was reused. Semantic concerns:
  parsed-but-invalid native records refresh proposed authority, the 2.5 s window is not
  configuration-derived, and one native timestamp controls both GGA position and GST accuracy.
- Physical discovery found no `/dev/serial/by-id`, no running GNSS receiver process, and no USB
  enumeration facility in this workspace.

## Validation

- Focused build and direct execution of `gnss_driver_test_unicore_session`: PASS.
- Portable CTest suite: PASS. The sandbox run passed 63/68; all five loopback/socket-dependent
  cases passed unchanged with normal network access.
- ROS2 Kilted disposable build and full suite: PASS, nine of nine CTest targets / 128 cases with
  a writable ROS log directory and normal DDS/loopback access.
- Touched-file `clang-format-21` check and `git diff --check`: PASS.
- Hardware root-cause attribution remains `HARDWARE_REQUIRED` unless supplied evidence proves an
  earlier internal boundary.

## Remaining delta

1. Obtain real-Unicore access or a synchronized capture of the existing RC2 freshness diagnostic,
   raw `/gps/fix`, receiver status, and independent motion.
2. Stop if payload changes advance while aggregate status stays frozen (post-tracker arbitration,
   with PR #7 leading for GGA), or if another boundary diverges.
3. Require an in-path receiver RX capture only if live valid/invalid/native-epoch diagnostics
   cannot distinguish wire from parsing/mapping.

## Do not redo

Do not re-audit publication caching, observation-sequence semantics, generic tri-state merge,
or parser object lifetime unless relevant code changes. Do not infer hardware failure from the
flat coordinate series. Do not reuse PR #7 implementation.

## Exact next action

On a host with the real Unicore exposed, capture the requested freshness/status/fix series during
independently proven motion. Do not add a second serial reader. If those metrics remain ambiguous,
capture the in-path RX bytes consumed by `ReceiverNode` and replay them.
