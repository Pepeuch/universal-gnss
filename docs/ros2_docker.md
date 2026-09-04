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
  for both arm64 images, but native arm64 runtime and receiver hardware remain
  required evidence.
- The image is non-root by default and does not use privileged mode or bind all
  of `/dev`.
- The Docker healthcheck confirms only that both launch-managed processes are
  present. It does **not** assert receiver connectivity, observation freshness,
  NTRIP connection/RTCM flow, correction validity, or RTK state.

The established scope is same-host Docker bridge DDS, amd64 receiver/caster
hardware, and emulated arm64 packaging. External-LAN DDS, native arm64 runtime
and hardware, serial renumbering, robot topology, and downstream platform
validation remain separate evidence requirements.

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

The `/universal_gnss_receiver/diagnostics` stream and its
`~/get_snapshot` response contain `universal_gnss/runtime_identity` with the
image version/revision, ROS distribution, and configured receiver family. It
is deployment identity only, not a health verdict; discovery diagnostics retain
the separate receiver model/firmware fields when they are actually known.

`arm/v7` is not supported for v0.7. Native arm64 runtime/hardware remains a
separate pending target despite the established BuildKit/QEMU package smoke.

For each released image, retain an SPDX or CycloneDX SBOM generated from the
immutable image digest, its exact Dockerfile revision, and the OCI
version/revision labels. The v0.7 CI establishes deterministic amd64 build
inputs and identity but does not publish attestations or multi-architecture
images; SBOM generation/provenance capture is a release artifact step before
any later publication, not a runtime dependency.

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

ROS logs default to `/var/log/universal_gnss`; mount it to external writable
storage. The container user defaults to UID/GID `1000`; use the Dockerfile
`APP_UID`/`APP_GID` build arguments or provision the mounted directory for that
identity. `ROS_LOG_DIR` may be overridden with another writable path.

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
support evidence; there is no automatic support-bundle export in v0.7.

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
same read-only parameter mount on every recreation. It does not make a stale
USB device grant valid, restore a volatile UM982 runtime profile, or prove
container-crash/reboot recovery; follow the USB-loss contract below and retain
those validation gates as pending.

### Runtime and restart contract

The entrypoint is not a supervisor: it validates the deployment schema/log
directory/default parameter-file boundary, sources the installed environments,
and `exec`s ROS launch. Invalid schema or unreadable required configuration
terminates the container before a receiver/NTRIP process is started; Docker's
restart policy repeats that checked startup only after the operator fixes the
external input. Every container recreation reads the mounted configuration
again, but physical receiver/profile persistence is not implied.

ROS launch owns `receiver_node` and `ntrip_node`; Universal GNSS does not embed
a second process supervisor or automatic receiver-device reattachment. Receiver
transport failure becomes receiver diagnostics/freshness state rather than a
Docker health claim. NTRIP has its independent reconnect state and does not
restart a healthy receiver. If either ROS process exits, the resulting launch/
container lifecycle is Docker's responsibility under the configured restart
policy; container crash/reboot and receiver-process restart matrices remain
unvalidated deployment gates.

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
| Valid v1 configuration, no mapped receiver | `receiver_node` and `ntrip_node` remained running; Docker process health became `healthy` | receiver discovery emitted `receiver_discovery_failed`; NTRIP independently reported disconnected | Docker health remains process-only; investigate ROS diagnostics and provide/re-resolve the selected device before recreating if needed |
| Unreadable parameter file | entrypoint exited `1` before launch | `event=parameters_file_not_readable`; no ROS diagnostics exist because ROS never starts | fix the external mount/path, then let Docker policy recreate or recreate explicitly |
| Unsupported configuration schema | entrypoint exited `2` before launch | `event=unsupported_configuration_schema_version`; no ROS diagnostics exist | supply a supported schema, then recreate/restart under the Docker policy |
| Clean Docker stop (`STOPSIGNAL SIGINT`) | container exited `0`, OOM false, restart count `0` with `restart: no` | launch-managed nodes received normal shutdown | normal explicit stop; this does not prove crash/daemon-reboot recovery |
| Receiver present then unavailable | not rerun | established USB-loss evidence requires stable by-id re-resolution, container recreation, and UM982 profile replay | hardware-required; do not infer recovery from a reused tty/major/minor |
| NTRIP unavailable while receiver remains healthy | not rerun | requires a visible selected receiver in this validation namespace | hardware-required here; existing live NTRIP and receiver evidence remains separate |

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

Use the repository-local `scripts/collect_support_snapshot.py` to create a
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

The current CH340/UM982 hardware result establishes a deliberately conservative
contract. USB loss makes the existing Docker device grant stale. Replugging may
return the same tty and major/minor number, yet the already-running container
does not regain access. Its process-only Docker healthcheck can therefore stay
healthy while receiver transport and observations are stale; use the ROS
diagnostics/status surfaces to distinguish that state. Stop and recreate the
container only after the chosen stable identity has been re-resolved. Never
silently substitute a different receiver.

The UM982's runtime-only configuration is volatile across USB power loss.
After recreation, replay the intended profile using the normal guarded
provisioning workflow before accepting operation. This image does not claim
transparent in-place hotplug recovery, automatic device reattachment, or an
old-byte cutoff. Serial renumbering, other receivers/adapters, and other USB
topologies require their own validation.

The image leaves DDS networking policy to deployment configuration. Docker's
default bridge is not disabled, and no RMW implementation, domain ID, or
localhost-only setting is hard-coded. Validate host-to-container,
container-to-host, and container-to-container discovery before selecting bridge
or host networking for a robot deployment.

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

### External-LAN DDS validation still required

This acceptance task is **BLOCKED_BY_ENVIRONMENT /
HARDWARE_OR_TOPOLOGY_REQUIRED**. The current validation workspace is in Docker
networking at `172.17.0.6`, on the `172.17.0.0/16` bridge, with no second
physical LAN host reachable; its normal project user also cannot access the
Docker daemon socket. No external-LAN claim is made.

The future acceptance topology is machine A running the Dockerized Universal
GNSS ROS2 node on Docker's default bridge and independent physical-LAN machine
B running a ROS2 node. Configure the same explicit `ROS_DOMAIN_ID`, then record
discovery and payload delivery separately for B -> container and container ->
B. Repeat the payload check with a different domain and require no delivery.
Record Fast DDS/RMW version and interface/discovery settings, host OS, LAN
addresses/prefixes, Docker network mode, and inspectable firewall, multicast,
or discovery-server configuration. Same-host bridge evidence neither proves nor
replaces this test; do not switch globally to host networking unless a real
default-bridge failure establishes that need.

`exec` is used for the launch process, so Docker `SIGTERM`/`SIGINT` reaches ROS
launch directly and its managed receiver/NTRIP processes receive normal launch
shutdown. Validate graceful shutdown against real hardware and a real caster
before treating that operational boundary as complete.
