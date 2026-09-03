# BlueOS Extension compatibility proposal

This directory is a design/skeleton proposal for a future BlueOS extension. It does
not add a BlueOS runtime, change Universal GNSS semantics, or introduce a ROS 2
dependency.

## Conclusion

Universal GNSS is **compatible with BlueOS through a thin adapter**. The portable
C++ libraries already provide the receiver parsing, normalised runtime state,
serial/TCP transport, NTRIP client, RTCM handling, and configuration planning.
BlueOS should supply only deployment concerns: configuration persistence, container
lifecycle, device selection, reconnect supervision, and a small status/config API.

The implementation boundary should be:

```text
BlueOS Extension container
  -> BlueOS-specific launcher + settings/config adapter + HTTP status API
  -> portable Universal GNSS runtime supervisor (implemented non-ROS target)
  -> receiver session / serial transport / NTRIP client / RTCM forwarding
  -> GnssRuntimeState + existing diagnostic and correction-health models
```

The future supervisor must compose existing portable classes. It must not reimplement
protocol parsing, receiver-family detection, runtime aggregation, auto-configuration,
NTRIP request/GGA policy, RTCM framing, or diagnostic semantics.

## Compatibility matrix

| Subsystem | Status | BlueOS assessment |
| --- | --- | --- |
| `gnss_core` | Compatible | Portable normalized state, aggregation, health, and semantic contracts can be used unchanged. |
| `gnss_protocols` | Compatible | Portable framing, validation, and semantic mapping; no BlueOS dependency. |
| `gnss_driver` | Requires adapter | Receiver sessions and discovery are reusable. A BlueOS supervisor must own open/close, reconnect backoff, device disappearance, and process lifecycle. |
| `gnss_transport` | Requires adapter | Linux POSIX serial/TCP/UDP are appropriate in the container, subject to granted device nodes and network. Transport does not solve hotplug policy. |
| `gnss_ntrip` | Requires adapter | `NtripClient`, GGA injection, correction health, and reconnect state are reusable, but an application loop must coordinate it with receiver writes. |
| `gnss_tools` | Compatible as operator tooling only | Keep the existing CLIs for diagnostics, probes, configuration plans/applies, and troubleshooting. Do not use the monitor CLIs as the container daemon. |
| `gnss_ros2` | Incompatible with the MVP boundary | Leave it out of the image. It remains a separate projection of the same portable semantics for ROS 2 deployments. |

## What the container should run

The first production-capable process should be a new, small
`universal_gnss_blueos_runtime` executable (name provisional), linked only to the
portable libraries. It should:

1. Load one persisted Universal GNSS-shaped configuration document.
2. Open one explicitly selected serial receiver.
3. Run the selected receiver session and retain the authoritative
   `GnssRuntimeState`/diagnostic snapshots.
4. Reopen after terminal serial failures with bounded backoff, treating each reopen
   as a new source incarnation.
5. Optionally own one NTRIP client and forward received RTCM bytes to that same
   receiver transport; it must not claim a new observation when publishing cached
   state.
6. Expose read-only status/diagnostics first, plus configuration validation and a
   deliberate restart/apply path later.

It should not start `gnss_serial_monitor`, `gnss_ntrip_monitor`, or
`gnss_config_apply` as background daemons. Those are operator CLI harnesses; their
current synchronous and terminal semantics are intentionally different from a
long-running supervisor.

## BlueOS packaging and metadata

BlueOS extension labels are Docker image metadata. A future Dockerfile must include:

- `version`: an image-tag-matching SemVer release;
- `permissions`: valid Docker-create JSON with only the requested binds/devices/ports;
- `authors`, `company` (documented as becoming `maintainer`), `readme`, and `links`;
- `type="device-integration"`;
- up to ten lowercase, hyphenated tags, proposed: `gnss`, `rtk`, `positioning`,
  `navigation`, `ntrip`.

`metadata/bazaar-metadata.json.template` is the separate registration record expected
by BlueOS-Extensions-Repository. Publication also needs an extension logo and a
company logo (once per owner). The repository metadata is not copied into the image.

Use a SemVer image tag for every public release. The Extensions Manager supports
startup, restart, logs, update/rollback, and custom permission settings, so runtime
configuration must tolerate process restarts and image rollbacks.

