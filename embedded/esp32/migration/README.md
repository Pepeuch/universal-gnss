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
| Fixed BASE / survey | Historical Unicore profile emits `FIX POSITION`, `SURVEY START`, `RTCMOUTPUT` and marks a queued profile applied | PARTIAL: verified-model UG BASE plans, single UART owner, typed asynchronous API and persistent interruption journal; physical/survey completion qualification remains open |
| RTCM/filter/radio framing | `rtcm_filter`, `lora_transport`, `espnow_rtcm` | Native components adapted with original protocol/CRC16, one existing UG CRC24Q implementation |
| Ethernet/Wi-Fi | W5500 fork, LAN8720, network events/captive AP | PARTIAL: official W5500 2.0.1, shared link ownership, boot-configured STA/AP; LAN8720 and physical validation remain open |
| NTRIP | Legacy client, publishers, local caster, multislot supervisor, QoS | PARTIAL: actual UG NtripClient/TCP-TLS backend, five SOURCE-v1 publishers and bounded authenticated local caster; final runtime qualification/QoS remains open |
| LoRa | Historical nonfunctional sketch | OPEN, deferred by maintainer: ESP-NOW first; no native LoRa activation or regional qualification |
| Persistence | `config.c`, NVS and slot configuration | PARTIAL: strict schema1, dual banks, immutable snapshots and journal; offline legacy JSON conversion and guarded previous-bank restore available; on-device legacy NVS import remains open |
| Web/API | `web_server.c`, `www/` and 37 explicit routes | PARTIAL: HTTPS configuration/status/BASE UI and bearer authentication; remaining useful historical operations still open |
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
C3 relay compiles the same network/router engine without GNSS driver sources.
Its Ethernet/ESP-NOW operation still requires the documented physical wiring test. The minimal rover excludes the gateway/receiver/network services.
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
python3 embedded/esp32/native/build.py --output /tmp/ug-native-s3 --profile gateway --board waveshare-s3-eth --local-gnss --provision
python3 embedded/esp32/native/build.py --output /tmp/ug-native-c3 --profile rover --board supermini-c3
cmake -S embedded/esp32/native/tests -B /tmp/ug-native-host -DUG_CJSON_SOURCE=/path/to/official-managed-cjson/cJSON
cmake --build /tmp/ug-native-host
ctest --test-dir /tmp/ug-native-host --output-on-failure
```

Outputs must be new external directories. Default peers are fictitious,
channel 1 is fixed; neither configuration nor partition tables authorize flash.
NVS initialization never automatically erases configuration on version/full errors.
Initial native builds S3, C3 rover and C3 relay skeleton pass IDF 6.1 compilation.
Twenty-five host cases now cover routing/source reset, command causality/quarantine,
vendor BASE plans, TCP/TLS backend seams, real loopback NTRIP client/publisher,
local caster policy, strict settings, and interrupted/corrupt persistent storage.
They pass under ASan/UBSan. A separate QuickJS browser test passes with mocked
DOM/fetch; this is not an HTTPS or physical-browser interoperability claim. Existing Linux discovery/auto-config and
configuration-apply regressions pass. Final firmware budgets and reproducibility
will be measured after the remaining services are actually linked.

HARDWARE_PENDING: no installed firmware changed, no new RF/USB/receiver/endurance
claim. The firmware is still PARTIAL: LoRa, remaining GNSS/Web/API features, legacy
configuration import, OTA, runtime/QoS and CI/portal remain open. No physical
GNSS/survey, Ethernet, TLS handshake or new RF/USB test is inferred from a build.


## Native network/configuration milestone

The gateway and GNSS-free relay share one UG NtripClient and router, with distinct
bounded queues/tasks for sockets and UART. A reconnect/configuration change/link
handover changes source ownership and discards old queued corrections. In-flight
physical bytes cannot be retracted. No software generation counter claims such a
cutoff. A cached GGA position is withheld after three seconds without a UG position
observation, including when other receiver records continue. Receipt clocks remain
IDF monotonic; SNTP affects certificate validity only.

The new SOURCE-v1 publisher uses UG transport/TLS and reconnect policy. Historical
publisher code actually emits SOURCE v1 regardless of its version label; HTTP-v2
upload/chunking support is not claimed. Four-client local caster policy adds
optional Basic authentication, bounded requests and strict cache-source changes;
existing Linux defaults remain unchanged.

Settings accept at most 8192 bytes, depth 12 and a bounded JSON node estimate;
unknown/duplicate fields and invalid roles fail atomically. GET/export redacts
secrets; redacted exports cannot reconstruct missing credentials. Two NVS records
provide schema/generation/corruption recovery. An unknown schema locks writes,
and an unreadable/full NVS is never automatically erased. CRC detects corruption,
not tampering. Production flash encryption/secure boot is not provisioned by this
work; secrets at rest remain subject to the device's existing flash protection.

Before command dispatch, the GNSS owner commits a persistent journal. Interrupted
or ambiguous application remains quarantined across reboot. Parser/telemetry reset
never clears it. No remote endpoint clears quarantine. Successful command ACKs do
not prove Survey-In completion or physical RTCM generation.

HTTPS starts only with a locally provisioned matching certificate/key. There is
no shared default credential and no plaintext API fallback. The server uses the
[official IDF HTTPS API](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/protocols/esp_https_server.html).
Use `native/tools/commission.py --output <new-private-external-directory>` to
prepare a unique test/admin token and self-signed P-256 identity. This tool never
opens a serial port or flashes. Its private console-input file must be transmitted
only through the intended local setup console, after independent port/hardware
identification. Verify/trust the certificate fingerprint locally. The local
commands are `tls-init <JSON>`, `config <JSON>` and `reboot CONFIRM`. The rover
links neither this console nor network/Web/settings services.

Wi-Fi/radio/TLS identity/name settings take effect after explicit reboot. The
current fixed-channel ESP-NOW profile rejects STA association; AP mode uses the
configured channel. No automatic pairing/channel switching is added. Default
compile peers are fictitious; real peer MACs must be provisioned deliberately.

### Current API coverage and intentional incompatibilities

- `/api/status` (`/status`) and `/api/capabilities`: bounded public diagnostics,
  without coordinates, credentials, raw receiver data or private MACs.
- Authenticated GET `/api/config` (`/config`): schema1, redacted secrets; POST
  applies a validated patch and commits before publishing its immutable snapshot.
- Authenticated GNSS status/diagnostics/BASE status and typed BASE application,
  with mandatory explicit confirmation; persistence has a second explicit token.
- Other inventoried operations remain OPEN. They return unavailable rather than
  a synthetic success. The old unauthenticated flat config is deliberately not
  accepted by the new schema; legacy import still needs an explicit reviewed map.

### Compile-only budgets at this milestone

S3 gateway: 1403744-byte app in each existing 3145728-byte OTA slot. C3 relay:
1288384-byte app in each 1310720-byte OTA slot. The common-root TLS bundle and size optimization are required for the existing
1280-KiB slots; detailed verified bytes are recorded by `native/verify.py` in the
external build directory. Assertions stay enabled. The C3 common-root bundle is
a deliberately smaller trust set, not disabled certificate/hostname verification;
endpoints outside it require a provisioned private CA. No partition was changed.
SDK dependency provisioning precedes network-disconnected UID1000 builds. The
compiled ELF is inspected for exactly one UG CRC implementation, the real UG
network/session symbols, and absent GNSS/admin dependencies in excluded profiles.
Full reproducibility, final RAM budgets and remaining firmware features still need
qualification at completion.

Worker waits round upward to at least one RTOS tick. At the SDK default of
100 Hz, an unadjusted five-millisecond wait truncates to zero and can starve
lower-priority network/UART tasks. Compile-time tests cover 50/100/1000-Hz
quantization and saturation without mapping a finite wait to an infinite one.

LoRa remains a deferred historical sketch, by maintainer priority. ESP-NOW is the
primary radio path. Existing framing/CRC16 code is retained; it does not establish
an operational LoRa radio or regulatory qualification. No LoRa driver, new RF
mode or regional emission policy is activated by this migration milestone.


## ESP-NOW software data-path qualification

The native host test now traverses a real loopback TCP caster, **UG NtripClient**,
UG gateway ownership/CRC validation, the unchanged ESP-NOW fragment wire format,
reassembly and a raw Linux PTY. It verifies nominal byte order, an absent fragment,
CRC16 corruption, and an RTCM payload with repaired fragment CRC16 but invalid
RTCM CRC24Q. Corrupt/partial frames never reach the PTY. This is a software replay,
not a new physical radio/USB qualification. Historical codec tests are also replayed
against the current native components (12 C groups and 11 Python cases).

Radio queueing preserves the original source receipt timestamp rather than
refreshing its age. Source invalidation clears queued transmissions; every next
fragment checks epoch and age. A missing MAC callback retains the existing TX
quarantine; no later packet can consume its delayed callback. Receiver counters
separate CRC16/CRC24Q rejection, completed RTCM frames, expiry and actual USB bytes.
They are read as snapshots; the rover never prints diagnostics into RTCM. S3
status exposes radio state/channel/queue/drop/TX counters without revealing MACs.
MAC acknowledgements are not proof of Linux USB delivery.

`radio.enabled=false` stops accepting new frames immediately and clears pending
radio frames through the control owner. In-flight RF bytes cannot be retracted.
Enabling a radio that was disabled at boot still requires reboot; peer/channel
changes are boot-applied and never select an access point's channel implicitly.
The build recipe accepts explicit `--peer-mac` and `--channel`; omitted peers are
fictitious compile-only values. No real/private MAC is committed.

Linux validation tools are now maintained under `embedded/esp32/tools/`:

```bash
python3 embedded/esp32/tools/check_rtcm_usb.py /dev/serial/by-id/<identified-rover> --frames 100 --timeout 60 --output /tmp/new-private-capture.rtcm
python3 embedded/esp32/tools/check_rtcm_capture.py /tmp/reference.rtcm /tmp/new-private-capture.rtcm
```

The first command opens only the explicitly supplied port, reads binary data and
reports CRC/noise/count/rate. Capture output is private and created exclusively;
existing files are never overwritten. The second command compares finite captures
including order, missing/extra frames and integrity. The independent Python CRC
is a host verifier, not another firmware CRC engine. Five new PTY/parser/tool
regressions pass. ROM boot bytes remain distinct from application logs and are
not silently discarded by these strict tools. No physical port was opened during
this migration. UG Linux/ROS2 consumers remain unchanged.


### UART resource correction

The inherited UART RX37 default conflicts with the configured octal PSRAM. The
[Espressif hardware guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html)
reserve GPIO33–37 in this memory mode. Native GNSS therefore starts disabled,
with no assigned TX/RX pins, even when the driver is compiled. An explicit,
validated pin pair and enable operation is required. The board resource mask
excludes flash/PSRAM, W5500, USB and strapping pins; tests cover those exclusions.
A mask is not a claim that a receiver is physically wired or voltage-compatible.
No physical hardware or installed firmware was touched while correcting this.
The setup page exposes the allowed candidates and the chosen UART/model/baud.


Latest ESP-NOW/UART milestone (compile-only): generated app binaries fit their
unchanged OTA slots: S3 gateway **1399920 / 3145728 bytes**, C3 relay **1284352 /
1310720 bytes**, C3 rover **682512 / 1310720 bytes**. Exact bytes are rechecked
against the generated partition table and ELF by `native/verify.py`; these budgets
do not constitute runtime heap/stack measurements. Sixteen host CTest cases,
five Linux capture-tool cases, the browser mock and historical codec regressions
pass. Native radio disable/expiry/counter code compiles on both chip families;
its physical timing/USB behaviour remains HARDWARE_PENDING. Start strict binary
capture after ROM boot and before enabling the correction source; an arbitrary
mid-frame start is deliberately not silently discarded.

## Private NTRIP trust and client identities

Native TLS endpoints can reference `nvs://ug_tls/<name>` for their CA bundle,
client certificate and client private key. Names contain 1–15 ASCII letters,
digits, `_` or `-`; namespace/path traversal is rejected. This is an ESP-IDF
adapter storage URI, not a Linux filename contract. The existing host filesystem
backend is unchanged. No SPIFFS driver/mount/automatic format is needed on C3.
Certificates are bounded to 4096 bytes and private keys to 2048 bytes; the parser
rejects malformed PEM and weak RSA/ECC keys before storage. NVS capacity remains
finite; a full partition returns an error, never an automatic erase. Production
flash protection remains a separate deployment prerequisite.

