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
