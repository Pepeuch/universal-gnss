# Auto Configuration Design

This document is the `v0.6-1` design pass for Auto Configuration.

Scope of this milestone:

- audit the current config/profile infrastructure
- define the portable Auto Configuration architecture
- identify what already exists vs what is missing
- prepare the next implementation milestone

Implemented in `v0.6-2`:

- `ReceiverAutoConfig` planner/report layer in `gnss_driver`
- direct planner coverage for u-blox, Unicore, generic NMEA, base gating, and
  persistent warnings
- `gnss_config_plan` wiring to the portable planner/report layer

Still intentionally deferred after `v0.6-2`:

- live ROS2 integration for the planner/report surface
- automatic receiver writes
- read-back verification beyond the current response-routing layer

Out of scope for this milestone:

- adding new live receiver writes
- adding MowgliNext-specific integration logic
- bypassing the existing Universal GNSS config/profile layer

## Audit Summary

### What already exists

Universal GNSS already has most of the low-level building blocks needed for
Auto Configuration:

- generic config intent vocabulary in
  `gnss_driver/include/universal_gnss_driver/receiver_config_profile.hpp`
- receiver-family capability and profile generation hooks in
  `gnss_driver/include/universal_gnss_driver/receiver_driver.hpp`
- vendor-specific command builders in:
  - `gnss_driver/src/ublox_config_profile_builder.cpp`
  - `gnss_driver/src/unicore_config_profile_builder.cpp`
- a guarded write path in:
  - `gnss_driver/src/receiver_command_dispatcher.cpp`
  - `gnss_driver/src/receiver_command_transaction_engine.cpp`
  - `gnss_driver/src/receiver_config_application.cpp`
- dry-run and review tooling in:
  - `gnss_tools/src/profile_preview.cpp`
  - `gnss_tools/src/config_plan.cpp`
  - `gnss_tools/src/config_apply.cpp`
- conservative response routers for the existing live apply path in:
  - `gnss_driver/src/ublox_response_router.cpp`
  - `gnss_driver/src/unicore_response_router.cpp`
- hardware-backed evidence that runtime-only config applies are already useful
  for validation, especially on the ZED-F9P ROS2 path

This means Auto Configuration does not need a brand-new execution stack. The
missing work is mainly a portable planning, validation, and reporting layer
that can sit above the current builders and below CLI/ROS2/operator surfaces.

### What is missing

The current stack is still missing the pieces that make configuration feel
portable, explainable, and safe:

- no live ROS2 planner/report integration exists yet
- no read-back verification/report step exists after apply
- no discovery-to-config policy exists yet
- no transport/session arbitration exists for configuring a receiver that is
  already being monitored by a live session or ROS2 node

There are also several scope and naming gaps that should be resolved before
implementation:

- `ReceiverProfile` currently means hardware family identity, while
  `ReceiverConfigProfile` means configuration intent
- the portable `base` role exists, but current vendor support is incomplete
- current "persistent" behavior is not semantically identical across vendors

## Important Current Gaps

### 1. "Base" is not implementation-ready across vendors

The portable config taxonomy already includes `rover`, `base`, and
`diagnostics`.

Current real support is narrower:

- u-blox has a `BuildUbloxBaseProfile(...)` helper, but it is still closer to a
  minimal monitored base-facing output profile than to a full RTK base bringup
  workflow
- survey-in orchestration remains deferred
- RTCM base output orchestration remains deferred
- Unicore does not currently support portable base-profile generation at all

Design consequence:

- Auto Configuration `v1` should treat `base` as a declared portable role, but
  not as a universally safe live-apply target
- rover and diagnostics should be the first-class live Auto Configuration roles
- base should remain explicitly gated until each vendor driver can prove that it
  supports a complete and honest base workflow

### 2. "Persistent" does not mean the same thing everywhere

Current behavior differs by vendor:

- u-blox driver-level persistent profile generation currently resolves to
  `RAM + BBR`
- Unicore persistent mode currently appends `SAVECONFIG`

That means "persistent" is already useful as a safety level, but not yet as a
fully portable storage guarantee.

Design consequence:

- Auto Configuration reports must surface vendor-specific persistence semantics
- planner output should report the actual storage target or persistence
  mechanism, not only a generic boolean
- rollback expectations must be tied to the real vendor behavior

### 3. Capability declarations are not fully aligned

Current capability declarations are slightly inconsistent:

- `UbloxDriver` advertises base-mode support, but the built-in static
  `ublox_f9_f10` receiver profile remains conservative and does not
  advertise base mode
- the built-in Unicore placeholder profile advertises base and survey-in
  capability, but `UnicoreDriver` cannot yet build a base profile

Design consequence:

