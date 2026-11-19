# S3 autonomous first boot

Lifecycle: ACTIVE
Status: PARTIAL
Scope: DEPLOYMENT / VALIDATION
Validation: HARDWARE_REQUIRED
Canonical tracking: [TODO](../../../../TODO.md); [durable qualification](../../../../docs/analysis/ESP32_S3_FRESH_INSTALL.md).
Base: main `cf02bede33e71b51ed9335b03c2c4af6535cd406`.
Branch: `fix/esp32-fresh-install`; source commit/push explicitly requested.

## Contract / established evidence

Autonomous S3HTTPS retains valid identity or generates a unique P256 identity before
network/radio, using startup hardware entropy. Existing atomic NVSblob, bounded
PSRAM workspace/readback/wiping; read/OOM/crypto errors fail closed. No HTTP fallback.
Admin diagnostics include password records. Explicit HTTP variant and immutable
firmware provenance propagate through build/CI/bundles/Flasher with legacy-source
compatibility. Formal Release workflow unchanged. Browser preserving-update remains
disabled; offline app-only plan is fail-closed. OTA CRC seed error was caught by real
metadata and corrected against independentSDK vectors before writing.

SDK/QEMU10starts including interruption/failures PASS. NativeASanUBSan79/79,
policy309/309, Python55/55, ChromiumWebUI5scenarios andFlasher8scenarios PASS.
S3HTTP/HTTPS all4install segments match across two independent build roots; C3USB/UART
build/role checks pass. No shared UG/ROS2/C3source/partition/schema/threshold change.

Physical: one authorized erased S3 install generates TLS in478606us and serves HTTPS.
Private double full backups match. App-only update devicehash and rawNVS-preservation
pass. Operator confirms3 full power cuts; certificate retained. A short TCPgap with
no uptime reset was excluded, not counted. F9P onS3TX43/RX44/460800 verifiedHPG1.32;
NMEA/standalonefix observed, no RTK/realRTCM claim. Native NMEA vendor-query selection
corrected without treating configured model as identity.

Confirmed WebUI reboot controls: ESP32 delayed restart returns login and invalidates
session; F9P GNSS-only hot restart uses existing UARTowner/journal, invalidates local
observations/corrections and verifies communication. No reliable resetACK or trusted
cutoff is claimed, no pre-existing quarantine cleared.

## Current operator state / recovery

Temporary first-install password caused operator401. Automatic logins stopped;
private current snapshot whole-device digest matched. Onlyoriginal24KiBNVS restored,
raw readback byte-identical. Latest HTTPSapp unchanged, original validTLS/password/
configuration restored. USB/network identity and publicAPIs pass; user explicitly
confirmed successful login with the original password. No credential reset/weakening.
Radiooff, upstream disconnected, internal138879B/largest34816B, normal pressure.
Private backups, passwords, certificates and traces never belong in shared state.
Do not reuse temporary test credentials/sessions/certificate against restored device.

## Remaining delta / exact continuation

Commit/push reviewed63files only, excluding unrelated untracked memory audit and all
local/private state. No PR/merge or further flash requested with source publication.
HostedCI remains to observe. Full P0 staysPARTIAL: two additional erased installations
and3cold boots each, latest HTTP hardware case, browser preserving-update USB preflight
and integration still open. Slow~39–40s login is pre-existing and not optimized here.
Do not mark hardware complete from builds or one erased install alone.

Use the report and local checkpoint for precise commands. Reuse unchanged tests;
never rerun old commissioning/password reset or assume a TCPgap proves power loss.
Preserve original operator access; coordinate physical cuts before any next write.
