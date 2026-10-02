# Universal GNSS MAVROS adapter

`universal_gnss_mavros` is an optional external MAVROS plugin. It makes raw
FCU MAVLink GNSS messages another input transport for Universal GNSS without
putting MAVROS headers or lifecycle policy into `gnss_core`.

The supported baseline is exactly MAVROS `2.16.0`, commit
`5c68b905ab30de6ce630822dc46c33467e8f23ea`. CMake rejects a different MAVROS
package version.

## Architecture

```text
Universal GNSS core state and ROS adapters
              ^                 |
              |                 v
MAVROS-free GNSS adapter   MAVROS-free RTCM encoder
              ^                 |
              \                 /
               UniversalGnssPlugin
                       ^ |
                       | v
                     MAVLink
```

The plugin registers as `universal_gnss` with MAVROS. It uses decoded
`make_handler()` callbacks for:

- `GPS_RAW_INT` -> GPS1
- `GPS2_RAW` -> GPS2
- `GPS_RTK` -> GPS1 RTK metadata/enrichment
- `GPS2_RTK` -> GPS2 RTK metadata/enrichment
- `SYSTEM_TIME` -> FCU boot-incarnation evidence and metadata

It also calls `enable_connection_cb()` and handles MAVROS connection state in
`connection_cb()`.

GPS1 and GPS2 have separate `GnssRuntimeAggregator` instances, genuine
position-observation sequences, metadata, source IDs, and topics. The plugin
does not choose between them; consumers select or remap the required source
using the existing Universal GNSS source/provenance contract.

## Topics and parameters

Within the MAVROS node namespace the plugin publishes:

- `~/gps1/status` and `~/gps2/status` as
  `universal_gnss_ros2/msg/GnssStatus`
- `~/gps1/fix` and `~/gps2/fix` as `sensor_msgs/msg/NavSatFix`

It also subscribes to the canonical Universal GNSS RTCM stream and injects each
`RtcmFrame` into the FCU as MAVLink `GPS_RTCM_DATA`. The conversion follows the
MAVLink fragmentation contract used by MAVROS: 180 bytes per fragment, up to
four fragments (720 bytes) per RTCM frame, with the 5-bit sequence and 2-bit
fragment identifiers encoded in `flags`. Oversized frames are rejected rather
than silently split across independent MAVLink sequences.

Parameters on the plugin subnode:

- `source_id_prefix` (default `mavlink:fcu`), producing stable logical IDs
  `<prefix>:gps1` and `<prefix>:gps2`
- `gps1_frame_id` (default `gps1`)
- `gps2_frame_id` (default `gps2`)
- `rtcm_input_topic` (default `/rtcm`), the Universal GNSS `RtcmFrame` input
  topic; deployments may override this to match their UG namespace

Each decoded raw GPS message is a genuine observation and advances that
receiver's sequence even when all numeric values equal the preceding message.
`GPS_RTK`/`GPS2_RTK` update only their receiver and do not invent a position
observation. MAVLink fix types, DOP, available v2 accuracy fields, satellite
count, ground speed/course, GPS2 DGPS age, and differential/RTK state map into
the existing capability/value model. MAVLink `alt` remains the reported MSL
altitude; no unproved geoid conversion is applied.

The public status/fix timestamp is `node->now()` captured in the message
handler. MAVLink GPS time, FCU boot time, and FCU Unix time are retained only as
adapter metadata and never replace ROS receipt time.

## Incarnation and freshness contract

An actual MAVROS connection-state transition immediately starts a new source
incarnation and clears both receiver aggregates, sequences, and raw/RTK
metadata. The plugin publishes invalidated status for both receivers at that
boundary.

`SYSTEM_TIME.time_boot_ms` is compared as a wrapping 32-bit serial counter. A
backward transition within the unambiguous half-range starts a new incarnation;
a natural `UINT32_MAX -> 0` wrap does not. Forward time passage, repeated timer
values, and absence of messages are never treated as incarnation evidence.

There is an unavoidable ordering gap: a post-reboot GPS message can arrive
before a later `SYSTEM_TIME` message proves the reboot. It can therefore be
published briefly under the preceding incarnation. Once the boot regression is
observed, both caches are invalidated and the generation changes. The adapter
does not claim to distinguish that early message without additional FCU
lifecycle evidence.

## Build

The normal repository build compiles and tests the MAVROS-free adapter but
leaves the external plugin disabled. Build the ament package in a ROS workspace
with the pinned MAVROS package available:

```bash
colcon build --packages-up-to universal_gnss_mavros
```

For the repository-root CMake build, opt in with
`-DUNIVERSAL_GNSS_BUILD_MAVROS_PLUGIN=ON` and make the installed
`universal_gnss_ros2` and pinned MAVROS prefixes discoverable.

RTCM injection is implemented directly in this plugin package. The generic
NTRIP client remains in Universal GNSS; the MAVROS plugin only translates the
canonical `RtcmFrame` transport into MAVLink and calls `uas->send_message()`.
`gnss_core` remains independent of MAVROS.