- Auto Configuration should gate live apply from driver-supported profile
  generation and validation, not from the looser built-in static profile table
- the static profile table should be cleaned up so planning docs, discovery, and
  driver behavior do not drift apart

### 4. The existing guarded apply path should be reused, not replaced

`gnss_config_apply` already proves out:

- dry-run first
- explicit runtime confirmation
- explicit persistent confirmation
- one-command-at-a-time dispatch
- timeout/retry handling
- vendor response routing

Design consequence:

- Auto Configuration should reuse the existing `ReceiverConfigApplication`
  execution path
- the new work should be to feed it a better portable plan/report object, not
  to create a second live-write mechanism

## Proposed Architecture

### Layer split

The intended Auto Configuration stack should be:

```text
discovery / manual target selection
  -> receiver driver selection
  -> portable auto-config planner
  -> validation report + command plan + rollback expectation
  -> existing guarded apply engine
  -> execution report
```

Responsibilities:

- discovery decides what receiver we are talking to
- the driver decides which high-level config roles are actually supported
- vendor builders generate commands
- the planner explains what would happen and whether it is safe/complete
- the existing apply path performs the live write later when explicitly enabled

### Portable request/plan/report objects

Implemented in `v0.6-2`:

- `ReceiverAutoConfigRequest`
- `ReceiverAutoConfigPlan`
- `ReceiverAutoConfigValidationSummary`
- `ReceiverAutoConfigRollbackExpectation`

Current implementation lives in:

- `gnss_driver/include/universal_gnss_driver/receiver_auto_config.hpp`
- `gnss_driver/src/receiver_auto_config.cpp`

Auto Configuration should add a driver-level portable API, likely centered on a
small `ReceiverAutoConfig*` family.

Suggested shape:

```cpp
enum class ReceiverAutoConfigApplyMode : std::uint8_t
{
  kPlanOnly,
  kRuntimeOnly,
  kPersistent,
};

struct ReceiverAutoConfigRequest
{
  ReceiverVendor vendor;
  std::string family;
  ReceiverConfigProfileKind profile_kind;
  ReceiverAutoConfigApplyMode apply_mode;
  std::optional<std::uint32_t> baud;
  std::optional<double> rate_hz;
};

struct ReceiverAutoConfigValidationFinding
{
  severity;
  code;
  message;
};

struct ReceiverAutoConfigRollbackExpectation
{
  summary;
  operator_action;
  vendor_storage_note;
  guaranteed;
};

struct ReceiverAutoConfigPlan
{
  ReceiverAutoConfigRequest request;
  std::vector<ReceiverCommand> commands;
  validation_report;
  rollback_expectation;
  ready_to_execute;
};
```

This should live in `gnss_driver`, not in `gnss_tools` or `gnss_ros2`, so
Universal GNSS stays the single source of truth.

### Profile abstraction

The existing `ReceiverConfigProfileKind` should remain the portable role
vocabulary.

For `v1`, the operational profile meanings should be:

- `rover`
  - first-class portable role
  - eligible for dry-run and later live runtime/persistent apply
- `diagnostics`
  - additive monitoring/output role
  - first-class portable role
  - ideal first target for ROS2-assisted field validation
- `base`
  - keep as a portable role in the API
  - do not treat it as generally live-ready until vendor support is complete

That gives us a clean rule:

- rover and diagnostics are `v1` implementation targets
- base remains architected now, but gated by validation

### Apply modes

Auto Configuration should standardize three modes:

- `plan_only`
  - build commands
  - validate support
  - emit report only
- `runtime_only`
  - prepare temporary/session-scoped changes
  - prefer this as the default first live mode
- `persistent`
  - request receiver-side saved state
  - require stronger reporting and stronger operator confirmation

This keeps dry-run and apply behavior consistent across CLI and ROS2.

### Validation report

The validation report should do more than count commands.

Minimum `v1` report content:

- selected vendor/family/profile
- requested apply mode
- whether the selected driver supports the profile
- whether requested overrides are supported
- runtime/persistent/factory-reset command counts
- whether the plan is safe to execute
- whether the plan is complete or only partial
- warnings about vendor-specific persistence semantics
- warnings about deferred base/survey orchestration
- rollback expectation summary

That report should be shareable by:

- `gnss_config_plan`
- `gnss_config_apply`
- future ROS2 diagnostics/services
- future GUI/Foxglove surfaces

### Rollback expectations

`v1` should define rollback expectations explicitly, but should not pretend that
automatic rollback exists when it does not.

Policy:

- runtime-only plans should report the expected temporary-clear path
  - for example reboot/reconnect/runtime reset, depending on vendor semantics
