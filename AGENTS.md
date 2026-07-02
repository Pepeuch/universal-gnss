# Universal GNSS - Agent Rules

Universal GNSS is hardware-agnostic.

MowgliNext is a downstream validation platform only.

Never introduce Mowgli-specific assumptions into Universal GNSS core.

## General

- Never silently change public behaviour.
- Never reduce coordinate precision.
- Never change protocol behaviour without documentation.
- Keep receiver-specific logic isolated.
- Generic NMEA must remain vendor-independent.

## Documentation

Every implementation must update:

- `TODO.md` if status changed
- `ROADMAP.md` if milestone changed
- `README.md` if user-visible feature changed

## Planning

Keep work separated across these buckets:

- Universal GNSS core tasks
- ROS2 package tasks
- receiver-specific backend tasks
- downstream integration tasks

## Handoff

Every response must explicitly report:

- files modified
- why each file changed
- tests executed
- validation performed
- remaining known limitations
- commit status
- push status

Never omit changes performed during the task.

## Vendors documentations

All specific vendor protocols are in `docs/vendors` PATH

## Downstream MowgliNext tracking rule

When implementing or modifying any Universal GNSS feature that affects ROS2 surfaces, diagnostics, launch behavior, runtime status, correction observability, operator visibility, or robot-side integration, always evaluate whether MowgliNext should consume or react to it.

If the change creates new downstream integration work, update `MOWGLINEXT_TODO.md`.

This applies especially to:

* new ROS2 messages, topics, parameters, diagnostics, or status fields
* changes to `ReceiverNode`, `NtripNode`, `ReplayNode`, launch files, or ROS2 adapters
* new GNSS, RTK, NTRIP, RTCM, correction-stream, parser, or receiver-health observability
* new warning/error states that a robot operator should see
* new localization/safety conditions that could affect autonomous navigation
* new field-validation requirements for real robot testing

`MOWGLINEXT_TODO.md` must remain downstream guidance only.

Do not add MowgliNext-specific logic inside Universal GNSS.

When updating `MOWGLINEXT_TODO.md`, describe:

* the new Universal GNSS capability
* why it matters for MowgliNext
* where the robot stack should consume it
* expected GUI/operator behavior
* expected localization or safety behavior, if relevant
* suggested robot field-validation checks

Mark these items as pending MowgliNext integration work, not as completed Universal GNSS work.

## Universal GNSS terminology rule

Universal GNSS uses GNSS/geodesy-first terminology in core APIs, protocol records, diagnostics, documentation, and ROS2 surfaces.

ROS2 integration must preserve the canonical Universal GNSS vocabulary instead of translating GNSS concepts into robot-specific terms.

Use canonical GNSS/geodetic terms such as:

* `baseline_azimuth_deg`
* `baseline_length_m`
* `baseline_pitch_deg`
* `baseline_solution_status`
* `antenna_baseline`
* `course_over_ground_deg`
* `correction_stream`
* `rtk_mode`

`heading_deg` may remain as a generic portable runtime field only when the
source already publishes a heading/azimuth-like quantity and Universal GNSS
does not yet expose a more specific public baseline field.

Avoid robot/application-specific terms in Universal GNSS APIs unless the data truly comes from that domain.

Do not use these terms for dual-antenna GNSS baseline data in Universal GNSS core or ROS2 surfaces:

* `yaw`
* `robot_yaw`
* `vehicle_yaw`
* `robot_heading`
* `body_heading`

Documentation may mention common aliases such as “GNSS heading” or “vehicle heading” only as explanatory text, but the canonical field name should remain geodetic.

When a risky public term cannot be renamed immediately, document the
compatibility/deprecation plan in `docs/terminology.md`.

Downstream applications such as MowgliNext may convert `baseline_azimuth_deg` into robot yaw or vehicle heading after applying their own antenna mounting transform.
