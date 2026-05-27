# Universal GNSS

Universal GNSS is a modular GNSS/RTK runtime stack designed for ROS 2, embedded systems, and RTK base stations.

The goal is to provide a vendor-agnostic GNSS layer capable of parsing, normalizing, configuring, and exposing GNSS data from multiple receiver families through a common runtime model.

## Goals

- Provide a portable GNSS core independent from ROS 2.
- Normalize GNSS runtime state across vendors.
- Support NMEA, RTCM3, u-blox UBX, Unicore, Quectel, and other protocols progressively.
- Expose consistent ROS 2 topics, services, and diagnostics.
- Support RTK rover and RTK base workflows.
- Keep parser, driver, transport, NTRIP, and ROS 2 layers separated.

## Non-goals

- Supporting every GNSS vendor message from day one.
- Replacing vendor tools for firmware updates.
- Mixing ROS 2 code into the portable parser core.

## License

This project is licensed under LGPLv3.

- Layers
- gnss_core

Portable C/C++ core with no ROS 2 dependency.

## Responsibilities:

- runtime model
- capability flags
- common GNSS types
- parser interfaces
- error/status types
- gnss_protocols

# Protocol-specific parsers.

## Planned protocols:

- NMEA
- RTCM3
- UBX
- Unicore
- Quectel PQTM/PAIR
- Septentrio SBF
- gnss_driver

# Receiver control layer.

## Responsibilities:

- UART/TCP/UDP transport
- auto-detection
- receiver configuration
- message rates
- rover/base mode
- PPS and constellation settings
- gnss_ntrip

## NTRIP and RTCM transport layer.

## Responsibilities:

- NTRIP client
- GGA injection
- RTCM frame relay
- correction age metrics
- reconnect/backoff
- gnss_ros2

# ROS 2 integration.

## Responsibilities:
```
/gps/fix
/gps/status
/gps/azimuth
/diagnostics
configuration services
gnss_rtk_base
```
# RTK base orchestration.

## Responsibilities:

- survey-in
- fixed base mode
- RTCM routing
- local caster integration
- gnss_esp32

# Embedded adapter for ESP32-based RTK gateways.

## Responsibilities:

- lightweight parser build
- WebUI/MQTT integration
- LoRa/RTCM routing
- NTRIP forwarding