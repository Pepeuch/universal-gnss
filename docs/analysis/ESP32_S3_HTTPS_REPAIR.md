# ESP32-S3 HTTPS commissioning repair — 2026-11-15

## Scope and baseline

Physical Waveshare ESP32-S3 W5500, 16 MiB flash and 8 MiB PSRAM,
Ethernet address `192.168.10.106`. Only the independently identified S3 USB
maintenance port was used; C3/C6 were untouched. Baseline commit:
`c66402248a344adf4490d9a797c7dcd9ae29c5a1`.

The implementation/test tree qualified before flashing was
`7e999c4285dd6ca01fa9b6efa412138e1f6f7b0c` (a local Git tree, not a commit).
Subsequent changes are documentation only. ESP-IDF 6.1 SDK:
`espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c`.
Builds/tests ran as UID 1000, without host source mounts or devices, with network
disconnected after dependency provisioning. Hardware access used a separate,
device-restricted container and exclusive serialized port ownership.

## Cause established independently

Initial UG maintenance `status` confirmed HTTPS failed with error 259 while
Ethernet ran. `history` reported configuration generation 0, no previous bank and
writable storage. A private NVS snapshot contained neither `ug_identity`/`https`
nor either saved configuration bank. Secure boot and flash encryption were not
enabled on this device.

After installing diagnostics, and **before provisioning**, the actual board
reported `tls_identity=identity_missing`, underlying SDK error 4354
(`ESP_ERR_NVS_NOT_FOUND`), and `admin_configured=0`. The startup event carried
the distinct local classification 28161 (`0x6e01`). Thus the observed 259 came
from the old boolean loader's loss of error detail: HTTPS had no stored TLS
identity. This evidence does not establish when or why that identity was lost.

## Firmware change

- Typed, fixed-label classifications distinguish missing, unreadable and invalid
  identity, allocation failure, crypto initialization failure, server startup and
  route registration failure. SDK numeric detail remains separately observable.
- The no-exceptions identity loader uses checked allocation, preserves the NVS
  record format, adds PEM terminators only in RAM and wipes released storage.
- `tls-status` is read-only; `status`, local events and S3 startup logging expose
  classifications without PEM, token, password or credential hash output.
- Native fault-injection tests compile the actual production loader and exercise
  NVS failures, invalid records, crypto/allocation failures, secure disposal and
  SDK server-error mapping. Boundary stubs are not cryptographic validation.

See the [commissioning contract](../../embedded/esp32/migration/README.md) and
[native test instructions](../../embedded/esp32/native/tests/README.md).

## Controlled physical repair

1. Read and privately saved all 16 MiB of flash using the ROM loader. A physical
   digest verification succeeded. Backups and credentials are outside Git in a
   private directory; they are deliberately not attached to this report.
2. Verified the installed application matched the previously qualified image,
   the bootloader and partition table matched the new build, and valid OTA
   metadata selected `ota_0` at `0x20000`.
3. Wrote only the 1,461,568-byte application at `0x20000`, then verified it.
   The entire 24 KiB NVS region was read back **before boot** and matched the
   pre-write backup byte for byte. No global erase, partition, OTA-data or
   bootloader write was performed.
4. Confirmed the missing-identity classification physically on the unprovisioned
   board. Used existing `commission.py`/`tls-init` to install a unique P-256
   identity covering the current IP. The earlier certificate covered only its
   DNS hostname. Recovered and preserved the prior privately saved S3 admin
   token after checking its matching provisioning hash; no generic token was
   introduced. Only the admin hash was patched into configuration, preserving
   other settings.
5. Rebooted explicitly after commissioning, then performed a second maintenance
   reboot. Both boots left HTTPS running with error 0; identity validation and
   admin configuration persisted, with configuration generation 1.

Installed application SHA-256:
`cbfba97b2044372b069579d7872376efd41062a3778cc57b1ac9241c921ab4ea`.

## Validation actually executed

| Validation | Result |
| --- | --- |
| Native host CTest with ASan/UBSan | 45/45, on host and pinned SDK |
| Asset/browser mocks and capture tools in isolated SDK | 1 + 9 + 6 + 8 tests passed |
| ESP32 CI/alpha policy tests | 17/17 |
| S3 gateway + local GNSS compilation and independent reconstruction | PASS, identical binaries |
| C3 relay compilation and independent reconstruction | PASS; only 16 bytes slot headroom |
| Eight ROS2 distribution selections | No changed source selected; manifest/generator unchanged |
| Hardware missing-identity startup/status/event classification | PASS |
| HTTPS certificate trust, exact fingerprint and IP SAN verification | PASS |
| `/`, `/app.js`, `/style.css`, `/api/resources`, `/api/status` | HTTP 200 over TLS, before/after second reboot |
| Protected configuration without token / wrong token / retained token | 401 / 401 / 200, before/after second reboot |
| Real Chromium WebUI assets, resource display, login, config read, logout | PASS, no configuration writes |
| Second hardware reboot, persisted identity/admin and HTTPS running | PASS |
| Port 80 | No listener observed |
| Touched C/C++ clang-format-21 and `git diff --check` | PASS |

The generator intentionally requires commits rather than arbitrary trees: no new
distribution publication or unsupported tree-based generation was performed.
The eight source selections were checked directly; all edited firmware files
remain outside ROS2 distributions. No shared UG engine or ROS2/Linux source changed.

## Preserved protections and limits

The 32 KiB main boot stack, S3 external-memory TLS configuration, PSRAM settings,
and memory admission thresholds 96/32 KiB and 48/16 KiB are unchanged. No PSRAM/JSON
optimization was attempted. A compact resource request observed 71,319 internal
free bytes, largest block 34,816 bytes, and 8,386,064 bytes of managed PSRAM.
Pressure was `reduced`: ordinary WebUI work was admitted while detailed satellite
work remained suppressed by the existing guard. Historical minimum free memory
does not define current pressure.

The C3 relay binary fits its unchanged slot by just 16 bytes; further firmware
growth needs explicit attention. This is a compilation result, not a C3 hardware
qualification. No GNSS receiver was attached: no GNSS reception, RTCM radio
delivery, RTK solution, endurance or range claim follows from this repair.
Invalid/unreadable identities and allocation errors were fault-injected in
software; live credentials were not deliberately corrupted to repeat them.

No HTTP fallback, shared certificate, partition change, shared-core change,
commit, push or publication was performed. Existing unrelated local work was
preserved. TODO/roadmap completion accounting is unchanged: this repairs and
diagnoses an existing commissioning surface rather than qualifying a new GNSS
milestone. No downstream ROS2 integration contract changed.
