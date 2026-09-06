# ROS 2 production container baseline

This is the first production-oriented Universal GNSS container baseline. It
starts the existing `receiver_and_ntrip.launch.py` launch description in one
container. ROS launch remains responsible for the existing `receiver_node` and
`ntrip_node` processes and their shutdown; this image introduces no second
GNSS, NTRIP, or configuration implementation.

Use the concise [v0.7 release checklist](v0.7_release_checklist.md) alongside
this operational guide when reviewing a release candidate.

## Scope and support boundary

- Target build distributions: ROS 2 Kilted (default) and Lyrical, selected by
  the `ROS_DISTRO` build argument from one Dockerfile. Kilted and Lyrical amd64
  builds and container lifecycle validation are established.
- Initial image architectures are `linux/amd64` and `linux/arm64`; `arm/v7` is
  not a target of this baseline. BuildKit/QEMU packaging and smoke are green
  for both arm64 images. Native Kilted build/runtime and receiver ingestion are
  proven on a physical arm64 Raspberry Pi; Lyrical-native remains separate.
- The image is non-root by default and does not use privileged mode or bind all
  of `/dev`.
- The Docker healthcheck calls bounded responsiveness services on both enabled
  launch-managed components. It does **not** assert receiver connectivity, observation freshness,
  NTRIP connection/RTCM flow, correction validity, or RTK state.

The established scope includes same-host Docker bridge DDS, physical-LAN DDS
under the explicit unicast contract below, amd64 receiver/caster hardware,
native Kilted arm64 runtime, and real-robot runtime. Lyrical-native, serial
renumbering, lifecycle matrices, and downstream platform validation remain
separate evidence requirements.

## Build

Build inputs are limited to the checked-out repository, the selected official
ROS base image, and distribution packages declared in the Dockerfile. The
final stage contains the merged installed ROS package and runtime dependencies,
not the source tree or build toolchain.

```bash
docker build \
  --build-arg ROS_DISTRO=kilted \
  --build-arg VERSION="$(git describe --always --dirty)" \
  --build-arg REVISION="$(git rev-parse HEAD)" \
  --build-arg CREATED="$(git show -s --format=%cI HEAD)" \
  --tag universal-gnss:ros2-kilted \
  .
```

Use `--build-arg ROS_DISTRO=lyrical` for the Lyrical image. Multi-architecture
build/publish automation is a later milestone; build each validated target
platform explicitly for now.

This release contract makes project-owned inputs traceable; it does not claim
bit-for-bit reproducibility across time. `ros:${ROS_DISTRO}-ros-base` is a
distribution tag rather than a pinned digest, and `apt-get` resolves the then
available Ubuntu/ROS package indexes. The checked-out Git revision, Dockerfile,
build arguments, and `.dockerignore` are deterministic project inputs; ROS
base-image digests and resolved package versions must be recorded with the
release SBOM/provenance artifact. Do not substitute a wall-clock `CREATED`
value for the source-commit timestamp.

### Release identity, upgrade, and rollback

For a release candidate, use an immutable tag containing both the ROS
distribution and Universal GNSS release, such as `universal-gnss:ros2-kilted-v0.7.0`.
Pass the matching release/version as `VERSION` and the exact source revision as
`REVISION`; pass the source commit's RFC 3339 timestamp as `CREATED`. The image
exposes standard OCI `title`, `description`, `version`, `revision`, `source`,
and `created` labels, and exposes version/revision as runtime environment for
the ROS diagnostic identity status. `CREATED` deliberately has no wall-clock
default, and `REVISION` has no synthetic fallback: ad-hoc builds that omit
either carry an empty OCI value rather than fabricated metadata. The runtime
diagnostic renders an omitted version/revision value as `unknown`. The Docker
CI matrix
builds Kilted and Lyrical amd64 images from the same Dockerfile and verifies the
full OCI label set, non-root image contract, ROS nodes, representative operator
tools, and absence of source/build/log directories in the final stage. It does
not publish an image or validate hardware.

The shared `/diagnostics` stream and the receiver's
`~/get_snapshot` response contain `universal_gnss/runtime_identity` with the
image version/revision, ROS distribution, and configured receiver family. It
is deployment identity only, not a health verdict; discovery diagnostics retain
the separate receiver model/firmware fields when they are actually known.

