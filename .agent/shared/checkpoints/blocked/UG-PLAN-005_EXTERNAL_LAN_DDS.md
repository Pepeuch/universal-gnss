# UG-PLAN-005 external-LAN DDS validation

Status: BLOCKED_BY_ENVIRONMENT — `HARDWARE_OR_TOPOLOGY_REQUIRED`

## Contract

Validate Kilted amd64 ROS 2 DDS across two physical or independently routed LAN
peers using Docker's default bridge first. Prove external-host -> container and
container -> external-host payload delivery on the same explicit
`ROS_DOMAIN_ID`, then prove a different domain receives no payload. Domain IDs
are not a security boundary. Do not use host networking as a substitute for the
default-bridge result.

## Established boundary

Same-host host/container, container/container, and domain isolation are already
proven and must not be rerun unless a control is genuinely required. The current
workspace is one Docker namespace (`eth0`, `172.17.0.6`), has no discoverable
second LAN peer, and the ordinary Docker bridge is `172.17.0.0/16`. Kilted uses
`rmw_fastrtps_cpp` with `ROS_AUTOMATIC_DISCOVERY_RANGE=SUBNET`.

## Unblock condition and procedure

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