## Permissions and devices

`metadata/permissions.receiver-mvp.json.template` is the proposed minimal request.
It contains three independently justified parts:

| Need | Permission | Notes |
| --- | --- | --- |
| Selected GNSS receiver | One `HostConfig.Devices` entry | Replace `REPLACE_WITH_HOST_TTY` with the resolved `/dev/ttyACM*` or `/dev/ttyUSB*` node and map it to `/dev/gnss-receiver`. Do not request `Privileged` or bind all of `/dev`. |
| Persisted settings and optional logs | One bind below `/usr/blueos/userdata/universal-gnss` | The container reads/writes `/data`; no credentials belong in the image. |
| Status/config HTTP service | Port 80 exposed and dynamically host-bound | This is optional for a headless prototype, but recommended for the first useful status API. |

Outbound TCP/DNS for an NTRIP caster needs no host port binding under normal Docker
bridge networking. `host.docker.internal` is only required if the extension calls a
BlueOS API; the receiver/NTRIP MVP should not need it.

### Serial-path policy

Users should select a stable `/dev/serial/by-id/...` identity in the BlueOS UI/config
when the host exposes it. At container creation, resolve that stable symlink to its
actual character device and grant only that node through `HostConfig.Devices`; the
runtime can receive it as `/dev/gnss-receiver`. Preserve both the requested stable
identity and resolved node in diagnostics.

This is safer than scanning/binding all `/dev`, and avoids treating a changing
`ttyUSB*`/`ttyACM*` name as a receiver identity. It also exposes a BlueOS limitation
to validate: Docker device grants are established when the container is created. If a
USB device hotplugs or re-enumerates, BlueOS may need to recreate/restart the
extension with an updated grant. The runtime must not silently attach a different
receiver after that event.

BlueOS documentation shows a broad privileged `/dev` bind as an example, but this
proposal deliberately does **not** request it. A real BlueOS device must validate that
the Extensions Manager accepts `HostConfig.Devices` for a selected serial node and
that the mount/restart behavior preserves the required device access.

## Configuration mapping

One versioned, persisted adapter document should map directly into existing portable
options. It is not a second configuration model.

| BlueOS setting | Portable target / contract |
| --- | --- |
| `receiver.family` (`auto`, `ublox`, `unicore`, `nmea`) | `ReceiverSessionKind` / receiver discovery selection |
| `receiver.device_id` | User-selected stable `/dev/serial/by-id/...` identity, retained for audit; resolved container device becomes `receiver.device_path` |
| `receiver.baud`, probe baud list, read timeout, chunk size | `PosixSerialConfig` and `ReceiverProbeConfig` |
| generic-NMEA auto fallback / platform-UART opt-in | Existing explicit discovery flags; default remains conservative |
| `receiver.auto_config` profile, output port, apply mode | Existing auto-config plan/apply request. MVP exposes dry-run/status only; live writes require the current explicit confirmation policy. |
| `ntrip.enabled`, caster host/port/mountpoint/TLS/credentials, GGA settings | Existing `NtripConfig`, TCP config, and `GgaInjectionPolicy` |
| `rtcm.forwarding.enabled` | Supervisor-owned byte flow from `NtripClient` to the selected receiver `ByteDuplex`; reuse the existing RTCM monitor/health model |
| logs/status retention | BlueOS bind at `/data`; do not persist transient source/session state across a new receiver incarnation |

Credentials should be supplied through BlueOS-managed persistent settings or a
user-provided file under `/data`, never Docker labels, image layers, URLs, or
diagnostic output.

## Minimum useful MVP

1. One explicitly selected USB/serial receiver, with an explicit family or
   conservative existing auto-detection.
2. Portable receiver runtime supervisor with bounded reconnect and source-incarnation
   reset semantics.
3. Read-only HTTP endpoints: health, receiver identity/path, normalized runtime
   snapshot, parser/session metrics, and a redacted configuration summary.
4. Persisted configuration using the mapping above, plus an operator-visible restart
   requirement after device-permission changes.

Explicitly **not** in the MVP:

- ROS 2, ROS messages, launch files, or a ROS bridge;
- NTRIP/RTCM forwarding (Phase 2), local caster, or base-station mode;
- automatic scanning/claiming of every serial port;
- live persistent/factory-reset receiver configuration from a web button;
- a custom rich BlueOS UI, device-selector replacement, fleet/cloud functions, or
  multi-receiver arbitration;