- persistent plans should report that rollback is manual unless an explicit
  restore profile exists
- apply/report tooling should surface expectation text before execution

This keeps the system honest and operator-readable.

## What Already Exists vs What Should Be Reused

### Reuse directly

- `ReceiverConfigProfileKind`
- `ReceiverDriver::BuildRoverProfile(...)`
- `ReceiverDriver::BuildDiagnosticsProfile(...)`
- `ReceiverDriver::BuildBaseProfile(...)`
- `UbloxConfigProfileBuilder`
- `UnicoreConfigProfileBuilder`
- `ReceiverCommandDispatcher`
- `ReceiverCommandTransactionEngine`
- `ReceiverConfigApplication`
- `gnss_profile_preview`
- `gnss_config_plan`
- `gnss_config_apply`

### Add on top

- portable planner/report types
- driver-level validation rules
- driver/profile capability-consistency cleanup
- vendor-specific persistence semantics in reports
- discovery-to-config policy
- ROS2 plan/apply integration layer

## Auto Configuration v1 Implementation Plan

### 1. Driver-level planner layer

Implemented:

- accepts a portable request
- selects the correct vendor/profile build path
- validates support and overrides
- emits commands plus validation and rollback-report metadata

Current next step:

- keep this as the source of truth while extending it to ROS2 and future GUI
  surfaces

### 2. u-blox support

For `v1`, support:

- rover
- diagnostics
- runtime-only
- persistent, with explicit reporting that the current persistent target is
  `RAM + BBR`

Do not oversell the current base helper as a complete RTK base workflow.

### 3. Unicore support

For `v1`, support:

- rover
- diagnostics
- runtime-only
- persistent through `SAVECONFIG`

Keep base explicitly unsupported until the config/profile layer can model it
honestly.

### 4. CLI integration

Refactor the CLIs so they consume the new driver-level planner:

- `gnss_profile_preview` remains the low-level raw builder preview tool
- `gnss_config_plan` now consumes the portable planner/report layer
- `gnss_config_apply` consumes the same shared plan/report object before later
  execution

### 5. ROS2 integration

ROS2 should not invent its own vendor config logic.

Initial integration target:

- plan-only surface first
- execution second
- reuse the same planner/report objects as CLI
- expose report status in diagnostics before enabling any live auto-write path

That keeps Universal GNSS as the only config source of truth.

### 6. Tests

Add focused tests for:

- planner validation and reporting
- unsupported profile gating
- incomplete-base warnings
- persistence-semantics reporting
- shared CLI formatting
- ROS2 diagnostics/report projection

## Proposed File Set For The Next Milestone

Likely new files:

- `gnss_driver/include/universal_gnss_driver/receiver_auto_config.hpp`
- `gnss_driver/src/receiver_auto_config.cpp`
- `gnss_driver/tests/test_receiver_auto_config.cpp`

Likely modified files:

- `gnss_driver/include/universal_gnss_driver/receiver_driver.hpp`
- `gnss_driver/src/ublox_driver.cpp`
- `gnss_driver/src/unicore_driver.cpp`
- `gnss_driver/include/universal_gnss_driver/receiver_profiles.hpp`
- `gnss_driver/src/receiver_profiles.cpp`
- `gnss_tools/include/universal_gnss_tools/config_plan.hpp`
- `gnss_tools/src/config_plan.cpp`
- `gnss_tools/include/universal_gnss_tools/config_apply.hpp`
- `gnss_tools/src/config_apply.cpp`
- `gnss_tools/tests/test_config_plan.cpp`
- `gnss_tools/tests/test_config_apply.cpp`
- `gnss_ros2/src/receiver_node.cpp`
- `gnss_ros2/tests/test_receiver_node.cpp`

Additional documentation likely needed next:

- `docs/tools.md`
- `docs/ros2.md`
- `docs/driver.md`

## Risk Points

- base role ambiguity: portable role exists before vendor implementations are
  complete
- persistence ambiguity: a single boolean is too weak for cross-vendor safety
- capability drift: built-in static profiles and live drivers can disagree
- session arbitration: configuring a receiver from the same process that is
  consuming telemetry will need an explicit ownership model
- no read-back verify yet: a positive ACK or `<OK` is not the same as proven
  final receiver state
- no automatic rollback: operator-facing reports must stay explicit about that

## Recommended Scope Boundary For The Next Implementation Milestone

Auto Configuration `v1` should be intentionally modest:

- portable planner/report layer
- u-blox rover + diagnostics
- Unicore rover + diagnostics
- shared CLI integration
- ROS2 plan/report integration

And should intentionally defer:

- full base bringup
- survey-in orchestration
- automatic rollback
- read-back state diff/verification beyond the current response-routing layer