Use authenticated HTTPS `/api/tls/object` or the local console command
`tls-object <JSON>`. The request contains `name`, `kind` (`certificate`,
`private-key` or `delete`), PEM when adding/replacing, and explicit `confirm`:
`REPLACE_TLS_OBJECT` or `DELETE_TLS_OBJECT`. There is no read/export API for
private objects. The UI clears the PEM field on success/logout. Updating objects
invalidates the upstream source and restarts NTRIP/caster connections without
reinterpreting an old correction as a new observation. Already transmitted bytes
cannot be recalled. The HTTPS server's own identity still changes only at reboot.

SDK-seam tests cover private-bundle loading, invalid namespace/traversal, absent
storage, oversized blobs and embedded NULs; strict settings and the browser mock
also pass. These are distinct from a real ESP TLS handshake. An additional attempt
to run the SDK's Mbed TLS package as a standalone host library was not executable:
the image omits the framework and standalone builtin bignum header. No vendor
files or compiler/security checks were altered to force that optional experiment.
The actual firmware crypto API compiles on S3 and C3; runtime TLS/NVS power-failure
qualification remains pending.

Early HTTP errors close the session rather than letting ESP-IDF purge an
unbounded unread body after authentication/size rejection. GET bodies are
rejected, and accepted JSON requests retain their aggregate read deadline.
The native ELF verifier also checks that copied, compiled `main` files and its
CMake declaration match the current source, detecting stale external build copies.

