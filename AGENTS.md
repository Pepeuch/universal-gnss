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
