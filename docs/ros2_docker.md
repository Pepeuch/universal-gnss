# ROS 2 production container baseline

This is the first production-oriented Universal GNSS container baseline. It
starts the existing `receiver_and_ntrip.launch.py` launch description in one
container. ROS launch remains responsible for the existing `receiver_node` and
`ntrip_node` processes and their shutdown; this image introduces no second
GNSS, NTRIP, or configuration implementation.

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
  --tag universal-gnss:ros2-kilted \
  .
```

Use `--build-arg ROS_DISTRO=lyrical` for the Lyrical image. Multi-architecture
build/publish automation is a later milestone; build each validated target
platform explicitly for now.

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

ROS logs default to `/var/log/universal_gnss`; mount it to external writable
storage. The container user defaults to UID/GID `1000`; use the Dockerfile
`APP_UID`/`APP_GID` build arguments or provision the mounted directory for that
identity. `ROS_LOG_DIR` may be overridden with another writable path.

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