The HTTPS worker runs at priority 2, below correction routing (5) and UART/radio
owners (4). This protects scheduling priority, not a claimed physical latency
bound. TLS-disabled TCP connections do not read stale/unused certificate settings;
the SDK-seam regression covers that case as well.

Historical multislot audit: `main/include/ntrip_slots.h` defines only a SERVER
role, and `ntrip_runtime.c` fans out to five independent uploaders. The native
single upstream plus five S3 publishers therefore preserves the implemented
model; five simultaneous incoming streams were not an existing feature. The old
client's GGA source was live UART text, not a configured manual/reference position.
The native source uses fresh UG observations instead. Manual reference GGA for a
GNSS-free VRS client is a future extension; it must not fabricate a physical GNSS
observation to satisfy an injection API.

## Constrained relay resource capacity

The C3 relay compiles the same gateway engine with two sinks (ESP-NOW and one
publisher), rather than reserving the S3's eight. It allocates one publisher
outbox and excludes the unavailable caster worker/outbox. Runtime startup rejects
capabilities larger than the compiled capacity; no requested slot is silently
ignored. The complete gateway contract is also compiled/tested with two sinks,
including out-of-range rejection, source invalidation, queue saturation and age.

IDF 6.1 builds pass for S3 and C3. The S3 application remains 1403744 bytes; C3
falls from 1288384 to 1281712 bytes in its unchanged 1310720-byte app slots.
The C3 ELF contains a 2204-byte publisher outbox and a 4792-byte gateway, with no
caster worker or caster outbox; SDK size reports 68576-byte BSS. These are static
link budgets, not measured runtime heap/stack headroom. The common-root bundle
remains necessary for this profile; trust verification and private CA support
are unchanged. Seventeen ASan/UBSan host cases pass. Rover dependencies, network
isolation and physical qualification are unchanged.

## UART ingress continuity and fairness

The GNSS owner drains at most eight 512-byte batches per turn and then yields.
Only the first read can wait; later reads are nonblocking. This removes the
previous one-batch-per-tick artificial ceiling without claiming physical UART
throughput. Each received batch keeps its actual monotonic receipt time.

The SDK UART event queue is examined before and after reads. FIFO/ring-buffer
full, parity/frame/break errors, read errors or a saturated event queue reject the
ambiguous batch, discard local input, invalidate receiver observations/model and
change the correction source incarnation. A full event queue is conservative
because SDK ISR notifications can otherwise be dropped. A failed local flush
keeps reads failed. These resets never clear a pending command's quarantine or
persistent mutation journal, and never establish a receiver-side prior-byte
cutoff. Bytes already transmitted cannot be recalled.

Authenticated GNSS diagnostics expose overflow, line-error, event saturation,
read-failure and drain-budget counters. Nineteen ASan/UBSan host cases pass,
including the actual SDK adapter against driver/queue stubs and a lost-input
pending-command regression. The S3 IDF 6.1 build passes; electrical behaviour,
maximum-rate loss/latency and real UART faults remain HARDWARE_PENDING. The
GNSS-free relay and rover do not link this adapter.

## Receiver-reported F9P Survey-In diagnostics

The existing UG protocol library now provides an additive `ParseUbxNavSvin`
decoder for the manufacturer's [UBX-NAV-SVIN v0 layout](https://content.u-blox.com/sites/default/files/documents/u-blox-F9-HPG-1.32_InterfaceDescription_UBX-22008968.pdf),
section 3.15.21, pp.156–157. It validates checksum, length, version, flags,
navigation-week time and high-precision residuals. ECEF centimetres plus signed
0.1-mm residuals are retained exactly as integer 0.1-mm coordinates. Existing
Linux/ROS2 runtime mappings are unchanged; this status does not create a GNSS
position, RTK FIX or correction observation.

The native owner polls a verified F9P once per second when no configuration is
pending/quarantined. It does not modify receiver output settings. Status exposes
the reported duration, observation count, accuracy, ECEF mean and active/valid
flags, with a three-second receipt freshness limit. Repeated reports with the
same navigation iTOW retain their first receipt time; polling cached data cannot
renew observation freshness. Reconfiguration, discontinuity and a new BASE
request discard earlier survey status. Receipt and GNSS clocks are never compared.

A fresh valid/inactive report describes the receiver's reported state, **not**
proof that the most recent request caused it. The API explicitly keeps
`confirms_current_request=false`; no hardware byte-cutoff or command-to-survey
causality is inferred from UART reset/ACK. Unicore TIME status is not silently
mapped to F9P Survey-In semantics.

