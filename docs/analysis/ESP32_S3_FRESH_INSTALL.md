# ESP32-S3 fresh-install HTTPS — P0 qualification

Status: **PARTIAL / HARDWARE_REQUIRED**. Branch: `fix/esp32-fresh-install`.
Base: main `cf02bede33e71b51ed9335b03c2c4af6535cd406`.
The maintainer authorized commit/push for review. No merge, tag, Release or new
hardware operation is included in that publication of the source branch.

## Cause and correction

The supplied blank-flash log showed `identity_missing`, SDK4354
(`ESP_ERR_NVS_NOT_FOUND`), service28161 (`0x6e01`), Ethernet running and both Web
ports closed. Previously `StartWebService()` only loaded `ug_identity/https`;
USB commissioning `tls-init` was its only provisioner. Software tests covered
provisioned HTTPS and policy mocks, not real autonomous blank-NVS generation.
The earlier physical repairs had not qualified this fresh-install case.

The S3 HTTPS boot now prepares its identity before console/network/radio owners:

- Retain a readable valid identity without rewriting it.
- Generate a unique P256 key and self-signed certificate for missing/invalid identity.
- Use hardware startup entropy around the first PSA initialization, load,
  generation, signing, persistence and readback; disable that entropy window before
  normal radio/ADC owners start. No Wi-Fi connection is needed to generate TLS.
- Store the existing single certificate/key blob atomically, check SDK errors and
  read it back. Temporary secrets are wiped. The checked6144B workspace is PSRAM;
  no silent internal-SRAM fallback. Read/crypto/OOM faults do not authorize replacement.
- Report stage, classified failure, SDK error, time/CPU, internal/PSRAM minima,
  largest block and boot-stack margin. Public DER fingerprints are available via
  USB for binding the network certificate to the identified board.
- Never start plaintext HTTP as recovery from HTTPS failure. HTTP is an explicit
  separate S3 build. Admin diagnostics now include the actual password record,
  rather than inspecting only the historical token hash.

The certificate has a random128-bit serial, CN/SAN `ug-<STA MAC>.local`, CAfalse,
digital-signature usage and2020–2049 validity to avoid an NTP prerequisite. It is
self-signed, not publicly trusted. DHCP IP/mDNS aliases may not match its SAN;
browser acceptance is an operator action. A custom certificate may be provisioned.
See [ESP-IDF RNG](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/system/random.html).

## Identity, CI, Flasher and update boundary

Native builds record UG tag version, full source SHA/tree, hardware/profile,
transport, SDK and modified-source flag. Boot/WebUI no longer say only development.
An untagged development source says `unversioned` without blocking branch builds.
Local review images are explicitly modified and cannot be packaged as published builds.
CI derives the real immutable commit and does not execute branch code on the host
just to inspect its supported profiles. Old immutable Releases keep their original
builder arguments and tests; no historical tag/archive is rewritten.

The native matrix is S3HTTPS, S3HTTP, C3USB and C3UART. Existing gates/check names,
isolation and non-root execution remain. HTTPS CI additionally exercises actual
SDK crypto/NVS in QEMU. Formal Release retains its historical HTTPS-S3/USB-C3 pair;
branch previews support the explicit HTTP variant. Publication workflows, existing
archives, catalogue contents, C3 source and functional versions are unchanged.

The Flasher distinguishes HTTP/HTTPS and identifies legacy builds requiring USB
TLS commissioning. Full installation warns about NVS erasure. The preserving-update
selector **remains disabled**, including when forced through the DOM. The offline
`native/tools/prepare_update.py` requires a hash-verified16MiB snapshot, matching
current app, unchanged bootloader/table, recognized S3 image and determinate OTA
selection. It proposes only the active application span. Browser USB compatibility
integration and power-loss-atomic update are not qualified by this offline tool.

Hardware exposed a CRC seed defect in that new preflight: it safely rejected a
valid OTA record before any write. It now uses the SDK6.1 ROM convention
`zlib.crc32(sequence, 0xffffffff)`, checked against independent seq1/seq2 vectors
(`0x4743989a` / `0x55f63774`), including real public OTA metadata. Five regressions
pass, including rejection of the former wrong-seed CRC and invalid/pending states.

## WebUI restart controls and receiver query

The dashboard adds confirmed ESP32 reboot. It rejects pending GNSS work and
subsequent mutations, sends the reply before restarting, then returns to login.
No settings, identity, partition or receiver factory reset is requested.

