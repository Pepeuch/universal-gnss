# Validation Boundaries

Current prerelease: **`v0.7.4-rc6`**, source
`d02e23f17d34e58f75f4c7a7f72f8d34ea766bfa`.

Current phase: validation before v0.7 stable. See the
[2026-11-08 rc6 passive mowing report](2026-11-08-rc6-passive-mowing.md)
for the exact Lyrical/arm64 image, measurements, limits and remaining gates.
The report includes four technical figures and a versioned, hash-verifying
extractor/plotter. The [shared review handoff](../../.agent/shared/RC6-20261108-REVIEW.md)
summarizes diagnostics and pending acceptance without duplicating the backlog.

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
session. For the rc6 u-blox MowgliNext baseline measured on 2026-11-08, the
command is below. Recheck the running image before reusing it. The installed
collector must match this revision: older copies imported GetReceiverSnapshot
from `universal_gnss_ros2.srv`; rc6 uses `universal_gnss_msgs.srv`. This campaign
used isolated official scripts under `versions/d02e23f/`, preserving older tools.

```bash
session="/home/pepeuch/universal-gnss-validation/results/robot-test-$(date +%Y%m%d-%H%M%S)"
/home/pepeuch/universal-gnss-validation/versions/d02e23f/scripts/validation/run_hardware_session.sh \
  --mode functional \
  --container mowgli-gps \
  --image ghcr.io/pepeuch/universal-gnss-ros2-lyrical@sha256:eafa78d1d28da22059be8d88f12e8c742eabb3008aede1f6f031c49b3da0ed7c \
  --git-revision d02e23f17d34e58f75f4c7a7f72f8d34ea766bfa \
  --receiver-by-id /dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00 \
  --parameters /home/pepeuch/mowglinext/docker/config/mowgli/mowgli_robot.yaml \
  --interval 20s --snapshot-interval 5m \
  --output "${session}"
```

Replace the u-blox path with
`/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0` only when UM982 is selected
on that machine. Never substitute transient `/dev/ttyACM*` or `/dev/ttyUSB*`.
If exactly one running container identifies itself as Universal GNSS through
its Compose-service or OCI image labels, `--container` may be omitted. This
supports both the standalone Compose service and the MowgliNext GPS sidecar;
use explicit `--container` selection when more than one candidate is running.
Confirm the revision-specific image tag and OCI revision label against the image
on the mower. The prepared MowgliNext Compose service has bounded Docker logs,
but no separate host-mounted ROS log or support-export directory. Therefore the
target command intentionally omits `--log-directory` and `--export-directory`;
the collector's `logs/` and `snapshots/` remain persistent under `--output`.

Functional collection runs until `Ctrl+C`/`SIGINT`. Evidence is flushed and a
final `summary.json` is generated. Do not start another endurance campaign merely
because a release gate remains unchecked. First reuse existing evidence and the
maintainer's recorded acceptance decision. Only an independently approved new
endurance campaign should use the explicit-duration template below:

```bash
/home/pepeuch/universal-gnss-validation/versions/d02e23f/scripts/validation/run_hardware_session.sh \
  --mode endurance --duration 24h \
  --container mowgli-gps \
  --image ghcr.io/pepeuch/universal-gnss-ros2-lyrical@sha256:eafa78d1d28da22059be8d88f12e8c742eabb3008aede1f6f031c49b3da0ed7c \
  --git-revision d02e23f17d34e58f75f4c7a7f72f8d34ea766bfa \
  --receiver-by-id /dev/serial/by-id/usb-u-blox_AG_-_www.u-blox.com_u-blox_GNSS_receiver-if00 \
  --parameters /home/pepeuch/mowglinext/docker/config/mowgli/mowgli_robot.yaml \
  --output "/home/pepeuch/universal-gnss-validation/results/endurance-$(date +%Y%m%d-%H%M%S)"
```

Defaults are one lightweight sample every five minutes and one redacted support
snapshot every 30 minutes. `--interval` and `--snapshot-interval` accept `s`,
`m`, `h`, and `d` suffixes. The functional example explicitly selects 20 seconds
and five minutes. Measure overhead on the target. These snapshots can miss short
RTK transitions; continuous passive subscriptions captured the approximately
nine-second event in the rc6 campaign. Each session contains:

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
/home/pepeuch/universal-gnss-validation/versions/d02e23f/scripts/validation/mark_hardware_event.sh --session "${session}" "starting mowing run"
/home/pepeuch/universal-gnss-validation/versions/d02e23f/scripts/validation/mark_hardware_event.sh --session "${session}" "manual stop"
/home/pepeuch/universal-gnss-validation/versions/d02e23f/scripts/validation/mark_hardware_event.sh --session "${session}" "receiver process kill"
/home/pepeuch/universal-gnss-validation/versions/d02e23f/scripts/validation/mark_hardware_event.sh --session "${session}" "rate mismatch low/high"
/home/pepeuch/universal-gnss-validation/versions/d02e23f/scripts/validation/mark_hardware_event.sh --session "${session}" "RTK reached Fixed"
```

Mark deliberate process/container/network interruptions, rate change and
restore, mowing-run boundaries, manual/safety stops, receiver/device
interventions, and naturally observed RTK transitions. A marker is correlation
evidence, not proof that the named state transition happened.

After stopping, regenerate and print the mechanical summary with:

```bash
python3 /home/pepeuch/universal-gnss-validation/versions/d02e23f/scripts/validation/analyze_hardware_session.py "${session}"
```

The analyzer reports observed IDs/PIDs/incarnations, sequence progression,
restart changes, RTK transitions, collection gaps, and resource peaks. It does
not award checklist credit. Process or incarnation changes do not prove
rejection of bytes from a retired physical incarnation.

Analysis caveats at rc6: the helper's fix-name map does not include numeric
6/7/8; the message schema defines them as 2D_FIX/3D_FIX/DGPS. Preserve raw fields
and decode against that schema. Its sequence max-minus-min summary must not be
used across different source incarnations. `GnssStatus.stamp` is the receipt of
the newest accepted runtime-field update, not necessarily a position epoch.
Use physical-position sequence, source identity, receiver epoch and position
freshness together. Older campaign documents and their image digests remain
historical evidence and must not be rewritten to rc6.

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
