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
  `universal_gnss_msgs/msg/GnssStatus`
- `~/gps1/fix` and `~/gps2/fix` as `sensor_msgs/msg/NavSatFix`

The headers under `universal_gnss_ros2/msg/` provide C++ compatibility aliases.
They do not define separate ROSIDL types; the canonical status and RTCM types
belong to `universal_gnss_msgs`.

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

### Default native build

The repository-root CMake option `UNIVERSAL_GNSS_BUILD_MAVROS_PLUGIN` defaults
to OFF. The entire MAVROS subdirectory, including the free adapter/encoder and
their tests, is omitted from that build. The default native CTest suite therefore
does not qualify MAVROS.

### Standalone ament package

From the repository root, in an environment containing ROS 2 Lyrical and the
pinned MAVROS dependencies, build the three packages in dependency order.
The example uses the `/opt/mavros` prefix supplied by the development image;
use the actual external prefix if your environment differs. Choose a fresh
temporary workspace for each validation run.

```bash
source /opt/ros/lyrical/setup.bash
source /opt/mavros/local_setup.bash
colcon --log-base /tmp/ug-mavros/log build \
  --build-base /tmp/ug-mavros/build \
  --install-base /tmp/ug-mavros/install \
  --base-paths universal_gnss_msgs adapters/ros2/gnss_ros2 adapters/mavros/gnss_mavros \
  --packages-up-to universal_gnss_mavros \
  --executor sequential --cmake-args -DCMAKE_BUILD_TYPE=Release
source /tmp/ug-mavros/install/setup.bash
colcon --log-base /tmp/ug-mavros/log test \
  --build-base /tmp/ug-mavros/build \
  --base-paths universal_gnss_msgs adapters/ros2/gnss_ros2 adapters/mavros/gnss_mavros \
  --packages-select universal_gnss_mavros --return-code-on-test-failure
colcon test-result --test-result-base /tmp/ug-mavros/build/universal_gnss_mavros --verbose
```

Explicit base paths avoid relying on recursive package discovery through the
native root CMake project. `universal_gnss_msgs` remains at the repository root.
Standalone mode enables the external plugin by default and finalizes its ament
exports and plugin index. The two package tests exercise the adapter and encoder.

### Integrated native build

After sourcing the standalone installation above and the external dependencies,
the root CMake can build the plugin against the installed ROS2 prefix:

```bash
cmake -S . -B /tmp/ug-mavros-integrated -DCMAKE_BUILD_TYPE=Release \
  -DUNIVERSAL_GNSS_BUILD_MAVROS_PLUGIN=ON -DUNIVERSAL_GNSS_BUILD_ROS2=OFF
cmake --build /tmp/ug-mavros-integrated --parallel 3 --target \
  universal_gnss_mavros_plugin gnss_mavros_test_adapter gnss_mavros_test_rtcm_encoder
ctest --test-dir /tmp/ug-mavros-integrated -R '^gnss_mavros_' --output-on-failure
```

This mode compiles and runs the two tests, but does not finalize the package
with `ament_package()`. Its automatic discovery through the ament plugin index
is **not qualified**; the index resource is absent in the validated integrated
installation. This example does not qualify a simultaneous root ROS2 build.

Compilation and passing adapter/encoder tests do not prove dynamic loading.
Standalone factory loading requires a real pluginlib ClassLoader with base
`mavros::plugin::PluginFactory` to discover `universal_gnss` and create its factory
from the expected installed shared library. The [relocation validation](../../../tools/branches/ROS2_MOVE.md)
records that separate software check. It does not create a UAS/live plugin
instance or qualify GNSS/MAVLink hardware operation.

The 111 pre-existing direct MAVROS formatting diagnostics remain a separate
debt. Neither this guide update nor a successful build resolves them.

RTCM injection is implemented directly in this plugin package. The generic
NTRIP client remains in Universal GNSS; the MAVROS plugin only translates the
canonical `RtcmFrame` transport into MAVLink and calls `uas->send_message()`.
`gnss_core` remains independent of MAVROS.