Twenty ASan/UBSan host cases, 69 native CTests, the browser mock and S3/C3 IDF 6.1
builds pass. Tests include malformed/truncated/wrong-version/CRC frames, signed
coordinate extrema, fragmented reports, cached iTOW freshness and command reset.
Current apps: S3 1406928 bytes; C3 relay 1281856 bytes. No receiver hardware or
new physical Survey-In qualification was performed.

## Typed UG receiver telemetry profiles

Authenticated POST `/api/gnss/telemetry/apply` accepts `rate_hz` (1, 2, 5 or 10),
optional `receiver_port` (1/2 for F9P), optional complete `constellations` object
(`gps`, `galileo`, `beidou`, `glonass` booleans), or Unicore `signal_groups`
(one/two model-qualified IDs). `confirm` must be `APPLY_GNSS_TELEMETRY`.
`persist=true` additionally requires
`persistent_confirmation=SAVE_RECEIVER_CONFIGURATION`. Unknown/duplicate fields,
unsupported rates/models/groups, cross-vendor options and disabling every
constellation are rejected. The UI provides the common telemetry/rate operation;
advanced signal selection uses this typed API rather than raw command text.

The F9P plan uses the existing UBX builder for navigation rate plus PVT, SAT,
STATUS, DOP and GGA. Only explicitly requested constellation flags are changed.
The Unicore plan uses existing UG model tables and command builders for GGA/GSV,
GST, PVTSLNA, BESTNAVA, RTK/RTCM status and satellite information; requested rates
apply to output periods, not an invented universal navigation-cycle setting.
Unicore output commands use the current command port: explicit alternate-port
selection is not qualified and is rejected. Base-only/low-power signal presets
are also rejected until their mode preconditions can be established.

The convenient UG Unicore diagnostics preset derives from a ROVER preset, so the
native BASE adapter deliberately builds only the requested telemetry operations.
It emits no implicit MODE, baud change, factory reset or RTK timeout mutation.
All requested commands abort on failure; an unsupported output is not reported
as successfully applied. Runtime-only is the default. Persistence still uses UG
safety approval and the same UART owner, single operation queue and NVS journal.
BASE requests also expose the existing UG `receiver_port` 1/2 selector for RTCM.

Local corrections are suspended before receiver mutation, with pending frames
invalidated. An indeterminate command or persistent journal keeps that local
source disabled, including after reboot; network-only NTRIP relay remains
independent. Successful acknowledgement clears the journal before local routing
can resume. No receiver-side byte cutoff or physical output qualification is
inferred from this software source boundary.

Twenty-one ASan/UBSan host cases and the browser mock pass. Tests verify all four
supported model plans, no implicit BASE-to-ROVER change, no unsolicited
constellation keys, approval, command safety and negative input/model cases.
S3 and C3 IDF 6.1 builds pass (1419824-byte S3 app; 1283392-byte C3 relay); physical receiver acceptance and
maximum UART bandwidth at each selected rate remain pending.

## Service supervision and degraded startup

Gateway/relay startup now follows explicit dependencies. Failed network-core,
router, Ethernet, radio or NTRIP setup does not skip independent GNSS/console
services. Failed publisher allocation no longer skips the remaining independent
slots. Radio starts before the administration services, preserving correction
resource priority. Unavailable TLS identity is reported as HTTPS startup failure;
there is still no unauthenticated/plaintext fallback or automatic NVS erase.

Public status and local `status` show bounded per-service lifecycle/error,
worker entry, progress age/staleness, watchdog registration/errors and sampled
minimum free stack bytes. Startup success is not a network link, a connected
caster, GNSS fix or physical correction delivery. Worker progress is not another
GNSS observation. The registry uses only one IDF monotonic clock and fixed slots;
reading status never refreshes progress or a watchdog.

The routing, GNSS and radio-control owners register/feed their own SDK task
watchdog; failures remain visible. Network workers have a 30-second progress
alarm, without a task-watchdog subscription that could mistake DNS/TLS waiting
for a CPU hang. Bounded owner progress uses a five-second diagnostic threshold.
The existing SDK watchdog initialization, timeout and panic policy are unchanged:
the current default is five seconds with panic disabled, **not a newly qualified
automatic reboot/recovery mechanism**. A missing task entry, backwards clock,
unexpected worker exit or watchdog error cannot retain a healthy cached status.
No monitor restarts workers, clears a receiver journal/quarantine or feeds another
task. Watchdog resets and startup faults on physical boards remain pending.

Twenty-two ASan/UBSan host cases pass, including injected startup dependency
failure, independent-service retention, startup/task-entry races, zero stack
headroom, stale progress and SDK watchdog errors. S3/C3 IDF 6.1 builds pass:
1422448-byte gateway and 1286368-byte relay apps in unchanged OTA slots. These
are link budgets; live TLS concurrency/heap margins still need board evidence.
A fresh C3 rover build passes at 682512 bytes and its ELF verifier rejects
gateway/admin/supervisor dependencies. The browser mock also passes.

## Explicit legacy configuration conversion

`native/tools/migrate_legacy_config.py` prepares a private, offline native schema1
candidate and review report from the historical `/config` JSON and optional full
`/api/ntrip` JSON. It targets the contract at the pinned historical commit, not
an authenticated firmware backup. Source hashes and the validator binary hash
are recorded. Original files are read-only; the tool never opens serial/network,
reads/writes NVS, applies configuration, reboots or flashes.

Build the actual native settings validator with the host-test CMake project,
then use an external private directory:

```bash
cmake --build /tmp/ug-native-host --target ug_validate_settings
python3 embedded/esp32/native/tools/migrate_legacy_config.py --legacy-config /private/legacy-config.json --legacy-ntrip /private/legacy-ntrip.json --validator /tmp/ug-native-host/ug_validate_settings --board waveshare-s3-eth --role gateway --output /private/new-ug-review
```

The candidate disables upstream, publishers, caster, Wi-Fi, GNSS and radio.
Review it before manually importing it through the authenticated native config
UI/API. These are requested settings, not proof that boot-only Wi-Fi/radio changes
have taken effect; the documented explicit reboot rules still apply. Provision
UG administration separately; old authentication is never reused. Candidates may
contain supplied plaintext secrets: output directory mode700 and files mode600,
new paths only, outside UG. Neither output file belongs in Git.

