# Native ESP32 reconstruction — implementation status

Work continues on `feat/esp32`, from `134e48b12acec960bba842cb06715847ec960a99`.
The transitional RTCM application lot is preserved in `7818dcc7b0dc9a8777f53a8f7561a6eced7def7b`.
`oldrtkesp/` is a local, ignored reference only. No build of the native firmware
reads it, downloads it or writes its working tree.

## Historical conservation

[legacy_inventory.json](legacy_inventory.json) accounts for all 336 Git entries
at `7a1ec69f5e9d07522c0284ec2dbae380f72c7e85`, their modes/objects, 37 explicit
HTTP contracts, and seven local branches. All seven branches are ancestors of
that reference; no extra branch commits were silently omitted. Migration
classification does not mean completion: OPEN items still need an equivalent.
Prebuilt images are REJECT inputs, not discarded functional requirements.
The two pre-existing manifest deletions remain untouched.

Source audit, rather than README claims, determines the required behaviour:

| Area | Existing evidence | Native status |
|---|---|---|
| GNSS telemetry/detection | `main/receiver.c`, NMEA/UBX and guessed text parsers | PARTIAL: actual UG sessions/probes/configuration libraries built for S3; native session owner tested |
| Fixed BASE / survey | Historical Unicore profile emits `FIX POSITION`, `SURVEY START`, `RTCMOUTPUT` and marks a queued profile applied | PARTIAL: new vendor-specific UG BASE plan builder, simulated command safety; operator/runtime integration still open |
| RTCM/filter/radio framing | `rtcm_filter`, `lora_transport`, `espnow_rtcm` | Native components adapted with original protocol/CRC16, one existing UG CRC24Q implementation |
| Ethernet/Wi-Fi | W5500 fork, LAN8720, network events/captive AP | OPEN: official IDF 6.1 drivers and network ownership integration |
| NTRIP | Legacy client, publishers, local caster, multislot supervisor, QoS | OPEN: one shared UG client/router/supervisor for BASE and GNSS-free relay |
| LoRa | SX126x driver, region tables, duty cycle, pipelines | OPEN: preserve regional policy and qualify native adapter; never enable unknown hardware |
| Persistence | `config.c`, NVS and slot configuration | OPEN: schema/rollback/credential handling and recovery without automatic erasure |
| Web/API | `web_server.c`, `www/` and 37 explicit routes | OPEN: retain useful contracts, strict validation/authentication, bounded monitoring |
| OTA/board assistants/Mammotion | Inventory covers profiles, board headers, scripts and docs | OPEN: distinguish actual implementation from historical TODO; no image publication/flash |

The old commands above are not a manufacturer contract and are not copied into
UG. The new builder does not weaken the existing Unicore profile builder's
refusal of BASE orchestration. It exposes a separate, explicit BASE plan.

## Receiver and correction contracts

`ug_receiver` directly compiles the owning UG driver source list. IDF excludes
host filesystem enumeration/POSIX opening; board-owned UART ByteDuplex instances
use the same byte analysis, model queries, builders and sessions. Timeout parsing
uses non-throwing `from_chars`, with the existing input/range checks preserved.
`ug_receiver_runtime` serializes observation parsing and command application.
It reuses `ReceiverConfigApplication` and the shared software response fence.
Partial/coalesced old replies cannot acknowledge a later dispatch. A timeout
quarantines the command engine; resetting observations never clears it. A software
fence does not prove a hardware prior-byte cutoff.

`ug_gateway` owns each source parser/incarnation and each bounded destination
queue. Reconfiguration invalidates partial frames and pending frames from that
source. Receipt timestamps remain in one monotonic domain; cached state is not
another observation. RecordReady must pass UG frame/CRC validation before fanout.
A slow destination drops whole frames without blocking another destination.

The future single `esp32` distribution has roles `gateway`, `relay-ntrip`, `rover`,
separate from boards. Gateway and GNSS-free relay use the same engine. The native
C3 relay skeleton excludes the GNSS library but is **not yet an operational
NTRIP relay**. The minimal rover excludes the gateway/receiver/network services.
Radio repeater support and the official Build & Flash portal remain separate
requirements; no experimental binaries are published automatically.

## BASE model/precision policy

The new `BuildBaseConfigProfile` uses verified model identity, UG safety commands
and the existing UG UBX configuration builder. It retains double precision in
the model; quantization happens only at the receiver's documented wire boundary.