`arm/v7` is not supported for v0.7. BuildKit/QEMU evidence remains distinct
from the proven native Kilted arm64 result; Lyrical-native is still pending.

For each released image, retain its immutable digest, exact Dockerfile revision,
OCI version/revision labels, and attached SBOM/provenance attestations. The
tag-gated `docker-release.yml` workflow builds Kilted and Lyrical indexes for
`linux/amd64` and `linux/arm64`, requests SBOM plus maximal BuildKit provenance,
and verifies both published platforms. A manual dry run builds without pushing;
publication is allowed only from a `v*` tag. The workflow definition and a dry
build are strategy/software evidence only: the release gate closes only after
the registry indexes and attestations are actually emitted and inspected.

Before an upgrade, retain the prior immutable image tag and back up the
operator-owned parameter file and log directory. Stop the old container cleanly,
start the new immutable tag with the same reviewed external mounts and selected
stable device identity, then verify its labels and normal ROS diagnostics. To
roll back, stop the new container and recreate the prior immutable tag with the
same external configuration; do not copy mutable state out of an image or
silently change receivers. Receiver persistent/runtime-profile behavior remains
receiver-specific, including the UM982 power-loss reprovisioning rule below.

## Configuration, credentials, and logs

The default entrypoint requires a read-only ROS parameter file at
`/etc/universal_gnss/parameters.yaml`. Begin with
[`docker/parameters.example.yaml`](../docker/parameters.example.yaml), make an
operator-owned copy outside the repository, and put NTRIP credentials only in
that protected copy. The example file intentionally contains empty credentials.

The parameter file controls the named `/universal_gnss_receiver` and
`/universal_gnss_ntrip` nodes. It is applied after launch defaults, so values
in the mounted file are the deployment configuration. Set
`UNIVERSAL_GNSS_PARAMETERS_FILE` only when mounting it at a different path.

The Docker deployment wrapper has `UNIVERSAL_GNSS_CONFIGURATION_SCHEMA_VERSION`
with current value `1`. It defaults to `1` when absent, so existing ROS2
parameter files continue unchanged. Set it explicitly in a Compose env file for
new deployments. An unknown value exits the entrypoint with code 2 before ROS
launch; it does not attempt an implicit migration. A future schema change must
add a deliberate migration/rejection rule while preserving this v1 default.

ROS logs default to `/var/log/universal_gnss`, and support exports default to
`/var/lib/universal_gnss/export`; mount both to external writable storage. The
container user defaults to UID/GID `1000`; use the Dockerfile `APP_UID`/`APP_GID`
build arguments or provision both mounted directories for that identity.
`ROS_LOG_DIR` and `UNIVERSAL_GNSS_EXPORT_DIR` may be overridden with other
writable paths. Entrypoint startup fails before ROS launch if either surface is
not writable.

ROS console lines use a stable, non-colored collection envelope:
`timestamp={time} severity={severity} logger={name} message={message}`. This is
a field-oriented Docker/Compose/BlueOS collection contract, not JSON; message
text remains human-readable and the structured `/diagnostics` topic remains the
authoritative health surface. NTRIP logs both failed and accepted `streaming`
state transitions without printing credentials.

Credentials belong only in the protected external parameter file. Do not pass
NTRIP user names/passwords in image tags, Docker build arguments, a committed
Compose env file, or ordinary shell environment variables: those values are
often visible in history, inspection output, or diagnostics. Runtime status and
supervisor error coverage redacts credentials, but operators must still keep
parameter files and support captures access-controlled.

Use bounded host and Docker log retention. The provided Compose example uses
Docker's `local` logging driver with five 10 MiB files; size and retention are
operator policy, and the mounted ROS log directory needs an equivalent host
rotation/archival policy. Preserve relevant logs before rotation when collecting
support evidence; snapshot export is explicit and bounded, never an automatic
upload.

### Docker Compose example

[`docker/compose.yaml`](../docker/compose.yaml) is a single-receiver production
example. Copy [`docker/.env.example`](../docker/.env.example) to an
operator-controlled path, replace the placeholders with one verified stable
receiver identity and external paths, then run:

```bash
docker compose \
  --env-file /srv/universal-gnss/compose.env \
  -f docker/compose.yaml up -d
```

