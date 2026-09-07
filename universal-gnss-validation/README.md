# Validation Boundaries

`v0.6.0` is released.

Current phase: post-`v0.6.x` stabilization.

This directory-level note exists to keep field validation, ROS2 package work,
receiver-specific backend work, and Universal GNSS core work from being mixed
together in the backlog.

## Passive mower hardware sessions

`scripts/validation/run_hardware_session.sh` records a passive, append-only
timeline around an already-running production Universal GNSS container. It does
not start or restart the container, command the mower, change navigation or
safety state, access receiver configuration services, or write persistent
receiver settings. Runtime observation failures are recorded and do not affect
Universal GNSS or MowgliNext.

Run it on the mower host immediately before the operator begins a functional
session. For the prepared u-blox MowgliNext target, the exact command is:

```bash
session="/home/pepeuch/universal-gnss-validation/results/robot-test-$(date +%Y%m%d-%H%M%S)"
/home/pepeuch/universal-gnss-validation/scripts/validation/run_hardware_session.sh \
  --mode functional \
  --container mowgli-gps \
  --image mowgli-gps:ug-dev-0bcfdff-healthcheck \
  --git-revision 0bcfdff9135992978769a6573ec62ae8e3d9a30f \
  --receiver-by-id /dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00 \
  --parameters /home/pepeuch/mowglinext/docker/config/mowgli/mowgli_robot.yaml \
  --output "${session}"
```

Replace the u-blox path with
`/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0` only when UM982 is selected
on that machine. Never substitute transient `/dev/ttyACM*` or `/dev/ttyUSB*`.
If exactly one running Compose service has the
`com.docker.compose.service=universal-gnss` label, `--container` may be omitted.
Confirm the revision-specific image tag and OCI revision label against the image
on the mower. The prepared MowgliNext Compose service has bounded Docker logs,
but no separate host-mounted ROS log or support-export directory. Therefore the
target command intentionally omits `--log-directory` and `--export-directory`;
the collector's `logs/` and `snapshots/` remain persistent under `--output`.

Functional collection runs until `Ctrl+C`/`SIGINT`. Evidence is flushed and a
final `summary.json` is generated. Later endurance collection requires an
explicit duration:

```bash
/home/pepeuch/universal-gnss-validation/scripts/validation/run_hardware_session.sh \
  --mode endurance --duration 24h \
  --container mowgli-gps \
  --image mowgli-gps:ug-dev-0bcfdff-healthcheck \
  --git-revision 0bcfdff9135992978769a6573ec62ae8e3d9a30f \
  --receiver-by-id /dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00 \
  --parameters /home/pepeuch/mowglinext/docker/config/mowgli/mowgli_robot.yaml \
  --output "/home/pepeuch/universal-gnss-validation/results/endurance-$(date +%Y%m%d-%H%M%S)"
```

Defaults are one lightweight sample every five minutes and one redacted support
snapshot every 30 minutes. `--interval` and `--snapshot-interval` accept `s`,
`m`, `h`, and `d` suffixes. Each session contains:

```text
metadata.json       immutable host/Git/image/container/device/config baseline
samples.jsonl       Docker, process, ROS diagnostic, resource and disk samples
events.jsonl        collector events, support snapshots and operator markers
summary.json        mechanical post-session summary; never release credit
logs/               reserved session-local log evidence
snapshots/          bounded, redacted support snapshots
```

The ROS probe calls only `get_snapshot` and the Receiver/NTRIP responsiveness
services. Docker health remains process responsiveness only; GNSS transport,
freshness, fix, RTK, NTRIP, RTCM, and correction evidence come from the
canonical snapshot and diagnostics. Parameter values, Docker environment, and
credential-like diagnostic values are omitted or redacted.

While collection runs, add operator actions to the same timestamped timeline
from another shell:

```bash
/home/pepeuch/universal-gnss-validation/scripts/validation/mark_hardware_event.sh --session "${session}" "starting mowing run"
/home/pepeuch/universal-gnss-validation/scripts/validation/mark_hardware_event.sh --session "${session}" "manual stop"
/home/pepeuch/universal-gnss-validation/scripts/validation/mark_hardware_event.sh --session "${session}" "receiver process kill"
/home/pepeuch/universal-gnss-validation/scripts/validation/mark_hardware_event.sh --session "${session}" "rate mismatch low/high"
/home/pepeuch/universal-gnss-validation/scripts/validation/mark_hardware_event.sh --session "${session}" "RTK reached Fixed"
```

Mark deliberate process/container/network interruptions, rate change and
restore, mowing-run boundaries, manual/safety stops, receiver/device
interventions, and naturally observed RTK transitions. A marker is correlation
evidence, not proof that the named state transition happened.

After stopping, regenerate and print the mechanical summary with:

```bash
python3 /home/pepeuch/universal-gnss-validation/scripts/validation/analyze_hardware_session.py "${session}"
```

The analyzer reports observed IDs/PIDs/incarnations, sequence progression,
restart changes, RTK transitions, collection gaps, and resource peaks. It does
not award checklist credit. Process or incarnation changes do not prove
rejection of bytes from a retired physical incarnation.

## Universal GNSS core validation

Track validation here when the issue is about:

- portable parser/runtime correctness
- runtime aggregation, diagnostics, and capability/value flags
- receiver discovery, planning, guarded apply, and CLI behavior
- transport, NTRIP, replay, export, and report tooling

Current pending example:

- generic NMEA validation: preserve the documented `runtime_only` boundary while
  keeping standard `GGA fix_quality` -> normalized `rtk_mode` coverage green

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