- privileged mode, `/dev:/dev`, host networking, or access to the Docker socket.

## Dockerfile decision

No `Dockerfile` is included yet. The portable non-ROS
`universal_gnss_supervisor` target now owns one selected serial receiver's session
lifecycle, reconnect backoff, incarnation boundary, and status snapshot. It does not
yet provide BlueOS configuration persistence, HTTP status/config endpoints, hardware
validation, or RTCM coordination, so a production container remains premature. Once
those BlueOS-specific pieces are designed, use a multi-stage Debian/Ubuntu Linux build
that compiles portable CMake targets only, copies that executable plus OpenSSL runtime
libraries into a minimal runtime image, and declares the labels described above. Do not
include `gnss_ros2` or source ROS packages.

Build and publish a multi-architecture manifest at least for `linux/arm/v7` and
`linux/arm64`/`arm/v8`; add `linux/amd64` for developer/testing convenience rather
than claiming it as the primary vehicle target. The portable Linux transport relies on
POSIX serial/TCP and OpenSSL, so all image variants must compile and test those paths.

## Risks and required physical evidence

| Risk | Consequence / mitigation |
| --- | --- |
| USB hotplug/re-enumeration | Device grant may refer to an old node. Require a verified BlueOS restart/recreate path; do not assume close/reopen proves a clean physical boundary. |
| Stable identity availability | Some serial bridges lack a useful `/dev/serial/by-id` link. Require explicit operator selection and clearly mark unstable fallback paths. |
| Container device permissions | `HostConfig.Devices` support and stable-link behavior need validation on the target BlueOS release. Do not fall back to privileged mode without that evidence. |
| ARM builds | Cross-build success is not receiver evidence. Build/test `arm/v7` and `arm64`; run physical serial/NTRIP tests on supported BlueOS hardware. |
| Lifecycle/restart | Configuration and device permission changes must be atomic across restarts; cached runtime state must not cross a new receiver incarnation. |
| Persistent secrets/logs | Keep only configuration/logs under `/data`; redact secrets and bound log retention. |
| Networking | Outbound caster DNS/TCP/TLS needs real network testing. No inbound NTRIP port is needed for the rover MVP. |

## Staged plan

### Phase 0 — skeleton

Keep this proposal, confirm BlueOS metadata/permission behavior on a development
device, select supported architectures, and add container build CI only after a
portable supervisor target exists.

### Phase 1 — receiver + diagnostics

Partial implementation: the generic non-ROS `universal_gnss_supervisor` accepts one
explicit serial device, baud rate, and receiver family; owns receiver/session lifecycle;
uses bounded reconnect backoff; and exposes a runtime/metrics lifecycle snapshot to its
native CLI. It intentionally has no persisted configuration or BlueOS HTTP API. Real
USB/UART power-cycle and hotplug validation remains required before a BlueOS adapter is
claimed ready.

### Phase 2 — NTRIP/RTCM

Compose the existing `NtripClient` with the supervisor, forward RTCM to the selected
receiver, apply the existing GGA/correction-health policies, and validate caster loss,
reconnect, and receiver-write recovery.

### Phase 3 — configuration/UI

Add a small relative-path-safe BlueOS web UI and `register_service` endpoint. Reuse the
same persisted configuration schema and existing dry-run/apply safeguards; add live
configuration only with an explicit confirmation/restart design.

### Phase 4 — publication

Build and test signed-off multi-architecture Docker tags, publish SemVer images,
submit the registry metadata and logos to BlueOS-Extensions-Repository, then validate
install/update/rollback from a real BlueOS Extensions Manager.

## External references

- [BlueOS extension development documentation](https://blueos.cloud/docs/stable/development/extensions/)
- [BlueOS Extensions Repository registration and Docker-label contract](https://github.com/bluerobotics/BlueOS-Extensions-Repository)
- [Water Linked DVL BlueOS extension Dockerfile](https://github.com/bluerobotics/BlueOS-Water-Linked-DVL/blob/master/Dockerfile)
- [BlueOS extension template](https://github.com/BlueOS-community/blueos-extension-template)