It uses `restart: unless-stopped` for Docker process recovery and reuses the
same read-only parameter mount on every recreation. The robot Phase-E matrix
below proves selected container/main-process/daemon/reboot behavior for its
recorded u-blox baseline. It does not make a stale USB device grant valid,
restore a volatile UM982 runtime profile, or make a running container evidence
of healthy GNSS transport; follow the USB-loss contract below.

### Runtime and restart contract

The entrypoint is not a supervisor: it validates the deployment schema/log
directory/default parameter-file boundary, sources the installed environments,
and `exec`s ROS launch. Invalid schema or unreadable required configuration
terminates the container before a receiver/NTRIP process is started; Docker's
restart policy repeats that checked startup only after the operator fixes the
external input. Every container recreation reads the mounted configuration
again, but physical receiver/profile persistence is not implied.

ROS launch owns `receiver_node` and `ntrip_node`; Universal GNSS does not embed
a second process supervisor or automatic receiver-device reattachment. Either
child's unexpected exit shuts down the combined launch/container so Docker can
recreate both children from the same external configuration. A child is never
independently respawned while the peer's caches survive. Receiver
and NTRIP receive one launch-generated source-incarnation token; each combined
launch start gets a new token, and NTRIP rejects samples from any other
incarnation even before its first current sample arrives. Receiver
transport failure becomes receiver diagnostics/freshness state rather than a
Docker health claim. NTRIP has its independent reconnect state and does not
restart a healthy receiver. Normal SIGINT/SIGTERM still follows launch's clean
group shutdown. The earlier Phase-E receiver-child failure applies to the prior
image; deterministic launch regression is now present, while physical container
recovery validation remains pending and receives no release credit yet.

Both enabled components expose `~/get_health` using `std_srvs/Trigger`. A
successful response means only that the component executor handled a bounded
request. The combined image always enables Receiver and NTRIP, so both must
respond. An enabled receiver with no available device remains responsive but
reports degraded transport/discovery and stale/no observation through diagnostics;
missing hardware never silently becomes a disabled mode. NTRIP connectivity,
RTCM flow, correction health, and RTK quality remain separate diagnostic states.

Entrypoint failures emit credential-free, stable key/value prefixes such as
`universal_gnss_entrypoint event=parameters_file_not_readable`; ROS diagnostics
remain the authoritative structured health/status surface. No generic non-ROS
status API is introduced by this container baseline.

### Bounded lifecycle evidence

The following short Kilted amd64 production-image matrix was run without a
mapped receiver and without changing receiver state. It is not an endurance,
hotplug, reboot, or process-crash claim.

| Scenario | Process / exit behavior | Application evidence | Restart / operator action |
| --- | --- | --- | --- |
| Valid v1 configuration, no mapped receiver | `receiver_node` and `ntrip_node` remained running; the current image's bounded component-response health became `healthy` | receiver discovery was ERROR; observation sequence stayed zero; fix/RTK/corrections stayed false; NTRIP was reconnecting with zero forwarded RTCM | Proven in the current local Kilted image; this closes health semantics only |
| Unreadable parameter file | entrypoint exited `1` before launch | `event=parameters_file_not_readable`; no ROS diagnostics exist because ROS never starts | fix the external mount/path, then let Docker policy recreate or recreate explicitly |
| Unsupported configuration schema | entrypoint exited `2` before launch | `event=unsupported_configuration_schema_version`; no ROS diagnostics exist | supply a supported schema, then recreate/restart under the Docker policy |
| Clean Docker stop (`STOPSIGNAL SIGINT`) | container exited `0`, OOM false, restart count `0` with `restart: no` | launch-managed nodes received normal shutdown | normal explicit stop; this does not prove crash/daemon-reboot recovery |
| Receiver present then unavailable | not rerun | established USB-loss evidence requires stable by-id re-resolution, container recreation, and UM982 profile replay | hardware-required; do not infer recovery from a reused tty/major/minor |
| NTRIP unavailable while receiver remains healthy | not rerun | requires a visible selected receiver in this validation namespace | hardware-required here; existing live NTRIP and receiver evidence remains separate |

### Robot Phase-E lifecycle evidence

The following matrix ran on 2026-09-05 on the recorded aarch64 robot/u-blox
baseline. Every post-start PASS required two location-redacted snapshots with
advancing observation state and healthy, non-stale receiver transport/parser;
Docker `running` or process health alone received no runtime-health credit.