Masked passwords (`CONFIG_VALUE_UNCHANGED` or `********`) are not credentials:
the candidate clears them and reports required re-entry, without retaining an
unrelated old UG password. Native caster credentials stay paired. Flat publisher
keys are deliberately not treated as authoritative because the historical
`ntr_slots_v1` store can supersede them; all five publication records come from
`/api/ntrip`, or remain empty/disabled. All five are preserved even for a C3,
whose actual native validator prevents enabling unsupported slots. TLS flags
from the slot export are retained; upstream TLS/GGA require explicit review.

Hardware pins/models, BASE coordinates/datum/survey presets, admin credentials,
TLS identities, LoRa and unsupported settings are **not** guessed/imported.
Untransferred flat fields, slot metadata, source metadata and previously enabled
services are listed for review. This is not automatic NVS migration or complete
restore of masked secrets/hardware state. The old `config` namespace and its
binary ABI are untouched; UG keeps its existing `ug_config` banks/journal and
never ports the historical automatic NVS erase fallback.

The tool rejects duplicate/nonfinite/over-budget JSON, incomplete/duplicate slot
exports, unsafe destinations and invalid native candidates before creating any
output. It calls the compiled UG parser rather than duplicating its validator.
Twenty-three ASan/UBSan host cases pass; the new Python case runs seven synthetic
end-to-end/negative tests. Firmware payloads did not change in this tooling phase,
so the preceding S3/C3/rover builds are reused rather than rerun.

## Bounded satellite reports and GNSS diagnostics

Authenticated `/api/gnss/satellites` and the native Web table expose the latest
available UG-decoded UBX-NAV-SAT, Unicore SATSINFOA or NMEA GSV report. Each view
retains its protocol/ID namespace, raw system IDs, optional signal scope and
software receiver incarnation. Unknown used/health values are omitted rather
than fabricated. GNSS status also exposes available UG RTK/accuracy/DOP/C/N0
and correction-age values, alongside its existing position-staleness indicator.
These are receiver reports; no position, RTK FIX or physical observation is
created by reading/publishing the satellite cache.

Vendor reports have separate caches. Repeating the same iTOW/week/time-reference
keeps the original receipt freshness; GPS/BDS epochs are never compared as one
clock. GSV has no measurement epoch: its explicit freshness basis is report
receipt, not proof of a current physical navigation solution. Fresh vendor
reports are preferred over GSV; stale data remains labelled and is never renewed
by a status request. Discovery consumes UART outside Feed, so its byte gap now
invalidates parser/report state without clearing any command quarantine.