- F9P: CFG-VALSET TMODE fixed LLH including 1e-9 degree/0.1mm residuals, or
  Survey-In duration and accuracy limit. Persistent layers require confirmation.
- UM980/UM982: explicit UNDULATION policy precedes fixed coordinates. TIME
  self-optimization is a distinct mode; its distance is a Flash-position reuse
  radius, not a standard-deviation threshold. TIME itself is persistent and
  therefore requires confirmation, even without SAVECONFIG.
- UM981: telemetry model support remains in UG. BASE command/datum qualification
  remains OPEN; no inference from UM980/UM982 creates a silent support claim.
- No survey-complete state or RTK FIX is inferred from a command acknowledgement.

Manufacturer sources: [N4 command manual R1.6](https://en.unicore.com/uploads/file/Unicore%20Reference%20Commands%20Manual%20For%20N4%20High%20Precision%20Products_V2_EN_R1.6.pdf),
sections 3.2/3.3/4.4/8.1; [u-blox F9 HPG 1.32 interface](https://content.u-blox.com/sites/default/files/documents/u-blox-F9-HPG-1.32_InterfaceDescription_UBX-22008968.pdf),
CFG-TMODE pp.267–268, message keys pp.285–286. These are executable plan tests,
not a claim of receiver hardware qualification.

## SDK, language and licensing decisions

Target SDK: **ESP-IDF 6.1**, official image
`espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`.
The [release](https://github.com/espressif/esp-idf/releases/tag/v6.1) and
[Ethernet migration](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/migration-guides/release-6.x/6.0/networking.html)
are verified. Native ESP-NOW uses the IDF 6 bandwidth enum; it does not retain a
private Wi-Fi fork. Dependency provisioning is separate from isolated builds.

| Technology | Safety/resources/integration | Decision |
|---|---|---|
| C | Small vendor/radio/IDF ABI, requires explicit bounds and ownership | Retain audited wire-compatible adapters, with tests |
| C++17 | Reuses the actual UG parsers/drivers/builders and safety model; bounded firmware queues | Current native engine; host ASan/UBSan, receiver budgets still to qualify |
| Rust | Useful for new untrusted-input state machines; requires an Xtensa toolchain fork as well as RISC-V tooling and a reviewed FFI | No new implementation yet: no measured benefit justifies a second toolchain or duplicate UG parser |

The [Espressif Rust toolchain documentation](https://docs.espressif.com/projects/rust/book/getting-started/toolchain.html)
describes the Xtensa/RISC-V distinction. This decision can change for a new
component after size/CPU/maintenance comparisons; memory safety testing remains
required in every language.

The three adapted radio/filter components retain historical provenance and GPLv3
terms, with [the original license](../licenses/legacy-GPL-3.0.txt). UG portable
libraries remain LGPLv3. Combined firmware is not represented as LGPL-only.
SDK/vendor notices and a final dependency inventory must accompany any future
publication. No historical source checkout, secret, private MAC, NVS dump or
binary backup is an input to a commit.

## Current reproducible commands and limits

With IDF 6.1 activated, normal user, from UG root:

```bash
python3 embedded/esp32/native/build.py --output /tmp/ug-native-s3 --profile gateway --board waveshare-s3-eth --local-gnss
python3 embedded/esp32/native/build.py --output /tmp/ug-native-c3 --profile rover --board supermini-c3
cmake -S embedded/esp32/native/tests -B /tmp/ug-native-host
cmake --build /tmp/ug-native-host
ctest --test-dir /tmp/ug-native-host --output-on-failure
```

Outputs must be new external directories. Default peers are fictitious,
channel 1 is fixed; neither configuration nor partition tables authorize flash.
NVS initialization never automatically erases configuration on version/full errors.
Initial native builds S3, C3 rover and C3 relay skeleton pass IDF 6.1 compilation.
Three new host tests cover routing/source reset, command causality/quarantine,
and vendor BASE plans, under ASan/UBSan. Existing Linux discovery/auto-config and
configuration-apply regressions pass. Final firmware budgets and reproducibility
will be measured after the remaining services are actually linked.

HARDWARE_PENDING: no installed firmware changed, no new RF/USB/receiver/endurance
claim. Native network services, operator BASE application, LoRa, persistence,
WebUI and CI/portal are not declared complete by these foundation builds.