| Case | Exact trigger and time (UTC) | Docker result | GNSS/runtime result | Verdict |
| --- | --- | --- | --- | --- |
| Clean container stop/start | `docker stop --time 10 ug-plan-005-robot-kilted` at `19:28:27`, exact legacy restoration, then `docker start ug-plan-005-robot-kilted` at `19:28:32` | stop exit `0`, OOM false, `RestartCount=0`, policy `unless-stopped`; manual start healthy at `19:28:38` | sequence `46 -> 75`, observations `144 -> 235`; intended device reopened | **PASS** |
| Receiver child termination | SIGKILL of only `receiver_node` via `docker exec` at `19:29:41` | ROS recorded exit `-9`; container remained running with state exit `0`, OOM false, `RestartCount=0`, policy `unless-stopped`, eventually `unhealthy`; no automatic restart | receiver was absent until explicit operator `docker restart --time 10`; recovery then advanced sequence `45 -> 75` | **FAIL** automatic receiver-process recovery |
| Operator container kill | `docker kill --signal KILL ug-plan-005-robot-kilted` at `19:32:18` | wait/die event exit `137`, signal `9`, OOM false, `RestartCount=0`, policy `unless-stopped`; remained exited through `19:34:02` | no post-start health because no automatic start occurred; exact legacy GPS was restored | **FAIL** automatic restart for this explicit Docker trigger; not process-crash evidence |
| Unexpected main application-process termination | SIGKILL of container PID 7 (`ros2 launch`, tini's main child) via `docker exec` at `19:38:28` | wait/die event exit `137`, OOM false; same container automatically started at `19:38:28.431`, `RestartCount=1`, policy `unless-stopped` | sequence `56 -> 85`, observations `176 -> 267`; healthy by `19:38:34` | **PASS** Docker restart-policy recovery |
| Docker daemon restart | `sudo -n /usr/bin/systemctl restart docker` at `19:52:04.598` with live restore false | command returned `0`; SIGINT/exit `0`, OOM false; dockerd PID `46198 -> 49209`; same container started `19:52:16.115`, `RestartCount=0`, policy `unless-stopped`; boot ID unchanged | sequence `55 -> 88`, observations `172 -> 276`; healthy by `19:52:22` | **PASS** |
| Host reboot/autostart | `sudo -n /usr/bin/systemctl reboot` at `19:54:01.944` | command returned `0`; SSH became unavailable; boot ID changed from `3248d0c3-c299-4977-ac2b-3acd3722b02a` to `c6292218-bbc3-4262-9374-877b53a82550`; prior exit `0`, OOM false; same container autostarted `19:54:38.322`, `RestartCount=0`, policy `unless-stopped` | sequence `312 -> 345`, observations `981 -> 1084`; healthy after the new boot | **PASS** |

After every successful restart boundary, inspection reconfirmed user
`1000:1000`, non-privileged default bridge, exactly the intended u-blox by-id
device mapped to `/dev/gnss-receiver` with GID 20, and no broad `/dev` mount.
The same external read-only configuration checksum was reused. No operator
receiver/runtime provisioning command was replayed for this u-blox baseline.
That observation is limited to these restart/reboot boundaries: it is not USB
hotplug/incarnation evidence, does not prove receiver-profile persistence, and
does not supersede the UM982 power-loss profile-replay requirement.

Final cleanup stopped current-UG cleanly (exit `0`, OOM false), restored its
test-only restart policy to `no`, released the receiver, and restored the exact
legacy Mowgli GPS container/image/configuration with exclusive `/dev/ttyACM1`
ownership. No unrelated Mowgli container definition was changed.

A second ROS CLI process in this constrained container did not discover the
diagnostics topic, so this matrix records the node/process/log evidence only;
it does not alter the established same-host DDS result or Docker networking.

### Device manager, backup, and support evidence

Production hosts need a udev/device-manager rule that creates or verifies one
operator-selected stable identity, grants its selected group read/write access,
and never selects a receiver by transient tty number. Resolve that identity into
`GNSS_DEVICE` immediately before Compose creation. The rule must not broadly
grant `/dev`, auto-attach a replacement receiver, or bypass the stale-grant
recreation contract.

Back up the external parameter file, Compose env file (where protected), image
tag/revision, selected stable receiver identity, and log directory metadata;
do not treat image layers as configuration storage. Restore by reviewing those
files, recreating the selected immutable image/container, and applying the
receiver-specific provisioning procedure where required. Never include NTRIP
credentials in a support archive.

Use the image-installed `universal-gnss-support-snapshot` (or the identical
repository script `scripts/collect_support_snapshot.py`) to create a
deterministic, non-secret JSON support artifact without receiver access or an
API. It records runtime identity, platform architecture, configuration schema,
a SHA-256 plus parameter-key shape (never parameter values), bounded direct-log
metadata, and an optional whitelist of standard OCI labels. It does not upload
anything, read raw logs, inspect Docker environment, recurse through the file
system, or include credentials.

```bash
python3 scripts/collect_support_snapshot.py \
  --output /srv/universal-gnss/support-snapshot.json \
  --parameters /srv/universal-gnss/parameters.yaml \
  --log-directory /srv/universal-gnss/log \
  --max-log-files 10 \
  --image universal-gnss:ros2-kilted-v0.7.0
```

Inside the Compose container, write directly to the persistent export mount:

```bash
docker compose -f docker/compose.yaml exec universal-gnss \
  universal-gnss-support-snapshot \
  --output /var/lib/universal_gnss/export/support-snapshot.json \
  --parameters /etc/universal_gnss/parameters.yaml \
  --log-directory /var/log/universal_gnss
```

The optional `--image` uses local `docker image inspect` only to collect the
six standard OCI labels; lack of Docker access is recorded as unavailable, not
an error. Capture actual receiver/NTRIP state separately from Docker health
using the ROS diagnostics/snapshot surface. Review the resulting artifact
before sharing: it is deliberately redacted but still records deployment
identity and parameter-key names.

## Serial and networking runtime contract

Map one explicit, stable host identity into the container. Prefer a host
`/dev/serial/by-id/...` path and map it to `/dev/gnss-receiver`; do not rely on
enumeration-sensitive `/dev/ttyACM*` or `/dev/ttyUSB*` names when a stable path
is available.

```bash
GNSS_DEVICE=/dev/serial/by-id/usb-example
GNSS_GROUP="$(stat --format='%g' "${GNSS_DEVICE}")"

docker run --rm \
  --device "${GNSS_DEVICE}:/dev/gnss-receiver" \
  --group-add "${GNSS_GROUP}" \
  --mount type=bind,src=/srv/universal-gnss/parameters.yaml,dst=/etc/universal_gnss/parameters.yaml,readonly \
  --mount type=bind,src=/srv/universal-gnss/log,dst=/var/log/universal_gnss \
  --env ROS_DOMAIN_ID=42 \
  universal-gnss:ros2-kilted
```

The group addition is necessary on hosts where the mapped serial device is not
world-readable (commonly `dialout`). It is not a substitute for host device
permissions. Do not add `--privileged` or mount all of `/dev` for this baseline.

### USB-loss and hotplug operational contract

The host-side stable identity is the authority for receiver selection. Resolve
one intended `/dev/serial/by-id/...` link immediately before `docker run`, map
only that resolved identity to `/dev/gnss-receiver`, and keep the configured
receiver identity/profile with the deployment. Do not select by `ttyACM*`,
`ttyUSB*`, major/minor number, or discovery order when a by-id link is present.

On platforms without `/dev/serial/by-id`, use a host udev/device-manager rule
or other operator-maintained identity mechanism that selects one receiver, then
verify the selected node and permissions before creating the container. This is
a deployment-specific fallback, not permission to guess from a transient tty
name or to attach every serial device.

The current CH340/UM982 and robot u-blox hardware results establish a
deliberately conservative contract. USB loss makes the existing Docker device
grant stale. Replugging may return the same tty and major/minor number, as in the
UM982 trial, or actually renumber, as when the robot u-blox changed from
`ttyACM1` / 166:1 to `ttyACM2` / 166:2. In both cases the already-running
container did not regain access. Its bounded component-response healthcheck can
therefore stay healthy while receiver transport and observations are stale; use
the ROS diagnostics/status surfaces to distinguish that state. Stop and
recreate the container only after the chosen stable identity has been
re-resolved.

Stable identity is also the wrong-receiver safety boundary. In the robot trial,
the expected u-blox by-id was absent while an unambiguous UM982/CH341 identity
was present. Starting the stopped container, still pinned only to the expected
u-blox by-id, failed with Docker's missing-device error and did not open or
silently substitute the UM982. After both receivers returned to their original
hosts, explicit by-id re-resolution and recreation restored fresh u-blox GNSS.
The swap power-cycled the UM982, so its established runtime-only profile was
replayed before accepting fresh UM982 transport on the second host; all 14
runtime commands completed and no persistent/reset command ran.

The UM982's runtime-only configuration is volatile across USB power loss.
After recreation, replay the intended profile using the normal guarded
provisioning workflow before accepting operation. This image does not claim
transparent in-place hotplug recovery, automatic device reattachment, or an
old-byte cutoff. The u-blox trial required no provisioning replay. Actual
renumbering and deliberate receiver substitution are proven only for the
recorded devices/topology; other receivers, adapters, and USB topologies require
their own validation.

The image leaves DDS networking policy to deployment configuration. Docker's
default bridge is not disabled, and no RMW implementation, domain ID, or
localhost-only setting is hard-coded. Validate host-to-container,
container-to-host, and container-to-container discovery before selecting bridge
or host networking for a robot deployment.

### Docker DNS and NTRIP recovery evidence

The image CI uses a private Docker bridge and a credential-free local fake
caster. It starts one Universal GNSS container while `caster.test` is absent,
requires an NTRIP connection failure, then starts a caster under that network
alias and requires an accepted response plus valid RTCM streaming. CI removes
the caster, requires another disconnected transition, recreates a different
caster container under the same alias, and requires a second accepted streaming
transition while the Universal GNSS container ID remains unchanged. This proves
that reconnect attempts re-resolve Docker DNS and recover after endpoint loss.
It does not prove external DNS, Internet routing, TLS, credentials, a specific
production caster, receiver acceptance of corrections, or RTK quality.

### Initial DDS topology evidence

On the validated Kilted amd64 development-host topology, Docker's default
bridge supported bidirectional discovery and `std_msgs/msg/String` delivery
between a host ROS 2 CLI node and one container when both used the same explicit
`ROS_DOMAIN_ID`. Two containers on one explicit Docker bridge network likewise
exchanged messages bidirectionally on the same domain; changing one container's
domain prevented delivery. Host networking was not required for these tests.

This is initial same-host development evidence, not external-LAN or robot
evidence. For developer workstations and a single-host robot, start with an
explicit domain and default/explicit bridge network, then validate the actual
host topology. For multiple ROS containers on one host, place participating
containers on an explicit Docker bridge network and use the same deliberate
domain. Domain IDs provide the observed DDS isolation; they are not an access
control mechanism. In this devcontainer/daemon topology,
`ROS_LOCALHOST_ONLY=1` still allowed host/container delivery, so it must not be
relied on for container or network isolation.

### Physical-LAN DDS evidence and required unicast contract

Kilted arm64 validation on the physical robot and a second Raspberry Pi first
used unmodified Docker default bridges with Fast DDS subnet discovery. It
failed discovery in both directions: `239.255.0.1` membership remained on each
host's separate `docker0`, not its physical LAN interface, and both bridges
assigned the overlapping locator `172.17.0.2/16`. No payload followed. Preserve
that negative result; a plain default bridge is not an external-LAN contract.

The single minimal alternative kept Docker network mode `bridge` and did not
use host networking. Each validation container mounted a host-specific Fast DDS
profile read-only, disabled builtin multicast, listed the opposite physical
host as an initial unicast peer, and announced its own physical host as the
external discovery/data locator. One fixed participant per endpoint used only
published UDP ports 36662 and 36663. Under that exact contract, distinct nonce
payloads and one remote publisher were observed in both physical directions.
A domain-117 publisher against the same-path domain-118 subscriber produced
zero remote discovery and zero payload.

The fixed pair supports the one-participant validation contract only. ROS CLI
daemon processes allocated additional participant ports and had to be stopped;
a deployment with multiple DDS participants must explicitly allocate, publish,
and announce every required locator rather than silently relying on this pair.
`ROS_DOMAIN_ID` provides the observed domain filter but is not a security or
access-control boundary. Revalidate whenever the hosts/interfaces, Docker
networking, RMW/Fast DDS version/profile, ports, or firewall/LAN policy changes.

`exec` is used for the launch process, so Docker `SIGTERM`/`SIGINT` reaches ROS
launch directly and its managed receiver/NTRIP processes receive normal launch
shutdown. Validate graceful shutdown against real hardware and a real caster
before treating that operational boundary as complete.