GNSS/BASE adds a confirmed GNSS-only hot restart for an actually verified ZED-F9P:
UBX-CFG-RST with navBbrMask0/resetMode2, preserving BBR, UART and configuration.
The existing single UART owner dispatches it without retries; observations and
local correction epochs are invalidated and routing suspended. The journal remains
until read-only identity verification; faults/timeouts retain quarantine. Existing
quarantine cannot be cleared by this command. CFG-RST has no reliable ACK: dispatch
and later communication do not prove a physical reset or trusted prior-byte cutoff.
See [u-blox integration manual](https://content.u-blox.com/sites/default/files/ZED-F9P_IntegrationManual_UBX-18010802.pdf).

The physical exercise also exposed an ESP32 query-selection bug: generic NMEA
passive detection prevented the explicit F9P selector from sending MON-VER. The
native selector now chooses the configured read-only vendor query for NMEA/unknown
without treating configuration as identity. Conflicting vendor evidence is retained.
The shared driver/protocol implementation is unchanged.

## Software evidence

| Validation actually executed | Result |
| --- | --- |
| Native ASan/UBSan | 79/79; affected six contracts replayed |
| Generator / CI policy | 309/309 |
| Existing Python scripts | 55/55 in isolated non-root Lyrical fixture |
| Lightweight JavaScript | WebUI23, portable6, Flasher16 |
| Chromium WebUI | Five HTTP/HTTPS desktop/mobile scenarios; serialized API peak1 |
| Chromium Flasher | Eight scenarios, metadata checks and forced-update rejection |
| Real SDK6.1 crypto/NVS in QEMU | Ten starts, including interrupted initialization |
| SDK native builds | S3HTTPS/S3HTTP/C3USB/C3UART and role/symbol/asset checks |
| S3 reproducibility | All four install segments identical in two independent build roots |
| Formatting / syntax | clang-format-21 on20 touched C/C++ files; Python AST/YAML |
| Diff / links | git diff --check;132 local links checked before final report consolidation |

The SDK/QEMU test covers fresh generation, reuse, corrupt identity recovery,
PSRAM OOM, write failure, retry and interruption after blob write/before commit,
with seven unrelated namespace sentinels unchanged. Its exact completion word is
written after assertions and read via isolated-loopback GDB because pinned QEMU
serial output is unavailable. It does not qualify hardware RNG, Ethernet or timing.
The test flash is private, ephemeral and removed, not uploaded as an artifact.

Generic host Python discovery initially lacked rclpy; the unchanged55-test suite
passed in a non-root Lyrical container with copied public sources and its own
throwaway Git fixture. An exploratory standalone MbedTLS fork build was unsuitable
and is not counted; actual SDK/QEMU evidence replaces that experiment. No failed
or unavailable check was converted to a PASS.

## Physical evidence

Hardware: identified Waveshare ESP32-S3 W5500, S3rev0.2, flash16MiB/PSRAM8MiB,
USB Serial/JTAG, Ethernet192.168.10.106. Firmware review base is the SHA above,
modified-source snapshot710492fc1c82197527179fb2cc31b672a7c23935; these local images
are not the later clean-commit CI artifacts. F9P wiring is S3TX43/RX44,460800baud.

Before the authorized erase, two independent full16MiB reads matched. Original
configuration, NVS, identity and password records were saved privately outside Git.
Chip, offsets and production partition table matched the candidate. The first
full erase/install boot generated TLS in478606µs (main-task CPU478680µs), reached
HTTPSrunning/error0 and matched the USB DER fingerprint to the network certificate
before sending credentials. Root, capabilities, status and resources returned200.
Fresh admin/admin was restricted; sensitive configuration was denied. Password
change revoked the session; the private test password then authenticated normally.

| Physical sample | Internal free B | Largest B | Historical/local minimum B | PSRAM free B |
| --- | ---: | ---: | ---: | ---: |
| Before TLS generation | 259655 | — | — | 8382600 |
| After TLS generation | 259027 | 163840 | 258055 local | 8381864 |
| Initial steady HTTPS | 141783 | 34816 | 74956 since boot | 8314684 |
| Cold restart1 | 140019 | 34816 | 74616 since boot | 8282584 |
| Cold restart2 | 139999 | 34816 | 74616 since boot | 8282584 |
| Final restored operator state | 138879 | 34816 | 74788 since boot | 8282620 |

Generation peaked at1600B internal and10776B PSRAM against its starting values;
retained deltas628B/736B are not total HTTPS server costs. Minimum boot-stack free
was23116B on fresh generation and17868B with persisted settings. A later valid-key
load took12631µs; final original-key load13190µs. Admission thresholds96/32KiB and
48/16KiB and the32768B boot-only stack were not lowered. Global minima and present
free memory are distinct; no multi-client/active-radio/endurance claim is made.

The maintainer confirmed **three electrical cuts**. Restart samples showed uptime8s,
9s, then16s where uninterrupted operation would have reached about140s. The same
first-install certificate persisted. A3.89s intermediate network interruption
without an uptime reset was excluded: a TCP gap alone is not evidence of a reboot.
Third-restart resource data was not captured with the same procedure; it is not
silently substituted with the later restored-state sample.

Read-only identification verified ZED-F9P/HPG1.32; current NMEA observations,12
satellites and standalone fix were observed without UART errors. This is not RTK
or physical RTCM delivery. Chromium hardware tests passed six tabs without another
login, authenticated/confirmation gates, F9P restart dispatch + identity re-read
with unchanged configuration/no quarantine, and ESP32 reboot/session invalidation.

The application-only update verified current app/bootloader/table/OTA, wrote only
0x20000, matched its device hash, and retained byte-identical NVS before boot.
After boot the certificate and private test login worked. The SDK stub reader
stalled twice near0x10e000; no write followed those failures. ROM read16MiB succeeded
in595.1s, with an independent matching whole-device digest and a matching separate
NVS read. Later snapshots reconstructed from that verified image plus current app
and raw NVS were independently verified against the **entire device digest** before
writes; they are verified reconstructions, not newly claimed full raw reads.

### Operator account restoration

After the fresh-install test, the temporary private password was still active when
the operator tried the normal password and received401. It should have been restored
before inviting normal use. Automatic login attempts stopped. The generic401 also
covers readiness/1s limiter/session slots; it is not proof of a long lockout.

Only the original24KiB NVS at0x9000 was restored from the duplicate verified backups,
with device hash and byte-identical readback. Latest HTTPS application, partition,
bootloader and OTA state were not rewritten. The original valid certificate was
reused without regeneration; USB/network pin and root/status/resources pass.
The maintainer then explicitly confirmed login with the original password succeeds.
Original configuration and credentials are restored, radiooff/upstream disconnected.
The interrupted post-cold automatic login is not counted as a passed test.
A gzip-less diagnostic request received the expected406; browser-style gzip passed.

## Budgets, remaining acceptance and publication boundary

| Local build | App bytes |
| --- | ---: |
| main HTTPS reference | 1490768 |
| Current S3 HTTPS | 1505408 |
| Current S3 HTTP | 1490032 |
| C3 USB / UART | 702976 / 702992 |

HTTPS adds14640B app and32B static DIRAM (143136→143168B). HTTP static DIRAM143136B.
Compressed WebUI assets21060→21579B (+519B). This link accounting is distinct from
measured runtime heap. S3 bootloader/table/OTA layout are unchanged. Review app SHA256:

- HTTPS: `d2066d09122783ff1fed9ccf44e7deccee6847645095c9444d54ef76e05563a4`
- HTTP: `4d5e5d8202107b1d875a4809c5a08c747a3895f4d667d5528ede6d54150dcd8c`

**Still open:** two additional independent erased installations and their three
cold boots each; latest HTTP hardware execution; browser preserving-update USB
preflight/integration; hosted CI of the published commit. Thus the complete P0
matrix is not declared closed. Auth remains slow (~39–40s for persisted passwords).
Factory-default verification0.876s, password change40.585s and Chromium login39.878s
are different operations; no KDF weakening was applied.

No real GNSS/RTCM load/RTK, multi-client endurance, radio memory, ROS2 hardware or
full ROS2/Kilted build campaign was newly qualified here. Shared UG, ROS2/ROSIDL,
C3 source, NVS schemas, production partitions, thresholds and Release workflows
are unchanged. No further board test is running. Private flash/NVS/certificates,
keys, passwords, sessions, serial traces and local checkpoints stay outside Git;
the unrelated local memory audit is excluded. Source commit/push does not by itself
qualify a new hardware image or authorize a merge/Release.
