# UG-PLAN-005 external-LAN DDS validation

Status: ACTIVE — physical peers inventoried; acceptance matrix not executed

## Contract

Validate Kilted ROS 2 DDS between the current-UG robot container and an
independent physical arm64 LAN peer, using Docker's default bridge first. Prove
external-host -> container and container -> external-host payload delivery on
the same explicit `ROS_DOMAIN_ID`, then prove a different domain receives no
payload. Domain IDs
are not a security boundary. Do not use host networking as a substitute for the
default-bridge result.

## Established boundary

Same-host host/container, container/container, and domain isolation are already
proven and must not be rerun unless a control is genuinely required. The current
workspace is one Docker namespace (`eth0`, `172.17.0.6`), has no discoverable
second LAN peer, and the ordinary Docker bridge is `172.17.0.0/16`. Kilted uses
`rmw_fastrtps_cpp` with `ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET`.

## Execution procedure

Provide Docker access on the container host and a second physical/routed LAN
peer. Record host OS/kernel, interface/IP/prefix, Docker network mode,
RMW/DDS configuration, explicit domain, and inspectable firewall/multicast or
discovery-server configuration. On default bridge, record discovery and payload
flow independently for both directions. Then repeat only the payload check on a
different domain and require no delivery. If discovery or traffic fails, retain
the exact failure and determine multicast/discovery/address-advertisement cause
before testing one minimal alternative. Native arm64, robot, MowgliNext, and
BlueOS remain out of this matrix.

## Evidence and invalidation

The host-daemon inspection was Docker 29.7.2 linux/amd64; its bridge had ICC
and IP masquerade enabled. It cannot establish external-LAN behavior. Re-run
this matrix if the target host/network, Docker mode, RMW configuration, DDS
discovery policy, or firewall policy changes.

## Phase A physical-peer evidence — 2026-09-05

- Robot `Peuchmower2`: native aarch64 Debian 13, Docker 29.8.0 arm64,
  `wlan0=192.168.10.35/24`.
- Second physical RPi `raspberrypi`: native aarch64 Debian 13, Docker 29.8.0
  arm64, `eth0=192.168.10.225/24`.
- Both use default route `192.168.10.1`, are independently SSH-reachable, and
  permit user-level Docker inspection. Their Docker default bridges are
  `172.17.0.0/16`; each also has an existing Compose bridge at `172.18.0.0/16`.
- Neither host has `ufw`, `nft`, or `iptables` CLI tooling installed. This is
  limited visibility, not proof that no kernel or Docker-managed rules exist.
- Existing Mowgli containers on both hosts use Kilted/Cyclone DDS, host
  networking, domain 0, and localhost-only discovery. They are stale baseline
  evidence and must not substitute for the current-UG/Fast DDS/default-bridge
  matrix.
- The former missing-peer/Docker-access unblock condition is satisfied. No DDS
  discovery or payload direction has been tested, so status is ACTIVE/PENDING
  with no acceptance credit.
