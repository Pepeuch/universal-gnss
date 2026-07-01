# Validation Boundaries

`v0.6.0` is released.

Current phase: post-`v0.6.x` stabilization.

This directory-level note exists to keep field validation, ROS2 package work,
receiver-specific backend work, and Universal GNSS core work from being mixed
together in the backlog.

## Universal GNSS core validation

Track validation here when the issue is about:

- portable parser/runtime correctness
- runtime aggregation, diagnostics, and capability/value flags
- receiver discovery, planning, guarded apply, and CLI behavior
- transport, NTRIP, replay, export, and report tooling

Current pending example:

- generic NMEA improvement: propagate `GGA` fix_quality `4/5` into normalized
  `rtk_mode` / `fix_type`

## ROS2 package validation

Track validation here when the issue is about:

- `ReceiverNode`, `NtripNode`, or `ReplayNode`
- ROS2 diagnostics/topic behavior
- ROS2 distro or architecture compatibility
- ROS2 CI, packaging, launch, and integrated operational behavior

## Receiver-specific backend validation

Track validation here when the issue is about:

- u-blox-specific config/reset/base behavior
- Unicore-specific semantic/config growth
- Quectel or Septentrio dedicated backend work

Do not treat future Quectel support as generic-NMEA backlog unless the message
is standard NMEA.

## Downstream integration validation

Track validation here when the issue is about:

- MowgliNext GUI, onboarding, install, or deployment behavior
- `navsat_to_absolute_pose`, `localization_monitor`, Nav2, or mower bringup
- field missions and localization stability checks

## MowgliNext note

- UM982 / Unicore runtime behavior has been validated through downstream
  MowgliNext field use
- downstream GUI/install issues stay downstream unless they expose a missing
  portable feature or a bug in this repository