GSV assembly is bounded to eight concurrent talker/signal scopes, a three-second
receipt window and sequential matching parts. Missing/reordered parts, changed
headers and duplicate satellite IDs invalidate a cycle. The public UG GSV record
does not retain the trailing signal ID, so the native adapter reads only that
metadata from the already framed/checksummed sentence; all satellite values and
CRC checks still come from UG. Its hexadecimal signal field follows the
[documented NMEA GSV layout](https://content.u-blox.com/sites/default/files/documents/u-blox-F9-HPG-1.32_InterfaceDescription_UBX-22008968.pdf),
section 2.7.15. A sequence without an epoch cannot prove that an entire missing
cycle was not skipped; no stronger cycle/measurement guarantee is claimed.
Views contain at most 64 entries, with explicit truncation. They are individual
reports, not a merged full sky across every signal/constellation. Native detail
framing bounds are 512-byte NMEA and 4096-byte Unicore ASCII.

Source invalidation clears caches/partial cycles; pending receiver mutations
hide previous satellite reports. Browser polling is single-flight, uses a
10-second request timeout, and discards private late responses after logout.
Rendering uses text nodes, without HTML injection or browser credential storage.
Twenty-four ASan/UBSan host cases and the browser mock pass, including real UG
NMEA/UBX/Unicore wire parsing, CRC rejection, interleaved signal scopes, capacity,
cache freshness, reset, no synthetic fix and delayed-logout response handling.
S3/C3 IDF 6.1 builds and ELF checks pass: apps 1431488 / 1289040 bytes. Rover code
is unchanged. Actual receiver throughput, completeness of real-device reports
and browser/HTTPS interoperability remain HARDWARE_PENDING.

## Read-only rediscovery and model capabilities

Authenticated POST `/api/gnss/detect` accepts only
`{"confirm":"DETECT_RECEIVER"}`. The same single UART owner queues read-only
model discovery, invalidates state across bytes consumed by discovery and rejects
requests when the UART is disabled or a mutation is busy. It does not clear a
command/persistent quarantine, its reason, or reactivate a quarantined correction
source. The operator explicitly confirms the possible correction pause. No raw
command text, reset or persistence bypass is exposed.

Authenticated `/api/gnss/capabilities` computes available BASE modes by invoking
the actual UG builders without dispatch: verified F9P fixed/Survey-In, UM980/982
fixed/TIME, UM981 telemetry only under the currently documented contract.
Unverified/unsupported models receive no speculative BASE capability. Qualified
signal-group options come from the existing UG model tables. The UI gates forms
with these software capabilities; the owner still rechecks identity and safety
at dispatch. Software capability is explicitly distinct from hardware qualification.
Twenty-four host cases and the browser mock pass; S3/C3 builds pass at 1434512 /
1290496 bytes. Receiver hardware tests were not replayed.

A targeted historical OTA audit found no active `esp_ota_*` application calls in
`main/` at the pinned reference. Includes and the obsolete copied
`main/include/esp_ota_ops.hold` are not an implemented updater. That header is
SUPERSEDED by official SDK headers; this closes only the vendor-header inventory
item, not the future OTA function. Secure image/board/partition compatibility,
installation-vs-update policy and rollback qualification remain OPEN; no OTA or
flashing endpoint is exposed by this migration milestone.

## Native language and integration qualification

IDF 6.1 actually defaults to GNU C23/C++26; `cxx_std_17` alone expresses only a
minimum. Native UG-owned components and `main` now explicitly compile as GNU
C17/C++17. SDK and managed components retain their own SDK defaults, as supported
by the SDK's C++ guide. The verifier checks the **last effective** language flag
for every compiled UG C/C++ source and checks the copied native root CMake file.
A regression rejects missing flags or a later C++26/C23 override. No compiler
warning was suppressed and no functional UG/Linux/ROS2 interface was changed.

The three native roles build and pass ELF/partition/role checks under these
explicit standards. Applications, bootloaders and partition tables are byte-for-
byte identical across two output directories for each role. Current app budgets:
S3 GNSS gateway **1431520 / 3145728** bytes; C3 relay **1288400 / 1310720**;
C3 rover **682512 / 1310720**. Project source units include exactly one UG CRC;
relay excludes the GNSS driver and rover excludes network/config/admin/supervisor.
These reproducibility checks used prepared pinned dependency caches, the official
IDF image and a disconnected UID1000 build container; they do not promise future
registry availability or attest hardware/runtime performance.

Twenty-five native ASan/UBSan CTest cases, the QuickJS browser case, the existing
55 Python tests and 200 generator/CI tests pass. All eight ROS2 distributions
retain 13 manifest components, pass generation/integrity/reproduction and exclude
embedded/runtime/development content. Kilted was generated only; no new Kilted or
ROS2 isolated build, device test, flash, push or publication was performed.
Previously qualified Linux native tests remain 69/69; no portable source changed
since that run. Native TLS concurrency/live heap, real receiver acceptance,
watchdog reset behaviour, OTA/rollback and the official portal remain unqualified.
The full migration therefore remains PARTIAL.


## Explicit native settings rollback

Authenticated `GET /api/config/history` exposes generation identifiers as decimal
strings, availability and write-lock state; it exposes no previous secrets.
`POST /api/config/rollback` requires exactly
`{"confirm":"ROLLBACK_CONFIGURATION","expected_generation":"3"}` with the
currently inspected generation. The local console offers `history` and the same
JSON through `rollback`. Stale requests, missing/invalid banks and ambiguous
generations fail without a write. Both banks are CRC/schema-validated, active
bank first, so a corrupted active bank cannot destroy the sole intact older bank.
Unknown schemas or storage I/O errors lock further writes; nothing is erased.

The previous settings are saved through the normal readback-verified path as a
**new generation**, then published as an immutable snapshot under the same writer
lock. Current administrative credentials, separate TLS objects/identity and the
`gnss_dirty` journal survive. A failed commit leaves the current RAM snapshot
intact and locks writes. This restores **gateway settings only**: it does not
undo commands already executed by a receiver, restore its persistent BASE mode,
clear quarantine or authorize recovery. Boot-only settings still need an explicit
reboot. This is distinct from future firmware-image OTA rollback.

Twenty-six native ASan/UBSan cases and the browser mock pass, including stale and
numeric-generation rejection, active-bank corruption, admin/journal preservation,
immutable readers and failed commit handling. S3 and C3 relay builds and native
verifiers pass under C17/C++17. Applications, bootloaders and partition tables
remain byte-identical between two build directories for both affected roles.
The unchanged rover retains its previous proof. No hardware NVS/power-loss claim
is made.


## Memory-aware Web admission

Historical `memory_policy.c` and `ntrip_runtime.c` are ADAPT references, not
imported allocators or a second NTRIP supervisor. Native correction queues remain
fixed and the routing/receiver/radio owners keep their existing priorities.
`ug_resource_policy.hpp` evaluates current **internal** RAM independently of PSRAM.
Below 96 KiB free or a 32 KiB largest block, satellite-table polling is refused.
Below 48 KiB free or a 16 KiB largest block, all variable API snapshots/mutations
are refused with HTTP 503/Retry-After before body allocation or command dispatch.
Missing/inconsistent internal samples fail closed. Historical minimum-free is
diagnostic only; a past low-water mark cannot latch the device in degraded mode.

Public `GET /api/resources` renders a fixed 512-byte-stack JSON report without
a cJSON tree and remains available under pressure. It exposes only memory sizes,
pressure and admission flags. The browser polls it first, reduces optional work
and removes cached receiver/satellite claims while detailed polling is suspended.
The static assets remain served from flash. No socket, active command, source
epoch, TTL, security check or GNSS quarantine is changed by this policy.

These thresholds are conservative initial admission policy, **not an atomic memory
reservation**, a guarantee of future allocations, a complete socket/CPU QoS policy,
or a proof of simultaneous TLS capacity. SDK HTTPS handshakes happen before the
application handler; HTTP 503 cannot prevent their allocations. The local console
remains independent of the Web gate, but memory-consuming console operations may
still fail. No automatic reboot, erase or disconnect is introduced. Thresholds
need native-device heap/concurrency qualification before deployment confidence.

Twenty-seven host ASan/UBSan cases pass, including exact threshold boundaries,
fragmentation despite free PSRAM, recovery after past low-water mark, invalid
samples and the operation admission matrix. The browser mock verifies normal,
reduced and critical polling plus resumption and existing authentication guards.

Both affected SDK roles compile and pass the ELF/role/partition verifier; the
minimal rover does not include this Web policy and is unchanged. No physical
heap/concurrency or firmware flash test was performed.


## Bounded service event history

The historical `log.c` printf hook and destructive `/log` byte stream are REJECT
mechanisms: they could capture arbitrary SDK text, secrets and receiver data.
The useful diagnostic history is ADAPT as a fixed 32-entry typed service-event
ring. It records boot/worker state transitions, changed watchdog errors and worker
exits, using service/state/kind enums, numeric error codes and monotonic receipt
times. No arbitrary message, credentials, MAC, position, raw GNSS or format string
is accepted. Normal heartbeats and repeated identical watchdog errors do not flood
the ring. The supervisor's existing mutex protects bounded copies; no event
allocation, new task, flash write or SDK logging hook is added.

Authenticated `GET /api/diagnostics/events`, the WebUI button and local console
`events` return non-destructive chronological snapshots. Sequence, overwritten and
rejected counters are explicit; HTTP counters are decimal strings. The sequence
and ring reset at boot and are not durable audit evidence. Ages use only the SDK
monotonic clock; a backwards interval is flagged rather than fabricated. There is
no remote log erase, SDK log-level or unbounded stream endpoint. This service
history is not yet a complete GNSS/NTRIP transaction log. The old `/log` shape is
not emulated; operators use the explicit native endpoint.

Detailed event responses share the reduced-memory admission rule with satellite
reports. Failed field allocation returns 503 instead of a partial event response.
The browser blocks late private replies after logout. Host ASan/UBSan tests cover
ordering, wrap/loss, independent immutable snapshots, invalid enum/time rejection,
actual boot failures and watchdog transitions. The minimal rover remains free of
supervisor/Web dependencies and event/log output.

All 28 native ASan/UBSan cases and the browser mock pass at this milestone.
S3 gateway (1438864 bytes) and C3 relay (1296080 bytes) compile and pass role,
language, partition and linked-CRC checks. The existing slots are unchanged.
No physical test or firmware publication was performed.


## Owner-scoped NTRIP diagnostics and controlled restart

Authenticated `GET /api/ntrip/runtime` reports the incoming client, the board's
actual publisher capacity (five S3, one C3) and the S3 local caster. Each owner
publishes fixed-size metrics with its configuration revision, network generation
and monotonic sample receipt. Samples older than five seconds, unsampled owners,
link loss, a changed link/configuration or a backwards clock cannot report an
active connection. The public status uses the same projection. A healthy worker
heartbeat is not evidence of a connected caster or fresh corrections.

`upstream_flowing` continues to use **UG's existing correction-flow timeout** and
is additionally masked when its owner sample is not current/connected. No second
flow clock or last-frame observation is invented. Counters remain diagnostic
history when samples are invalid. The incoming `reconnect_count`, received bytes,
frames and GGA counts have the lifetime/reset semantics of the actual UG client;
bytes include received protocol traffic, not only RTCM payload. Publisher counters
are their owner's lifetime SOURCE-v1 connects, complete frames/bytes, failures and
expired frames. Caster counters are accepted clients, rejected requests, served
frames and slow-client disconnects; no unmeasured caster-byte value is exposed.
Large counters/revisions are decimal strings, not lossy JSON doubles. No endpoint
contains hostnames, mountpoints, credentials or private MAC addresses.

Authenticated `POST /api/ntrip/restart` accepts exactly
`{"confirm":"RESTART_NTRIP_SERVICES"}`. It uses the existing cancellation/network-
generation mechanism, immediately invalidates the incoming correction source and
requests all network owners to reconnect asynchronously. HTTP 202 is an accepted
request, not proof of completed reconnection. Receiver configuration, quarantine,
NVS, Ethernet/Wi-Fi settings and radio channel are unchanged. Existing config
patches remain the way to enable/disable selected slots; old flat slot APIs are
not silently emulated. The UI offers explicit read/restart actions with session
guards. Memory admission still applies before body allocation/dispatch.

The host regression verifies exact sample-age boundaries, stale link/revision,
missing samples, backwards clock, counter retention, immutable projection and
strict restart confirmations (including duplicate/extra keys). Browser tests
exercise the request shape and logout clearing. No live reconnect or hardware
network qualification is claimed.

All 29 native ASan/UBSan cases and the browser test pass. S3 gateway
1443136-byte and C3 relay 1300144-byte images pass the native ELF/role/partition
checks. The C3 relay now has only 10576 bytes of application-slot margin; further
features require measured size/admission review, never silent repartitioning.


## Deterministic compressed Web assets

The native Web component embeds only build-generated gzip representations of
`index.html`, `app.js` and `style.css`, from an explicit three-file allowlist.
The source assets remain readable and browser-tested. No timestamps, original
paths or filenames enter gzip headers. The pinned SDK/Python/zlib environment
defines the compression implementation; no cross-zlib reproducibility promise is
made. CMake regenerates assets when a source or the packer changes, and uses
stable linker symbol names. All generated files stay in the external build tree.
The verifier decompresses each generated representation, compares it with the
current source and requires it to occur exactly once in the linked application.

Static routes return `Content-Encoding: gzip`, their original media type and
`Vary: Accept-Encoding`, with existing CSP/authentication/cache policies. A client
explicitly refusing gzip gets 406; absent Accept-Encoding expresses no preference
(RFC 9110). The bounded parser handles gzip/wildcard weights, explicit exclusion
and conflicting duplicates conservatively. API responses remain uncompressed and
unchanged. Decompression runs in the browser, with no firmware inflate buffer or
additional network/routing task. A real browser/TLS session on native hardware is
still pending; source-level browser tests are not that qualification.

Current source assets total 20869 bytes and their gzip payloads total 7585 bytes.
Before compression, the C3 relay app was 1300144 bytes with 10576 bytes of slot
margin. The measured linked reduction includes the HTTP negotiation code and is
reported by the native verifier. This saves flash without changing either board's
partition map, NVS, firmware role, radio protocol or public GNSS contracts.
Host tests cover exact decompression, independent output paths, allowlist, gzip
headers, stale/missing/duplicate linked representations and encoding negotiation.

Final apps are **1430832** bytes (S3) and **1287808** bytes (C3 relay): net
reductions of 12304 and 12336 bytes respectively, after negotiation code. C3 slot
margin rises to **22912** bytes. The 31 native ASan/UBSan cases pass. App,
bootloader and partition binaries are byte-identical in two independent build
directories for both affected roles; the unchanged minimal rover retains its
previous reproducibility proof. No hardware/browser deployment is claimed.


## Optional C3 rover UART output

USB Serial/JTAG remains the rover default. A native C3 rover can instead select
UART1 TX only at build time, with an explicit GPIO from the conservative
`4,5,6,7,10` allowlist and baud `115200,230400,460800,921600`, 8N1 and no flow
control. Strapping pins, flash pins and USB GPIO18/19 are not selectable. This
is a software pin policy, **not** approval of a connected board, peripheral or
voltage. Wiring and receiver-level compatibility require physical review.

```bash
python3 embedded/esp32/native/build.py --output /tmp/ug-rover-uart --profile rover --board supermini-c3 --rover-uart-tx 4 --rover-uart-baud 115200
python3 embedded/esp32/native/verify.py --work /tmp/ug-rover-uart --profile rover --rover-uart-tx 4 --rover-uart-baud 115200
```

The recipe rejects invalid board/role/pin/baud combinations before creating
output; native CMake also checks the UART selection. The UART driver uses no TX
ring, disables unused RX interrupts, assigns no RX/RTS/CTS pins and uses the
SDK's nonblocking `uart_tx_chars`. A pre-start-only writer registration selects
the backend before ESP-NOW initialization. The custom backend does not install
the USB Serial/JTAG output driver. No runtime Web/NVS/GNSS service is added to
the rover. Radio framing, fragment CRC16, reassembly, TTL and the single actual
UG CRC24Q implementation remain unchanged.

USB and UART share the same borrowed-buffer output pump, with a 200-ms enqueue
budget, at most 20-ms requested waits and an additional 4096-call guard for a
faulty clock/yield implementation. Partial writes preserve byte order; invalid
writer counts and errors terminate the attempt. No padding, logging or retry
of an already accepted prefix is introduced. Driver acceptance is **not physical
delivery**; scheduler stalls or an unavailable output can still truncate a byte
stream, including bytes already queued in a hardware FIFO. This is not a hard
real-time or end-to-end atomic-frame guarantee. `output_bytes/frames/errors`
count driver acceptance for either backend; existing `usb_*` counters retain
USB-only meaning. No counters are printed into RTCM.

For an explicitly identified external USB-UART adapter on Linux, the existing
strict capture tool now offers an optional host-side baud:

```bash
python3 embedded/esp32/tools/check_rtcm_usb.py /dev/serial/by-id/<identified-adapter> --baud 115200 --frames 100 --timeout 60 --output /tmp/new-private-uart-capture.rtcm
```

Without `--baud`, its existing USB behaviour is retained. With it, termios sets
the selected speed, 8N1 and no hardware/software flow control, and restores the
original attributes on success or failure. Unsupported baud is rejected before
opening a device. These examples were not executed against physical ports.
Adapter modem-control/reset effects remain part of separate hardware
qualification; no physical port was opened in this software-only migration.

Validation: the previously completed **33 native ASan/UBSan cases** include pump
partial/stalled/error/oversized-return/clock-wrap/frozen-clock tests and the actual
UART adapter's setup, nonblocking call and failure cleanups. **Eight capture-tool
tests** now pass, including four PTY baud variants, order/CRC/noise, restoration
on timeout and no open for invalid baud. SDK 6.1 builds and linked-backend/role/
partition/log checks pass for USB rover, UART rover, S3 gateway and C3 relay.
Independent USB and UART builds have byte-identical apps, bootloaders and partition
tables. Final app sizes at this milestone: S3 **1430896 / 3145728**, C3 relay
**1287808 / 1310720**, C3 USB rover **682816 / 1310720**, C3 UART rover
**694288 / 1310720** bytes. No partitions or SDK dependencies changed.

Physical acceptance remains HARDWARE_PENDING: identify the exact C3 and adapter,
verify 3.3-V UART compatibility/common ground and selected free GPIO before any
wiring or authorized flash, then replay finite valid RTCM over S3 Ethernet/ESP-NOW,
compare captured UART bytes/order/CRC with the source, and measure drops/rate plus
boot/reset behaviour. Any board, pin, baud, firmware, adapter or topology change
invalidates that physical baseline. No such trial, endurance run, flash or RF
claim is part of this software result. The full migration remains PARTIAL.


## Aggregate native network capacity

The actual IDF 6.1 default was ten sockets on both boards. This was insufficient
for the S3's permitted concurrent services: one incoming client plus five
publishers, HTTPS with three client sessions plus three internal sockets, and
the local caster with a listener plus four clients already require **17 FDs**.
The caster can accept one extra FD before rejecting an over-limit client. Two
additional descriptors are reserved, giving a provisioned S3 budget of **20**.
The C3 relay retains **10**: two NTRIP endpoints, six HTTPS descriptors and two
reserves. No publisher, caster client or HTTPS session limit was reduced.

The pinned mDNS component is currently configured with its raw-lwIP backend: it
shares one UDP PCB across interfaces/address families, without a socket
descriptor. The pinned lwIP DNS resolver can use four source-port PCBs; SNTP
uses one. Three additional PCBs conservatively cover shared DHCPv4/DHCPv6
clients and the single Wi-Fi AP DHCP server. Together with two HTTP controls
and two reserves, the UDP budget is **13**. DNS capacity is derived from the
effective lwIP options, not assumed to be one request. The optional mDNS socket backend instead uses one dual-stack FD per interface;
the policy accounts for it and would require a larger socket allocation. It is
not silently enabled. Caster/HTTPS listeners and active TCP PCBs are checked
separately (S3 active budget 16, relay 7; listeners 2/1). Current SDK pools remain
16 for active TCP, listening TCP and UDP.

`ug_network_budget.hpp` owns the three-HTTPS/four-caster client constants used
by the actual service configurations. The native network source statically
asserts its aggregate requirements against the effective `sdkconfig.h`; a build
with a missing reserve or insufficient FD/TCP/listener/UDP pool cannot succeed.
The recipe explicitly provisions 20/10 sockets, and the verifier reports the
actual SDK limits. Public capabilities expose these **static** limits, never an
assertion of current free sockets or available TLS capacity. The minimal rover
acquires no network budget, service or provisioning change.

This is necessary resource accounting, not a guarantee against allocation failure,
TCP TIME_WAIT pressure, DNS/reconnect timing, live heap exhaustion or three
simultaneous TLS handshakes. Existing bounded queues, cancellation/source epochs,
worker failures, memory admission and service degradation remain in force. Live
peak heap/stack/network concurrency still needs qualification without changing
GNSS observations, radio framing or core NTRIP semantics.

Host ASan/UBSan tests cover both board budgets, exact thresholds for all four
pools, invalid capabilities, varied DNS capacity and optional socket-backed mDNS.
Native recipe/verifier policy tests preserve the C3 limit and rover exclusion. Fresh
S3/C3 builds pass with their effective limits, without repartitioning, changing
SDK dependency versions or using a device.

All 34 native ASan/UBSan cases pass. Compiling the actual SDK network service
with each insufficient pool in turn (FD 19, active TCP 15, listeners 1, UDP 12)
fails at the intended static assertion. No guard or test was weakened.

Final apps are S3 **1431152 / 3145728** and C3 relay **1288240 / 1310720**
bytes, with byte-identical app/bootloader/partition files in two build directories.
SDK size reports show linker-region occupancy of S3 DIRAM **212300 / 341760**
and C3 DRAM **159686 / 321296** bytes. These are static link budgets, not measured
runtime heap headroom after task stacks/TLS/network allocation. Rover images and
Linux/ROS2 payloads are unaffected by this network-only provisioning change.
